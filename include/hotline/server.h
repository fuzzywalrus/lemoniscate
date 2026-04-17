/*
 * server.h - Hotline server core
 */

#ifndef HOTLINE_SERVER_H
#define HOTLINE_SERVER_H

#include "hotline/types.h"
#include "hotline/config.h"
#include "hotline/handlers.h"
#include "hotline/logger.h"
#include "hotline/stats.h"
#include "hotline/file_store.h"
#include "hotline/client_manager.h"
#include "hotline/chat.h"
#include "hotline/client_conn.h"
#include "hotline/file_transfer.h"
#include "hotline/tracker.h"
#include "hotline/tls.h"
#include "mobius/flat_news.h"
#include "mobius/threaded_news_yaml.h"
#include <pthread.h>

/* Forward declaration for Mnemosyne sync */
typedef struct mn_sync mn_sync_opaque_t;

/* Forward declaration for chat history storage (see hotline/chat_history.h) */
typedef struct lm_chat_history_s lm_chat_history_t;

/* Rate limiter entry (token bucket) */
typedef struct hl_rate_limiter {
    char                     ip[64];
    double                   tokens;
    time_t                   last_check;
    struct hl_rate_limiter  *next;
} hl_rate_limiter_t;

/* The Hotline server */
typedef struct hl_server {
    char                net_interface[64];   /* Go: NetInterface string */
    int                 port;                /* Go: Port int */

    hl_rate_limiter_t  *rate_limiters;       /* Go: rateLimiters map[string]*rate.Limiter */
    pthread_mutex_t     rate_limiters_mu;     /* Go: rateLimitersMu sync.Mutex */

    /* Handler dispatch table indexed by transaction type code. */
    hl_handler_func_t   handlers[HL_HANDLER_TABLE_SIZE];

    hl_config_t         config;              /* Go: Config Config */
    hl_logger_t        *logger;              /* Go: Logger *slog.Logger */

    uint8_t             tracker_pass_id[4];  /* Go: TrackerPassID [4]byte */

    hl_stats_t         *stats;               /* Go: Stats Counter */
    hl_file_store_t    *fs;                  /* Go: FS FileStore */

    /* Outbox pipe: write end for producers, read end for the event loop. */
    int                 outbox_pipe[2];

    uint8_t            *agreement;           /* Go: Agreement io.ReadSeeker */
    size_t              agreement_len;
    uint8_t            *banner;              /* Go: Banner []byte */
    size_t              banner_len;

    hl_xfer_mgr_t      *file_transfer_mgr;   /* Go: FileTransferMgr FileTransferMgr */
    hl_client_mgr_t    *client_mgr;          /* Go: ClientMgr ClientManager */
    hl_chat_mgr_t      *chat_mgr;            /* Go: ChatMgr ChatManager */
    hl_account_mgr_t   *account_mgr;         /* Go: AccountManager AccountManager */
    mobius_threaded_news_t *threaded_news;    /* Go: ThreadedNewsMgr ThreadedNewsMgr */
    hl_ban_mgr_t       *ban_list;            /* Go: BanList BanMgr */
    mobius_flat_news_t *flat_news;            /* Go: MessageBoard io.ReadWriteSeeker */

    /* Text encoding.
     * Config "encoding" field selects macintosh (MacRoman) or utf-8.
     * On Tiger, CoreFoundation handles the conversion. */
    int                 use_mac_roman;       /* 1 = MacRoman, 0 = UTF-8 */

    void               *mnemosyne_sync;       /* mn_sync_t* (opaque to hotline layer) */

    lm_chat_history_t  *chat_history;         /* Chat history storage (NULL if disabled) */

    volatile int        shutdown;            /* Go: context cancellation */
    volatile int        reload_pending;      /* Set by SIGHUP handler */
    char                config_dir[1024];    /* Config directory for reloads */

    int                 listen_fd;           /* Main protocol listener */
    int                 transfer_fd;         /* File transfer listener */

    /* TLS state */
    hl_tls_server_ctx_t tls_ctx;             /* Loaded cert/key context */
    int                 tls_listen_fd;       /* TLS protocol listener (-1 = disabled) */
    int                 tls_transfer_fd;     /* TLS file transfer listener (-1 = disabled) */
    int                 tls_port;            /* TLS base port (0 = disabled) */
} hl_server_t;

/* --- Lifecycle --- */

/* Create a new server with default managers. */
hl_server_t *hl_server_new(void);

/* Free server and all owned resources */
void hl_server_free(hl_server_t *srv);

/* Start listening and serving. Blocks until shutdown. */
int hl_server_listen_and_serve(hl_server_t *srv);

/* Signal the server to shut down gracefully */
void hl_server_shutdown(hl_server_t *srv);

/* --- Handler registration --- */

/* Register a handler for a transaction type. */
void hl_server_handle_func(hl_server_t *srv, const hl_tran_type_t type,
                           hl_handler_func_t handler);

/* --- Transaction sending --- */

/* Send a serialized transaction to a client fd. Returns 0 on success. */
int hl_server_send_transaction(int fd, hl_transaction_t *t);

/* Broadcast a transaction to all clients except sender. */
void hl_server_broadcast(hl_server_t *srv, hl_client_conn_t *sender,
                         hl_transaction_t *t);

/* Send a transaction to a specific client. */
void hl_server_send_to_client(hl_client_conn_t *cc, hl_transaction_t *t);

/* --- Rate limiting --- */

/* Check if a connection from this IP is allowed.
 * Returns 1 if allowed, 0 if rate-limited. */
int hl_server_rate_limit_check(hl_server_t *srv, const char *ip);

#endif /* HOTLINE_SERVER_H */
