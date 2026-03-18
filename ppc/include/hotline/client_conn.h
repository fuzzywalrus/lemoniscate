/*
 * client_conn.h - Per-client connection state
 *
 * Maps to: hotline/client_conn.go
 */

#ifndef HOTLINE_CLIENT_CONN_H
#define HOTLINE_CLIENT_CONN_H

#include "hotline/types.h"
#include "hotline/access.h"
#include "hotline/user.h"
#include "hotline/transaction.h"
#include "hotline/logger.h"
#include <pthread.h>

/* Forward declarations */
struct hl_server;

/* Ensure hl_client_conn_t typedef exists (may already be forward-declared
 * by client_manager.h or chat.h) */
#ifndef HL_CLIENT_CONN_TYPEDEF
#define HL_CLIENT_CONN_TYPEDEF
typedef struct hl_client_conn hl_client_conn_t;
#endif

/* Account info — maps to Go Account struct (subset needed for auth) */
typedef struct {
    char               login[128];
    char               name[128];
    char               password[128];   /* bcrypt hash */
    hl_access_bitmap_t access;
    char               file_root[1024]; /* Optional per-account file root */
} hl_account_t;

/* AccountManager vtable — maps to Go AccountManager interface */
typedef struct hl_account_mgr hl_account_mgr_t;
typedef struct {
    hl_account_t *(*get)(hl_account_mgr_t *self, const char *login);
    hl_account_t **(*list)(hl_account_mgr_t *self, int *out_count);
    int (*create)(hl_account_mgr_t *self, const hl_account_t *acct);
    int (*update)(hl_account_mgr_t *self, const hl_account_t *acct, const char *new_login);
    int (*del)(hl_account_mgr_t *self, const char *login);
} hl_account_mgr_vtable_t;

struct hl_account_mgr {
    const hl_account_mgr_vtable_t *vt;
};

/* BanMgr vtable — maps to Go BanMgr interface */
typedef struct hl_ban_mgr hl_ban_mgr_t;
typedef struct {
    int (*is_banned)(hl_ban_mgr_t *self, const char *ip);
    int (*is_username_banned)(hl_ban_mgr_t *self, const char *username);
    int (*is_nickname_banned)(hl_ban_mgr_t *self, const char *nickname);
    int (*add)(hl_ban_mgr_t *self, const char *ip);
} hl_ban_mgr_vtable_t;

struct hl_ban_mgr {
    const hl_ban_mgr_vtable_t *vt;
};

/*
 * hl_client_conn_t - Per-connection state
 * Maps to: Go ClientConn struct
 *
 * This struct is referenced by client_manager.h via forward declaration.
 * The 'id' field MUST be the first meaningful field after the fd for
 * the client_manager's sort comparator to work.
 */
struct hl_client_conn {
    int                 fd;              /* Go: Connection io.ReadWriteCloser */
    char                remote_addr[64]; /* Go: RemoteAddr string */
    hl_client_id_t      id;              /* Go: ID ClientID [2]byte */
    uint8_t             icon[2];         /* Go: Icon []byte (size 2) */
    uint8_t             version[2];      /* Go: Version []byte */

    pthread_mutex_t     flags_mu;        /* Go: FlagsMU sync.Mutex */
    hl_user_flags_t     flags;           /* Go: Flags UserFlags */

    uint8_t             user_name[256];  /* Go: UserName []byte */
    uint16_t            user_name_len;
    hl_account_t       *account;         /* Go: Account *Account (not owned) */
    int                 idle_time;       /* Go: IdleTime int (seconds) */
    struct hl_server   *server;          /* Go: Server *Server (backref) */
    uint8_t             auto_reply[256]; /* Go: AutoReply []byte */
    uint16_t            auto_reply_len;

    hl_logger_t        *logger;          /* Go: Logger *slog.Logger */

    /* Read buffer for transaction scanning */
    uint8_t             read_buf[65536];
    size_t              read_buf_len;

    pthread_rwlock_t    mu;              /* Go: mu sync.RWMutex */
};

/*
 * hl_client_conn_new - Allocate and initialize a client connection.
 * Maps to: Go code in handleNewConnection that builds ClientConn
 */
hl_client_conn_t *hl_client_conn_new(int fd, const char *remote_addr,
                                     struct hl_server *server);

/* Free a client connection (does NOT close the fd) */
void hl_client_conn_free(hl_client_conn_t *cc);

/*
 * hl_client_conn_authorize - Check if client has a specific permission.
 * Maps to: Go ClientConn.Authorize()
 */
int hl_client_conn_authorize(const hl_client_conn_t *cc, int access_bit);

/*
 * hl_client_conn_new_reply - Create a reply transaction.
 * Maps to: Go ClientConn.NewReply()
 * Caller must hl_transaction_free() the result.
 */
int hl_client_conn_new_reply(hl_client_conn_t *cc, const hl_transaction_t *request,
                             hl_transaction_t *reply,
                             const hl_field_t *fields, uint16_t field_count);

/*
 * hl_client_conn_new_err_reply - Create an error reply.
 * Maps to: Go ClientConn.NewErrReply()
 */
int hl_client_conn_new_err_reply(hl_client_conn_t *cc, const hl_transaction_t *request,
                                 hl_transaction_t *reply, const char *err_msg);

#endif /* HOTLINE_CLIENT_CONN_H */
