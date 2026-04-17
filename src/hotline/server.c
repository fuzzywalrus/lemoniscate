/*
 * server.c - Hotline server core with kqueue event loop
 *
 * Maps to: hotline/server.go
 *
 * Uses kqueue for I/O multiplexing (available on Tiger 10.4+).
 * The event loop accepts connections, performs the login flow,
 * reads transactions from clients, dispatches to handlers,
 * and sends responses.
 */

#include "hotline/server.h"
#include "hotline/tls.h"
#include "hotline/handshake.h"
#include "hotline/hope.h"
#include "hotline/chacha20poly1305.h"
#include "hotline/field.h"
#include "hotline/user.h"
#include "hotline/password.h"
#include "hotline/transfer.h"
#include "hotline/file_transfer.h"
#include "hotline/flattened_file_object.h"
#include "hotline/file_types.h"
#include "hotline/file_path.h"
#include "mobius/transaction_handlers.h"
#include "mobius/mnemosyne_sync.h"
#include "mobius/config_loader.h"
#include "hotline/chat_history.h"
#include "hotline/encoding.h"

#include "hotline/platform/platform_event.h"
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <dirent.h>
#include <sys/stat.h>

/* Constants — maps to Go server.go constants */
#define HL_PER_IP_RATE_INTERVAL  2   /* seconds between allowed connections per IP */
#define HL_TRACKER_UPDATE_FREQ 300   /* seconds between tracker registrations */
#define HL_USER_IDLE_SECONDS   300   /* seconds before user is marked away */
#define HL_IDLE_CHECK_INTERVAL  10   /* seconds between idle checks */
#define HL_DEFAULT_PORT       5500

/* Timer IDs */
#define HL_TIMER_IDLE          1     /* Idle check (10s) */
#define HL_TIMER_MN_HEARTBEAT  2     /* Mnemosyne heartbeat (300s) */
#define HL_TIMER_MN_PERIODIC   3     /* Mnemosyne periodic check (900s) */
#define HL_TIMER_MN_CHUNK      4     /* Mnemosyne chunk tick (2s) */

/* Mnemosyne timer intervals */
#define MN_HEARTBEAT_MS     300000   /* 5 minutes */
#define MN_PERIODIC_MS      900000   /* 15 minutes */
#define MN_CHUNK_TICK_MS      2000   /* 2 seconds */
#define MN_STARTUP_DELAY_SECS  30    /* seconds before first full sync */

/* --- Helpers: write with EINTR handling (used for local file I/O) --- */

static int write_all(int fd, const uint8_t *buf, size_t n)
{
    size_t total = 0;
    while (total < n) {
        ssize_t w = write(fd, buf + total, n - total);
        if (w < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (w == 0) return -1;
        total += (size_t)w;
    }
    return 0;
}

/* --- Send a transaction to a client fd (raw, no HOPE encryption) --- */

int hl_server_send_transaction(int fd, hl_transaction_t *t)
{
    size_t wire_size = hl_transaction_wire_size(t);
    uint8_t *buf = (uint8_t *)malloc(wire_size);
    if (!buf) return -1;

    int written = hl_transaction_serialize(t, buf, wire_size);
    if (written < 0) { free(buf); return -1; }

    int rc = write_all(fd, buf, (size_t)written);
    free(buf);
    return rc;
}

/* TLS-aware version for pre-login sends where we have a conn wrapper */
static int send_transaction_via_conn(hl_tls_conn_t *conn, hl_transaction_t *t)
{
    size_t wire_size = hl_transaction_wire_size(t);
    uint8_t *buf = (uint8_t *)malloc(wire_size);
    if (!buf) return -1;

    int written = hl_transaction_serialize(t, buf, wire_size);
    if (written < 0) { free(buf); return -1; }

    int rc = hl_conn_write_all(conn, buf, (size_t)written);
    free(buf);
    return rc;
}

/* --- Send a transaction to a client connection (HOPE-aware) --- */

static int send_transaction_to_client(hl_client_conn_t *cc, hl_transaction_t *t)
{
    size_t wire_size = hl_transaction_wire_size(t);
    uint8_t *buf = (uint8_t *)malloc(wire_size);
    if (!buf) return -1;

    int written = hl_transaction_serialize(t, buf, wire_size);
    if (written < 0) { free(buf); return -1; }

    int rc;

    if (cc->hope && cc->hope->aead_active) {
        /* AEAD: seal into length-prefixed frame */
        size_t frame_size = 4 + (size_t)written + HL_POLY1305_TAG_SIZE;
        uint8_t *frame_buf = (uint8_t *)malloc(frame_size);
        if (!frame_buf) { free(buf); return -1; }

        uint64_t counter_before = cc->hope->aead.send_counter;
        int frame_written = hl_hope_aead_encrypt_transaction(
            cc->hope, buf, (size_t)written, frame_buf, frame_size);
        free(buf);

        if (frame_written < 0) { free(frame_buf); return -1; }

        hl_log_debug(cc->logger, "[HOPE-AEAD-W] Sealed frame: %d bytes plaintext -> %d bytes frame (counter=%llu)",
                     written, frame_written, (unsigned long long)counter_before);

        if (cc->conn) {
            rc = hl_conn_write_all(cc->conn, frame_buf, (size_t)frame_written);
        } else {
            rc = write_all(cc->fd, frame_buf, (size_t)frame_written);
        }
        free(frame_buf);
    } else {
        /* RC4 or plaintext */
        if (cc->hope && cc->hope->active &&
            cc->hope->mac_alg != HL_HOPE_MAC_INVERSE) {
            hl_hope_encrypt_transaction(cc->hope, buf, (size_t)written);
        }

        if (cc->conn) {
            rc = hl_conn_write_all(cc->conn, buf, (size_t)written);
        } else {
            rc = write_all(cc->fd, buf, (size_t)written);
        }
        free(buf);
    }

    return rc;
}

/* --- Broadcast a transaction to all clients except sender --- */

void hl_server_broadcast(hl_server_t *srv, hl_client_conn_t *sender,
                         hl_transaction_t *t)
{
    int count = 0;
    hl_client_conn_t **clients = srv->client_mgr->vt->list(srv->client_mgr, &count);
    if (!clients) return;

    int i;
    for (i = 0; i < count; i++) {
        if (!hl_type_eq(clients[i]->id, sender->id)) {
            /* Set target client ID and send */
            memcpy(t->client_id, clients[i]->id, 2);
            send_transaction_to_client(clients[i], t);
        }
    }
    free(clients);
}

/* --- Send a transaction to a specific client --- */

void hl_server_send_to_client(hl_client_conn_t *cc, hl_transaction_t *t)
{
    memcpy(t->client_id, cc->id, 2);
    send_transaction_to_client(cc, t);
}

static void log_chat_history_event(hl_client_conn_t *cc, const char *verb)
{
    char nick_utf8[LM_CHAT_MAX_NICK_LEN + 1];
    char body[LM_CHAT_MAX_BODY_LEN + 1];
    int sender_is_macroman;
    int n;

    if (!cc || !cc->server) return;
    if (!cc->server->chat_history || !cc->server->config.chat_history_enabled) return;
    if (!cc->server->config.chat_history_log_joins) return;
    if (cc->user_name_len == 0) return;

    sender_is_macroman = cc->utf8_encoding ? 0 : cc->server->use_mac_roman;
    if (sender_is_macroman) {
        n = hl_macroman_to_utf8((const char *)cc->user_name,
                                cc->user_name_len,
                                nick_utf8, sizeof(nick_utf8));
        if (n < 0) n = 0;
        nick_utf8[n] = '\0';
    } else {
        size_t nl = cc->user_name_len < LM_CHAT_MAX_NICK_LEN
                  ? cc->user_name_len : LM_CHAT_MAX_NICK_LEN;
        memcpy(nick_utf8, cc->user_name, nl);
        nick_utf8[nl] = '\0';
    }

    n = snprintf(body, sizeof(body), "%s %s", nick_utf8, verb);
    if (n <= 0) return;

    lm_chat_history_append(cc->server->chat_history, 0,
                           HL_CHAT_FLAG_IS_SERVER_MSG, 0, "", body);
}

/* Notify all other clients that this user's visible profile changed.
 * Matches Mobius Go behavior for TranNotifyChangeUser payload fields. */
static void broadcast_notify_change_user(hl_server_t *srv, hl_client_conn_t *cc)
{
    hl_field_t fields[4];
    memset(fields, 0, sizeof(fields));

    hl_field_new(&fields[0], FIELD_USER_NAME,
                 (const uint8_t *)cc->user_name, cc->user_name_len);
    hl_field_new(&fields[1], FIELD_USER_ID, cc->id, 2);
    hl_field_new(&fields[2], FIELD_USER_ICON_ID, cc->icon, 2);
    hl_field_new(&fields[3], FIELD_USER_FLAGS, cc->flags, 2);

    hl_transaction_t notify;
    memset(&notify, 0, sizeof(notify));
    memcpy(notify.type, TRAN_NOTIFY_CHANGE_USER, 2);
    notify.fields = fields;
    notify.field_count = 4;

    hl_server_broadcast(srv, cc, &notify);

    int i;
    for (i = 0; i < 4; i++) {
        hl_field_free(&fields[i]);
    }
}

/* --- Rate limiting (token bucket) --- */

int hl_server_rate_limit_check(hl_server_t *srv, const char *ip)
{
    time_t now = time(NULL);

    pthread_mutex_lock(&srv->rate_limiters_mu);

    hl_rate_limiter_t *rl;
    for (rl = srv->rate_limiters; rl; rl = rl->next) {
        if (strcmp(rl->ip, ip) == 0) break;
    }

    if (!rl) {
        rl = (hl_rate_limiter_t *)calloc(1, sizeof(hl_rate_limiter_t));
        if (!rl) {
            pthread_mutex_unlock(&srv->rate_limiters_mu);
            return 1;
        }
        strncpy(rl->ip, ip, sizeof(rl->ip) - 1);
        rl->tokens = 1.0;
        rl->last_check = now;
        rl->next = srv->rate_limiters;
        srv->rate_limiters = rl;
    }

    double elapsed = difftime(now, rl->last_check);
    rl->tokens += elapsed / (double)HL_PER_IP_RATE_INTERVAL;
    if (rl->tokens > 1.0) rl->tokens = 1.0;
    rl->last_check = now;

    int allowed;
    if (rl->tokens >= 1.0) {
        rl->tokens -= 1.0;
        allowed = 1;
    } else {
        allowed = 0;
    }

    pthread_mutex_unlock(&srv->rate_limiters_mu);
    return allowed;
}

/* --- Server constructor --- */

hl_server_t *hl_server_new(void)
{
    hl_server_t *srv = (hl_server_t *)calloc(1, sizeof(hl_server_t));
    if (!srv) return NULL;

    srv->port = HL_DEFAULT_PORT;
    hl_config_init(&srv->config);

    srv->logger     = hl_stderr_logger_new();
    srv->stats      = hl_stats_new();
    srv->fs         = hl_os_file_store_new();
    srv->client_mgr = hl_mem_client_mgr_new();
    srv->chat_mgr   = hl_mem_chat_mgr_new();

    pthread_mutex_init(&srv->rate_limiters_mu, NULL);

    if (pipe(srv->outbox_pipe) < 0) {
        hl_server_free(srv);
        return NULL;
    }
    fcntl(srv->outbox_pipe[0], F_SETFL, O_NONBLOCK);

    int ufd = open("/dev/urandom", O_RDONLY);
    if (ufd >= 0) {
        read(ufd, srv->tracker_pass_id, 4);
        close(ufd);
    } else {
        /* Fallback if /dev/urandom unavailable (e.g. chroot on Tiger) */
        uint32_t seed = (uint32_t)(time(NULL) ^ getpid());
        memcpy(srv->tracker_pass_id, &seed, 4);
    }

    srv->listen_fd = -1;
    srv->transfer_fd = -1;
    srv->tls_listen_fd = -1;
    srv->tls_transfer_fd = -1;
    srv->tls_port = 0;

    return srv;
}

void hl_server_free(hl_server_t *srv)
{
    if (!srv) return;

    hl_rate_limiter_t *rl = srv->rate_limiters;
    while (rl) {
        hl_rate_limiter_t *next = rl->next;
        free(rl);
        rl = next;
    }

    if (srv->listen_fd >= 0) close(srv->listen_fd);
    if (srv->transfer_fd >= 0) close(srv->transfer_fd);
    if (srv->tls_listen_fd >= 0) close(srv->tls_listen_fd);
    if (srv->tls_transfer_fd >= 0) close(srv->tls_transfer_fd);
    hl_tls_server_ctx_free(&srv->tls_ctx);
    if (srv->outbox_pipe[0] >= 0) close(srv->outbox_pipe[0]);
    if (srv->outbox_pipe[1] >= 0) close(srv->outbox_pipe[1]);

    if (srv->agreement) free(srv->agreement);
    if (srv->banner) free(srv->banner);

    pthread_mutex_destroy(&srv->rate_limiters_mu);

    if (srv->logger) srv->logger->vt->free(srv->logger);
    if (srv->stats) hl_stats_free(srv->stats);
    if (srv->client_mgr) hl_mem_client_mgr_free(srv->client_mgr);
    if (srv->chat_mgr) hl_mem_chat_mgr_free(srv->chat_mgr);
    if (srv->fs) hl_file_store_free(srv->fs);
    if (srv->file_transfer_mgr) hl_mem_xfer_mgr_free(srv->file_transfer_mgr);

    free(srv);
}

void hl_server_handle_func(hl_server_t *srv, const hl_tran_type_t type,
                           hl_handler_func_t handler)
{
    uint16_t code = hl_type_to_u16(type);
    if (code < HL_HANDLER_TABLE_SIZE) {
        srv->handlers[code] = handler;
    }
}

void hl_server_shutdown(hl_server_t *srv)
{
    srv->shutdown = 1;
}

/* --- TCP listener setup --- */

static int create_listener(const char *interface, int port)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    int opt = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)port);

    if (interface[0] != '\0') {
        addr.sin_addr.s_addr = inet_addr(interface);
    } else {
        addr.sin_addr.s_addr = INADDR_ANY;
    }

    if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        close(fd);
        return -1;
    }

    if (listen(fd, 128) < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

/* --- Disconnect a client and clean up --- */

static void disconnect_client(hl_server_t *srv, hl_client_conn_t *cc, hl_event_loop_t *evloop)
{
    hl_log_info(srv->logger, "Client disconnected: %s (id=%d)",
                cc->remote_addr, hl_read_u16(cc->id));

    log_chat_history_event(cc, "signed off");

    /* Remove from event loop (closing fd does this automatically, but be explicit) */
    hl_event_remove_fd(evloop, cc->fd);

    if (cc->conn) {
        hl_conn_close(cc->conn);
        cc->conn = NULL;
    } else {
        close(cc->fd);
    }
    cc->fd = -1;

    /* Notify other clients */
    hl_transaction_t notify;
    memset(&notify, 0, sizeof(notify));
    memcpy(notify.type, TRAN_NOTIFY_DELETE_USER, 2);
    hl_field_t nfield;
    hl_field_new(&nfield, FIELD_USER_ID, cc->id, 2);
    notify.fields = &nfield;
    notify.field_count = 1;
    hl_server_broadcast(srv, cc, &notify);
    hl_field_free(&nfield);
    notify.fields = NULL;

    /* Remove from client manager */
    srv->client_mgr->vt->del(srv->client_mgr, cc->id);
    hl_stats_decrement(srv->stats, HL_STAT_CURRENTLY_CONNECTED);

    hl_client_conn_free(cc);
}

/* --- Process incoming data from a client --- */

static void process_client_data(hl_server_t *srv, hl_client_conn_t *cc, hl_event_loop_t *evloop)
{
    /* Read available data into the client's accumulation buffer */
    if (cc->read_buf_len >= sizeof(cc->read_buf)) {
        hl_log_error(srv->logger, "Client %d buffer full, disconnecting",
                     hl_read_u16(cc->id));
        disconnect_client(srv, cc, evloop);
        return;
    }

    ssize_t n;
    if (cc->conn) {
        n = hl_conn_read(cc->conn,
                         cc->read_buf + cc->read_buf_len,
                         sizeof(cc->read_buf) - cc->read_buf_len);
    } else {
        n = read(cc->fd,
                 cc->read_buf + cc->read_buf_len,
                 sizeof(cc->read_buf) - cc->read_buf_len);
    }
    if (n < 0 && errno == EINTR) return;
    if (n <= 0) {
        disconnect_client(srv, cc, evloop);
        return;
    }
    cc->read_buf_len += (size_t)n;

    /* AEAD clients: frame-based decryption (cannot decrypt partial frames) */
    if (cc->hope && cc->hope->aead_active) {
        while (cc->read_buf_len > 0) {
            int frame_result = hl_hope_aead_scan_frame(
                cc->read_buf, cc->read_buf_len, sizeof(cc->read_buf));

            if (frame_result == 0) break; /* need more data */

            if (frame_result < 0) {
                hl_log_error(srv->logger,
                    "AEAD frame too large or invalid from client %d (buf_len=%zu), disconnecting",
                    hl_read_u16(cc->id), cc->read_buf_len);
                disconnect_client(srv, cc, evloop);
                return;
            }

            size_t frame_len = (size_t)frame_result;

            /* Decrypt frame into temporary buffer */
            uint8_t decrypt_buf[65536];
            size_t plaintext_len = 0;
            uint64_t counter_before = cc->hope->aead.recv_counter;
            if (hl_hope_aead_decrypt_frame(cc->hope,
                    cc->read_buf, frame_len,
                    decrypt_buf, &plaintext_len) != 0) {
                hl_log_error(srv->logger,
                    "AEAD tag verification failed from client %d (frame_len=%zu, counter=%llu), disconnecting",
                    hl_read_u16(cc->id), frame_len, (unsigned long long)counter_before);
                disconnect_client(srv, cc, evloop);
                return;
            }

            hl_log_debug(srv->logger,
                "[HOPE-AEAD-R] Opened frame: %zu bytes -> %zu bytes plaintext (counter=%llu) from client %d",
                frame_len, plaintext_len, (unsigned long long)counter_before, hl_read_u16(cc->id));

            /* Consume frame from read_buf */
            if (frame_len < cc->read_buf_len) {
                memmove(cc->read_buf, cc->read_buf + frame_len,
                        cc->read_buf_len - frame_len);
            }
            cc->read_buf_len -= frame_len;

            /* Parse and dispatch the decrypted transaction */
            hl_transaction_t t;
            if (hl_transaction_deserialize(&t, decrypt_buf, plaintext_len) < 0) {
                hl_log_error(srv->logger,
                    "Transaction parse error after AEAD decrypt for client %d",
                    hl_read_u16(cc->id));
                continue;
            }

            /* Dispatch to handler (same code path as below) */
            uint16_t type_code = hl_type_to_u16(t.type);
            hl_handler_func_t handler = (type_code < HL_HANDLER_TABLE_SIZE)
                ? srv->handlers[type_code] : NULL;

            if (handler) {
                hl_log_debug(srv->logger, "Handling %s from client %d (AEAD)",
                            hl_transaction_type_name(t.type), hl_read_u16(cc->id));

                if (!hl_type_eq(t.type, TRAN_KEEP_ALIVE)) {
                    pthread_rwlock_wrlock(&cc->mu);
                    cc->idle_time = 0;
                    if (hl_user_flags_is_set(cc->flags, HL_USER_FLAG_AWAY)) {
                        hl_user_flags_set(cc->flags, HL_USER_FLAG_AWAY, 0);
                        broadcast_notify_change_user(srv, cc);
                    }
                    pthread_rwlock_unlock(&cc->mu);
                }

                hl_transaction_t *responses = NULL;
                int resp_count = 0;
                handler(cc, &t, &responses, &resp_count);

                if (responses && resp_count > 0) {
                    int r;
                    for (r = 0; r < resp_count; r++) {
                        hl_transaction_t *resp = &responses[r];
                        hl_client_id_t target_id;
                        memcpy(target_id, resp->client_id, 2);

                        if (target_id[0] == 0 && target_id[1] == 0) {
                            hl_server_send_to_client(cc, resp);
                        } else {
                            hl_client_conn_t *target = srv->client_mgr->vt->get(
                                srv->client_mgr, target_id);
                            if (target) {
                                send_transaction_to_client(target, resp);
                            }
                        }
                        hl_transaction_free(resp);
                    }
                    free(responses);
                }
            } else {
                hl_log_info(srv->logger, "Unhandled transaction %s (%d) from client %d (AEAD)",
                           hl_transaction_type_name(t.type), type_code,
                           hl_read_u16(cc->id));
            }

            hl_transaction_free(&t);
        }
        return; /* AEAD path handled — don't fall through to RC4/plaintext */
    }

    /* RC4 HOPE: decrypt newly received data incrementally */
    if (cc->hope && cc->hope->active &&
        cc->hope->mac_alg != HL_HOPE_MAC_INVERSE) {
        hl_hope_decrypt_incremental(cc->hope, cc->read_buf, cc->read_buf_len);
    }

    /* Scan for complete transactions and dispatch.
     * For HOPE RC4 clients, only scan up to the decrypted boundary. */
    size_t scan_limit = cc->read_buf_len;
    if (cc->hope && cc->hope->active &&
        cc->hope->mac_alg != HL_HOPE_MAC_INVERSE) {
        scan_limit = cc->hope->decrypt_offset;
    }

    while (scan_limit > 0) {
        int tran_len = hl_transaction_scan(cc->read_buf, scan_limit);
        if (tran_len < 0) {
            hl_log_error(srv->logger, "Transaction scan error for client %d",
                         hl_read_u16(cc->id));
            cc->read_buf_len = 0;
            break;
        }
        if (tran_len == 0) break; /* Need more data */

        /* Parse transaction */
        hl_transaction_t t;
        int consumed = hl_transaction_deserialize(&t, cc->read_buf, (size_t)tran_len);
        if (consumed < 0) {
            hl_log_error(srv->logger, "Transaction parse error for client %d",
                         hl_read_u16(cc->id));
            cc->read_buf_len = 0;
            break;
        }

        /* Dispatch to handler */
        uint16_t type_code = hl_type_to_u16(t.type);
        hl_handler_func_t handler = (type_code < HL_HANDLER_TABLE_SIZE)
            ? srv->handlers[type_code] : NULL;

        if (handler) {
            hl_log_debug(srv->logger, "Handling %s from client %d",
                        hl_transaction_type_name(t.type), hl_read_u16(cc->id));

            /* Reset idle timer (except for keepalive) */
            if (!hl_type_eq(t.type, TRAN_KEEP_ALIVE)) {
                pthread_rwlock_wrlock(&cc->mu);
                cc->idle_time = 0;
                /* Clear away flag if set */
                if (hl_user_flags_is_set(cc->flags, HL_USER_FLAG_AWAY)) {
                    hl_user_flags_set(cc->flags, HL_USER_FLAG_AWAY, 0);
                    /* Notify others that user is no longer away. */
                    broadcast_notify_change_user(srv, cc);
                }
                pthread_rwlock_unlock(&cc->mu);
            }

            /* Call handler */
            hl_transaction_t *responses = NULL;
            int resp_count = 0;
            handler(cc, &t, &responses, &resp_count);

            /* Send responses */
            if (responses && resp_count > 0) {
                int r;
                for (r = 0; r < resp_count; r++) {
                    hl_transaction_t *resp = &responses[r];
                    /* Route to correct client */
                    hl_client_id_t target_id;
                    memcpy(target_id, resp->client_id, 2);

                    if (target_id[0] == 0 && target_id[1] == 0) {
                        /* No specific target — send to requesting client */
                        hl_server_send_to_client(cc, resp);
                    } else {
                        hl_client_conn_t *target = srv->client_mgr->vt->get(
                            srv->client_mgr, target_id);
                        if (target) {
                            send_transaction_to_client(target, resp);
                        }
                    }
                    hl_transaction_free(resp);
                }
                free(responses);
            }
        } else {
            hl_log_info(srv->logger, "Unhandled transaction %s (%d) from client %d",
                       hl_transaction_type_name(t.type), type_code,
                       hl_read_u16(cc->id));
        }

        hl_transaction_free(&t);

        /* Shift remaining data */
        if ((size_t)consumed < cc->read_buf_len) {
            memmove(cc->read_buf, cc->read_buf + consumed,
                    cc->read_buf_len - (size_t)consumed);
        }
        cc->read_buf_len -= (size_t)consumed;

        /* Adjust HOPE decrypt offset after consuming bytes */
        if (cc->hope && cc->hope->active) {
            hl_hope_adjust_offset(cc->hope, (size_t)consumed);
        }

        /* Update scan_limit for next iteration */
        scan_limit = cc->read_buf_len;
        if (cc->hope && cc->hope->active &&
            cc->hope->mac_alg != HL_HOPE_MAC_INVERSE) {
            scan_limit = cc->hope->decrypt_offset;
        }
    }
}

/* --- Handle a new client connection (handshake + login) --- */

static void handle_new_connection(hl_server_t *srv, int client_fd,
                                  struct sockaddr_in *client_addr,
                                  hl_event_loop_t *evloop,
                                  hl_tls_conn_t *tls_conn)
{
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr->sin_addr, ip_str, sizeof(ip_str));

    /* Rate limiting check */
    if (!hl_server_rate_limit_check(srv, ip_str)) {
        hl_log_info(srv->logger, "Rate limited: %s", ip_str);
        if (tls_conn) hl_conn_close(tls_conn); else close(client_fd);
        return;
    }

    /* Create unified connection wrapper:
     * TLS connections arrive pre-wrapped; plain connections get wrapped here. */
    hl_tls_conn_t *conn = tls_conn ? tls_conn : hl_conn_wrap_plain(client_fd);
    if (!conn) { close(client_fd); return; }

    /* Set socket timeouts to prevent a slow client from blocking the event loop.
     * These are removed after login completes (client moves to non-blocking kqueue). */
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* Handshake (application-layer, runs over TLS if active) */
    if (hl_perform_handshake_server_conn(conn) < 0) {
        hl_log_error(srv->logger, "Handshake failed: %s", ip_str);
        hl_conn_close(conn);
        return;
    }

    /* Read login transaction — maps to Go handleNewConnection() */
    /* Read enough data for the transaction header first */
    uint8_t header_buf[22];
    if (hl_conn_read_full(conn, header_buf, 22) < 0) {
        hl_log_error(srv->logger, "Failed to read login from %s", ip_str);
        hl_conn_close(conn);
        return;
    }

    uint32_t total_size = hl_read_u32(header_buf + 12);
    if (total_size > 65535 || total_size < 2) { /* Must hold at least param_count */
        hl_conn_close(conn);
        return;
    }

    size_t full_len = 20 + total_size;
    uint8_t *login_buf = (uint8_t *)malloc(full_len);
    if (!login_buf) { hl_conn_close(conn); return; }

    memcpy(login_buf, header_buf, 22);
    if (full_len > 22) {
        if (hl_conn_read_full(conn, login_buf + 22, full_len - 22) < 0) {
            free(login_buf);
            hl_conn_close(conn);
            return;
        }
    }

    /* Parse login transaction */
    hl_transaction_t login_tran;
    if (hl_transaction_deserialize(&login_tran, login_buf, full_len) < 0) {
        free(login_buf);
        hl_conn_close(conn);
        return;
    }
    free(login_buf);

    /* Extract login fields */
    const hl_field_t *f_login = hl_transaction_get_field(&login_tran, FIELD_USER_LOGIN);
    const hl_field_t *f_password = hl_transaction_get_field(&login_tran, FIELD_USER_PASSWORD);
    const hl_field_t *f_name = hl_transaction_get_field(&login_tran, FIELD_USER_NAME);
    const hl_field_t *f_icon = hl_transaction_get_field(&login_tran, FIELD_USER_ICON_ID);
    const hl_field_t *f_version = hl_transaction_get_field(&login_tran, FIELD_VERSION);

    /* --- HOPE detection and negotiation --- */
    hl_hope_state_t *hope = NULL;
    int is_hope = 0;
    hl_account_t *hope_acct = NULL;

    if (srv->config.enable_hope && hl_hope_detect_probe(&login_tran)) {
        hl_log_info(srv->logger, "HOPE identification from %s", ip_str);

        hope = (hl_hope_state_t *)calloc(1, sizeof(hl_hope_state_t));
        if (!hope) {
            hl_transaction_free(&login_tran);
            hl_conn_close(conn);
            return;
        }

        /* Build and send HOPE negotiation reply */
        hl_transaction_t hope_reply;
        hl_hope_cipher_mode_t negotiated_cipher_mode = HL_HOPE_CIPHER_MODE_NONE;
        hl_hope_cipher_policy_t cipher_policy = hl_hope_parse_cipher_policy(
            srv->config.hope_cipher_policy);
        if (hl_hope_build_negotiation_reply(hope, &login_tran, &hope_reply,
                                            ip_str, (uint16_t)srv->port,
                                            srv->config.hope_legacy_mode,
                                            cipher_policy,
                                            &negotiated_cipher_mode) < 0) {
            hl_log_info(srv->logger, "HOPE: no acceptable algorithms from %s (policy=%s)",
                        ip_str, srv->config.hope_cipher_policy);
            free(hope);
            hl_transaction_free(&login_tran);
            hl_conn_close(conn);
            return;
        }

        hl_log_info(srv->logger,
            "HOPE negotiation: mac=%d, cipher_mode=%s, policy=%s, session_key[0..3]=%02x%02x%02x%02x from %s",
            (int)hope->mac_alg,
            negotiated_cipher_mode == HL_HOPE_CIPHER_MODE_AEAD ? "AEAD" :
            negotiated_cipher_mode == HL_HOPE_CIPHER_MODE_RC4 ? "RC4" : "NONE",
            srv->config.hope_cipher_policy,
            hope->session_key[0], hope->session_key[1],
            hope->session_key[2], hope->session_key[3],
            ip_str);

        send_transaction_via_conn(conn, &hope_reply);
        hl_transaction_free(&hope_reply);

        /* Free the probe login and read the authenticated login */
        hl_transaction_free(&login_tran);

        /* Read second (authenticated) login transaction */
        uint8_t header_buf2[22];
        if (hl_conn_read_full(conn, header_buf2, 22) < 0) {
            hl_log_error(srv->logger, "HOPE: failed to read auth login from %s", ip_str);
            free(hope);
            hl_conn_close(conn);
            return;
        }

        uint32_t total_size2 = hl_read_u32(header_buf2 + 12);
        if (total_size2 > 65535 || total_size2 < 2) {
            free(hope);
            hl_conn_close(conn);
            return;
        }

        size_t full_len2 = 20 + total_size2;
        uint8_t *login_buf2 = (uint8_t *)malloc(full_len2);
        if (!login_buf2) { free(hope); hl_conn_close(conn); return; }

        memcpy(login_buf2, header_buf2, 22);
        if (full_len2 > 22) {
            if (hl_conn_read_full(conn, login_buf2 + 22, full_len2 - 22) < 0) {
                free(login_buf2);
                free(hope);
                hl_conn_close(conn);
                return;
            }
        }

        if (hl_transaction_deserialize(&login_tran, login_buf2, full_len2) < 0) {
            free(login_buf2);
            free(hope);
            hl_conn_close(conn);
            return;
        }
        free(login_buf2);

        /* Re-extract fields from authenticated login */
        f_login = hl_transaction_get_field(&login_tran, FIELD_USER_LOGIN);
        f_password = hl_transaction_get_field(&login_tran, FIELD_USER_PASSWORD);
        f_name = hl_transaction_get_field(&login_tran, FIELD_USER_NAME);
        f_icon = hl_transaction_get_field(&login_tran, FIELD_USER_ICON_ID);
        f_version = hl_transaction_get_field(&login_tran, FIELD_VERSION);

        /* Verify HOPE-authenticated credentials */
        if (srv->account_mgr &&
            hl_hope_verify_login(hope, f_login, f_password,
                                 srv->account_mgr, &hope_acct)) {
            hl_log_info(srv->logger, "HOPE login verified: %s from %s",
                        hope_acct->login, ip_str);

            /* Derive transport encryption keys based on negotiated cipher */
            if (negotiated_cipher_mode == HL_HOPE_CIPHER_MODE_AEAD) {
                hl_hope_aead_derive_keys(hope, hope_acct->password);
                hl_log_info(srv->logger,
                    "HOPE AEAD keys derived: encode[0..3]=%02x%02x%02x%02x, decode[0..3]=%02x%02x%02x%02x, ft_base[0..3]=%02x%02x%02x%02x",
                    hope->aead.encode_key[0], hope->aead.encode_key[1],
                    hope->aead.encode_key[2], hope->aead.encode_key[3],
                    hope->aead.decode_key[0], hope->aead.decode_key[1],
                    hope->aead.decode_key[2], hope->aead.decode_key[3],
                    hope->aead.ft_base_key[0], hope->aead.ft_base_key[1],
                    hope->aead.ft_base_key[2], hope->aead.ft_base_key[3]);
            } else {
                hl_hope_derive_keys(hope, hope_acct->password);
                hl_log_info(srv->logger, "HOPE RC4 keys derived for %s", hope_acct->login);
            }
            is_hope = 1;
        } else {
            hl_log_info(srv->logger, "HOPE login failed from %s", ip_str);
            /* Send error reply */
            hl_transaction_t err_reply;
            memset(&err_reply, 0, sizeof(err_reply));
            err_reply.is_reply = 1;
            memcpy(err_reply.id, login_tran.id, 4);
            err_reply.error_code[3] = 1;
            hl_field_t err_field;
            const char *msg = "Incorrect login.";
            hl_field_new(&err_field, FIELD_ERROR, (const uint8_t *)msg,
                         (uint16_t)strlen(msg));
            err_reply.fields = &err_field;
            err_reply.field_count = 1;
            send_transaction_via_conn(conn, &err_reply);
            hl_field_free(&err_field);
            err_reply.fields = NULL;
            hl_transaction_free(&login_tran);
            free(hope);
            hl_conn_close(conn);
            return;
        }
    }

    /* --- Standard (non-HOPE) login credential processing --- */
    char login_str[128] = "guest";

    if (!is_hope) {
        /* Decode login (255-rotation obfuscated) */
        if (f_login && f_login->data_len > 0) {
            hl_field_decode_obfuscated_string(f_login, login_str, sizeof(login_str));
        }
    } else {
        /* Use the verified HOPE account's login name */
        strncpy(login_str, hope_acct->login, sizeof(login_str) - 1);
        login_str[sizeof(login_str) - 1] = '\0';
    }

    /* Check bans */
    if (srv->ban_list) {
        if (srv->ban_list->vt->is_username_banned(srv->ban_list, login_str) ||
            srv->ban_list->vt->is_banned(srv->ban_list, ip_str)) {
            hl_log_info(srv->logger, "Banned user/IP rejected: %s (%s)", login_str, ip_str);
            /* Send error and close */
            hl_transaction_t err_reply;
            memset(&err_reply, 0, sizeof(err_reply));
            err_reply.is_reply = 1;
            memcpy(err_reply.id, login_tran.id, 4);
            err_reply.error_code[3] = 1;
            hl_field_t err_field;
            const char *ban_msg = "You are banned from this server.";
            hl_field_new(&err_field, FIELD_ERROR, (const uint8_t *)ban_msg,
                         (uint16_t)strlen(ban_msg));
            err_reply.fields = &err_field;
            err_reply.field_count = 1;
            send_transaction_via_conn(conn, &err_reply);
            hl_field_free(&err_field);
            err_reply.fields = NULL;
            hl_transaction_free(&login_tran);
            if (hope) free(hope);
            hl_conn_close(conn);
            return;
        }
    }

    /* Create client connection */
    char remote_addr[80];
    snprintf(remote_addr, sizeof(remote_addr), "%s:%d",
             ip_str, ntohs(client_addr->sin_port));

    hl_client_conn_t *cc = hl_client_conn_new(client_fd, remote_addr, srv);
    if (!cc) {
        hl_transaction_free(&login_tran);
        if (hope) free(hope);
        hl_conn_close(conn);
        return;
    }
    cc->logger = srv->logger;
    cc->conn = conn;              /* Transfer conn ownership to client */
    cc->is_tls = (tls_conn != NULL);

    /* Attach HOPE state if negotiated */
    if (is_hope) {
        cc->hope = hope;
    }

    /* Set user info from login fields */
    if (f_name && f_name->data_len > 0) {
        uint16_t len = f_name->data_len < sizeof(cc->user_name) - 1
                     ? f_name->data_len : (uint16_t)(sizeof(cc->user_name) - 1);
        memcpy(cc->user_name, f_name->data, len);
        cc->user_name[len] = '\0';
        cc->user_name_len = len;
    } else {
        uint16_t len = (uint16_t)strlen(login_str);
        memcpy(cc->user_name, login_str, len);
        cc->user_name[len] = '\0';
        cc->user_name_len = len;
    }

    if (f_icon && f_icon->data_len >= 2) {
        memcpy(cc->icon, f_icon->data + (f_icon->data_len - 2), 2);
    }

    if (f_version && f_version->data_len >= 2) {
        memcpy(cc->version, f_version->data + (f_version->data_len - 2), 2);
    }

    /* Authenticate — maps to Go handleLogin() password check */
    if (is_hope) {
        /* HOPE already verified credentials — assign the matched account */
        cc->account = hope_acct;
        if (hl_access_is_set(hope_acct->access, ACCESS_DISCON_USER)) {
            pthread_mutex_lock(&cc->flags_mu);
            hl_user_flags_set(cc->flags, HL_USER_FLAG_ADMIN, 1);
            pthread_mutex_unlock(&cc->flags_mu);
        }
    } else if (srv->account_mgr) {
        hl_account_t *acct = srv->account_mgr->vt->get(srv->account_mgr, login_str);
        if (!acct) {
            hl_log_info(srv->logger, "Login failed (unknown account): %s from %s",
                        login_str, ip_str);
            /* Send error reply and disconnect */
            hl_transaction_t err_reply;
            memset(&err_reply, 0, sizeof(err_reply));
            err_reply.is_reply = 1;
            memcpy(err_reply.id, login_tran.id, 4);
            err_reply.error_code[3] = 1;
            hl_field_t err_field;
            const char *msg = "Incorrect login.";
            hl_field_new(&err_field, FIELD_ERROR, (const uint8_t *)msg,
                         (uint16_t)strlen(msg));
            err_reply.fields = &err_field;
            err_reply.field_count = 1;
            send_transaction_to_client(cc, &err_reply);
            hl_field_free(&err_field);
            err_reply.fields = NULL;
            hl_transaction_free(&login_tran);
            hl_client_conn_free(cc);
            close(client_fd);
            return;
        }

        /* Verify password if the account has one set */
        if (acct->password[0] != '\0') {
            char password_str[128] = "";
            if (f_password && f_password->data_len > 0) {
                hl_field_decode_obfuscated_string(f_password, password_str,
                                                   sizeof(password_str));
            }
            if (!hl_password_verify(password_str, acct->password)) {
                hl_log_info(srv->logger, "Login failed (bad password): %s from %s",
                            login_str, ip_str);
                hl_transaction_t err_reply;
                memset(&err_reply, 0, sizeof(err_reply));
                err_reply.is_reply = 1;
                memcpy(err_reply.id, login_tran.id, 4);
                err_reply.error_code[3] = 1;
                hl_field_t err_field;
                const char *msg = "Incorrect login.";
                hl_field_new(&err_field, FIELD_ERROR, (const uint8_t *)msg,
                             (uint16_t)strlen(msg));
                err_reply.fields = &err_field;
                err_reply.field_count = 1;
                send_transaction_to_client(cc, &err_reply);
                hl_field_free(&err_field);
                err_reply.fields = NULL;
                hl_transaction_free(&login_tran);
                hl_client_conn_free(cc);
                close(client_fd);
                return;
            }
        }

        cc->account = acct;
        /* Check if admin */
        if (hl_access_is_set(acct->access, ACCESS_DISCON_USER)) {
            pthread_mutex_lock(&cc->flags_mu);
            hl_user_flags_set(cc->flags, HL_USER_FLAG_ADMIN, 1);
            pthread_mutex_unlock(&cc->flags_mu);
        }
    }

    /* Add to client manager (assigns ID) */
    srv->client_mgr->vt->add(srv->client_mgr, cc);
    hl_stats_increment(srv->stats, HL_STAT_CURRENTLY_CONNECTED);
    hl_stats_increment(srv->stats, HL_STAT_CONNECTION_COUNTER);

    int current = hl_stats_get(srv->stats, HL_STAT_CURRENTLY_CONNECTED);
    int peak = hl_stats_get(srv->stats, HL_STAT_CONNECTION_PEAK);
    if (current > peak) {
        hl_stats_set(srv->stats, HL_STAT_CONNECTION_PEAK, current);
    }

    hl_log_info(srv->logger, "Login: %s as \"%.*s\" (id=%d)",
                remote_addr, cc->user_name_len, cc->user_name,
                hl_read_u16(cc->id));

    /* Negotiate capabilities (large file support, etc.) */
    {
        const hl_field_t *f_caps = hl_transaction_get_field(&login_tran, FIELD_CAPABILITIES);
        uint16_t client_caps = 0;
        if (f_caps && f_caps->data_len >= 2) {
            client_caps = hl_read_u16(f_caps->data);
        }
        /* Enable large file mode if client advertises it */
        if (client_caps & HL_CAPABILITY_LARGE_FILES) {
            cc->large_file_mode = 1;
            hl_log_info(srv->logger, "Large file mode enabled for %s",
                        cc->remote_addr);
        }
        if (client_caps & HL_CAPABILITY_TEXT_ENCODING) {
            cc->utf8_encoding = 1;
            hl_log_info(srv->logger, "UTF-8 wire encoding enabled for %s",
                        cc->remote_addr);
        }
        if ((client_caps & HL_CAPABILITY_CHAT_HISTORY) &&
            srv->config.chat_history_enabled && srv->chat_history) {
            cc->chat_history_capable = 1;
            hl_log_info(srv->logger, "Chat history capability enabled for %s",
                        cc->remote_addr);
        }
    }

    /* Activate HOPE transport encryption BEFORE sending login reply.
     * Per Janus interop: the login reply and all subsequent transactions
     * are sent encrypted. The client activates its reader immediately
     * after sending step 3, so it expects the reply to be encrypted. */
    if (cc->hope && cc->hope->mac_alg != HL_HOPE_MAC_INVERSE) {
        if (cc->hope->aead.ft_base_key_set) {
            /* AEAD keys were derived — activate AEAD transport */
            cc->hope->aead_active = 1;
            hl_log_info(srv->logger, "HOPE AEAD transport encryption activated for %s (ChaCha20-Poly1305)",
                        cc->remote_addr);
        } else {
            /* RC4 transport */
            cc->hope->active = 1;
            hl_log_info(srv->logger, "HOPE transport encryption activated for %s (RC4)",
                        cc->remote_addr);
        }
    }

    /* Send login reply sequence — maps to Go login response in server.go:593-615
     *
     * 1. Reply to login transaction with FieldVersion, FieldCommunityBannerID, FieldServerName, FieldCapabilities
     * 2. Send TranUserAccess with access bitmap (new transaction, not reply)
     * 3. Send TranShowAgreement with agreement text (new transaction, not reply)
     *
     * All sent via send_transaction_to_client() which encrypts if HOPE is active.
     */

    /* Step 1: Login reply with server info */
    {
        uint8_t version_bytes[2] = {0x00, 0xBE}; /* 190 = Hotline 1.8+ */
        uint8_t banner_id[2] = {0, 0};
        hl_field_t reply_fields[6];
        int fc = 0;
        hl_field_new(&reply_fields[fc++], FIELD_VERSION, version_bytes, 2);
        hl_field_new(&reply_fields[fc++], FIELD_COMMUNITY_BANNER_ID, banner_id, 2);
        hl_field_new(&reply_fields[fc++], FIELD_SERVER_NAME,
                     (const uint8_t *)srv->config.name,
                     (uint16_t)strlen(srv->config.name));

        /* Echo confirmed capabilities back to client */
        {
            uint16_t echoed_caps = 0;
            if (cc->large_file_mode)      echoed_caps |= HL_CAPABILITY_LARGE_FILES;
            if (cc->utf8_encoding)        echoed_caps |= HL_CAPABILITY_TEXT_ENCODING;
            if (cc->chat_history_capable) echoed_caps |= HL_CAPABILITY_CHAT_HISTORY;
            if (echoed_caps) {
                uint8_t caps[2];
                hl_write_u16(caps, echoed_caps);
                hl_field_new(&reply_fields[fc++], FIELD_CAPABILITIES, caps, 2);
            }
        }

        if (cc->chat_history_capable) {
            uint8_t max_msgs_be[4];
            uint8_t max_days_be[4];
            uint32_t mm = srv->config.chat_history_max_msgs;
            uint32_t md = srv->config.chat_history_max_days;

            max_msgs_be[0] = (uint8_t)(mm >> 24);
            max_msgs_be[1] = (uint8_t)(mm >> 16);
            max_msgs_be[2] = (uint8_t)(mm >> 8);
            max_msgs_be[3] = (uint8_t)mm;
            max_days_be[0] = (uint8_t)(md >> 24);
            max_days_be[1] = (uint8_t)(md >> 16);
            max_days_be[2] = (uint8_t)(md >> 8);
            max_days_be[3] = (uint8_t)md;

            hl_field_new(&reply_fields[fc++], FIELD_HISTORY_MAX_MSGS, max_msgs_be, 4);
            hl_field_new(&reply_fields[fc++], FIELD_HISTORY_MAX_DAYS, max_days_be, 4);
        }

        hl_transaction_t reply;
        memset(&reply, 0, sizeof(reply));
        reply.is_reply = 1;
        memcpy(reply.id, login_tran.id, 4);
        memcpy(reply.client_id, cc->id, 2);
        reply.fields = reply_fields;
        reply.field_count = (uint16_t)fc;
        send_transaction_to_client(cc, &reply);

        int fi;
        for (fi = 0; fi < fc; fi++) hl_field_free(&reply_fields[fi]);
        reply.fields = NULL;
    }

    /* Step 2: Send UserAccess bitmap as new transaction */
    if (cc->account) {
        hl_field_t access_field;
        hl_field_new(&access_field, FIELD_USER_ACCESS, cc->account->access, 8);
        hl_transaction_t access_tran;
        hl_transaction_new(&access_tran, TRAN_USER_ACCESS, cc->id, &access_field, 1);
        send_transaction_to_client(cc, &access_tran);
        hl_field_free(&access_field);
        hl_transaction_free(&access_tran);
    }

    /* Step 3: Send ShowAgreement as new transaction */
    {
        int show_agreement = (srv->agreement && srv->agreement_len > 0);
        if (cc->account && hl_access_is_set(cc->account->access, ACCESS_NO_AGREEMENT)) {
            show_agreement = 0;
        }

        if (show_agreement) {
            hl_field_t agree_field;
            hl_field_new(&agree_field, FIELD_DATA, srv->agreement,
                         (uint16_t)(srv->agreement_len > 65535 ? 65535 : srv->agreement_len));
            hl_transaction_t agree_tran;
            hl_transaction_new(&agree_tran, TRAN_SHOW_AGREEMENT, cc->id,
                               &agree_field, 1);
            send_transaction_to_client(cc, &agree_tran);
            hl_field_free(&agree_field);
            hl_transaction_free(&agree_tran);
        } else {
            /* No agreement — send ShowAgreement with NoServerAgreement=1 */
            uint8_t no_agree = 1;
            hl_field_t na_field;
            /* Field 152 = FieldNoServerAgreement (reuse FIELD_BANNER_TYPE slot) */
            uint8_t field_no_agree[2] = {0x00, 0x98};
            hl_field_new(&na_field, field_no_agree, &no_agree, 1);
            hl_transaction_t na_tran;
            hl_transaction_new(&na_tran, TRAN_SHOW_AGREEMENT, cc->id,
                               &na_field, 1);
            send_transaction_to_client(cc, &na_tran);
            hl_field_free(&na_field);
            hl_transaction_free(&na_tran);
        }
    }

    if (!cc->chat_history_capable && srv->chat_history &&
        srv->config.chat_history_enabled &&
        srv->config.chat_history_legacy_broadcast &&
        srv->config.chat_history_legacy_count > 0 &&
        hl_client_conn_authorize(cc, ACCESS_READ_CHAT)) {
        lm_chat_entry_t *entries = NULL;
        size_t n = 0;
        uint8_t has_more = 0;
        uint16_t limit = (uint16_t)srv->config.chat_history_legacy_count;
        if (limit > LM_CHAT_HISTORY_MAX_LIMIT) limit = LM_CHAT_HISTORY_MAX_LIMIT;
        if (lm_chat_history_query(srv->chat_history, 0, 0, 0, limit,
                                  &entries, &n, &has_more) == 0 && entries) {
            size_t i;
            for (i = 0; i < n; i++) {
                lm_chat_entry_t *e = &entries[i];
                char line[4200];
                char wire[4200];
                int line_len;
                int wire_len;
                int want_macroman;

                if (e->flags & HL_CHAT_FLAG_IS_ACTION) {
                    line_len = snprintf(line, sizeof(line),
                                        "\r *** %s %s", e->nick, e->body);
                } else if (e->flags & HL_CHAT_FLAG_IS_SERVER_MSG) {
                    line_len = snprintf(line, sizeof(line), "\r %s", e->body);
                } else {
                    line_len = snprintf(line, sizeof(line),
                                        "\r %s: %s", e->nick, e->body);
                }
                if (line_len < 0) continue;
                if (line_len > (int)sizeof(line) - 1)
                    line_len = (int)sizeof(line) - 1;

                want_macroman = cc->utf8_encoding ? 0 : srv->use_mac_roman;
                wire_len = line_len;
                if (want_macroman) {
                    int w = hl_utf8_to_macroman(line, (size_t)line_len,
                                                wire, sizeof(wire));
                    if (w < 0) {
                        memcpy(wire, line, (size_t)line_len);
                    } else {
                        wire_len = w;
                    }
                } else {
                    memcpy(wire, line, (size_t)line_len);
                }

                {
                    hl_field_t cf;
                    hl_transaction_t ct;
                    hl_field_new(&cf, FIELD_DATA, (const uint8_t *)wire,
                                 (uint16_t)wire_len);
                    hl_transaction_new(&ct, TRAN_CHAT_MSG, cc->id, &cf, 1);
                    send_transaction_to_client(cc, &ct);
                    hl_field_free(&cf);
                    hl_transaction_free(&ct);
                }
            }
            lm_chat_history_entries_free(entries);
        }
    }

    /* Notify others immediately for 1.2.3-style login flows where the
     * nickname was already provided in TranLogin. Newer clients are
     * announced on TranAgreed after sending user info/options. */
    if (f_name && f_name->data_len > 0) {
        broadcast_notify_change_user(srv, cc);
    }

    log_chat_history_event(cc, "signed on");

    hl_transaction_free(&login_tran);

    /* Clear socket timeouts — client is now managed by event loop */
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* Register client fd with event loop for read events */
    if (hl_event_add_fd(evloop, client_fd, cc) < 0) {
        hl_log_error(srv->logger, "Failed to register client fd with event loop");
        disconnect_client(srv, cc, evloop);
        return;
    }
}

/* --- Handle file transfer connection --- */

/* --- Folder download helpers --- */

/* Encode a relative path into the Hotline folder download FileHeader format:
 * [2 bytes] path_item_count
 * For each segment: [2 bytes reserved][1 byte name_len][N bytes name]
 * Returns bytes written to buf. */
static int encode_file_path(const char *rel_path, uint8_t *buf, size_t buf_len)
{
    /* Count segments */
    int seg_count = 0;
    const char *p = rel_path;
    while (*p) {
        if (*p == '/') { seg_count++; p++; continue; }
        seg_count++;
        while (*p && *p != '/') p++;
    }

    size_t pos = 0;
    if (pos + 2 > buf_len) return -1;
    hl_write_u16(buf + pos, (uint16_t)seg_count);
    pos += 2;

    p = rel_path;
    while (*p) {
        if (*p == '/') { p++; continue; }
        const char *seg_start = p;
        while (*p && *p != '/') p++;
        size_t seg_len = (size_t)(p - seg_start);
        if (seg_len > 255) seg_len = 255;
        if (pos + 3 + seg_len > buf_len) return -1;
        buf[pos++] = 0; buf[pos++] = 0; /* reserved */
        buf[pos++] = (uint8_t)seg_len;
        memcpy(buf + pos, seg_start, seg_len);
        pos += seg_len;
    }
    return (int)pos;
}

/* Send a single file's data in FILP format during folder download.
 * Returns 0 on success, -1 on error. */
static int send_folder_file(hl_tls_conn_t *conn, const char *file_path,
                             const char *filename)
{
    FILE *f = fopen(file_path, "rb");
    if (!f) return -1;

    fseek(f, 0, SEEK_END);
    uint32_t file_size = (uint32_t)ftell(f);
    fseek(f, 0, SEEK_SET);

    size_t name_len = strlen(filename);
    if (name_len > 128) name_len = 128;

    /* Calculate and send transfer size (4 bytes) */
    uint32_t info_data_size = 72 + (uint32_t)name_len + 2;
    uint32_t xfer_size = 24 + 16 + info_data_size + 16 + file_size;
    uint8_t xfer_buf[4];
    hl_write_u32(xfer_buf, xfer_size);
    if (hl_conn_write_all(conn, xfer_buf, 4) < 0) { fclose(f); return -1; }

    /* FILP header */
    uint8_t filp_hdr[24];
    memset(filp_hdr, 0, sizeof(filp_hdr));
    memcpy(filp_hdr, "FILP", 4);
    filp_hdr[4] = 0; filp_hdr[5] = 1;
    filp_hdr[22] = 0; filp_hdr[23] = 2;
    if (hl_conn_write_all(conn, filp_hdr, 24) < 0) { fclose(f); return -1; }

    /* INFO fork header */
    uint8_t info_hdr[16];
    memset(info_hdr, 0, sizeof(info_hdr));
    memcpy(info_hdr, "INFO", 4);
    hl_write_u32(info_hdr + 12, info_data_size);
    if (hl_conn_write_all(conn, info_hdr, 16) < 0) { fclose(f); return -1; }

    /* INFO fork data */
    const hl_file_type_entry_t *ftype = hl_file_type_from_filename(filename);
    uint8_t info_data[256];
    memset(info_data, 0, sizeof(info_data));
    memcpy(info_data, "AMAC", 4);
    memcpy(info_data + 4, ftype->type, 4);
    memcpy(info_data + 8, ftype->creator, 4);
    info_data[40] = 0; info_data[41] = 0x01;
    hl_write_u16(info_data + 70, (uint16_t)name_len);
    memcpy(info_data + 72, filename, name_len);
    info_data[72 + name_len] = 0;
    info_data[72 + name_len + 1] = 0;
    if (hl_conn_write_all(conn, info_data, info_data_size) < 0) { fclose(f); return -1; }

    /* DATA fork header */
    uint8_t data_hdr[16];
    memset(data_hdr, 0, sizeof(data_hdr));
    memcpy(data_hdr, "DATA", 4);
    hl_write_u32(data_hdr + 12, file_size);
    if (hl_conn_write_all(conn, data_hdr, 16) < 0) { fclose(f); return -1; }

    /* DATA fork data */
    uint8_t fbuf[8192];
    size_t n;
    while ((n = fread(fbuf, 1, sizeof(fbuf), f)) > 0) {
        if (hl_conn_write_all(conn, fbuf, n) < 0) { fclose(f); return -1; }
    }
    fclose(f);
    return 0;
}

/* Recursively send folder contents using the interactive folder download protocol.
 * root_path: the base folder path (for computing relative paths)
 * dir_path: current directory being enumerated
 * Returns 0 on success, -1 on error/disconnect. */
static int send_folder_recursive(hl_server_t *srv, hl_tls_conn_t *conn,
                                  const char *root_path, const char *dir_path)
{
    DIR *dir = opendir(dir_path);
    if (!dir) return -1;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        /* Compute relative path from root */
        const char *rel = full_path + strlen(root_path) + 1;

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        int is_dir = S_ISDIR(st.st_mode);

        /* Build and send FileHeader: [2-byte size][2-byte type][encoded path] */
        uint8_t path_buf[1024];
        int path_len = encode_file_path(rel, path_buf, sizeof(path_buf));
        if (path_len < 0) continue;

        uint16_t header_size = 2 + (uint16_t)path_len; /* type(2) + path */
        uint8_t file_header[1030];
        size_t hpos = 0;
        hl_write_u16(file_header + hpos, header_size); hpos += 2;
        hl_write_u16(file_header + hpos, is_dir ? 1 : 0); hpos += 2;
        memcpy(file_header + hpos, path_buf, (size_t)path_len); hpos += (size_t)path_len;

        if (hl_conn_write_all(conn, file_header, hpos) < 0) { closedir(dir); return -1; }

        /* Read client action */
        uint8_t action[2];
        if (hl_conn_read_full(conn, action, 2) < 0) { closedir(dir); return -1; }

        if (is_dir) {
            /* Recurse into subdirectory */
            if (send_folder_recursive(srv, conn, root_path, full_path) < 0) {
                closedir(dir);
                return -1;
            }
        } else if (action[1] == HL_DL_FLDR_ACTION_SEND_FILE) {
            /* Send file data */
            if (send_folder_file(conn, full_path, entry->d_name) < 0) {
                hl_log_error(srv->logger, "Folder download: failed to send %s", rel);
                closedir(dir);
                return -1;
            }
            /* Read next action (client acknowledges receipt) */
            if (hl_conn_read_full(conn, action, 2) < 0) { closedir(dir); return -1; }
        }
        /* action[1] == DL_FLDR_ACTION_NEXT_FILE (3) — skip, continue loop */
    }

    closedir(dir);
    return 0;
}

/* Write data through AEAD framing for encrypted file transfers.
 * Seals `data` into a length-prefixed AEAD frame and writes it.
 * Uses server->client direction (0x00). */
static int aead_write_frame(hl_tls_conn_t *conn, const uint8_t key[32],
                            uint64_t *counter, const uint8_t *data, size_t data_len)
{
    size_t frame_size = 4 + data_len + HL_POLY1305_TAG_SIZE;
    uint8_t *frame = (uint8_t *)malloc(frame_size);
    if (!frame) return -1;

    /* Build nonce: direction 0x00 (server->client) + counter */
    uint8_t nonce[12];
    memset(nonce, 0, 12);
    nonce[4]  = (uint8_t)(*counter >> 56);
    nonce[5]  = (uint8_t)(*counter >> 48);
    nonce[6]  = (uint8_t)(*counter >> 40);
    nonce[7]  = (uint8_t)(*counter >> 32);
    nonce[8]  = (uint8_t)(*counter >> 24);
    nonce[9]  = (uint8_t)(*counter >> 16);
    nonce[10] = (uint8_t)(*counter >> 8);
    nonce[11] = (uint8_t)(*counter);

    uint8_t *ct = frame + 4;
    uint8_t *tag = ct + data_len;
    hl_chacha20_poly1305_encrypt(key, nonce, data, data_len, ct, tag);

    uint32_t payload = (uint32_t)(data_len + HL_POLY1305_TAG_SIZE);
    frame[0] = (uint8_t)(payload >> 24);
    frame[1] = (uint8_t)(payload >> 16);
    frame[2] = (uint8_t)(payload >> 8);
    frame[3] = (uint8_t)(payload);

    int rc = hl_conn_write_all(conn, frame, frame_size);
    free(frame);
    (*counter)++;
    return rc;
}

static void handle_file_transfer_connection(hl_server_t *srv, int client_fd,
                                             hl_tls_conn_t *xfer_conn)
{
    /* Create unified connection wrapper for file transfer I/O */
    hl_tls_conn_t *conn = xfer_conn ? xfer_conn : hl_conn_wrap_plain(client_fd);
    if (!conn) { close(client_fd); return; }

    /* Read 16-byte HTXF header to get reference number */
    uint8_t hdr_buf[HL_TRANSFER_HEADER_SIZE];
    if (hl_conn_read_full(conn, hdr_buf, HL_TRANSFER_HEADER_SIZE) < 0) {
        hl_conn_close(conn);
        return;
    }

    hl_transfer_header_t hdr;
    if (hl_transfer_header_parse(&hdr, hdr_buf, HL_TRANSFER_HEADER_SIZE) < 0 ||
        !hl_transfer_header_valid(&hdr)) {
        hl_conn_close(conn);
        return;
    }

    hl_log_info(srv->logger, "File transfer connection, ref=%02x%02x%02x%02x",
                hdr.reference_number[0], hdr.reference_number[1],
                hdr.reference_number[2], hdr.reference_number[3]);

    /* Look up transfer by ref num */
    hl_file_transfer_t *ft = NULL;
    if (srv->file_transfer_mgr) {
        ft = srv->file_transfer_mgr->vt->get(srv->file_transfer_mgr,
                                               hdr.reference_number);
    }

    if (!ft) {
        hl_log_error(srv->logger, "No transfer found for ref number");
        hl_conn_close(conn);
        return;
    }

    /* Derive per-transfer AEAD key if this is an AEAD transfer */
    uint8_t xfer_key[32];
    int xfer_aead = 0;
    uint64_t xfer_send_counter = 0;
    uint64_t xfer_recv_counter = 0;
    (void)xfer_recv_counter; /* Used when AEAD upload decryption is implemented */

    if (ft->ft_aead) {
        hl_hkdf_sha256(ft->ft_base_key, 32,
                       hdr.reference_number, 4,
                       (const uint8_t *)"hope-ft-ref", 11,
                       xfer_key, 32);
        xfer_aead = 1;
        hl_log_info(srv->logger, "AEAD file transfer key derived for ref %02x%02x%02x%02x",
                    hdr.reference_number[0], hdr.reference_number[1],
                    hdr.reference_number[2], hdr.reference_number[3]);
    }

    /* Handle banner download — send raw banner data */
    if (ft->type == HL_XFER_BANNER_DOWNLOAD) {
        if (srv->banner && srv->banner_len > 0) {
            if (xfer_aead) {
                /* Seal banner into AEAD frame */
                size_t frame_size = 4 + srv->banner_len + HL_POLY1305_TAG_SIZE;
                uint8_t *frame = (uint8_t *)malloc(frame_size);
                if (frame) {
                    uint8_t nonce[12];
                    memset(nonce, 0, 12); /* direction 0x00 (server->client), counter 0 */
                    uint8_t *ct = frame + 4;
                    uint8_t *tag = ct + srv->banner_len;
                    hl_chacha20_poly1305_encrypt(xfer_key, nonce,
                        srv->banner, srv->banner_len, ct, tag);
                    uint32_t payload = (uint32_t)(srv->banner_len + HL_POLY1305_TAG_SIZE);
                    frame[0] = (uint8_t)(payload >> 24);
                    frame[1] = (uint8_t)(payload >> 16);
                    frame[2] = (uint8_t)(payload >> 8);
                    frame[3] = (uint8_t)(payload);
                    hl_conn_write_all(conn, frame, frame_size);
                    free(frame);
                }
            } else {
                hl_conn_write_all(conn, srv->banner, srv->banner_len);
            }
            hl_log_info(srv->logger, "Banner sent (%zu bytes, aead=%d)", srv->banner_len, xfer_aead);
        }
        /* Remove completed transfer (ft points into manager array — do NOT free) */
        srv->file_transfer_mgr->vt->del(srv->file_transfer_mgr,
                                         hdr.reference_number);
        hl_conn_close(conn);
        return;
    }

    /* Handle file download — send FILP-wrapped file data */
    if (ft->type == HL_XFER_FILE_DOWNLOAD) {
        FILE *f = fopen(ft->file_root, "rb");
        if (!f) {
            hl_log_error(srv->logger, "Failed to open file: %s", ft->file_root);
            srv->file_transfer_mgr->vt->del(srv->file_transfer_mgr,
                                             hdr.reference_number);
            hl_conn_close(conn);
            return;
        }

        /* Get file size */
        fseek(f, 0, SEEK_END);
        uint32_t file_size = (uint32_t)ftell(f);
        fseek(f, 0, SEEK_SET);

        /* Check for resume offset */
        uint32_t data_offset = 0;
        int resuming = 0;
        if (ft->resume_data && ft->resume_data->fork_info_count > 0) {
            data_offset = hl_read_u32(ft->resume_data->fork_info[0].data_size);
            if (data_offset > file_size) data_offset = file_size;
            resuming = 1;
            hl_log_info(srv->logger, "Resuming file download at offset %u/%u: %s",
                        data_offset, file_size, ft->file_root);
        }

        /* Extract filename from path */
        const char *filename = strrchr(ft->file_root, '/');
        filename = filename ? filename + 1 : ft->file_root;
        size_t name_len = strlen(filename);
        if (name_len > 128) name_len = 128;

        /* Build FILP header + INFO fork + DATA fork header */
        /* FILP header: "FILP" + version(2) + reserved(16) + fork_count(2) = 24 */
        uint8_t filp_hdr[24];
        memset(filp_hdr, 0, sizeof(filp_hdr));
        memcpy(filp_hdr, "FILP", 4);
        filp_hdr[4] = 0; filp_hdr[5] = 1; /* version 1 */
        filp_hdr[22] = 0; filp_hdr[23] = 2; /* 2 forks (INFO + DATA) */

        /* INFO fork header: "INFO" + compression(4) + rsvd(4) + data_size(4) = 16 */
        uint32_t info_data_size = 72 + (uint32_t)name_len + 2; /* fixed(72) + name + comment_size(2) */
        uint8_t info_fork_hdr[16];
        memset(info_fork_hdr, 0, sizeof(info_fork_hdr));
        memcpy(info_fork_hdr, "INFO", 4);
        hl_write_u32(info_fork_hdr + 12, info_data_size);

        /* INFO fork data: platform(4) + type(4) + creator(4) + flags(4) +
         * platform_flags(4) + rsvd(32) + create_date(8) + modify_date(8) +
         * name_script(2) + name_size(2) + name(n) + comment_size(2) */
        const hl_file_type_entry_t *ftype = hl_file_type_from_filename(filename);

        uint8_t info_data[256];
        memset(info_data, 0, sizeof(info_data));
        memcpy(info_data, "AMAC", 4); /* platform */
        memcpy(info_data + 4, ftype->type, 4);
        memcpy(info_data + 8, ftype->creator, 4);
        info_data[40] = 0; info_data[41] = 0x01; /* platform flags */
        hl_write_u16(info_data + 70, (uint16_t)name_len);
        memcpy(info_data + 72, filename, name_len);
        info_data[72 + name_len] = 0;
        info_data[72 + name_len + 1] = 0;

        /* DATA fork header: "DATA" + compression(4) + rsvd(4) + data_size(4) = 16
         * Note: data_size is the ORIGINAL full size, not adjusted for resume. */
        uint8_t data_fork_hdr[16];
        memset(data_fork_hdr, 0, sizeof(data_fork_hdr));
        memcpy(data_fork_hdr, "DATA", 4);
        hl_write_u32(data_fork_hdr + 12, file_size);

        /* Send FILP headers (always sent, even for resume) */
        if (xfer_aead) {
            /* Concatenate all FILP metadata into one frame */
            size_t meta_len = 24 + 16 + info_data_size + 16;
            uint8_t *meta = (uint8_t *)malloc(meta_len);
            if (meta) {
                size_t mpos = 0;
                memcpy(meta + mpos, filp_hdr, 24); mpos += 24;
                memcpy(meta + mpos, info_fork_hdr, 16); mpos += 16;
                memcpy(meta + mpos, info_data, info_data_size); mpos += info_data_size;
                memcpy(meta + mpos, data_fork_hdr, 16); mpos += 16;
                aead_write_frame(conn, xfer_key, &xfer_send_counter, meta, mpos);
                free(meta);
            }
        } else {
            hl_conn_write_all(conn, filp_hdr, 24);
            hl_conn_write_all(conn, info_fork_hdr, 16);
            hl_conn_write_all(conn, info_data, info_data_size);
            hl_conn_write_all(conn, data_fork_hdr, 16);
        }

        /* Seek past already-transferred bytes for resume */
        if (data_offset > 0) {
            fseek(f, (long)data_offset, SEEK_SET);
        }

        /* Send file data (from offset onward) */
        uint8_t fbuf[8192];
        size_t total_sent = 0;
        size_t n;
        while ((n = fread(fbuf, 1, sizeof(fbuf), f)) > 0) {
            int wrc;
            if (xfer_aead) {
                wrc = aead_write_frame(conn, xfer_key, &xfer_send_counter, fbuf, n);
            } else {
                wrc = hl_conn_write_all(conn, fbuf, n);
            }
            if (wrc < 0) break;
            total_sent += n;
        }
        fclose(f);

        hl_log_info(srv->logger, "File sent: %s (%zu bytes%s + FILP headers, aead=%d)",
                    ft->file_root, total_sent,
                    resuming ? ", resumed" : "", xfer_aead);

        srv->file_transfer_mgr->vt->del(srv->file_transfer_mgr,
                                         hdr.reference_number);
        hl_conn_close(conn);
        return;
    }

    /* Handle file upload — receive FILP-wrapped data from client */
    if (ft->type == HL_XFER_FILE_UPLOAD) {
        /* Set up AEAD stream reader if this is an encrypted transfer */
        hl_aead_stream_reader_t aead_reader;
        int use_aead_reader = 0;
        if (xfer_aead) {
            hl_aead_reader_init(&aead_reader, conn, xfer_key);
            use_aead_reader = 1;
            hl_log_info(srv->logger, "Upload: AEAD stream reader initialized");
        }

        /* Macros to abstract reads through plain or AEAD path */
        #define UPLOAD_READ_FULL(buf, len) \
            (use_aead_reader ? hl_aead_reader_read_full(&aead_reader, (buf), (len)) \
                             : hl_conn_read_full(conn, (buf), (len)))

        /* Step 1: Read FILP header (24 bytes) */
        uint8_t filp_hdr[HL_FLAT_FILE_HEADER_SIZE];
        if (UPLOAD_READ_FULL(filp_hdr, HL_FLAT_FILE_HEADER_SIZE) < 0) {
            hl_log_error(srv->logger, "Upload: failed to read FILP header (aead=%d)", xfer_aead);
            goto upload_cleanup;
        }
        if (memcmp(filp_hdr, "FILP", 4) != 0) {
            hl_log_error(srv->logger, "Upload: invalid FILP magic (got %02x%02x%02x%02x, aead=%d)",
                        filp_hdr[0], filp_hdr[1], filp_hdr[2], filp_hdr[3], xfer_aead);
            goto upload_cleanup;
        }

        /* Step 2: Read INFO fork header (16 bytes) to get info data size */
        uint8_t info_hdr[HL_FORK_HEADER_SIZE];
        if (UPLOAD_READ_FULL(info_hdr, HL_FORK_HEADER_SIZE) < 0) {
            hl_log_error(srv->logger, "Upload: failed to read INFO fork header");
            goto upload_cleanup;
        }
        uint32_t info_data_size = hl_read_u32(info_hdr + 12);
        if (info_data_size > 4096) {
            hl_log_error(srv->logger, "Upload: INFO fork too large (%u)", info_data_size);
            goto upload_cleanup;
        }

        /* Step 3: Read INFO fork data and discard (we use filename from handler) */
        if (info_data_size > 0) {
            uint8_t *info_buf = (uint8_t *)malloc(info_data_size);
            if (!info_buf) goto upload_cleanup;
            if (UPLOAD_READ_FULL(info_buf, info_data_size) < 0) {
                free(info_buf);
                hl_log_error(srv->logger, "Upload: failed to read INFO fork data");
                goto upload_cleanup;
            }
            free(info_buf);
        }

        /* Step 4: Read DATA fork header (16 bytes) to get file size */
        uint8_t data_hdr[HL_FORK_HEADER_SIZE];
        if (UPLOAD_READ_FULL(data_hdr, HL_FORK_HEADER_SIZE) < 0) {
            hl_log_error(srv->logger, "Upload: failed to read DATA fork header");
            goto upload_cleanup;
        }
        uint32_t data_size = hl_read_u32(data_hdr + 12);

        /* Step 5: Open .incomplete file for writing */
        char incomplete_path[1088];
        snprintf(incomplete_path, sizeof(incomplete_path), "%s.incomplete", ft->file_root);

        int out_fd = open(incomplete_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (out_fd < 0) {
            hl_log_error(srv->logger, "Upload: failed to create %s: %s",
                        incomplete_path, strerror(errno));
            goto upload_cleanup;
        }

        /* Step 6: Stream DATA fork from socket to file */
        uint8_t fbuf[8192];
        uint32_t remaining = data_size;
        size_t total_received = 0;
        int write_error = 0;

        while (remaining > 0) {
            size_t chunk = remaining < sizeof(fbuf) ? remaining : sizeof(fbuf);
            ssize_t r;
            if (use_aead_reader) {
                r = hl_aead_reader_read(&aead_reader, fbuf, chunk);
            } else {
                r = hl_conn_read(conn, fbuf, chunk);
            }
            if (r < 0) {
                if (!use_aead_reader && errno == EINTR) continue;
                hl_log_error(srv->logger, "Upload: read error%s",
                            use_aead_reader ? " (AEAD)" : "");
                write_error = 1;
                break;
            }
            if (r == 0) {
                hl_log_error(srv->logger, "Upload: client disconnected mid-transfer");
                write_error = 1;
                break;
            }
            if (write_all(out_fd, fbuf, (size_t)r) < 0) {
                hl_log_error(srv->logger, "Upload: write error: %s", strerror(errno));
                write_error = 1;
                break;
            }
            remaining -= (uint32_t)r;
            total_received += (size_t)r;
        }

        #undef UPLOAD_READ_FULL
        close(out_fd);

        if (write_error) {
            /* Leave .incomplete for possible resume later */
            hl_log_info(srv->logger, "Upload incomplete: %s (%zu/%u bytes)",
                        ft->file_root, total_received, data_size);
        } else {
            /* Rename .incomplete to final path */
            if (rename(incomplete_path, ft->file_root) != 0) {
                hl_log_error(srv->logger, "Upload: rename failed: %s", strerror(errno));
            } else {
                hl_log_info(srv->logger, "File received: %s (%zu bytes)",
                            ft->file_root, total_received);

                /* Mnemosyne: queue incremental file add */
                if (srv->mnemosyne_sync) {
                    const char *root = srv->config.file_root;
                    size_t root_len = strlen(root);
                    const char *rel = ft->file_root;
                    if (strncmp(rel, root, root_len) == 0 && rel[root_len] == '/')
                        rel = rel + root_len + 1;
                    /* Extract filename from path */
                    const char *fname = strrchr(rel, '/');
                    fname = fname ? fname + 1 : rel;
                    mn_queue_file_add((mn_sync_t *)srv->mnemosyne_sync,
                                      rel, fname, (uint64_t)total_received,
                                      "file", "");
                }
            }
        }

upload_cleanup:
        if (use_aead_reader) hl_aead_reader_free(&aead_reader);
        srv->file_transfer_mgr->vt->del(srv->file_transfer_mgr,
                                         hdr.reference_number);
        hl_conn_close(conn);
        return;
    }

    /* Handle folder download — interactive multi-file protocol */
    if (ft->type == HL_XFER_FOLDER_DOWNLOAD) {
        hl_log_info(srv->logger, "Folder download: %s", ft->file_root);
        /* Read initial 2-byte action from client */
        uint8_t init_action[2];
        if (hl_conn_read_full(conn, init_action, 2) < 0) {
            hl_log_error(srv->logger, "Folder download: client didn't send initial action");
            srv->file_transfer_mgr->vt->del(srv->file_transfer_mgr,
                                             hdr.reference_number);
            hl_conn_close(conn);
            return;
        }
        send_folder_recursive(srv, conn, ft->file_root, ft->file_root);
        srv->file_transfer_mgr->vt->del(srv->file_transfer_mgr,
                                         hdr.reference_number);
        hl_conn_close(conn);
        return;
    }

    /* Handle folder upload — receive multiple files from client */
    if (ft->type == HL_XFER_FOLDER_UPLOAD) {
        hl_log_info(srv->logger, "Folder upload: %s", ft->file_root);
        uint16_t item_count = hl_read_u16(ft->folder_item_count);

        /* Send initial action: "send first item" */
        uint8_t action_next[2] = {0x00, HL_DL_FLDR_ACTION_NEXT_FILE};
        if (hl_conn_write_all(conn, action_next, 2) < 0) goto folder_upload_done;

        uint16_t item;
        for (item = 0; item < item_count; item++) {
            /* Read folder upload header: [2-byte size][2-byte isFolder][2-byte pathItemCount] */
            uint8_t fu_hdr[6];
            if (hl_conn_read_full(conn, fu_hdr, 6) < 0) break;

            uint16_t data_size = hl_read_u16(fu_hdr);
            uint16_t is_folder = hl_read_u16(fu_hdr + 2);
            uint16_t path_item_count = hl_read_u16(fu_hdr + 4);

            /* Read path segments and build relative path */
            char rel_path[2048] = "";
            size_t rp_len = 0;
            uint16_t seg;
            for (seg = 0; seg < path_item_count; seg++) {
                uint8_t seg_hdr[3]; /* 2 reserved + 1 length */
                if (hl_conn_read_full(conn, seg_hdr, 3) < 0) goto folder_upload_done;
                uint8_t seg_len = seg_hdr[2];
                char seg_name[256];
                if (seg_len > 0) {
                    if (hl_conn_read_full(conn, (uint8_t *)seg_name, seg_len) < 0)
                        goto folder_upload_done;
                    seg_name[seg_len] = '\0';
                }
                /* Reject traversal attempts (e.g. ".." or embedded "/") */
                if (!hl_is_safe_path_component(seg_name, seg_len))
                    goto folder_upload_done;
                if (rp_len > 0 && rp_len < sizeof(rel_path) - 1)
                    rel_path[rp_len++] = '/';
                size_t copy_len = seg_len;
                if (rp_len + copy_len >= sizeof(rel_path))
                    copy_len = sizeof(rel_path) - rp_len - 1;
                memcpy(rel_path + rp_len, seg_name, copy_len);
                rp_len += copy_len;
            }
            rel_path[rp_len] = '\0';
            (void)data_size; /* consumed by path reading */

            /* Build full filesystem path */
            char item_path[2048];
            snprintf(item_path, sizeof(item_path), "%s/%s", ft->file_root, rel_path);

            if (is_folder) {
                /* Create directory */
                mkdir(item_path, 0755);
                hl_log_debug(srv->logger, "Folder upload: mkdir %s", rel_path);
                /* Tell client to send next item */
                if (hl_conn_write_all(conn, action_next, 2) < 0) break;
            } else {
                /* Tell client to send the file */
                uint8_t action_send[2] = {0x00, HL_DL_FLDR_ACTION_SEND_FILE};
                if (hl_conn_write_all(conn, action_send, 2) < 0) break;

                /* Read 4-byte transfer size (we don't use it, just need to advance) */
                uint8_t xfer_size_buf[4];
                if (hl_conn_read_full(conn, xfer_size_buf, 4) < 0) break;

                /* Read FILP header */
                uint8_t filp_hdr[HL_FLAT_FILE_HEADER_SIZE];
                if (hl_conn_read_full(conn, filp_hdr, HL_FLAT_FILE_HEADER_SIZE) < 0) break;

                /* Read INFO fork header + data (discard) */
                uint8_t info_fh[HL_FORK_HEADER_SIZE];
                if (hl_conn_read_full(conn, info_fh, HL_FORK_HEADER_SIZE) < 0) break;
                uint32_t info_sz = hl_read_u32(info_fh + 12);
                if (info_sz > 0 && info_sz < 65536) {
                    uint8_t *info_tmp = (uint8_t *)malloc(info_sz);
                    if (info_tmp) {
                        hl_conn_read_full(conn, info_tmp, info_sz);
                        free(info_tmp);
                    }
                }

                /* Read DATA fork header */
                uint8_t data_fh[HL_FORK_HEADER_SIZE];
                if (hl_conn_read_full(conn, data_fh, HL_FORK_HEADER_SIZE) < 0) break;
                uint32_t file_data_size = hl_read_u32(data_fh + 12);

                /* Receive file data into .incomplete file */
                char inc_path[2100];
                snprintf(inc_path, sizeof(inc_path), "%s.incomplete", item_path);
                int out_fd = open(inc_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (out_fd < 0) {
                    hl_log_error(srv->logger, "Folder upload: can't create %s", inc_path);
                    break;
                }

                uint8_t fbuf[8192];
                uint32_t rem = file_data_size;
                while (rem > 0) {
                    size_t chunk = rem < sizeof(fbuf) ? rem : sizeof(fbuf);
                    ssize_t r = hl_conn_read(conn, fbuf, chunk);
                    if (r <= 0) { close(out_fd); goto folder_upload_done; }
                    if (write_all(out_fd, fbuf, (size_t)r) < 0) { close(out_fd); goto folder_upload_done; }
                    rem -= (uint32_t)r;
                }
                close(out_fd);

                /* Rename .incomplete to final */
                rename(inc_path, item_path);
                hl_log_info(srv->logger, "Folder upload: received %s (%u bytes)",
                            rel_path, file_data_size);

                /* Tell client to send next item */
                if (hl_conn_write_all(conn, action_next, 2) < 0) break;
            }
        }

folder_upload_done:
        hl_log_info(srv->logger, "Folder upload complete: %s (%d items)",
                    ft->file_root, item_count);
        srv->file_transfer_mgr->vt->del(srv->file_transfer_mgr,
                                         hdr.reference_number);
        hl_conn_close(conn);
        return;
    }

    hl_log_info(srv->logger, "Unhandled transfer type %d", ft->type);
    srv->file_transfer_mgr->vt->del(srv->file_transfer_mgr,
                                     hdr.reference_number);
    hl_conn_close(conn);
}

/* --- Main event loop --- */

int hl_server_listen_and_serve(hl_server_t *srv)
{
    srv->listen_fd = create_listener(srv->net_interface, srv->port);
    if (srv->listen_fd < 0) {
        hl_log_error(srv->logger, "Failed to listen on port %d: %s",
                     srv->port, strerror(errno));
        return -1;
    }

    srv->transfer_fd = create_listener(srv->net_interface, srv->port + 1);
    if (srv->transfer_fd < 0) {
        hl_log_error(srv->logger, "Failed to listen on file transfer port %d: %s",
                     srv->port + 1, strerror(errno));
        return -1;
    }

    hl_log_info(srv->logger, "Listening on %s:%d (transfers on %d)",
                srv->net_interface[0] ? srv->net_interface : "0.0.0.0",
                srv->port, srv->port + 1);

    /* Create TLS listeners if configured */
    if (srv->tls_ctx.enabled && srv->tls_port > 0) {
        srv->tls_listen_fd = create_listener(srv->net_interface, srv->tls_port);
        if (srv->tls_listen_fd < 0) {
            hl_log_error(srv->logger, "Failed to listen on TLS port %d: %s",
                         srv->tls_port, strerror(errno));
        } else {
            srv->tls_transfer_fd = create_listener(srv->net_interface,
                                                    srv->tls_port + 1);
            if (srv->tls_transfer_fd < 0) {
                hl_log_error(srv->logger,
                    "Failed to listen on TLS transfer port %d: %s",
                    srv->tls_port + 1, strerror(errno));
                close(srv->tls_listen_fd);
                srv->tls_listen_fd = -1;
            } else {
                hl_log_info(srv->logger,
                    "TLS enabled on %s:%d (transfers on %d)",
                    srv->net_interface[0] ? srv->net_interface : "0.0.0.0",
                    srv->tls_port, srv->tls_port + 1);
            }
        }
    }

    hl_event_loop_t *evloop = hl_event_loop_new();
    if (!evloop) {
        hl_log_error(srv->logger, "Failed to create event loop: %s", strerror(errno));
        return -1;
    }

    if (hl_event_add_fd(evloop, srv->listen_fd, NULL) < 0 ||
        hl_event_add_fd(evloop, srv->transfer_fd, NULL) < 0) {
        hl_log_error(srv->logger, "Failed to register listener fds: %s", strerror(errno));
        hl_event_loop_free(evloop);
        return -1;
    }
    if (srv->tls_listen_fd >= 0)
        hl_event_add_fd(evloop, srv->tls_listen_fd, NULL);
    if (srv->tls_transfer_fd >= 0)
        hl_event_add_fd(evloop, srv->tls_transfer_fd, NULL);

    if (hl_event_add_timer(evloop, HL_TIMER_IDLE, HL_IDLE_CHECK_INTERVAL * 1000) < 0) {
        hl_log_error(srv->logger, "Failed to register timer: %s", strerror(errno));
        hl_event_loop_free(evloop);
        return -1;
    }

    /* Mnemosyne timers */
    mn_sync_t *mn_sync = (mn_sync_t *)srv->mnemosyne_sync;
    int mn_chunk_timer_active = 0;
    if (mn_sync && mn_sync_enabled(mn_sync)) {
        hl_event_add_timer(evloop, HL_TIMER_MN_HEARTBEAT, MN_HEARTBEAT_MS);
        hl_event_add_timer(evloop, HL_TIMER_MN_PERIODIC, MN_PERIODIC_MS);
        hl_log_info(srv->logger, "Mnemosyne timers registered (heartbeat 5m, periodic 15m)");
    }

    hl_log_info(srv->logger, "Server started, entering event loop");

    /* Tracker registration timer — register immediately, then every 300s */
    int tracker_elapsed = HL_TRACKER_UPDATE_FREQ; /* trigger on first tick */
    int chat_prune_elapsed = 0;
    const int chat_prune_freq = 3600;
    int chat_fsync_elapsed = 0;
    const int chat_fsync_freq = 60;

    while (!srv->shutdown) {
        hl_event_t events[64];

        int nev = hl_event_poll(evloop, events, 64, 1000);
        if (nev < 0) {
            hl_log_error(srv->logger, "Event poll failed: %s", strerror(errno));
            break;
        }

        int i;
        for (i = 0; i < nev; i++) {
            hl_event_t *ev = &events[i];

            if (ev->type == HL_EVENT_TIMER && ev->fd == HL_TIMER_IDLE) {
                /* Idle check — maps to Go keepaliveHandler() */
                int count = 0;
                hl_client_conn_t **clients = srv->client_mgr->vt->list(
                    srv->client_mgr, &count);
                if (clients) {
                    int j;
                    for (j = 0; j < count; j++) {
                        hl_client_conn_t *cc = clients[j];
                        pthread_rwlock_wrlock(&cc->mu);
                        cc->idle_time += HL_IDLE_CHECK_INTERVAL;

                        if (cc->idle_time > HL_USER_IDLE_SECONDS &&
                            !hl_user_flags_is_set(cc->flags, HL_USER_FLAG_AWAY)) {
                            hl_user_flags_set(cc->flags, HL_USER_FLAG_AWAY, 1);
                            /* Notify others that user is now away. */
                            broadcast_notify_change_user(srv, cc);
                        }
                        pthread_rwlock_unlock(&cc->mu);
                    }
                    free(clients);
                }

                if (srv->chat_history && srv->config.chat_history_enabled) {
                    chat_prune_elapsed += HL_IDLE_CHECK_INTERVAL;
                    if (chat_prune_elapsed >= chat_prune_freq) {
                        chat_prune_elapsed = 0;
                        if (lm_chat_history_prune(srv->chat_history) == 0) {
                            hl_log_info(srv->logger, "chat history prune: completed");
                        } else {
                            hl_log_error(srv->logger, "chat history prune: failed");
                        }
                    }

                    chat_fsync_elapsed += HL_IDLE_CHECK_INTERVAL;
                    if (chat_fsync_elapsed >= chat_fsync_freq) {
                        chat_fsync_elapsed = 0;
                        lm_chat_history_fsync(srv->chat_history);
                    }
                }

                /* Periodic tracker registration */
                tracker_elapsed += HL_IDLE_CHECK_INTERVAL;
                if (tracker_elapsed >= HL_TRACKER_UPDATE_FREQ &&
                    srv->config.enable_tracker_registration &&
                    srv->config.tracker_count > 0) {
                    tracker_elapsed = 0;
                    int user_count = 0;
                    hl_client_conn_t **ul = srv->client_mgr->vt->list(
                        srv->client_mgr, &user_count);
                    if (ul) free(ul);
                    int reg_ok = hl_tracker_register_all(
                        (const char (*)[256])srv->config.trackers,
                        srv->config.tracker_count,
                        (uint16_t)srv->port,
                        (uint16_t)user_count,
                        (uint16_t)srv->tls_port,
                        srv->tracker_pass_id,
                        srv->config.name,
                        srv->config.description);
                    if (reg_ok < srv->config.tracker_count) {
                        hl_log_error(srv->logger,
                            "Tracker re-registration: %d/%d succeeded",
                            reg_ok, srv->config.tracker_count);
                    }
                }

                /* Mnemosyne: startup delay — trigger first full sync after 30s */
                if (mn_sync && mn_sync_enabled(mn_sync) &&
                    !mn_sync->startup_delay_done) {
                    mn_sync->startup_ticks += HL_IDLE_CHECK_INTERVAL;
                    if (mn_sync->startup_ticks >= MN_STARTUP_DELAY_SECS) {
                        mn_sync->startup_delay_done = 1;
                        if (mn_load_cursor(mn_sync)) {
                            mn_sync->chunked_sync_active = 1;
                            hl_log_info(srv->logger,
                                "Mnemosyne: resuming interrupted sync");
                        } else {
                            mn_start_full_sync(mn_sync);
                        }
                        /* Start chunk timer if needed */
                        if (mn_sync->chunked_sync_active && !mn_chunk_timer_active) {
                            hl_event_add_timer(evloop, HL_TIMER_MN_CHUNK,
                                               MN_CHUNK_TICK_MS);
                            mn_chunk_timer_active = 1;
                        }
                    }
                }

                /* Check for SIGHUP reload */
                if (srv->reload_pending) {
                    srv->reload_pending = 0;

                    /* Reload config from disk */
                    if (srv->config_dir[0]) {
                        hl_log_info(srv->logger, "Reloading configuration (SIGHUP)");
                        mobius_load_config(&srv->config, srv->config_dir);
                    }

                    /* Mnemosyne: stop timers, reconfigure, restart */
                    if (mn_sync) {
                        hl_event_remove_timer(evloop, HL_TIMER_MN_HEARTBEAT);
                        hl_event_remove_timer(evloop, HL_TIMER_MN_PERIODIC);
                        if (mn_chunk_timer_active) {
                            hl_event_remove_timer(evloop, HL_TIMER_MN_CHUNK);
                            mn_chunk_timer_active = 0;
                        }

                        if (mn_sync_reconfigure(mn_sync, srv)) {
                            hl_event_add_timer(evloop, HL_TIMER_MN_HEARTBEAT, MN_HEARTBEAT_MS);
                            hl_event_add_timer(evloop, HL_TIMER_MN_PERIODIC, MN_PERIODIC_MS);
                            hl_log_info(srv->logger, "Mnemosyne: timers restarted after SIGHUP");
                            mn_start_full_sync(mn_sync);
                        } else {
                            hl_log_info(srv->logger, "Mnemosyne: sync disabled after SIGHUP");
                            srv->mnemosyne_sync = NULL;
                            mn_sync = NULL;
                        }
                    }
                }
            }
            else if (ev->type == HL_EVENT_TIMER && ev->fd == HL_TIMER_MN_HEARTBEAT) {
                /* Mnemosyne heartbeat */
                if (mn_sync && mn_sync_enabled(mn_sync)) {
                    mn_send_heartbeat(mn_sync);
                    /* Drain incrementals if not chunking */
                    mn_drain_incremental_queue(mn_sync);

                    /* Manage chunk tick timer */
                    if (mn_sync->chunked_sync_active && !mn_chunk_timer_active) {
                        hl_event_add_timer(evloop, HL_TIMER_MN_CHUNK, MN_CHUNK_TICK_MS);
                        mn_chunk_timer_active = 1;
                    } else if (!mn_sync->chunked_sync_active && mn_chunk_timer_active) {
                        hl_event_remove_timer(evloop, HL_TIMER_MN_CHUNK);
                        mn_chunk_timer_active = 0;
                    }
                }
            }
            else if (ev->type == HL_EVENT_TIMER && ev->fd == HL_TIMER_MN_PERIODIC) {
                /* Mnemosyne periodic drift check */
                if (mn_sync && mn_sync_enabled(mn_sync)) {
                    mn_periodic_check(mn_sync);

                    /* Start chunk timer if sync was triggered */
                    if (mn_sync->chunked_sync_active && !mn_chunk_timer_active) {
                        hl_event_add_timer(evloop, HL_TIMER_MN_CHUNK, MN_CHUNK_TICK_MS);
                        mn_chunk_timer_active = 1;
                    }
                }
            }
            else if (ev->type == HL_EVENT_TIMER && ev->fd == HL_TIMER_MN_CHUNK) {
                /* Mnemosyne chunk tick */
                if (mn_sync && mn_sync_enabled(mn_sync)) {
                    mn_do_sync_tick(mn_sync);

                    /* Remove chunk timer when sync finishes */
                    if (!mn_sync->chunked_sync_active && mn_chunk_timer_active) {
                        hl_event_remove_timer(evloop, HL_TIMER_MN_CHUNK);
                        mn_chunk_timer_active = 0;
                    }
                }
            }
            else if (ev->fd == srv->listen_fd) {
                /* New connection on protocol port */
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                int client_fd = accept(srv->listen_fd,
                                       (struct sockaddr *)&client_addr,
                                       &addr_len);
                if (client_fd >= 0) {
                    handle_new_connection(srv, client_fd, &client_addr,
                                         evloop, NULL);
                }
            }
            else if (ev->fd == srv->transfer_fd) {
                /* New connection on file transfer port */
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                int client_fd = accept(srv->transfer_fd,
                                       (struct sockaddr *)&client_addr,
                                       &addr_len);
                if (client_fd >= 0) {
                    handle_file_transfer_connection(srv, client_fd, NULL);
                }
            }
            else if (srv->tls_listen_fd >= 0 &&
                     ev->fd == srv->tls_listen_fd) {
                /* New TLS connection on protocol port */
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                int client_fd = accept(srv->tls_listen_fd,
                                       (struct sockaddr *)&client_addr,
                                       &addr_len);
                if (client_fd >= 0) {
                    hl_tls_conn_t *tconn = hl_tls_accept(&srv->tls_ctx,
                                                          client_fd);
                    if (tconn) {
                        handle_new_connection(srv, tconn->fd, &client_addr,
                                              evloop, tconn);
                    }
                }
            }
            else if (srv->tls_transfer_fd >= 0 &&
                     ev->fd == srv->tls_transfer_fd) {
                /* New TLS connection on file transfer port */
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                int client_fd = accept(srv->tls_transfer_fd,
                                       (struct sockaddr *)&client_addr,
                                       &addr_len);
                if (client_fd >= 0) {
                    hl_tls_conn_t *tconn = hl_tls_accept(&srv->tls_ctx,
                                                          client_fd);
                    if (tconn) {
                        handle_file_transfer_connection(srv, tconn->fd, tconn);
                    }
                }
            }
            else if (ev->type == HL_EVENT_EOF && ev->udata != NULL) {
                /* Client disconnected */
                hl_client_conn_t *cc = (hl_client_conn_t *)ev->udata;
                disconnect_client(srv, cc, evloop);
            }
            else if (ev->type == HL_EVENT_READ && ev->udata != NULL) {
                /* Data available from a connected client */
                hl_client_conn_t *cc = (hl_client_conn_t *)ev->udata;
                process_client_data(srv, cc, evloop);
            }
        }
    }

    hl_log_info(srv->logger, "Server shutting down");

    /* Disconnect all clients */
    int count = 0;
    hl_client_conn_t **clients = srv->client_mgr->vt->list(srv->client_mgr, &count);
    if (clients) {
        int j;
        for (j = 0; j < count; j++) {
            if (clients[j]->conn) {
                hl_conn_close(clients[j]->conn);
                clients[j]->conn = NULL;
                clients[j]->fd = -1;
            } else if (clients[j]->fd >= 0) {
                close(clients[j]->fd);
                clients[j]->fd = -1;
            }
        }
        free(clients);
    }

    hl_event_loop_free(evloop);
    return 0;
}
