/*
 * client_conn.c - Per-client connection state
 *
 * Maps to: hotline/client_conn.go
 */

#include "hotline/client_conn.h"
#include "hotline/config.h"
#include "hotline/tls.h"
#include "hotline/hope.h"
#include <stdlib.h>
#include <string.h>

/* --- Colored Nicknames: color resolution cascade (fogWraith DATA_COLOR extension) ---
 * See openspec/changes/colored-nicknames/design.md Decision 3.
 */
uint32_t hl_nick_color_resolve(const hl_client_conn_t *c, const hl_config_t *cfg)
{
    if (!c || !cfg) return 0xFFFFFFFFu;

    /* Tier 1: per-account YAML Color (always wins when set). */
    if (c->account && c->account->nick_color != 0u) {
        return c->account->nick_color;
    }

    /* Tier 2: client-sent color, gated by honor_client_colors. */
    if (cfg->colored_nicknames.honor_client_colors && c->nick_color != 0u) {
        return c->nick_color;
    }

    /* Tiers 3/4: class default. */
    if (c->account) {
        hl_account_class_t klass = hl_access_classify(c->account->access);
        if (klass == HL_CLASS_ADMIN &&
            cfg->colored_nicknames.default_admin_color != 0xFFFFFFFFu) {
            return cfg->colored_nicknames.default_admin_color;
        }
        if (klass == HL_CLASS_GUEST &&
            cfg->colored_nicknames.default_guest_color != 0xFFFFFFFFu) {
            return cfg->colored_nicknames.default_guest_color;
        }
    }

    /* Tier 5: no color. */
    return 0xFFFFFFFFu;
}

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
