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
#include "hotline/handshake.h"
#include "hotline/field.h"
#include "hotline/user.h"
#include "hotline/password.h"
#include "hotline/transfer.h"
#include "hotline/file_transfer.h"
#include "hotline/flattened_file_object.h"
#include "hotline/file_types.h"
#include "hotline/file_path.h"
#include "hotline/hope.h"
#include "mobius/transaction_handlers.h"
#include <openssl/sha.h> /* SHA_DIGEST_LENGTH for HOPE key derivation */

#include <sys/event.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
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

/* --- Helpers: read/write with EINTR handling --- */

static int read_full(int fd, uint8_t *buf, size_t n)
{
    size_t total = 0;
    while (total < n) {
        ssize_t r = read(fd, buf + total, n - total);
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return -1;
        total += (size_t)r;
    }
    return 0;
}

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

/* --- Send a transaction to a client fd --- */

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

/* --- Broadcast a transaction to all clients except sender --- */

void hl_server_broadcast(hl_server_t *srv, hl_client_conn_t *sender,
                         hl_transaction_t *t)
{
    int count = 0;
    hl_client_conn_t **clients = srv->client_mgr->vt->list(srv->client_mgr, &count);
    if (!clients) return;

    /* Serialize once, then send to each client (encrypting per-client if needed) */
    size_t wire_size = hl_transaction_wire_size(t);
    uint8_t *buf = (uint8_t *)malloc(wire_size);
    if (!buf) { free(clients); return; }

    int i;
    for (i = 0; i < count; i++) {
        if (!hl_type_eq(clients[i]->id, sender->id)) {
            memcpy(t->client_id, clients[i]->id, 2);
            int written = hl_transaction_serialize(t, buf, wire_size);
            if (written > 0) {
                /* Always route through hl_hope_write which handles
                 * both HOPE encryption and TLS via the conn wrapper. */
                hl_hope_write(clients[i], buf, (size_t)written);
            }
        }
    }
    free(buf);
    free(clients);
}

/* --- Send a transaction to a specific client --- */

void hl_server_send_to_client(hl_client_conn_t *cc, hl_transaction_t *t)
{
    memcpy(t->client_id, cc->id, 2);

    /* HOPE-aware send: encrypts if transport encryption is active.
     * For non-HOPE clients, falls through to plain write. */
    size_t wire_size = hl_transaction_wire_size(t);
    uint8_t *buf = (uint8_t *)malloc(wire_size);
    if (!buf) return;

    int written = hl_transaction_serialize(t, buf, wire_size);
    if (written > 0) {
        /* Always route through hl_hope_write which handles
         * both HOPE encryption and TLS via the conn wrapper. */
        hl_hope_write(cc, buf, (size_t)written);
    }
    free(buf);
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

    /* Disable Nagle's algorithm — Hotline sends many small transactions
     * (chat, keepalives, user notifications) and Nagle can add up to 200ms
     * latency per message. Safe to revert by removing this setsockopt call
     * if TCP_NODELAY causes throughput issues on slow links. */
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opt, sizeof(opt));

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

static void disconnect_client(hl_server_t *srv, hl_client_conn_t *cc, int kq)
{
    hl_log_info(srv->logger, "Client disconnected: %s (id=%d)",
                cc->remote_addr, hl_read_u16(cc->id));

    /* Remove from kqueue (closing fd does this automatically, but be explicit) */
    struct kevent change;
    EV_SET(&change, cc->fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
    kevent(kq, &change, 1, NULL, 0, NULL);

    close(cc->fd);
    cc->fd = -1;

    /* Notify other clients (skip for users without ShowInList permission) */
    if (!cc->account ||
        hl_access_is_set(cc->account->access, ACCESS_SHOW_IN_LIST)) {
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
    }

    /* Remove from client manager */
    srv->client_mgr->vt->del(srv->client_mgr, cc->id);
    hl_stats_decrement(srv->stats, HL_STAT_CURRENTLY_CONNECTED);

    hl_client_conn_free(cc);
}

/* --- Process incoming data from a client --- */

static void process_client_data(hl_server_t *srv, hl_client_conn_t *cc, int kq)
{
    /* Read available data into the client's accumulation buffer */
    if (cc->read_buf_len >= sizeof(cc->read_buf)) {
        hl_log_error(srv->logger, "Client %d buffer full, disconnecting",
                     hl_read_u16(cc->id));
        disconnect_client(srv, cc, kq);
        return;
    }

    /* Use HOPE-aware read — decrypts in-place if transport encryption is active.
     * Falls through to plain read() if HOPE is not active on this connection. */
    ssize_t n = hl_hope_read(cc,
                     cc->read_buf + cc->read_buf_len,
                     sizeof(cc->read_buf) - cc->read_buf_len);
    if (n < 0 && (errno == EINTR || errno == EAGAIN)) return;
    if (n <= 0) {
        disconnect_client(srv, cc, kq);
        return;
    }
    cc->read_buf_len += (size_t)n;

    /* Scan for complete transactions and dispatch */
    while (cc->read_buf_len > 0) {
        int tran_len = hl_transaction_scan(cc->read_buf, cc->read_buf_len);
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
                    /* Notify others that user is no longer away (skip hidden) */
                    if (!cc->account ||
                        hl_access_is_set(cc->account->access,
                                         ACCESS_SHOW_IN_LIST))
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
                            hl_server_send_to_client(target, resp);
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
    }
}

/* --- Handle a new client connection (handshake + login) --- */

static void handle_new_connection(hl_server_t *srv, int client_fd,
                                  struct sockaddr_in *client_addr, int kq,
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

    /* Quick peek to detect zero-data connections (e.g. tracker probes).
     * Trackers do a TCP connect-back with no data to verify the server
     * is reachable — handle these fast instead of blocking for seconds.
     * Skip for TLS connections (already past TLS handshake). */
    if (!tls_conn) {
        struct timeval peek_tv;
        peek_tv.tv_sec = 1;
        peek_tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &peek_tv, sizeof(peek_tv));
        uint8_t peek_buf[1];
        ssize_t peeked = recv(client_fd, peek_buf, 1, MSG_PEEK);
        if (peeked <= 0) {
            hl_log_info(srv->logger, "Zero-data connection from %s (tracker probe?)",
                        ip_str);
            hl_conn_close(conn);
            return;
        }
    }

    /* Set socket timeouts to prevent a slow client from blocking the event loop.
     * These are removed after login completes (client moves to non-blocking kqueue). */
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* Handshake (application-layer, runs over TLS if active) */
    if (hl_perform_handshake_server_conn(conn) < 0) {
        hl_log_info(srv->logger, "Handshake failed from %s (possible tracker probe)",
                    ip_str);
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

    /* --- HOPE Secure Login detection ---
     * If the login field is a single 0x00 byte, this is a HOPE Phase 1 probe.
     * We negotiate MAC/cipher, send a session key, then read the authenticated
     * Phase 3 login. If HOPE is disabled or not detected, fall through to
     * the legacy login flow. Remove this block if HOPE causes issues. */
    int hope_authenticated = 0;
    int hope_mac_algo = HL_HOPE_MAC_INVERSE;
    uint8_t hope_session_key[HL_HOPE_SESSION_KEY_LEN];
    int hope_cipher = HL_HOPE_CIPHER_NONE;
    char hope_password_plain[128] = ""; /* needed for key derivation */

    if (srv->config.enable_hope && hl_hope_detect(&login_tran)) {
        hl_log_info(srv->logger, "HOPE Phase 1 from %s", ip_str);

        /* Parse client's MAC algorithm preferences */
        const hl_field_t *f_mac = hl_transaction_get_field(&login_tran,
                                                            FIELD_HOPE_MAC_ALGORITHM);
        int client_algos[HL_HOPE_MAC_COUNT];
        int algo_count = 0;
        if (f_mac && f_mac->data_len > 0) {
            algo_count = hl_hope_parse_mac_list(f_mac->data, f_mac->data_len,
                                                 client_algos, HL_HOPE_MAC_COUNT);
        }
        hope_mac_algo = hl_hope_select_mac(client_algos, algo_count);

        /* Parse cipher preference */
        const hl_field_t *f_cipher = hl_transaction_get_field(&login_tran,
                                                               FIELD_HOPE_SERVER_CIPHER);
        if (f_cipher && f_cipher->data_len > 0) {
            char cipher_name[32];
            size_t clen = f_cipher->data_len < sizeof(cipher_name) - 1
                        ? f_cipher->data_len : sizeof(cipher_name) - 1;
            memcpy(cipher_name, f_cipher->data, clen);
            cipher_name[clen] = '\0';
            hope_cipher = hl_hope_parse_cipher(cipher_name);
        }

        /* Generate session key: IP(4) + port(2) + random(58) */
        uint32_t srv_ip = 0;
        if (srv->net_interface[0] != '\0') {
            srv_ip = ntohl(inet_addr(srv->net_interface));
        } else {
            srv_ip = ntohl(client_addr->sin_addr.s_addr);
        }
        hl_hope_generate_session_key(srv_ip, (uint16_t)ntohs(client_addr->sin_port),
                                      hope_session_key);

        /* Build HOPE Task reply (transaction type 0x00010000 = 65536) */
        {
            const char *mac_name = hl_hope_mac_name(hope_mac_algo);
            uint8_t login_marker = 0x01; /* non-empty = "use MAC auth" */

            /* Encode MAC algorithm in list format: <u16:count=1> <u8:len> <name>
             * Navigator's decode_algorithm_selection expects this wire format. */
            uint8_t mac_list[64];
            size_t mac_name_len = strlen(mac_name);
            hl_write_u16(mac_list, 1); /* count = 1 */
            mac_list[2] = (uint8_t)mac_name_len;
            memcpy(mac_list + 3, mac_name, mac_name_len);
            uint16_t mac_list_len = (uint16_t)(3 + mac_name_len);

            hl_field_t task_fields[5];
            int tfc = 0;
            hl_field_new(&task_fields[tfc++], FIELD_HOPE_SESSION_KEY,
                         hope_session_key, HL_HOPE_SESSION_KEY_LEN);
            hl_field_new(&task_fields[tfc++], FIELD_HOPE_MAC_ALGORITHM,
                         mac_list, mac_list_len);
            hl_field_new(&task_fields[tfc++], FIELD_USER_LOGIN,
                         &login_marker, 1);
            if (hope_cipher != HL_HOPE_CIPHER_NONE) {
                /* Encode cipher in list format: <u16:count=1> <u8:len> <name> */
                const char *cn = (hope_cipher == HL_HOPE_CIPHER_RC4) ? "RC4" : "BLOWFISH";
                size_t cn_len = strlen(cn);
                uint8_t cipher_list[32];
                hl_write_u16(cipher_list, 1);
                cipher_list[2] = (uint8_t)cn_len;
                memcpy(cipher_list + 3, cn, cn_len);
                hl_field_new(&task_fields[tfc++], FIELD_HOPE_SERVER_CIPHER,
                             cipher_list, (uint16_t)(3 + cn_len));
                hl_field_new(&task_fields[tfc++], FIELD_HOPE_CLIENT_CIPHER,
                             cipher_list, (uint16_t)(3 + cn_len));
            }

            hl_transaction_t task_reply;
            memset(&task_reply, 0, sizeof(task_reply));
            task_reply.is_reply = 1;
            /* Task type 65536 = 0x00010000 → type bytes {0x00, 0x00} with
             * the high bytes in the ID. Per HOPE spec, use the login_tran's ID. */
            memcpy(task_reply.id, login_tran.id, 4);
            task_reply.fields = task_fields;
            task_reply.field_count = (uint16_t)tfc;
            send_transaction_via_conn(conn, &task_reply);

            int fi;
            for (fi = 0; fi < tfc; fi++) hl_field_free(&task_fields[fi]);
            task_reply.fields = NULL;
        }

        /* Free Phase 1 login transaction and read Phase 3 */
        hl_transaction_free(&login_tran);
        memset(&login_tran, 0, sizeof(login_tran));

        /* Read Phase 3 login transaction */
        uint8_t p3_header[22];
        if (hl_conn_read_full(conn, p3_header, 22) < 0) {
            hl_log_error(srv->logger, "HOPE Phase 3 read failed from %s", ip_str);
            hl_conn_close(conn);
            return;
        }
        uint32_t p3_total = hl_read_u32(p3_header + 12);
        if (p3_total > 65535 || p3_total < 2) { hl_conn_close(conn); return; }

        size_t p3_len = 20 + p3_total;
        uint8_t *p3_buf = (uint8_t *)malloc(p3_len);
        if (!p3_buf) { hl_conn_close(conn); return; }
        memcpy(p3_buf, p3_header, 22);
        if (p3_len > 22) {
            if (hl_conn_read_full(conn, p3_buf + 22, p3_len - 22) < 0) {
                free(p3_buf);
                hl_conn_close(conn);
                return;
            }
        }

        if (hl_transaction_deserialize(&login_tran, p3_buf, p3_len) < 0) {
            free(p3_buf);
            hl_conn_close(conn);
            return;
        }
        free(p3_buf);

        hope_authenticated = 1;
        hl_log_info(srv->logger, "HOPE Phase 3 received from %s (MAC: %s, Cipher: %s)",
                    ip_str, hl_hope_mac_name(hope_mac_algo),
                    hope_cipher == HL_HOPE_CIPHER_RC4 ? "RC4" :
                    hope_cipher == HL_HOPE_CIPHER_BLOWFISH ? "Blowfish" : "none");
    }

    /* Extract login fields — from Phase 3 if HOPE, or from original if legacy */
    const hl_field_t *f_login = hl_transaction_get_field(&login_tran, FIELD_USER_LOGIN);
    const hl_field_t *f_password = hl_transaction_get_field(&login_tran, FIELD_USER_PASSWORD);
    const hl_field_t *f_name = hl_transaction_get_field(&login_tran, FIELD_USER_NAME);
    const hl_field_t *f_icon = hl_transaction_get_field(&login_tran, FIELD_USER_ICON_ID);
    const hl_field_t *f_version = hl_transaction_get_field(&login_tran, FIELD_VERSION);

    /* Decode login — HOPE uses MAC'd login, legacy uses 255-rotation */
    char login_str[128] = "guest";
    if (!hope_authenticated) {
        /* Legacy: 255-rotation obfuscated */
        if (f_login && f_login->data_len > 0) {
            hl_field_decode_obfuscated_string(f_login, login_str, sizeof(login_str));
        }
    } else {
        /* HOPE Phase 3: login field encoding depends on mac_login flag.
         * If mac_login was set (login_marker=0x01 in our reply), the client
         * sends MAC(login_bytes, session_key) — we must iterate all accounts
         * and find which one matches. If mac_login was not set, the login
         * is 255-XOR inverted (same as legacy). */
        if (f_login && f_login->data_len > 0) {
            if (hope_mac_algo != HL_HOPE_MAC_INVERSE) {
                /* MAC'd login — iterate all accounts to find a match */
                int found = 0;
                if (srv->account_mgr) {
                    int acct_count = 0;
                    hl_account_t **accts = srv->account_mgr->vt->list(
                        srv->account_mgr, &acct_count);
                    if (accts) {
                        int ai;
                        for (ai = 0; ai < acct_count; ai++) {
                            uint8_t expected_mac[20]; /* SHA1 is largest */
                            int mac_len = hl_hope_compute_mac(hope_mac_algo,
                                (const uint8_t *)accts[ai]->login,
                                strlen(accts[ai]->login),
                                hope_session_key, HL_HOPE_SESSION_KEY_LEN,
                                expected_mac, sizeof(expected_mac));
                            if (mac_len > 0 &&
                                (size_t)mac_len == f_login->data_len &&
                                memcmp(expected_mac, f_login->data, (size_t)mac_len) == 0) {
                                strncpy(login_str, accts[ai]->login,
                                        sizeof(login_str) - 1);
                                found = 1;
                                break;
                            }
                        }
                        free(accts);
                    }
                }
                if (!found) {
                    /* No account matched — try "guest" as fallback */
                    uint8_t guest_mac[20];
                    int gml = hl_hope_compute_mac(hope_mac_algo,
                        (const uint8_t *)"guest", 5,
                        hope_session_key, HL_HOPE_SESSION_KEY_LEN,
                        guest_mac, sizeof(guest_mac));
                    if (gml > 0 && (size_t)gml == f_login->data_len &&
                        memcmp(guest_mac, f_login->data, (size_t)gml) == 0) {
                        strncpy(login_str, "guest", sizeof(login_str) - 1);
                    }
                    /* If still no match, login_str stays "guest" as default */
                }
            } else {
                /* INVERSE: same as legacy 255-XOR */
                hl_field_decode_obfuscated_string(f_login, login_str,
                                                   sizeof(login_str));
            }
        }
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
        hl_conn_close(conn);
        return;
    }
    cc->conn = conn;              /* Transfer conn ownership to client */
    cc->is_tls = (tls_conn != NULL);
    cc->logger = srv->logger;

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
    if (srv->account_mgr) {
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
            send_transaction_via_conn(conn, &err_reply);
            hl_field_free(&err_field);
            err_reply.fields = NULL;
            hl_transaction_free(&login_tran);
            hl_client_conn_free(cc);
            close(client_fd);
            return;
        }

        /* Verify password if the account has one set */
        if (acct->password[0] != '\0') {
            int pw_ok = 0;

            if (hope_authenticated) {
                /* HOPE: verify MAC'd password against session key.
                 * Try HOPE-format password first (reversible), fall back
                 * to INVERSE which doesn't need plaintext. */
                if (f_password && f_password->data_len > 0) {
                    char plain_pw[128] = "";
                    int have_plain = 0;

                    /* Try to decrypt HOPE-stored password */
                    if (srv->hope_master_key_loaded &&
                        strncmp(acct->password, "hope:", 5) == 0) {
                        if (hl_hope_password_decrypt(srv->hope_master_key,
                                acct->password, plain_pw, sizeof(plain_pw)) == 0)
                            have_plain = 1;
                    }

                    if (have_plain) {
                        /* Full HMAC verification with plaintext password */
                        pw_ok = hl_hope_verify_password(hope_mac_algo,
                            plain_pw, hope_session_key,
                            f_password->data, f_password->data_len);
                        strncpy(hope_password_plain, plain_pw, sizeof(hope_password_plain) - 1);
                    } else if (hope_mac_algo == HL_HOPE_MAC_INVERSE) {
                        /* INVERSE fallback: decode and verify against stored hash */
                        char pw_str[128] = "";
                        hl_field_decode_obfuscated_string(f_password, pw_str, sizeof(pw_str));
                        pw_ok = hl_password_verify(pw_str, acct->password);
                        strncpy(hope_password_plain, pw_str, sizeof(hope_password_plain) - 1);
                        memset(pw_str, 0, sizeof(pw_str));
                    }
                    memset(plain_pw, 0, sizeof(plain_pw));
                }
            } else {
                /* Legacy: 255-rotation decode + SHA-1 hash verification */
                char password_str[128] = "";
                if (f_password && f_password->data_len > 0) {
                    hl_field_decode_obfuscated_string(f_password, password_str,
                                                       sizeof(password_str));
                }
                pw_ok = hl_password_verify(password_str, acct->password);
                memset(password_str, 0, sizeof(password_str));
            }

            if (!pw_ok) {
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
                send_transaction_via_conn(conn, &err_reply);
                hl_field_free(&err_field);
                err_reply.fields = NULL;
                hl_transaction_free(&login_tran);
                hl_client_conn_free(cc);
                close(client_fd);
                return;
            }
        } else if (hope_authenticated && f_password && f_password->data_len > 0) {
            /* Guest account (empty password) with HOPE — verify MAC of empty string */
            if (!hl_hope_verify_password(hope_mac_algo, "",
                    hope_session_key, f_password->data, f_password->data_len)) {
                /* MAC mismatch on empty password — shouldn't happen but be safe */
                hl_log_info(srv->logger, "HOPE guest MAC mismatch from %s", ip_str);
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

    /* Set HOPE state on the connection if authenticated via HOPE.
     * Transport encryption is initialized here if a cipher was negotiated. */
    if (hope_authenticated) {
        cc->hope_active = 1;
        cc->hope_mac_algo = hope_mac_algo;
        memcpy(cc->hope_session_key, hope_session_key, HL_HOPE_SESSION_KEY_LEN);

        /* Set up transport encryption if cipher was negotiated */
        if (hope_cipher == HL_HOPE_CIPHER_RC4 && hope_password_plain[0] != '\0') {
            uint8_t encode_key[SHA_DIGEST_LENGTH];
            uint8_t decode_key[SHA_DIGEST_LENGTH];
            const uint8_t *pw = (const uint8_t *)hope_password_plain;
            size_t pw_len = strlen(hope_password_plain);

            /* Compute password MAC for key derivation */
            uint8_t pw_mac[SHA_DIGEST_LENGTH];
            int mac_len = hl_hope_compute_mac(hope_mac_algo,
                pw, pw_len, hope_session_key, HL_HOPE_SESSION_KEY_LEN,
                pw_mac, sizeof(pw_mac));

            if (mac_len > 0) {
                int key_len = hl_hope_derive_keys(hope_mac_algo,
                    pw, pw_len, pw_mac, (size_t)mac_len,
                    encode_key, decode_key, sizeof(encode_key));
                if (key_len > 0) {
                    hl_hope_init_rc4(cc, encode_key, (size_t)key_len,
                                     decode_key, (size_t)key_len);
                    hl_log_info(srv->logger, "HOPE RC4 transport encryption active for %s",
                                ip_str);
                }
            }
            memset(encode_key, 0, sizeof(encode_key));
            memset(decode_key, 0, sizeof(decode_key));
            memset(pw_mac, 0, sizeof(pw_mac));
        }
        memset(hope_password_plain, 0, sizeof(hope_password_plain));
    }

    /* Large file mode — default on. Old clients simply ignore unknown fields.
     * If a client sends FIELD_CAPABILITIES, respect it; otherwise assume support. */
    cc->large_file_mode = 1;
    {
        const hl_field_t *f_caps = hl_transaction_get_field(&login_tran, FIELD_CAPABILITIES);
        if (f_caps && f_caps->data_len >= 2) {
            uint16_t client_caps = hl_read_u16(f_caps->data);
            if (!(client_caps & HL_CAPABILITY_LARGE_FILES))
                cc->large_file_mode = 0;
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

    /* Send login reply sequence — maps to Go login response in server.go:593-615
     *
     * 1. Reply to login transaction with FieldVersion, FieldCommunityBannerID, FieldServerName
     * 2. Send TranUserAccess with access bitmap (new transaction, not reply)
     * 3. Send TranShowAgreement with agreement text (new transaction, not reply)
     */

    /* Step 1: Login reply with server info + capabilities */
    {
        uint8_t version_bytes[2] = {0x00, 0xBE}; /* 190 = Hotline 1.8+ */
        uint8_t banner_id[2] = {0, 0};

        hl_field_t reply_fields[6]; /* version + banner + name + capabilities + access + room */
        int fc = 0;
        hl_field_new(&reply_fields[fc++], FIELD_VERSION, version_bytes, 2);
        hl_field_new(&reply_fields[fc++], FIELD_COMMUNITY_BANNER_ID, banner_id, 2);
        hl_field_new(&reply_fields[fc++], FIELD_SERVER_NAME,
                     (const uint8_t *)srv->config.name,
                     (uint16_t)strlen(srv->config.name));

        /* Echo confirmed capabilities back to client */
        if (cc->large_file_mode) {
            uint8_t caps[2];
            hl_write_u16(caps, HL_CAPABILITY_LARGE_FILES);
            hl_field_new(&reply_fields[fc++], FIELD_CAPABILITIES, caps, 2);
        }

        /* Include UserAccess in the login reply — modern clients (Navigator)
         * read it from the reply, not from a separate TranUserAccess push. */
        if (cc->account) {
            hl_field_new(&reply_fields[fc++], FIELD_USER_ACCESS,
                         cc->account->access, 8);
        }

        hl_transaction_t reply;
        memset(&reply, 0, sizeof(reply));
        reply.is_reply = 1;
        memcpy(reply.id, login_tran.id, 4);
        memcpy(reply.client_id, cc->id, 2);
        reply.fields = reply_fields;
        reply.field_count = (uint16_t)fc;
        send_transaction_via_conn(conn, &reply);

        int fi;
        for (fi = 0; fi < fc; fi++) hl_field_free(&reply_fields[fi]);
        reply.fields = NULL;
    }

    /* Step 2: Send UserAccess as a separate transaction for legacy clients
     * that expect it (Hotline 1.x). Modern clients read it from the login reply. */
    if (cc->account) {
        hl_field_t access_field;
        hl_field_new(&access_field, FIELD_USER_ACCESS, cc->account->access, 8);
        hl_transaction_t access_tran;
        hl_transaction_new(&access_tran, TRAN_USER_ACCESS, cc->id, &access_field, 1);
        send_transaction_via_conn(conn, &access_tran);
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
            send_transaction_via_conn(conn, &agree_tran);
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
            send_transaction_via_conn(conn, &na_tran);
            hl_field_free(&na_field);
            hl_transaction_free(&na_tran);
        }
    }

    /* Notify others immediately for 1.2.3-style login flows where the
     * nickname was already provided in TranLogin. Newer clients are
     * announced on TranAgreed after sending user info/options.
     * Skip notification for users without ShowInList permission. */
    if (f_name && f_name->data_len > 0) {
        if (!cc->account ||
            hl_access_is_set(cc->account->access, ACCESS_SHOW_IN_LIST))
            broadcast_notify_change_user(srv, cc);
    }

    hl_transaction_free(&login_tran);

    /* Clear socket timeouts — client is now managed by kqueue */
    tv.tv_sec = 0;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* Register client fd with kqueue for read events */
    struct kevent change;
    EV_SET(&change, client_fd, EVFILT_READ, EV_ADD, 0, 0, cc);
    if (kevent(kq, &change, 1, NULL, 0, NULL) < 0) {
        hl_log_error(srv->logger, "Failed to register client fd with kqueue");
        disconnect_client(srv, cc, kq);
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
static int send_folder_file(hl_tls_conn_t *conn, const char *file_path, const char *filename)
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

static void handle_file_transfer_connection(hl_server_t *srv, int client_fd,
                                             hl_tls_conn_t *xfer_conn)
{
    /* Wrap plain connections; TLS connections arrive pre-wrapped */
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

    /* Handle banner download — send raw banner data */
    if (ft->type == HL_XFER_BANNER_DOWNLOAD) {
        if (srv->banner && srv->banner_len > 0) {
            hl_conn_write_all(conn, srv->banner, srv->banner_len);
            hl_log_info(srv->logger, "Banner sent (%zu bytes)", srv->banner_len);
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
        hl_conn_write_all(conn, filp_hdr, 24);
        hl_conn_write_all(conn, info_fork_hdr, 16);
        hl_conn_write_all(conn, info_data, info_data_size);
        hl_conn_write_all(conn, data_fork_hdr, 16);

        /* Seek past already-transferred bytes for resume */
        if (data_offset > 0) {
            fseek(f, (long)data_offset, SEEK_SET);
        }

        /* Send file data (from offset onward) */
        uint8_t fbuf[8192];
        size_t total_sent = 0;
        size_t n;
        while ((n = fread(fbuf, 1, sizeof(fbuf), f)) > 0) {
            if (hl_conn_write_all(conn, fbuf, n) < 0) break;
            total_sent += n;
        }
        fclose(f);

        hl_log_info(srv->logger, "File sent: %s (%zu bytes%s + FILP headers)",
                    ft->file_root, total_sent,
                    resuming ? ", resumed" : "");

        srv->file_transfer_mgr->vt->del(srv->file_transfer_mgr,
                                         hdr.reference_number);
        hl_conn_close(conn);
        return;
    }

    /* Handle file upload — receive FILP-wrapped data from client */
    if (ft->type == HL_XFER_FILE_UPLOAD) {
        /* Step 1: Read FILP header (24 bytes) */
        uint8_t filp_hdr[HL_FLAT_FILE_HEADER_SIZE];
        if (hl_conn_read_full(conn, filp_hdr, HL_FLAT_FILE_HEADER_SIZE) < 0) {
            hl_log_error(srv->logger, "Upload: failed to read FILP header");
            goto upload_cleanup;
        }
        if (memcmp(filp_hdr, "FILP", 4) != 0) {
            hl_log_error(srv->logger, "Upload: invalid FILP magic");
            goto upload_cleanup;
        }

        /* Step 2: Read INFO fork header (16 bytes) to get info data size */
        uint8_t info_hdr[HL_FORK_HEADER_SIZE];
        if (hl_conn_read_full(conn, info_hdr, HL_FORK_HEADER_SIZE) < 0) {
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
            if (hl_conn_read_full(conn, info_buf, info_data_size) < 0) {
                free(info_buf);
                hl_log_error(srv->logger, "Upload: failed to read INFO fork data");
                goto upload_cleanup;
            }
            free(info_buf);
        }

        /* Step 4: Read DATA fork header (16 bytes) to get file size */
        uint8_t data_hdr[HL_FORK_HEADER_SIZE];
        if (hl_conn_read_full(conn, data_hdr, HL_FORK_HEADER_SIZE) < 0) {
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
            ssize_t r = hl_conn_read(conn, fbuf, chunk);
            if (r < 0) {
                if (errno == EINTR) continue;
                hl_log_error(srv->logger, "Upload: read error: %s", strerror(errno));
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
            }
        }

upload_cleanup:
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

    int kq = kqueue();
    if (kq < 0) {
        hl_log_error(srv->logger, "kqueue() failed: %s", strerror(errno));
        return -1;
    }

    struct kevent changes[6];
    int nchanges = 0;

    EV_SET(&changes[nchanges++], srv->listen_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    EV_SET(&changes[nchanges++], srv->transfer_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (srv->tls_listen_fd >= 0)
        EV_SET(&changes[nchanges++], srv->tls_listen_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    if (srv->tls_transfer_fd >= 0)
        EV_SET(&changes[nchanges++], srv->tls_transfer_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    EV_SET(&changes[nchanges++], 1, EVFILT_TIMER, EV_ADD, 0,
           HL_IDLE_CHECK_INTERVAL * 1000, NULL);

    if (kevent(kq, changes, nchanges, NULL, 0, NULL) < 0) {
        hl_log_error(srv->logger, "kevent() register failed: %s", strerror(errno));
        close(kq);
        return -1;
    }

    hl_log_info(srv->logger, "Server started, entering event loop");

    /* Tracker registration timer — register immediately, then every 300s */
    int tracker_elapsed = HL_TRACKER_UPDATE_FREQ; /* trigger on first tick */

    while (!srv->shutdown) {
        struct kevent events[64];
        struct timespec timeout = {1, 0};

        int nev = kevent(kq, NULL, 0, events, 64, &timeout);
        if (nev < 0) {
            if (errno == EINTR) continue;
            hl_log_error(srv->logger, "kevent() wait failed: %s", strerror(errno));
            break;
        }

        int i;
        for (i = 0; i < nev; i++) {
            struct kevent *ev = &events[i];

            if (ev->filter == EVFILT_TIMER) {
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
                            /* Notify others that user is now away (skip hidden) */
                            if (!cc->account ||
                                hl_access_is_set(cc->account->access,
                                                 ACCESS_SHOW_IN_LIST))
                                broadcast_notify_change_user(srv, cc);
                        }
                        pthread_rwlock_unlock(&cc->mu);
                    }
                    free(clients);
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
            }
            else if ((int)ev->ident == srv->listen_fd) {
                /* New connection on protocol port */
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                int client_fd = accept(srv->listen_fd,
                                       (struct sockaddr *)&client_addr,
                                       &addr_len);
                if (client_fd >= 0) {
                    /* TCP_NODELAY on accepted client socket — see create_listener()
                     * comment for rationale. Remove if causing throughput issues. */
                    int nodelay = 1;
                    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY,
                               &nodelay, sizeof(nodelay));
                    handle_new_connection(srv, client_fd, &client_addr, kq, NULL);
                }
            }
            else if ((int)ev->ident == srv->transfer_fd) {
                /* New connection on file transfer port */
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                int client_fd = accept(srv->transfer_fd,
                                       (struct sockaddr *)&client_addr,
                                       &addr_len);
                if (client_fd >= 0) {
                    /* TCP_NODELAY on transfer socket — file transfers send
                     * large chunks so this matters less, but keeps behavior
                     * consistent. Remove if causing throughput issues. */
                    int nodelay = 1;
                    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY,
                               &nodelay, sizeof(nodelay));
                    handle_file_transfer_connection(srv, client_fd, NULL);
                }
            }
            else if (srv->tls_listen_fd >= 0 &&
                     (int)ev->ident == srv->tls_listen_fd) {
                /* New TLS connection on protocol port */
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                int client_fd = accept(srv->tls_listen_fd,
                                       (struct sockaddr *)&client_addr,
                                       &addr_len);
                if (client_fd >= 0) {
                    int nodelay = 1;
                    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY,
                               &nodelay, sizeof(nodelay));
                    hl_tls_conn_t *tconn = hl_tls_accept(&srv->tls_ctx,
                                                          client_fd);
                    if (tconn) {
                        handle_new_connection(srv, tconn->fd, &client_addr,
                                              kq, tconn);
                    }
                    /* hl_tls_accept closes fd on failure */
                }
            }
            else if (srv->tls_transfer_fd >= 0 &&
                     (int)ev->ident == srv->tls_transfer_fd) {
                /* New TLS connection on file transfer port */
                struct sockaddr_in client_addr;
                socklen_t addr_len = sizeof(client_addr);
                int client_fd = accept(srv->tls_transfer_fd,
                                       (struct sockaddr *)&client_addr,
                                       &addr_len);
                if (client_fd >= 0) {
                    int nodelay = 1;
                    setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY,
                               &nodelay, sizeof(nodelay));
                    hl_tls_conn_t *tconn = hl_tls_accept(&srv->tls_ctx,
                                                          client_fd);
                    if (tconn) {
                        handle_file_transfer_connection(srv, tconn->fd, tconn);
                    }
                    /* hl_tls_accept closes fd on failure */
                }
            }
            else if (ev->filter == EVFILT_READ && ev->udata != NULL) {
                /* Data available from a connected client */
                hl_client_conn_t *cc = (hl_client_conn_t *)ev->udata;
                if (ev->flags & EV_EOF) {
                    disconnect_client(srv, cc, kq);
                } else {
                    process_client_data(srv, cc, kq);
                }
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
            if (clients[j]->fd >= 0) {
                close(clients[j]->fd);
                clients[j]->fd = -1;
            }
        }
        free(clients);
    }

    close(kq);
    return 0;
}
