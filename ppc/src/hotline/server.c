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
#include "hotline/transfer.h"
#include "hotline/file_transfer.h"

#include <sys/event.h>
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
            hl_server_send_transaction(clients[i]->fd, t);
        }
    }
    free(clients);
}

/* --- Send a transaction to a specific client --- */

void hl_server_send_to_client(hl_client_conn_t *cc, hl_transaction_t *t)
{
    memcpy(t->client_id, cc->id, 2);
    hl_server_send_transaction(cc->fd, t);
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
    }

    srv->listen_fd = -1;
    srv->transfer_fd = -1;

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

static void process_client_data(hl_server_t *srv, hl_client_conn_t *cc, int kq)
{
    /* Read available data into the client's accumulation buffer */
    if (cc->read_buf_len >= sizeof(cc->read_buf)) {
        hl_log_error(srv->logger, "Client %d buffer full, disconnecting",
                     hl_read_u16(cc->id));
        disconnect_client(srv, cc, kq);
        return;
    }

    ssize_t n = read(cc->fd,
                     cc->read_buf + cc->read_buf_len,
                     sizeof(cc->read_buf) - cc->read_buf_len);
    if (n < 0 && errno == EINTR) return;
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
                    /* Notify others of status change */
                    hl_user_t u;
                    memcpy(u.id, cc->id, 2);
                    memcpy(u.icon, cc->icon, 2);
                    memcpy(u.flags, cc->flags, 2);
                    u.name_len = cc->user_name_len;
                    memcpy(u.name, cc->user_name, u.name_len);

                    uint8_t ubuf[512];
                    int ulen = hl_user_serialize(&u, ubuf, sizeof(ubuf));
                    if (ulen > 0) {
                        hl_transaction_t notify;
                        memset(&notify, 0, sizeof(notify));
                        memcpy(notify.type, TRAN_NOTIFY_CHANGE_USER, 2);
                        hl_field_t ufield;
                        hl_field_new(&ufield, FIELD_USERNAME_WITH_INFO, ubuf, (uint16_t)ulen);
                        notify.fields = &ufield;
                        notify.field_count = 1;
                        hl_server_broadcast(srv, cc, &notify);
                        hl_field_free(&ufield);
                        notify.fields = NULL;
                    }
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
                            hl_server_send_transaction(target->fd, resp);
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
                                  struct sockaddr_in *client_addr, int kq)
{
    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &client_addr->sin_addr, ip_str, sizeof(ip_str));

    /* Rate limiting check */
    if (!hl_server_rate_limit_check(srv, ip_str)) {
        hl_log_info(srv->logger, "Rate limited: %s", ip_str);
        close(client_fd);
        return;
    }

    /* Set socket timeouts to prevent a slow client from blocking the event loop.
     * These are removed after login completes (client moves to non-blocking kqueue). */
    struct timeval tv;
    tv.tv_sec = 10;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(client_fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    /* Handshake */
    if (hl_perform_handshake_server(client_fd) < 0) {
        hl_log_error(srv->logger, "Handshake failed: %s", ip_str);
        close(client_fd);
        return;
    }

    /* Read login transaction — maps to Go handleNewConnection() */
    /* Read enough data for the transaction header first */
    uint8_t header_buf[22];
    if (read_full(client_fd, header_buf, 22) < 0) {
        hl_log_error(srv->logger, "Failed to read login from %s", ip_str);
        close(client_fd);
        return;
    }

    uint32_t total_size = hl_read_u32(header_buf + 12);
    if (total_size > 65535 || total_size < 2) { /* Must hold at least param_count */
        close(client_fd);
        return;
    }

    size_t full_len = 20 + total_size;
    uint8_t *login_buf = (uint8_t *)malloc(full_len);
    if (!login_buf) { close(client_fd); return; }

    memcpy(login_buf, header_buf, 22);
    if (full_len > 22) {
        if (read_full(client_fd, login_buf + 22, full_len - 22) < 0) {
            free(login_buf);
            close(client_fd);
            return;
        }
    }

    /* Parse login transaction */
    hl_transaction_t login_tran;
    if (hl_transaction_deserialize(&login_tran, login_buf, full_len) < 0) {
        free(login_buf);
        close(client_fd);
        return;
    }
    free(login_buf);

    /* Extract login fields */
    const hl_field_t *f_login = hl_transaction_get_field(&login_tran, FIELD_USER_LOGIN);
    const hl_field_t *f_password = hl_transaction_get_field(&login_tran, FIELD_USER_PASSWORD);
    const hl_field_t *f_name = hl_transaction_get_field(&login_tran, FIELD_USER_NAME);
    const hl_field_t *f_icon = hl_transaction_get_field(&login_tran, FIELD_USER_ICON_ID);
    const hl_field_t *f_version = hl_transaction_get_field(&login_tran, FIELD_VERSION);

    /* Decode login (255-rotation obfuscated) */
    char login_str[128] = "guest";
    if (f_login && f_login->data_len > 0) {
        hl_field_decode_obfuscated_string(f_login, login_str, sizeof(login_str));
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
            hl_server_send_transaction(client_fd, &err_reply);
            hl_field_free(&err_field);
            err_reply.fields = NULL;
            hl_transaction_free(&login_tran);
            close(client_fd);
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
        close(client_fd);
        return;
    }
    cc->logger = srv->logger;

    /* Set user info from login fields */
    if (f_name && f_name->data_len > 0) {
        uint16_t len = f_name->data_len < sizeof(cc->user_name) - 1
                     ? f_name->data_len : (uint16_t)(sizeof(cc->user_name) - 1);
        memcpy(cc->user_name, f_name->data, len);
        cc->user_name_len = len;
    } else {
        memcpy(cc->user_name, login_str, strlen(login_str));
        cc->user_name_len = (uint16_t)strlen(login_str);
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
            hl_server_send_transaction(client_fd, &err_reply);
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
            /* TODO: bcrypt comparison when bcrypt library is integrated.
             * For now, compare plaintext password. */
            if (strcmp(password_str, acct->password) != 0) {
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
                hl_server_send_transaction(client_fd, &err_reply);
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

    /* Send login reply — maps to Go login response sequence */
    /* 1. ShowAgreement (if agreement exists and user doesn't have NoAgreement access) */
    int show_agreement = (srv->agreement && srv->agreement_len > 0);
    if (cc->account && hl_access_is_set(cc->account->access, ACCESS_NO_AGREEMENT)) {
        show_agreement = 0;
    }

    if (show_agreement) {
        hl_field_t agree_field;
        hl_field_new(&agree_field, FIELD_DATA, srv->agreement, (uint16_t)srv->agreement_len);
        hl_transaction_t agree_tran;
        memset(&agree_tran, 0, sizeof(agree_tran));
        agree_tran.is_reply = 1;
        memcpy(agree_tran.id, login_tran.id, 4);
        memcpy(agree_tran.type, TRAN_SHOW_AGREEMENT, 2);
        memcpy(agree_tran.client_id, cc->id, 2);
        agree_tran.fields = &agree_field;
        agree_tran.field_count = 1;
        hl_server_send_transaction(client_fd, &agree_tran);
        hl_field_free(&agree_field);
        agree_tran.fields = NULL;
    } else {
        /* No agreement — send empty reply to complete login */
        hl_transaction_t reply;
        memset(&reply, 0, sizeof(reply));
        reply.is_reply = 1;
        memcpy(reply.id, login_tran.id, 4);
        memcpy(reply.client_id, cc->id, 2);
        hl_server_send_transaction(client_fd, &reply);
    }

    /* 2. Send UserAccess bitmap */
    if (cc->account) {
        hl_field_t access_field;
        hl_field_new(&access_field, FIELD_USER_ACCESS, cc->account->access, 8);
        hl_transaction_t access_tran;
        memset(&access_tran, 0, sizeof(access_tran));
        memcpy(access_tran.type, TRAN_USER_ACCESS, 2);
        memcpy(access_tran.client_id, cc->id, 2);
        access_tran.fields = &access_field;
        access_tran.field_count = 1;
        hl_server_send_transaction(client_fd, &access_tran);
        hl_field_free(&access_field);
        access_tran.fields = NULL;
    }

    /* 3. Notify others of new user */
    hl_user_t u;
    memcpy(u.id, cc->id, 2);
    memcpy(u.icon, cc->icon, 2);
    memcpy(u.flags, cc->flags, 2);
    u.name_len = cc->user_name_len;
    memcpy(u.name, cc->user_name, u.name_len);

    uint8_t ubuf[512];
    int ulen = hl_user_serialize(&u, ubuf, sizeof(ubuf));
    if (ulen > 0) {
        hl_transaction_t notify;
        memset(&notify, 0, sizeof(notify));
        memcpy(notify.type, TRAN_NOTIFY_CHANGE_USER, 2);
        hl_field_t ufield;
        hl_field_new(&ufield, FIELD_USERNAME_WITH_INFO, ubuf, (uint16_t)ulen);
        notify.fields = &ufield;
        notify.field_count = 1;
        hl_server_broadcast(srv, cc, &notify);
        hl_field_free(&ufield);
        notify.fields = NULL;
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

static void handle_file_transfer_connection(hl_server_t *srv, int client_fd)
{
    /* Read 16-byte HTXF header to get reference number */
    uint8_t hdr_buf[HL_TRANSFER_HEADER_SIZE];
    if (read_full(client_fd, hdr_buf, HL_TRANSFER_HEADER_SIZE) < 0) {
        close(client_fd);
        return;
    }

    hl_transfer_header_t hdr;
    if (hl_transfer_header_parse(&hdr, hdr_buf, HL_TRANSFER_HEADER_SIZE) < 0 ||
        !hl_transfer_header_valid(&hdr)) {
        close(client_fd);
        return;
    }

    hl_log_info(srv->logger, "File transfer connection, ref=%02x%02x%02x%02x",
                hdr.reference_number[0], hdr.reference_number[1],
                hdr.reference_number[2], hdr.reference_number[3]);

    /* Look up transfer by ref num — actual download/upload logic would go here */
    /* For now, close after logging */
    close(client_fd);
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

    int kq = kqueue();
    if (kq < 0) {
        hl_log_error(srv->logger, "kqueue() failed: %s", strerror(errno));
        return -1;
    }

    struct kevent changes[4];
    int nchanges = 0;

    EV_SET(&changes[nchanges++], srv->listen_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    EV_SET(&changes[nchanges++], srv->transfer_fd, EVFILT_READ, EV_ADD, 0, 0, NULL);
    EV_SET(&changes[nchanges++], 1, EVFILT_TIMER, EV_ADD, 0,
           HL_IDLE_CHECK_INTERVAL * 1000, NULL);

    if (kevent(kq, changes, nchanges, NULL, 0, NULL) < 0) {
        hl_log_error(srv->logger, "kevent() register failed: %s", strerror(errno));
        close(kq);
        return -1;
    }

    hl_log_info(srv->logger, "Server started, entering event loop");

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

                            /* Notify others of away status */
                            hl_user_t u;
                            memcpy(u.id, cc->id, 2);
                            memcpy(u.icon, cc->icon, 2);
                            memcpy(u.flags, cc->flags, 2);
                            u.name_len = cc->user_name_len;
                            memcpy(u.name, cc->user_name, u.name_len);

                            uint8_t ubuf[512];
                            int ulen = hl_user_serialize(&u, ubuf, sizeof(ubuf));
                            if (ulen > 0) {
                                hl_transaction_t notify;
                                memset(&notify, 0, sizeof(notify));
                                memcpy(notify.type, TRAN_NOTIFY_CHANGE_USER, 2);
                                hl_field_t ufield;
                                hl_field_new(&ufield, FIELD_USERNAME_WITH_INFO,
                                             ubuf, (uint16_t)ulen);
                                notify.fields = &ufield;
                                notify.field_count = 1;
                                hl_server_broadcast(srv, cc, &notify);
                                hl_field_free(&ufield);
                                notify.fields = NULL;
                            }
                        }
                        pthread_rwlock_unlock(&cc->mu);
                    }
                    free(clients);
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
                    handle_new_connection(srv, client_fd, &client_addr, kq);
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
                    handle_file_transfer_connection(srv, client_fd);
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
