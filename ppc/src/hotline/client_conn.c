/*
 * client_conn.c - Per-client connection state
 *
 * Maps to: hotline/client_conn.go
 */

#include "hotline/client_conn.h"
#include "hotline/hope.h"
#include "hotline/tls.h"
#include <stdlib.h>
#include <string.h>

hl_client_conn_t *hl_client_conn_new(int fd, const char *remote_addr,
                                     struct hl_server *server)
{
    hl_client_conn_t *cc = (hl_client_conn_t *)calloc(1, sizeof(hl_client_conn_t));
    if (!cc) return NULL;

    cc->fd = fd;
    cc->conn = NULL;  /* Caller sets this after creation */
    cc->is_tls = 0;
    strncpy(cc->remote_addr, remote_addr, sizeof(cc->remote_addr) - 1);
    cc->server = server;
    cc->idle_time = 0;

    pthread_mutex_init(&cc->flags_mu, NULL);
    pthread_rwlock_init(&cc->mu, NULL);

    return cc;
}

void hl_client_conn_free(hl_client_conn_t *cc)
{
    if (!cc) return;
    if (cc->hope) {
        hl_hope_state_free(cc->hope);
        free(cc->hope);
        cc->hope = NULL;
    }
    if (cc->conn) {
        hl_conn_free(cc->conn);  /* Free wrapper without closing fd */
        cc->conn = NULL;
    }
    pthread_mutex_destroy(&cc->flags_mu);
    pthread_rwlock_destroy(&cc->mu);
    free(cc);
}

int hl_client_conn_authorize(const hl_client_conn_t *cc, int access_bit)
{
    /* Maps to: Go ClientConn.Authorize() */
    if (!cc->account) return 0;
    return hl_access_is_set(cc->account->access, access_bit);
}

int hl_client_conn_new_reply(hl_client_conn_t *cc, const hl_transaction_t *request,
                             hl_transaction_t *reply,
                             const hl_field_t *fields, uint16_t field_count)
{
    /* Maps to: Go ClientConn.NewReply() */
    memset(reply, 0, sizeof(*reply));
    reply->is_reply = 1;
    memcpy(reply->type, request->type, 2);
    memcpy(reply->id, request->id, 4);
    memcpy(reply->client_id, cc->id, 2);

    if (field_count > 0 && fields) {
        reply->fields = (hl_field_t *)calloc(field_count, sizeof(hl_field_t));
        if (!reply->fields) return -1;

        uint16_t i;
        for (i = 0; i < field_count; i++) {
            if (hl_field_new(&reply->fields[i], fields[i].type,
                             fields[i].data, fields[i].data_len) < 0) {
                uint16_t j;
                for (j = 0; j < i; j++) hl_field_free(&reply->fields[j]);
                free(reply->fields);
                reply->fields = NULL;
                return -1;
            }
        }
        reply->field_count = field_count;
    }

    return 0;
}

int hl_client_conn_new_err_reply(hl_client_conn_t *cc, const hl_transaction_t *request,
                                 hl_transaction_t *reply, const char *err_msg)
{
    /* Maps to: Go ClientConn.NewErrReply() */
    hl_field_t field;
    if (hl_field_new(&field, FIELD_ERROR,
                     (const uint8_t *)err_msg, (uint16_t)strlen(err_msg)) < 0)
        return -1;

    int rc = hl_client_conn_new_reply(cc, request, reply, &field, 1);
    if (rc == 0) {
        /* Set error code to 1 — maps to Go: ErrorCode [4]byte{0,0,0,1} */
        reply->error_code[3] = 1;
    }

    hl_field_free(&field);
    return rc;
}
