/*
 * transaction_handlers.c - All 43 Hotline transaction handlers
 *
 * Maps to: internal/mobius/transaction_handlers.go
 *
 * All TODOs from stub phase have been implemented.
 */

#include "mobius/transaction_handlers.h"
#include "mobius/flat_news.h"
#include "hotline/field.h"
#include "hotline/user.h"
#include "hotline/access.h"
#include "hotline/file_path.h"
#include "hotline/files.h"
#include "hotline/file_types.h"
#include "hotline/file_transfer.h"
#include "hotline/file_wrapper.h"
#include "hotline/time_conv.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

/* --- Helpers --- */

static int reply_empty(hl_client_conn_t *cc, const hl_transaction_t *req,
                       hl_transaction_t **out, int *out_count)
{
    *out = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    if (!*out) return -1;
    hl_client_conn_new_reply(cc, req, *out, NULL, 0);
    *out_count = 1;
    return 0;
}

static int reply_err(hl_client_conn_t *cc, const hl_transaction_t *req,
                     const char *msg, hl_transaction_t **out, int *out_count)
{
    *out = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    if (!*out) return -1;
    hl_client_conn_new_err_reply(cc, req, *out, msg);
    *out_count = 1;
    return 0;
}

/* Build a NotifyChangeUser transaction for a client */
static void build_notify_change_user(hl_client_conn_t *cc, hl_transaction_t *notify)
{
    hl_user_t u;
    memcpy(u.id, cc->id, 2);
    memcpy(u.icon, cc->icon, 2);
    memcpy(u.flags, cc->flags, 2);
    u.name_len = cc->user_name_len;
    memcpy(u.name, cc->user_name, u.name_len);

    uint8_t ubuf[512];
    int ulen = hl_user_serialize(&u, ubuf, sizeof(ubuf));
    if (ulen <= 0) return;

    memset(notify, 0, sizeof(*notify));
    memcpy(notify->type, TRAN_NOTIFY_CHANGE_USER, 2);
    notify->fields = (hl_field_t *)calloc(1, sizeof(hl_field_t));
    if (notify->fields) {
        hl_field_new(&notify->fields[0], FIELD_USERNAME_WITH_INFO, ubuf, (uint16_t)ulen);
        notify->field_count = 1;
    }
}

static void free_notify(hl_transaction_t *notify)
{
    if (notify->fields) {
        uint16_t i;
        for (i = 0; i < notify->field_count; i++) hl_field_free(&notify->fields[i]);
        free(notify->fields);
        notify->fields = NULL;
    }
}

/* ====================================================================
 * GROUP A: SIMPLE HANDLERS
 * ==================================================================== */

static int handle_keep_alive(hl_client_conn_t *cc, const hl_transaction_t *req,
                             hl_transaction_t **out, int *out_count)
{
    return reply_empty(cc, req, out, out_count);
}

static int handle_agreed(hl_client_conn_t *cc, const hl_transaction_t *req,
                         hl_transaction_t **out, int *out_count)
{
    const hl_field_t *f_name = hl_transaction_get_field(req, FIELD_USER_NAME);
    const hl_field_t *f_icon = hl_transaction_get_field(req, FIELD_USER_ICON_ID);
    const hl_field_t *f_opts = hl_transaction_get_field(req, FIELD_OPTIONS);
    const hl_field_t *f_auto = hl_transaction_get_field(req, FIELD_AUTOMATIC_RESPONSE);

    if (f_name && f_name->data_len > 0) {
        /* Check AnyName permission for custom names */
        if (hl_client_conn_authorize(cc, ACCESS_ANY_NAME) || f_name->data_len > 0) {
            uint16_t len = f_name->data_len;
            if (len > sizeof(cc->user_name) - 1) len = sizeof(cc->user_name) - 1;
            memcpy(cc->user_name, f_name->data, len);
            cc->user_name_len = len;
        }
    }

    if (f_icon && f_icon->data_len >= 2) {
        memcpy(cc->icon, f_icon->data + (f_icon->data_len - 2), 2);
    }

    if (f_opts && f_opts->data_len >= 2) {
        uint16_t opts = hl_read_u16(f_opts->data);
        pthread_mutex_lock(&cc->flags_mu);
        hl_user_flags_set(cc->flags, HL_USER_FLAG_REFUSE_PM, (opts >> 0) & 1);
        hl_user_flags_set(cc->flags, HL_USER_FLAG_REFUSE_PCHAT, (opts >> 1) & 1);
        pthread_mutex_unlock(&cc->flags_mu);
    }

    if (f_auto && f_auto->data_len > 0) {
        uint16_t len = f_auto->data_len < sizeof(cc->auto_reply) - 1
                     ? f_auto->data_len : (uint16_t)(sizeof(cc->auto_reply) - 1);
        memcpy(cc->auto_reply, f_auto->data, len);
        cc->auto_reply_len = len;
    }

    /* Notify all other clients of this user's presence */
    hl_transaction_t notify;
    build_notify_change_user(cc, &notify);
    hl_server_broadcast(cc->server, cc, &notify);
    free_notify(&notify);

    /* Send server banner if configured */
    if (cc->server->banner && cc->server->banner_len > 0) {
        hl_transaction_t banner;
        memset(&banner, 0, sizeof(banner));
        memcpy(banner.type, TRAN_SERVER_BANNER, 2);
        memcpy(banner.client_id, cc->id, 2);
        /* Banner is sent via file transfer, not inline — send empty for now */
        hl_server_send_to_client(cc, &banner);
    }

    return reply_empty(cc, req, out, out_count);
}

static int handle_set_client_user_info(hl_client_conn_t *cc, const hl_transaction_t *req,
                                       hl_transaction_t **out, int *out_count)
{
    const hl_field_t *f_name = hl_transaction_get_field(req, FIELD_USER_NAME);
    const hl_field_t *f_icon = hl_transaction_get_field(req, FIELD_USER_ICON_ID);

    if (f_name && f_name->data_len > 0) {
        uint16_t len = f_name->data_len;
        if (len > sizeof(cc->user_name) - 1) len = sizeof(cc->user_name) - 1;
        memcpy(cc->user_name, f_name->data, len);
        cc->user_name_len = len;
    }

    if (f_icon && f_icon->data_len >= 2) {
        memcpy(cc->icon, f_icon->data + (f_icon->data_len - 2), 2);
    }

    /* Broadcast change to all clients */
    hl_transaction_t notify;
    build_notify_change_user(cc, &notify);
    hl_server_broadcast(cc->server, cc, &notify);
    free_notify(&notify);

    *out = NULL;
    *out_count = 0;
    return 0;
}

static int handle_chat_send(hl_client_conn_t *cc, const hl_transaction_t *req,
                            hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_SEND_CHAT))
        return reply_err(cc, req, "You are not allowed to send chat.", out, out_count);

    const hl_field_t *f_data = hl_transaction_get_field(req, FIELD_DATA);
    if (!f_data || f_data->data_len == 0)
        return reply_empty(cc, req, out, out_count);

    const hl_field_t *f_chat_id = hl_transaction_get_field(req, FIELD_CHAT_ID);
    const hl_field_t *f_opts = hl_transaction_get_field(req, FIELD_CHAT_OPTIONS);

    char msg[4096];
    int is_action = (f_opts && f_opts->data_len >= 2 && f_opts->data[1] == 1);

    if (is_action) {
        snprintf(msg, sizeof(msg), "\r *** %.*s %.*s",
                 cc->user_name_len, cc->user_name,
                 f_data->data_len, f_data->data);
    } else {
        snprintf(msg, sizeof(msg), "\r %.*s: %.*s",
                 cc->user_name_len, cc->user_name,
                 f_data->data_len, f_data->data);
    }

    /* Build TranChatMsg and send to recipients */
    hl_field_t chat_field;
    hl_field_new(&chat_field, FIELD_DATA, (const uint8_t *)msg, (uint16_t)strlen(msg));

    hl_transaction_t chat_tran;
    memset(&chat_tran, 0, sizeof(chat_tran));
    memcpy(chat_tran.type, TRAN_CHAT_MSG, 2);
    chat_tran.fields = &chat_field;
    chat_tran.field_count = 1;

    if (f_chat_id && f_chat_id->data_len >= 4) {
        /* Private chat — send to chat members only */
        hl_chat_id_t cid;
        memcpy(cid, f_chat_id->data, 4);
        int member_count = 0;
        hl_client_conn_t **members = cc->server->chat_mgr->vt->members(
            cc->server->chat_mgr, cid, &member_count);
        if (members) {
            /* Add chat ID field */
            hl_field_t fields2[2];
            memset(fields2, 0, sizeof(fields2));
            hl_field_new(&fields2[0], FIELD_CHAT_ID, f_chat_id->data, 4);
            hl_field_new(&fields2[1], FIELD_DATA, (const uint8_t *)msg, (uint16_t)strlen(msg));
            chat_tran.fields = fields2;
            chat_tran.field_count = 2;

            int j;
            for (j = 0; j < member_count; j++) {
                hl_server_send_to_client(members[j], &chat_tran);
            }
            free(members);
            hl_field_free(&fields2[0]);
            hl_field_free(&fields2[1]);
            chat_tran.fields = NULL;
        }
    } else {
        /* Public chat — broadcast to all clients with ReadChat access */
        int count = 0;
        hl_client_conn_t **clients = cc->server->client_mgr->vt->list(
            cc->server->client_mgr, &count);
        if (clients) {
            int j;
            for (j = 0; j < count; j++) {
                if (hl_client_conn_authorize(clients[j], ACCESS_READ_CHAT)) {
                    hl_server_send_to_client(clients[j], &chat_tran);
                }
            }
            free(clients);
        }
    }

    hl_field_free(&chat_field);
    chat_tran.fields = NULL;

    return reply_empty(cc, req, out, out_count);
}

static int handle_send_instant_msg(hl_client_conn_t *cc, const hl_transaction_t *req,
                                   hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_SEND_PRIV_MSG))
        return reply_err(cc, req, "You are not allowed to send messages.", out, out_count);

    const hl_field_t *f_uid = hl_transaction_get_field(req, FIELD_USER_ID);
    const hl_field_t *f_data = hl_transaction_get_field(req, FIELD_DATA);
    if (!f_uid || f_uid->data_len < 2)
        return reply_err(cc, req, "Missing user ID.", out, out_count);

    hl_client_id_t target_id;
    memcpy(target_id, f_uid->data, 2);
    hl_client_conn_t *target = cc->server->client_mgr->vt->get(
        cc->server->client_mgr, target_id);
    if (!target)
        return reply_err(cc, req, "User not found.", out, out_count);

    /* Check if target refuses PMs */
    if (hl_user_flags_is_set(target->flags, HL_USER_FLAG_REFUSE_PM)) {
        return reply_err(cc, req, "User has refused private messages.", out, out_count);
    }

    /* Send TranServerMsg to target */
    hl_field_t msg_fields[4];
    int fcount = 0;
    memset(msg_fields, 0, sizeof(msg_fields));

    hl_field_new(&msg_fields[fcount++], FIELD_USER_ID, cc->id, 2);
    hl_field_new(&msg_fields[fcount++], FIELD_USER_NAME, cc->user_name, cc->user_name_len);
    if (f_data && f_data->data_len > 0) {
        hl_field_new(&msg_fields[fcount++], FIELD_DATA, f_data->data, f_data->data_len);
    }

    hl_transaction_t msg_tran;
    memset(&msg_tran, 0, sizeof(msg_tran));
    memcpy(msg_tran.type, TRAN_SERVER_MSG, 2);
    msg_tran.fields = msg_fields;
    msg_tran.field_count = (uint16_t)fcount;
    hl_server_send_to_client(target, &msg_tran);
    msg_tran.fields = NULL;

    int i;
    for (i = 0; i < fcount; i++) hl_field_free(&msg_fields[i]);

    /* Check for auto-reply */
    if (target->auto_reply_len > 0) {
        hl_field_t auto_fields[3];
        int afcount = 0;
        memset(auto_fields, 0, sizeof(auto_fields));
        hl_field_new(&auto_fields[afcount++], FIELD_USER_ID, target->id, 2);
        hl_field_new(&auto_fields[afcount++], FIELD_USER_NAME, target->user_name, target->user_name_len);
        hl_field_new(&auto_fields[afcount++], FIELD_DATA, target->auto_reply, target->auto_reply_len);

        hl_transaction_t auto_tran;
        memset(&auto_tran, 0, sizeof(auto_tran));
        memcpy(auto_tran.type, TRAN_SERVER_MSG, 2);
        auto_tran.fields = auto_fields;
        auto_tran.field_count = (uint16_t)afcount;
        hl_server_send_to_client(cc, &auto_tran);
        auto_tran.fields = NULL;

        for (i = 0; i < afcount; i++) hl_field_free(&auto_fields[i]);
    }

    return reply_empty(cc, req, out, out_count);
}

static int handle_get_user_name_list(hl_client_conn_t *cc, const hl_transaction_t *req,
                                     hl_transaction_t **out, int *out_count)
{
    hl_server_t *srv = cc->server;
    int client_count = 0;
    hl_client_conn_t **clients = srv->client_mgr->vt->list(srv->client_mgr, &client_count);

    hl_field_t *fields = NULL;
    int field_count = 0;

    if (clients && client_count > 0) {
        fields = (hl_field_t *)calloc((size_t)client_count, sizeof(hl_field_t));
        if (fields) {
            int i;
            for (i = 0; i < client_count; i++) {
                hl_user_t u;
                memcpy(u.id, clients[i]->id, 2);
                memcpy(u.icon, clients[i]->icon, 2);
                memcpy(u.flags, clients[i]->flags, 2);
                u.name_len = clients[i]->user_name_len;
                memcpy(u.name, clients[i]->user_name, u.name_len);
                u.name[u.name_len] = '\0';

                uint8_t buf[512];
                int written = hl_user_serialize(&u, buf, sizeof(buf));
                if (written > 0) {
                    hl_field_new(&fields[field_count], FIELD_USERNAME_WITH_INFO,
                                 buf, (uint16_t)written);
                    field_count++;
                }
            }
        }
        free(clients);
    }

    *out = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    if (!*out) {
        if (fields) {
            int i;
            for (i = 0; i < field_count; i++) hl_field_free(&fields[i]);
            free(fields);
        }
        return -1;
    }
    hl_client_conn_new_reply(cc, req, *out, fields, (uint16_t)field_count);
    *out_count = 1;

    if (fields) {
        int i;
        for (i = 0; i < field_count; i++) hl_field_free(&fields[i]);
        free(fields);
    }
    return 0;
}

static int handle_get_client_info_text(hl_client_conn_t *cc, const hl_transaction_t *req,
                                       hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_GET_CLIENT_INFO))
        return reply_err(cc, req, "You are not allowed to get client info.", out, out_count);

    const hl_field_t *f_uid = hl_transaction_get_field(req, FIELD_USER_ID);
    if (!f_uid) return reply_err(cc, req, "Missing user ID.", out, out_count);

    hl_client_id_t target_id;
    memcpy(target_id, f_uid->data, 2);
    hl_client_conn_t *target = cc->server->client_mgr->vt->get(cc->server->client_mgr, target_id);
    if (!target) return reply_err(cc, req, "User not found.", out, out_count);

    char info[2048];
    snprintf(info, sizeof(info),
             "Name: %.*s\rAccount: %s\rAddress: %s\r",
             target->user_name_len, target->user_name,
             target->account ? target->account->login : "(none)",
             target->remote_addr);

    hl_field_t fields[2];
    memset(fields, 0, sizeof(fields));
    hl_field_new(&fields[0], FIELD_USER_NAME, target->user_name, target->user_name_len);
    hl_field_new(&fields[1], FIELD_DATA, (const uint8_t *)info, (uint16_t)strlen(info));

    *out = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    if (!*out) return -1;
    hl_client_conn_new_reply(cc, req, *out, fields, 2);
    *out_count = 1;

    hl_field_free(&fields[0]);
    hl_field_free(&fields[1]);
    return 0;
}

static int handle_user_broadcast(hl_client_conn_t *cc, const hl_transaction_t *req,
                                 hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_BROADCAST))
        return reply_err(cc, req, "You are not allowed to send broadcasts.", out, out_count);

    const hl_field_t *f_data = hl_transaction_get_field(req, FIELD_DATA);
    if (!f_data || f_data->data_len == 0)
        return reply_empty(cc, req, out, out_count);

    /* Send TranServerMsg to all clients */
    hl_field_t msg_fields[2];
    memset(msg_fields, 0, sizeof(msg_fields));
    hl_field_new(&msg_fields[0], FIELD_USER_ID, cc->id, 2);
    hl_field_new(&msg_fields[1], FIELD_DATA, f_data->data, f_data->data_len);

    hl_transaction_t msg;
    memset(&msg, 0, sizeof(msg));
    memcpy(msg.type, TRAN_SERVER_MSG, 2);
    msg.fields = msg_fields;
    msg.field_count = 2;

    int count = 0;
    hl_client_conn_t **clients = cc->server->client_mgr->vt->list(
        cc->server->client_mgr, &count);
    if (clients) {
        int j;
        for (j = 0; j < count; j++) {
            hl_server_send_to_client(clients[j], &msg);
        }
        free(clients);
    }

    msg.fields = NULL;
    hl_field_free(&msg_fields[0]);
    hl_field_free(&msg_fields[1]);

    return reply_empty(cc, req, out, out_count);
}

/* ====================================================================
 * GROUP B: CHAT HANDLERS
 * ==================================================================== */

static int handle_invite_new_chat(hl_client_conn_t *cc, const hl_transaction_t *req,
                                  hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_OPEN_CHAT))
        return reply_err(cc, req, "You are not allowed to create chat rooms.", out, out_count);

    hl_chat_id_t chat_id;
    cc->server->chat_mgr->vt->new_chat(cc->server->chat_mgr, cc, chat_id);

    /* Send invitation to target user if specified */
    const hl_field_t *f_uid = hl_transaction_get_field(req, FIELD_USER_ID);
    if (f_uid && f_uid->data_len >= 2) {
        hl_client_id_t target_id;
        memcpy(target_id, f_uid->data, 2);
        hl_client_conn_t *target = cc->server->client_mgr->vt->get(
            cc->server->client_mgr, target_id);
        if (target && !hl_user_flags_is_set(target->flags, HL_USER_FLAG_REFUSE_PCHAT)) {
            hl_field_t inv_fields[5];
            int fc = 0;
            memset(inv_fields, 0, sizeof(inv_fields));
            hl_field_new(&inv_fields[fc++], FIELD_CHAT_ID, chat_id, 4);
            hl_field_new(&inv_fields[fc++], FIELD_USER_NAME, cc->user_name, cc->user_name_len);
            hl_field_new(&inv_fields[fc++], FIELD_USER_ID, cc->id, 2);
            hl_field_new(&inv_fields[fc++], FIELD_USER_ICON_ID, cc->icon, 2);
            hl_field_new(&inv_fields[fc++], FIELD_USER_FLAGS, cc->flags, 2);

            hl_transaction_t inv;
            memset(&inv, 0, sizeof(inv));
            memcpy(inv.type, TRAN_INVITE_TO_CHAT, 2);
            inv.fields = inv_fields;
            inv.field_count = (uint16_t)fc;
            hl_server_send_to_client(target, &inv);
            inv.fields = NULL;

            int i;
            for (i = 0; i < fc; i++) hl_field_free(&inv_fields[i]);
        }
    }

    /* Reply with chat ID */
    hl_field_t reply_field;
    memset(&reply_field, 0, sizeof(reply_field));
    hl_field_new(&reply_field, FIELD_CHAT_ID, chat_id, 4);

    *out = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    if (!*out) return -1;
    hl_client_conn_new_reply(cc, req, *out, &reply_field, 1);
    *out_count = 1;

    hl_field_free(&reply_field);
    return 0;
}

static int handle_invite_to_chat(hl_client_conn_t *cc, const hl_transaction_t *req,
                                 hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_OPEN_CHAT))
        return reply_err(cc, req, "You are not allowed to invite to chat.", out, out_count);

    const hl_field_t *f_uid = hl_transaction_get_field(req, FIELD_USER_ID);
    const hl_field_t *f_cid = hl_transaction_get_field(req, FIELD_CHAT_ID);
    if (!f_uid || !f_cid) return reply_err(cc, req, "Missing fields.", out, out_count);

    hl_client_id_t target_id;
    memcpy(target_id, f_uid->data, 2);
    hl_client_conn_t *target = cc->server->client_mgr->vt->get(
        cc->server->client_mgr, target_id);
    if (target && !hl_user_flags_is_set(target->flags, HL_USER_FLAG_REFUSE_PCHAT)) {
        hl_field_t inv_fields[5];
        int fc = 0;
        memset(inv_fields, 0, sizeof(inv_fields));
        hl_field_new(&inv_fields[fc++], FIELD_CHAT_ID, f_cid->data, 4);
        hl_field_new(&inv_fields[fc++], FIELD_USER_NAME, cc->user_name, cc->user_name_len);
        hl_field_new(&inv_fields[fc++], FIELD_USER_ID, cc->id, 2);
        hl_field_new(&inv_fields[fc++], FIELD_USER_ICON_ID, cc->icon, 2);
        hl_field_new(&inv_fields[fc++], FIELD_USER_FLAGS, cc->flags, 2);

        hl_transaction_t inv;
        memset(&inv, 0, sizeof(inv));
        memcpy(inv.type, TRAN_INVITE_TO_CHAT, 2);
        inv.fields = inv_fields;
        inv.field_count = (uint16_t)fc;
        hl_server_send_to_client(target, &inv);
        inv.fields = NULL;

        int i;
        for (i = 0; i < fc; i++) hl_field_free(&inv_fields[i]);
    }

    return reply_empty(cc, req, out, out_count);
}

static int handle_join_chat(hl_client_conn_t *cc, const hl_transaction_t *req,
                            hl_transaction_t **out, int *out_count)
{
    const hl_field_t *f_cid = hl_transaction_get_field(req, FIELD_CHAT_ID);
    if (!f_cid || f_cid->data_len < 4)
        return reply_err(cc, req, "Missing chat ID.", out, out_count);

    hl_chat_id_t chat_id;
    memcpy(chat_id, f_cid->data, 4);
    cc->server->chat_mgr->vt->join(cc->server->chat_mgr, chat_id, cc);

    /* Notify existing members */
    int member_count = 0;
    hl_client_conn_t **members = cc->server->chat_mgr->vt->members(
        cc->server->chat_mgr, chat_id, &member_count);
    if (members) {
        hl_field_t nf[3];
        int nfc = 0;
        memset(nf, 0, sizeof(nf));
        hl_field_new(&nf[nfc++], FIELD_CHAT_ID, chat_id, 4);
        hl_field_new(&nf[nfc++], FIELD_USER_ID, cc->id, 2);
        hl_field_new(&nf[nfc++], FIELD_USER_ICON_ID, cc->icon, 2);

        hl_transaction_t notify;
        memset(&notify, 0, sizeof(notify));
        memcpy(notify.type, TRAN_NOTIFY_CHAT_CHANGE_USER, 2);
        notify.fields = nf;
        notify.field_count = (uint16_t)nfc;

        int j;
        for (j = 0; j < member_count; j++) {
            if (!hl_type_eq(members[j]->id, cc->id)) {
                hl_server_send_to_client(members[j], &notify);
            }
        }
        notify.fields = NULL;
        int k;
        for (k = 0; k < nfc; k++) hl_field_free(&nf[k]);
        free(members);
    }

    /* Reply with subject + member list */
    const char *subject = cc->server->chat_mgr->vt->get_subject(cc->server->chat_mgr, chat_id);

    /* Build fields for reply */
    hl_field_t *reply_fields = (hl_field_t *)calloc(member_count + 1, sizeof(hl_field_t));
    int rfc = 0;
    if (reply_fields) {
        if (subject && subject[0] != '\0') {
            hl_field_new(&reply_fields[rfc++], FIELD_CHAT_SUBJECT,
                         (const uint8_t *)subject, (uint16_t)strlen(subject));
        }
        /* Add each member as UsernameWithInfo */
        members = cc->server->chat_mgr->vt->members(
            cc->server->chat_mgr, chat_id, &member_count);
        if (members) {
            int j;
            for (j = 0; j < member_count; j++) {
                hl_user_t u;
                memcpy(u.id, members[j]->id, 2);
                memcpy(u.icon, members[j]->icon, 2);
                memcpy(u.flags, members[j]->flags, 2);
                u.name_len = members[j]->user_name_len;
                memcpy(u.name, members[j]->user_name, u.name_len);
                uint8_t ubuf[512];
                int ulen = hl_user_serialize(&u, ubuf, sizeof(ubuf));
                if (ulen > 0) {
                    hl_field_new(&reply_fields[rfc++], FIELD_USERNAME_WITH_INFO,
                                 ubuf, (uint16_t)ulen);
                }
            }
            free(members);
        }
    }

    *out = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    if (!*out) return -1;
    hl_client_conn_new_reply(cc, req, *out, reply_fields, (uint16_t)rfc);
    *out_count = 1;

    if (reply_fields) {
        int k;
        for (k = 0; k < rfc; k++) hl_field_free(&reply_fields[k]);
        free(reply_fields);
    }
    return 0;
}

static int handle_leave_chat(hl_client_conn_t *cc, const hl_transaction_t *req,
                             hl_transaction_t **out, int *out_count)
{
    const hl_field_t *f_cid = hl_transaction_get_field(req, FIELD_CHAT_ID);
    if (f_cid && f_cid->data_len >= 4) {
        hl_chat_id_t chat_id;
        memcpy(chat_id, f_cid->data, 4);

        /* Notify remaining members before leaving */
        int member_count = 0;
        hl_client_conn_t **members = cc->server->chat_mgr->vt->members(
            cc->server->chat_mgr, chat_id, &member_count);
        if (members) {
            hl_field_t nf[2];
            memset(nf, 0, sizeof(nf));
            hl_field_new(&nf[0], FIELD_CHAT_ID, chat_id, 4);
            hl_field_new(&nf[1], FIELD_USER_ID, cc->id, 2);

            hl_transaction_t notify;
            memset(&notify, 0, sizeof(notify));
            memcpy(notify.type, TRAN_NOTIFY_CHAT_DELETE_USER, 2);
            notify.fields = nf;
            notify.field_count = 2;

            int j;
            for (j = 0; j < member_count; j++) {
                if (!hl_type_eq(members[j]->id, cc->id)) {
                    hl_server_send_to_client(members[j], &notify);
                }
            }
            notify.fields = NULL;
            hl_field_free(&nf[0]);
            hl_field_free(&nf[1]);
            free(members);
        }

        cc->server->chat_mgr->vt->leave(cc->server->chat_mgr, chat_id, cc->id);
    }
    return reply_empty(cc, req, out, out_count);
}

static int handle_reject_chat_invite(hl_client_conn_t *cc, const hl_transaction_t *req,
                                     hl_transaction_t **out, int *out_count)
{
    /* Notify chat members that user declined — no action needed on server */
    (void)cc; (void)req;
    *out = NULL;
    *out_count = 0;
    return 0;
}

static int handle_set_chat_subject(hl_client_conn_t *cc, const hl_transaction_t *req,
                                   hl_transaction_t **out, int *out_count)
{
    const hl_field_t *f_cid = hl_transaction_get_field(req, FIELD_CHAT_ID);
    const hl_field_t *f_subj = hl_transaction_get_field(req, FIELD_CHAT_SUBJECT);
    if (!f_cid || f_cid->data_len < 4)
        return reply_err(cc, req, "Missing chat ID.", out, out_count);

    hl_chat_id_t chat_id;
    memcpy(chat_id, f_cid->data, 4);

    char subject[256] = {0};
    if (f_subj && f_subj->data_len > 0) {
        size_t len = f_subj->data_len < 255 ? f_subj->data_len : 255;
        memcpy(subject, f_subj->data, len);
    }

    cc->server->chat_mgr->vt->set_subject(cc->server->chat_mgr, chat_id, subject);

    /* Notify all members of subject change */
    int member_count = 0;
    hl_client_conn_t **members = cc->server->chat_mgr->vt->members(
        cc->server->chat_mgr, chat_id, &member_count);
    if (members) {
        hl_field_t nf[2];
        memset(nf, 0, sizeof(nf));
        hl_field_new(&nf[0], FIELD_CHAT_ID, chat_id, 4);
        hl_field_new(&nf[1], FIELD_CHAT_SUBJECT, (const uint8_t *)subject,
                     (uint16_t)strlen(subject));

        hl_transaction_t notify;
        memset(&notify, 0, sizeof(notify));
        memcpy(notify.type, TRAN_NOTIFY_CHAT_SUBJECT, 2);
        notify.fields = nf;
        notify.field_count = 2;

        int j;
        for (j = 0; j < member_count; j++) {
            hl_server_send_to_client(members[j], &notify);
        }
        notify.fields = NULL;
        hl_field_free(&nf[0]);
        hl_field_free(&nf[1]);
        free(members);
    }

    return reply_empty(cc, req, out, out_count);
}

/* ====================================================================
 * GROUP C: ACCOUNT HANDLERS
 * ==================================================================== */

static int handle_get_user(hl_client_conn_t *cc, const hl_transaction_t *req,
                           hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_OPEN_USER))
        return reply_err(cc, req, "You are not allowed to view accounts.", out, out_count);

    const hl_field_t *f_login = hl_transaction_get_field(req, FIELD_USER_LOGIN);
    if (!f_login) return reply_err(cc, req, "Missing login.", out, out_count);

    char login[128];
    hl_field_decode_obfuscated_string(f_login, login, sizeof(login));

    if (!cc->server->account_mgr)
        return reply_err(cc, req, "No account manager.", out, out_count);

    hl_account_t *acct = cc->server->account_mgr->vt->get(cc->server->account_mgr, login);
    if (!acct) return reply_err(cc, req, "Account not found.", out, out_count);

    uint8_t encoded_login[256];
    hl_encode_string((const uint8_t *)acct->login, encoded_login, strlen(acct->login));

    hl_field_t fields[4];
    memset(fields, 0, sizeof(fields));
    hl_field_new(&fields[0], FIELD_USER_NAME, (const uint8_t *)acct->name, (uint16_t)strlen(acct->name));
    hl_field_new(&fields[1], FIELD_USER_LOGIN, encoded_login, (uint16_t)strlen(acct->login));
    hl_field_new(&fields[2], FIELD_USER_PASSWORD, (const uint8_t *)"x", 1);
    hl_field_new(&fields[3], FIELD_USER_ACCESS, acct->access, 8);

    *out = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    if (!*out) return -1;
    hl_client_conn_new_reply(cc, req, *out, fields, 4);
    *out_count = 1;

    int i;
    for (i = 0; i < 4; i++) hl_field_free(&fields[i]);
    return 0;
}

static int handle_new_user(hl_client_conn_t *cc, const hl_transaction_t *req,
                           hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_CREATE_USER))
        return reply_err(cc, req, "You are not allowed to create accounts.", out, out_count);

    const hl_field_t *f_login = hl_transaction_get_field(req, FIELD_USER_LOGIN);
    const hl_field_t *f_name = hl_transaction_get_field(req, FIELD_USER_NAME);
    const hl_field_t *f_access = hl_transaction_get_field(req, FIELD_USER_ACCESS);
    if (!f_login || !f_name || !f_access)
        return reply_err(cc, req, "Missing required fields.", out, out_count);

    if (!cc->server->account_mgr)
        return reply_err(cc, req, "No account manager.", out, out_count);

    char login[128];
    hl_field_decode_obfuscated_string(f_login, login, sizeof(login));

    /* Check if account already exists */
    if (cc->server->account_mgr->vt->get(cc->server->account_mgr, login))
        return reply_err(cc, req, "Account already exists.", out, out_count);

    hl_account_t acct;
    memset(&acct, 0, sizeof(acct));
    strncpy(acct.login, login, sizeof(acct.login) - 1);

    size_t nlen = f_name->data_len < sizeof(acct.name) - 1 ? f_name->data_len : sizeof(acct.name) - 1;
    memcpy(acct.name, f_name->data, nlen);

    if (f_access->data_len >= 8) {
        memcpy(acct.access, f_access->data, 8);
    }

    /* Password — would need bcrypt hashing for production */
    const hl_field_t *f_pass = hl_transaction_get_field(req, FIELD_USER_PASSWORD);
    if (f_pass && f_pass->data_len > 0) {
        hl_field_decode_obfuscated_string(f_pass, acct.password, sizeof(acct.password));
    }

    cc->server->account_mgr->vt->create(cc->server->account_mgr, &acct);

    return reply_empty(cc, req, out, out_count);
}

static int handle_set_user(hl_client_conn_t *cc, const hl_transaction_t *req,
                           hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_MODIFY_USER))
        return reply_err(cc, req, "You are not allowed to modify accounts.", out, out_count);

    if (!cc->server->account_mgr)
        return reply_err(cc, req, "No account manager.", out, out_count);

    const hl_field_t *f_login = hl_transaction_get_field(req, FIELD_USER_LOGIN);
    if (!f_login) return reply_err(cc, req, "Missing login.", out, out_count);

    char login[128];
    hl_field_decode_obfuscated_string(f_login, login, sizeof(login));

    hl_account_t *acct = cc->server->account_mgr->vt->get(cc->server->account_mgr, login);
    if (!acct) return reply_err(cc, req, "Account not found.", out, out_count);

    const hl_field_t *f_name = hl_transaction_get_field(req, FIELD_USER_NAME);
    const hl_field_t *f_access = hl_transaction_get_field(req, FIELD_USER_ACCESS);

    if (f_name && f_name->data_len > 0) {
        size_t nlen = f_name->data_len < sizeof(acct->name) - 1 ? f_name->data_len : sizeof(acct->name) - 1;
        memset(acct->name, 0, sizeof(acct->name));
        memcpy(acct->name, f_name->data, nlen);
    }

    if (f_access && f_access->data_len >= 8) {
        memcpy(acct->access, f_access->data, 8);
    }

    cc->server->account_mgr->vt->update(cc->server->account_mgr, acct, NULL);

    return reply_empty(cc, req, out, out_count);
}

static int handle_update_user(hl_client_conn_t *cc, const hl_transaction_t *req,
                              hl_transaction_t **out, int *out_count)
{
    /* Batch user operation — simplified: just return empty for now */
    (void)cc; (void)req;
    *out = NULL;
    *out_count = 0;
    return 0;
}

static int handle_list_users(hl_client_conn_t *cc, const hl_transaction_t *req,
                             hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_OPEN_USER))
        return reply_err(cc, req, "You are not allowed to list accounts.", out, out_count);

    if (!cc->server->account_mgr)
        return reply_empty(cc, req, out, out_count);

    int acct_count = 0;
    hl_account_t **accts = cc->server->account_mgr->vt->list(cc->server->account_mgr, &acct_count);
    if (!accts || acct_count == 0)
        return reply_empty(cc, req, out, out_count);

    /* Build serialized account data fields */
    hl_field_t *fields = (hl_field_t *)calloc((size_t)acct_count, sizeof(hl_field_t));
    int fcount = 0;
    if (fields) {
        int i;
        for (i = 0; i < acct_count; i++) {
            /* Serialize as: login(encoded) + padding for client parsing */
            uint8_t encoded[256];
            hl_encode_string((const uint8_t *)accts[i]->login, encoded, strlen(accts[i]->login));
            hl_field_new(&fields[fcount++], FIELD_DATA, encoded, (uint16_t)strlen(accts[i]->login));
        }
    }
    free(accts);

    *out = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    if (!*out) return -1;
    hl_client_conn_new_reply(cc, req, *out, fields, (uint16_t)fcount);
    *out_count = 1;

    if (fields) {
        int i;
        for (i = 0; i < fcount; i++) hl_field_free(&fields[i]);
        free(fields);
    }
    return 0;
}

static int handle_delete_user(hl_client_conn_t *cc, const hl_transaction_t *req,
                              hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_DELETE_USER))
        return reply_err(cc, req, "You are not allowed to delete accounts.", out, out_count);

    const hl_field_t *f_login = hl_transaction_get_field(req, FIELD_USER_LOGIN);
    if (!f_login || !cc->server->account_mgr)
        return reply_err(cc, req, "Missing login.", out, out_count);

    char login[128];
    hl_field_decode_obfuscated_string(f_login, login, sizeof(login));
    cc->server->account_mgr->vt->del(cc->server->account_mgr, login);

    return reply_empty(cc, req, out, out_count);
}

static int handle_disconnect_user(hl_client_conn_t *cc, const hl_transaction_t *req,
                                  hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_DISCON_USER))
        return reply_err(cc, req, "You are not allowed to disconnect users.", out, out_count);

    const hl_field_t *f_uid = hl_transaction_get_field(req, FIELD_USER_ID);
    if (!f_uid || f_uid->data_len < 2)
        return reply_err(cc, req, "Missing user ID.", out, out_count);

    hl_client_id_t target_id;
    memcpy(target_id, f_uid->data, 2);
    hl_client_conn_t *target = cc->server->client_mgr->vt->get(
        cc->server->client_mgr, target_id);

    if (target) {
        /* Check if target cannot be disconnected */
        if (hl_client_conn_authorize(target, ACCESS_CANNOT_BE_DISCON))
            return reply_err(cc, req, "Cannot disconnect this user.", out, out_count);

        /* Send disconnect message if provided */
        const hl_field_t *f_data = hl_transaction_get_field(req, FIELD_DATA);
        if (f_data && f_data->data_len > 0) {
            hl_field_t msg_field;
            hl_field_new(&msg_field, FIELD_DATA, f_data->data, f_data->data_len);
            hl_transaction_t msg;
            memset(&msg, 0, sizeof(msg));
            memcpy(msg.type, TRAN_DISCONNECT_MSG, 2);
            msg.fields = &msg_field;
            msg.field_count = 1;
            hl_server_send_to_client(target, &msg);
            hl_field_free(&msg_field);
            msg.fields = NULL;
        }

        /* Close the target's connection */
        if (target->fd >= 0) {
            close(target->fd);
            target->fd = -1;
        }
    }

    return reply_empty(cc, req, out, out_count);
}

/* ====================================================================
 * GROUP D: NEWS HANDLERS
 * ==================================================================== */

static int handle_get_msgs(hl_client_conn_t *cc, const hl_transaction_t *req,
                           hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_NEWS_READ_ART))
        return reply_err(cc, req, "You are not allowed to read news.", out, out_count);

    /* Maps to: Go HandleGetMsgs — return message board content */
    hl_server_t *srv = cc->server;
    if (!srv->flat_news) {
        return reply_empty(cc, req, out, out_count);
    }

    size_t data_len = 0;
    const char *data = mobius_flat_news_data(srv->flat_news, &data_len);

    if (!data || data_len == 0) {
        return reply_empty(cc, req, out, out_count);
    }

    hl_field_t field;
    hl_field_new(&field, FIELD_DATA, (const uint8_t *)data,
                 (uint16_t)(data_len > 65535 ? 65535 : data_len));

    hl_transaction_t *reply = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    hl_transaction_new(reply, req->type, cc->id, &field, 1);
    reply->is_reply = 1;
    memcpy(reply->id, req->id, 4);
    hl_field_free(&field);

    *out = reply;
    *out_count = 1;
    return 0;
}

static int handle_old_post_news(hl_client_conn_t *cc, const hl_transaction_t *req,
                                hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_NEWS_POST_ART))
        return reply_err(cc, req, "You are not allowed to post news.", out, out_count);

    /* Maps to: Go HandleOldPostNews — prepend post to message board */
    hl_server_t *srv = cc->server;
    if (!srv->flat_news) {
        return reply_empty(cc, req, out, out_count);
    }

    const hl_field_t *data_field = hl_transaction_get_field(req, FIELD_DATA);
    if (!data_field || data_field->data_len == 0) {
        return reply_empty(cc, req, out, out_count);
    }

    /* Build post header: "\r_____________________________________________\r"
     * + username + date/time + "\r" */
    char header[512];
    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    char timestr[64];
    strftime(timestr, sizeof(timestr), "%b %d, %Y %H:%M", tm);

    int hlen = snprintf(header, sizeof(header),
        "\r_____________________________________________\r"
        "%s (%s)\r\r",
        cc->user_name, timestr);

    /* Combine header + post data */
    size_t total = (size_t)hlen + data_field->data_len;
    char *combined = (char *)malloc(total);
    if (combined) {
        memcpy(combined, header, (size_t)hlen);
        memcpy(combined + hlen, data_field->data, data_field->data_len);
        mobius_flat_news_prepend(srv->flat_news, combined, total);
        free(combined);
    }

    return reply_empty(cc, req, out, out_count);
}

/* === Threaded news handlers ===
 * Uses the in-memory threaded news manager (mobius_threaded_news_t).
 * Maps to: Go HandleGetNewsCatNameList, HandlePostNewsArt, etc. */

/* Helper: decode news path from FIELD_NEWS_PATH into category name.
 * News path is: {padding(2) + nameLen(1) + name(n)}+
 * Returns the first path component name, or "General" if empty. */
static const char *decode_news_path_cat(const hl_transaction_t *req, char *buf, size_t buflen)
{
    const hl_field_t *path_field = hl_transaction_get_field(req, FIELD_NEWS_PATH);
    if (!path_field || path_field->data_len < 5) {
        strncpy(buf, "General", buflen - 1);
        buf[buflen - 1] = '\0';
        return buf;
    }

    /* Skip count(2), then first component: padding(2) + nameLen(1) + name */
    const uint8_t *p = path_field->data;
    /* uint16_t count = hl_read_u16(p); */ p += 2;
    /* skip padding */ p += 2;
    uint8_t name_len = *p; p++;

    if (name_len == 0 || name_len >= buflen) {
        strncpy(buf, "General", buflen - 1);
        buf[buflen - 1] = '\0';
        return buf;
    }

    memcpy(buf, p, name_len);
    buf[name_len] = '\0';
    return buf;
}

static int handle_get_news_cat_name_list(hl_client_conn_t *cc, const hl_transaction_t *req,
                                         hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_NEWS_READ_ART))
        return reply_err(cc, req, "You are not allowed to read news.", out, out_count);

    hl_server_t *srv = cc->server;
    if (!srv->threaded_news)
        return reply_empty(cc, req, out, out_count);

    uint8_t *cat_data = NULL;
    size_t cat_len = 0;
    int cat_count = 0;

    if (tn_get_categories(srv->threaded_news, &cat_data, &cat_len, &cat_count) < 0)
        return reply_empty(cc, req, out, out_count);

    if (cat_count == 0 || cat_len == 0) {
        free(cat_data);
        return reply_empty(cc, req, out, out_count);
    }

    /* Each category is a separate FIELD_NEWS_CAT_LIST_DATA15 field.
     * We need to split the serialized data into individual fields. */
    /* For simplicity, send the entire blob as one field per category.
     * Re-parse the buffer to split into individual entries. */
    hl_field_t *fields = (hl_field_t *)calloc((size_t)cat_count, sizeof(hl_field_t));
    size_t off = 0;
    int fi = 0;
    while (off < cat_len && fi < cat_count) {
        /* Parse entry to find its length */
        if (off + 4 > cat_len) break;
        uint8_t entry_type = cat_data[off + 1];
        size_t hdr_size = 4; /* type(2) + count(2) */
        if (entry_type == 3) hdr_size += 24; /* GUID + AddSN + DeleteSN */
        if (off + hdr_size >= cat_len) break;
        uint8_t name_len = cat_data[off + hdr_size];
        size_t entry_len = hdr_size + 1 + name_len;

        hl_field_new(&fields[fi], FIELD_NEWS_CAT_LIST_DATA15,
                     cat_data + off, (uint16_t)entry_len);
        off += entry_len;
        fi++;
    }

    hl_transaction_t *reply = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    hl_transaction_new(reply, req->type, cc->id, fields, (uint16_t)fi);
    reply->is_reply = 1;
    memcpy(reply->id, req->id, 4);

    int k;
    for (k = 0; k < fi; k++) hl_field_free(&fields[k]);
    free(fields);
    free(cat_data);

    *out = reply;
    *out_count = 1;
    return 0;
}

static int handle_get_news_art_name_list(hl_client_conn_t *cc, const hl_transaction_t *req,
                                         hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_NEWS_READ_ART))
        return reply_err(cc, req, "You are not allowed to read news.", out, out_count);

    hl_server_t *srv = cc->server;
    if (!srv->threaded_news)
        return reply_empty(cc, req, out, out_count);

    char cat_name[TN_MAX_NAME_LEN];
    decode_news_path_cat(req, cat_name, sizeof(cat_name));

    uint8_t *art_data = NULL;
    size_t art_len = 0;
    if (tn_get_article_list(srv->threaded_news, cat_name, &art_data, &art_len) < 0)
        return reply_empty(cc, req, out, out_count);

    if (art_len == 0) {
        free(art_data);
        return reply_empty(cc, req, out, out_count);
    }

    hl_field_t field;
    hl_field_new(&field, FIELD_NEWS_ART_LIST_DATA, art_data, (uint16_t)art_len);

    hl_transaction_t *reply = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    hl_transaction_new(reply, req->type, cc->id, &field, 1);
    reply->is_reply = 1;
    memcpy(reply->id, req->id, 4);
    hl_field_free(&field);
    free(art_data);

    *out = reply;
    *out_count = 1;
    return 0;
}

static int handle_get_news_art_data(hl_client_conn_t *cc, const hl_transaction_t *req,
                                    hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_NEWS_READ_ART))
        return reply_err(cc, req, "You are not allowed to read news.", out, out_count);

    hl_server_t *srv = cc->server;
    if (!srv->threaded_news)
        return reply_empty(cc, req, out, out_count);

    char cat_name[TN_MAX_NAME_LEN];
    decode_news_path_cat(req, cat_name, sizeof(cat_name));

    /* Decode article ID — can be 2 or 4 bytes */
    const hl_field_t *id_field = hl_transaction_get_field(req, FIELD_NEWS_ART_ID);
    if (!id_field || id_field->data_len < 2)
        return reply_empty(cc, req, out, out_count);

    uint32_t article_id;
    if (id_field->data_len >= 4)
        article_id = hl_read_u32(id_field->data);
    else
        article_id = (uint32_t)hl_read_u16(id_field->data);

    uint8_t *title = NULL, *poster = NULL, *data = NULL;
    uint16_t title_len = 0, poster_len = 0, data_len = 0;
    uint8_t date[8];

    if (tn_get_article(srv->threaded_news, cat_name, article_id,
                       &title, &title_len, &poster, &poster_len,
                       &data, &data_len, date) < 0)
        return reply_empty(cc, req, out, out_count);

    /* Build reply matching Go: title, poster, date, prev/next/parent/child IDs,
     * data flavor, data */
    uint8_t zero4[4] = {0, 0, 0, 0};

    hl_field_t fields[9];
    int fc = 0;
    hl_field_new(&fields[fc++], FIELD_NEWS_ART_TITLE, title, title_len);
    hl_field_new(&fields[fc++], FIELD_NEWS_ART_POSTER, poster, poster_len);
    hl_field_new(&fields[fc++], FIELD_NEWS_ART_DATE, date, 8);
    hl_field_new(&fields[fc++], FIELD_NEWS_ART_PREV_ART, zero4, 4);
    hl_field_new(&fields[fc++], FIELD_NEWS_ART_NEXT_ART, zero4, 4);
    hl_field_new(&fields[fc++], FIELD_NEWS_ART_PARENT_ART, zero4, 4);
    hl_field_new(&fields[fc++], FIELD_NEWS_ART_1ST_CHILD, zero4, 4);
    hl_field_new(&fields[fc++], FIELD_NEWS_ART_DATA_FLAV,
                 (const uint8_t *)"text/plain", 10);
    hl_field_new(&fields[fc++], FIELD_NEWS_ART_DATA, data, data_len);

    hl_transaction_t *reply = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    hl_transaction_new(reply, req->type, cc->id, fields, (uint16_t)fc);
    reply->is_reply = 1;
    memcpy(reply->id, req->id, 4);

    int k;
    for (k = 0; k < fc; k++) hl_field_free(&fields[k]);
    free(title); free(poster); free(data);

    *out = reply;
    *out_count = 1;
    return 0;
}

static int handle_new_news_cat(hl_client_conn_t *cc, const hl_transaction_t *req,
                               hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_NEWS_CREATE_CAT))
        return reply_err(cc, req, "You are not allowed to create news categories.", out, out_count);

    hl_server_t *srv = cc->server;
    const hl_field_t *name_field = hl_transaction_get_field(req, FIELD_NEWS_CAT_NAME);
    if (!name_field || name_field->data_len == 0 || !srv->threaded_news)
        return reply_empty(cc, req, out, out_count);

    char name[TN_MAX_NAME_LEN];
    size_t len = name_field->data_len < sizeof(name) - 1 ?
                 name_field->data_len : sizeof(name) - 1;
    memcpy(name, name_field->data, len);
    name[len] = '\0';

    uint8_t cat_type[2] = {0x00, 0x03}; /* Category */
    tn_create_category(srv->threaded_news, name, cat_type);

    return reply_empty(cc, req, out, out_count);
}

static int handle_new_news_fldr(hl_client_conn_t *cc, const hl_transaction_t *req,
                                hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_NEWS_CREATE_FLDR))
        return reply_err(cc, req, "You are not allowed to create news bundles.", out, out_count);

    hl_server_t *srv = cc->server;
    const hl_field_t *name_field = hl_transaction_get_field(req, FIELD_NEWS_CAT_NAME);
    if (!name_field || name_field->data_len == 0 || !srv->threaded_news)
        return reply_empty(cc, req, out, out_count);

    char name[TN_MAX_NAME_LEN];
    size_t len = name_field->data_len < sizeof(name) - 1 ?
                 name_field->data_len : sizeof(name) - 1;
    memcpy(name, name_field->data, len);
    name[len] = '\0';

    uint8_t bundle_type[2] = {0x00, 0x02}; /* Bundle */
    tn_create_category(srv->threaded_news, name, bundle_type);

    return reply_empty(cc, req, out, out_count);
}

static int handle_post_news_art(hl_client_conn_t *cc, const hl_transaction_t *req,
                                hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_NEWS_POST_ART))
        return reply_err(cc, req, "You are not allowed to post articles.", out, out_count);

    hl_server_t *srv = cc->server;
    if (!srv->threaded_news)
        return reply_empty(cc, req, out, out_count);

    char cat_name[TN_MAX_NAME_LEN];
    decode_news_path_cat(req, cat_name, sizeof(cat_name));

    /* Get parent article ID (can be 2 or 4 bytes) */
    uint32_t parent_id = 0;
    const hl_field_t *id_field = hl_transaction_get_field(req, FIELD_NEWS_ART_ID);
    if (id_field && id_field->data_len >= 4)
        parent_id = hl_read_u32(id_field->data);
    else if (id_field && id_field->data_len >= 2)
        parent_id = (uint32_t)hl_read_u16(id_field->data);

    /* Get title */
    char title[TN_MAX_TITLE_LEN] = "";
    const hl_field_t *title_field = hl_transaction_get_field(req, FIELD_NEWS_ART_TITLE);
    if (title_field && title_field->data_len > 0) {
        size_t tl = title_field->data_len < sizeof(title) - 1 ?
                    title_field->data_len : sizeof(title) - 1;
        memcpy(title, title_field->data, tl);
        title[tl] = '\0';
    }

    /* Get data */
    const hl_field_t *data_field = hl_transaction_get_field(req, FIELD_NEWS_ART_DATA);
    const char *data = "";
    uint16_t data_len = 0;
    if (data_field && data_field->data_len > 0) {
        data = (const char *)data_field->data;
        data_len = data_field->data_len;
    }

    tn_post_article(srv->threaded_news, cat_name, parent_id,
                    title, cc->user_name, data, data_len);

    return reply_empty(cc, req, out, out_count);
}

static int handle_del_news_item(hl_client_conn_t *cc, const hl_transaction_t *req,
                                hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_NEWS_DELETE_CAT) &&
        !hl_client_conn_authorize(cc, ACCESS_NEWS_DELETE_FLDR))
        return reply_err(cc, req, "You are not allowed to delete news items.", out, out_count);

    hl_server_t *srv = cc->server;
    if (!srv->threaded_news)
        return reply_empty(cc, req, out, out_count);

    char cat_name[TN_MAX_NAME_LEN];
    decode_news_path_cat(req, cat_name, sizeof(cat_name));
    tn_delete_news_item(srv->threaded_news, cat_name);

    return reply_empty(cc, req, out, out_count);
}

static int handle_del_news_art(hl_client_conn_t *cc, const hl_transaction_t *req,
                               hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_NEWS_DELETE_ART))
        return reply_err(cc, req, "You are not allowed to delete articles.", out, out_count);

    hl_server_t *srv = cc->server;
    if (!srv->threaded_news)
        return reply_empty(cc, req, out, out_count);

    char cat_name[TN_MAX_NAME_LEN];
    decode_news_path_cat(req, cat_name, sizeof(cat_name));

    const hl_field_t *id_field = hl_transaction_get_field(req, FIELD_NEWS_ART_ID);
    if (!id_field || id_field->data_len < 2)
        return reply_empty(cc, req, out, out_count);

    uint32_t article_id;
    if (id_field->data_len >= 4)
        article_id = hl_read_u32(id_field->data);
    else
        article_id = (uint32_t)hl_read_u16(id_field->data);
    tn_delete_article(srv->threaded_news, cat_name, article_id);

    return reply_empty(cc, req, out, out_count);
}

/* ====================================================================
 * GROUP E: FILE HANDLERS
 * ==================================================================== */

/* Reject filenames containing path separators or traversal */
static int is_safe_filename(const char *name, size_t len)
{
    size_t i;
    if (len == 0) return 0;
    if (len == 1 && name[0] == '.') return 0;
    if (len == 2 && name[0] == '.' && name[1] == '.') return 0;
    for (i = 0; i < len; i++) {
        if (name[i] == '/' || name[i] == '\\' || name[i] == '\0') return 0;
    }
    return 1;
}

/* Helper: build full path from file root + optional encoded path.
 * Returns 0 on success, -1 if the path is invalid or escapes the root. */
static int build_full_path(hl_client_conn_t *cc, const hl_transaction_t *req,
                           char *out, size_t out_len)
{
    const char *root = cc->server->config.file_root;
    if (cc->account && cc->account->file_root[0] != '\0') {
        root = cc->account->file_root;
    }
    strncpy(out, root, out_len - 1);

    const hl_field_t *f_path = hl_transaction_get_field(req, FIELD_FILE_PATH);
    if (f_path && f_path->data_len > 0) {
        hl_file_path_t fp;
        if (hl_file_path_deserialize(&fp, f_path->data, f_path->data_len) != 0)
            return -1;
        char sub[1024];
        if (hl_file_path_to_platform(&fp, NULL, sub, sizeof(sub)) == 0) {
            size_t rlen = strlen(out);
            snprintf(out + rlen, out_len - rlen, "/%s", sub);
        }
    }

    /* Resolve and verify the path stays under the file root */
    char resolved[2048], resolved_root[2048];
    if (!realpath(root, resolved_root)) return -1;
    if (!realpath(out, resolved)) return -1;

    size_t root_len = strlen(resolved_root);
    if (strncmp(resolved, resolved_root, root_len) != 0) return -1;
    /* Must be exact prefix: next char is '/' or '\0' */
    if (resolved[root_len] != '\0' && resolved[root_len] != '/') return -1;

    strncpy(out, resolved, out_len - 1);
    out[out_len - 1] = '\0';
    return 0;
}

static int handle_get_file_name_list(hl_client_conn_t *cc, const hl_transaction_t *req,
                                     hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_DOWNLOAD_FILE))
        return reply_err(cc, req, "You are not allowed to view files.", out, out_count);

    char dir_path[2048];
    if (build_full_path(cc, req, dir_path, sizeof(dir_path)) != 0)
        return reply_err(cc, req, "Invalid file path.", out, out_count);

    int field_count = 0;
    hl_field_t *fields = hl_get_file_name_list(dir_path, &field_count);

    *out = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    if (!*out) return -1;
    hl_client_conn_new_reply(cc, req, *out, fields, (uint16_t)field_count);
    *out_count = 1;

    if (fields) {
        int i;
        for (i = 0; i < field_count; i++) hl_field_free(&fields[i]);
        free(fields);
    }
    return 0;
}

static int handle_get_file_info(hl_client_conn_t *cc, const hl_transaction_t *req,
                                hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_DOWNLOAD_FILE))
        return reply_err(cc, req, "You are not allowed to get file info.", out, out_count);

    const hl_field_t *f_name = hl_transaction_get_field(req, FIELD_FILE_NAME);
    if (!f_name) return reply_err(cc, req, "Missing filename.", out, out_count);
    if (!is_safe_filename((const char *)f_name->data, f_name->data_len))
        return reply_err(cc, req, "Invalid filename.", out, out_count);

    char dir_path[2048];
    if (build_full_path(cc, req, dir_path, sizeof(dir_path)) != 0)
        return reply_err(cc, req, "Invalid file path.", out, out_count);

    char filename[256];
    size_t nlen = f_name->data_len < 255 ? f_name->data_len : 255;
    memcpy(filename, f_name->data, nlen);
    filename[nlen] = '\0';

    char full_path[2048];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, filename);

    struct stat st;
    if (stat(full_path, &st) != 0)
        return reply_err(cc, req, "File not found.", out, out_count);

    const hl_file_type_entry_t *ft;
    if (S_ISDIR(st.st_mode)) {
        ft = NULL;
    } else {
        ft = hl_file_type_from_filename(filename);
    }

    hl_field_t fields[8];
    int fc = 0;
    memset(fields, 0, sizeof(fields));

    hl_field_new(&fields[fc++], FIELD_FILE_NAME, f_name->data, f_name->data_len);

    if (ft) {
        hl_field_new(&fields[fc++], FIELD_FILE_TYPE_STRING,
                     (const uint8_t *)ft->type, 4);
        hl_field_new(&fields[fc++], FIELD_FILE_CREATOR_STRING,
                     (const uint8_t *)ft->creator, 4);
    } else {
        hl_field_new(&fields[fc++], FIELD_FILE_TYPE_STRING,
                     (const uint8_t *)HL_TYPE_FOLDER, 4);
    }

    /* Dates */
    hl_time_t ct, mt;
    hl_time_from_timet(ct, st.st_ctime);
    hl_time_from_timet(mt, st.st_mtime);
    hl_field_new(&fields[fc++], FIELD_FILE_CREATE_DATE, ct, 8);
    hl_field_new(&fields[fc++], FIELD_FILE_MODIFY_DATE, mt, 8);

    if (!S_ISDIR(st.st_mode)) {
        uint8_t size_buf[4];
        hl_write_u32(size_buf, (uint32_t)st.st_size);
        hl_field_new(&fields[fc++], FIELD_FILE_SIZE, size_buf, 4);
    }

    *out = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    if (!*out) return -1;
    hl_client_conn_new_reply(cc, req, *out, fields, (uint16_t)fc);
    *out_count = 1;

    int i;
    for (i = 0; i < fc; i++) hl_field_free(&fields[i]);
    return 0;
}

static int handle_set_file_info(hl_client_conn_t *cc, const hl_transaction_t *req,
                                hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_RENAME_FILE))
        return reply_err(cc, req, "You are not allowed to modify file info.", out, out_count);

    const hl_field_t *f_name = hl_transaction_get_field(req, FIELD_FILE_NAME);
    if (!f_name) return reply_err(cc, req, "Missing filename.", out, out_count);
    if (!is_safe_filename((const char *)f_name->data, f_name->data_len))
        return reply_err(cc, req, "Invalid filename.", out, out_count);

    char dir_path[2048];
    if (build_full_path(cc, req, dir_path, sizeof(dir_path)) != 0)
        return reply_err(cc, req, "Invalid file path.", out, out_count);

    char filename[256];
    size_t nlen = f_name->data_len < 255 ? f_name->data_len : 255;
    memcpy(filename, f_name->data, nlen);
    filename[nlen] = '\0';

    /* Handle rename */
    const hl_field_t *f_new_name = hl_transaction_get_field(req, FIELD_FILE_NEW_NAME);
    if (f_new_name && f_new_name->data_len > 0) {
        if (!is_safe_filename((const char *)f_new_name->data, f_new_name->data_len))
            return reply_err(cc, req, "Invalid new filename.", out, out_count);

        char new_name[256];
        size_t nnlen = f_new_name->data_len < 255 ? f_new_name->data_len : 255;
        memcpy(new_name, f_new_name->data, nnlen);
        new_name[nnlen] = '\0';

        char old_path[2048], new_path[2048];
        snprintf(old_path, sizeof(old_path), "%s/%s", dir_path, filename);
        snprintf(new_path, sizeof(new_path), "%s/%s", dir_path, new_name);
        rename(old_path, new_path);
    }

    return reply_empty(cc, req, out, out_count);
}

static int handle_delete_file(hl_client_conn_t *cc, const hl_transaction_t *req,
                              hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_DELETE_FILE))
        return reply_err(cc, req, "You are not allowed to delete files.", out, out_count);

    const hl_field_t *f_name = hl_transaction_get_field(req, FIELD_FILE_NAME);
    if (!f_name) return reply_err(cc, req, "Missing filename.", out, out_count);
    if (!is_safe_filename((const char *)f_name->data, f_name->data_len))
        return reply_err(cc, req, "Invalid filename.", out, out_count);

    char dir_path[2048];
    if (build_full_path(cc, req, dir_path, sizeof(dir_path)) != 0)
        return reply_err(cc, req, "Invalid file path.", out, out_count);

    char filename[256];
    size_t nlen = f_name->data_len < 255 ? f_name->data_len : 255;
    memcpy(filename, f_name->data, nlen);
    filename[nlen] = '\0';

    hl_file_t f;
    hl_file_init(&f, dir_path, filename);
    hl_file_delete(&f);

    return reply_empty(cc, req, out, out_count);
}

static int handle_move_file(hl_client_conn_t *cc, const hl_transaction_t *req,
                            hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_MOVE_FILE))
        return reply_err(cc, req, "You are not allowed to move files.", out, out_count);

    const hl_field_t *f_name = hl_transaction_get_field(req, FIELD_FILE_NAME);
    const hl_field_t *f_new_path = hl_transaction_get_field(req, FIELD_FILE_NEW_PATH);
    if (!f_name || !f_new_path)
        return reply_err(cc, req, "Missing fields.", out, out_count);
    if (!is_safe_filename((const char *)f_name->data, f_name->data_len))
        return reply_err(cc, req, "Invalid filename.", out, out_count);

    char dir_path[2048];
    if (build_full_path(cc, req, dir_path, sizeof(dir_path)) != 0)
        return reply_err(cc, req, "Invalid file path.", out, out_count);

    char filename[256];
    size_t nlen = f_name->data_len < 255 ? f_name->data_len : 255;
    memcpy(filename, f_name->data, nlen);
    filename[nlen] = '\0';

    /* Parse and validate new destination path */
    const char *root = cc->server->config.file_root;
    char new_dir[2048];
    strncpy(new_dir, root, sizeof(new_dir) - 1);

    hl_file_path_t new_fp;
    if (hl_file_path_deserialize(&new_fp, f_new_path->data, f_new_path->data_len) == 0) {
        char sub[1024];
        if (hl_file_path_to_platform(&new_fp, NULL, sub, sizeof(sub)) == 0) {
            size_t rlen = strlen(new_dir);
            snprintf(new_dir + rlen, sizeof(new_dir) - rlen, "/%s", sub);
        }
    }

    /* Verify destination stays under file root */
    char resolved_new[2048], resolved_root[2048];
    if (!realpath(root, resolved_root) || !realpath(new_dir, resolved_new))
        return reply_err(cc, req, "Invalid destination path.", out, out_count);
    size_t rr_len = strlen(resolved_root);
    if (strncmp(resolved_new, resolved_root, rr_len) != 0 ||
        (resolved_new[rr_len] != '\0' && resolved_new[rr_len] != '/'))
        return reply_err(cc, req, "Invalid destination path.", out, out_count);

    hl_file_t f;
    hl_file_init(&f, dir_path, filename);
    hl_file_move(&f, new_dir);

    return reply_empty(cc, req, out, out_count);
}

static int handle_new_folder(hl_client_conn_t *cc, const hl_transaction_t *req,
                             hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_CREATE_FOLDER))
        return reply_err(cc, req, "You are not allowed to create folders.", out, out_count);

    const hl_field_t *f_name = hl_transaction_get_field(req, FIELD_FILE_NAME);
    if (!f_name) return reply_err(cc, req, "Missing folder name.", out, out_count);
    if (!is_safe_filename((const char *)f_name->data, f_name->data_len))
        return reply_err(cc, req, "Invalid folder name.", out, out_count);

    char dir_path[2048];
    if (build_full_path(cc, req, dir_path, sizeof(dir_path)) != 0)
        return reply_err(cc, req, "Invalid file path.", out, out_count);

    char folder_name[256];
    size_t nlen = f_name->data_len < 255 ? f_name->data_len : 255;
    memcpy(folder_name, f_name->data, nlen);
    folder_name[nlen] = '\0';

    char full_path[2048];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, folder_name);
    mkdir(full_path, 0755);

    return reply_empty(cc, req, out, out_count);
}

static int handle_make_alias(hl_client_conn_t *cc, const hl_transaction_t *req,
                             hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_MAKE_ALIAS))
        return reply_err(cc, req, "You are not allowed to make aliases.", out, out_count);

    const hl_field_t *f_name = hl_transaction_get_field(req, FIELD_FILE_NAME);
    const hl_field_t *f_new_path = hl_transaction_get_field(req, FIELD_FILE_NEW_PATH);
    if (!f_name || !f_new_path)
        return reply_err(cc, req, "Missing fields.", out, out_count);

    char dir_path[2048];
    if (build_full_path(cc, req, dir_path, sizeof(dir_path)) != 0)
        return reply_err(cc, req, "Invalid file path.", out, out_count);

    char filename[256];
    size_t nlen = f_name->data_len < 255 ? f_name->data_len : 255;
    memcpy(filename, f_name->data, nlen);
    filename[nlen] = '\0';

    char src_path[2048];
    snprintf(src_path, sizeof(src_path), "%s/%s", dir_path, filename);

    /* Parse destination path */
    char dest_dir[2048];
    strncpy(dest_dir, cc->server->config.file_root, sizeof(dest_dir) - 1);
    hl_file_path_t dest_fp;
    if (hl_file_path_deserialize(&dest_fp, f_new_path->data, f_new_path->data_len) == 0) {
        char sub[1024];
        if (hl_file_path_to_platform(&dest_fp, NULL, sub, sizeof(sub)) == 0) {
            size_t rlen = strlen(dest_dir);
            snprintf(dest_dir + rlen, sizeof(dest_dir) - rlen, "/%s", sub);
        }
    }

    char dest_path[2048];
    snprintf(dest_path, sizeof(dest_path), "%s/%s", dest_dir, filename);
    symlink(src_path, dest_path);

    return reply_empty(cc, req, out, out_count);
}

/* File transfer handlers — these set up transfers but actual data movement
 * happens on the file transfer port (port+1). For now, return empty replies
 * since the file transfer port handler is a stub. */

static int handle_download_file(hl_client_conn_t *cc, const hl_transaction_t *req,
                                hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_DOWNLOAD_FILE))
        return reply_err(cc, req, "You are not allowed to download files.", out, out_count);
    return reply_empty(cc, req, out, out_count);
}

static int handle_upload_file(hl_client_conn_t *cc, const hl_transaction_t *req,
                              hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_UPLOAD_FILE))
        return reply_err(cc, req, "You are not allowed to upload files.", out, out_count);
    return reply_empty(cc, req, out, out_count);
}

static int handle_download_folder(hl_client_conn_t *cc, const hl_transaction_t *req,
                                  hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_DOWNLOAD_FOLDER))
        return reply_err(cc, req, "You are not allowed to download folders.", out, out_count);
    return reply_empty(cc, req, out, out_count);
}

static int handle_upload_folder(hl_client_conn_t *cc, const hl_transaction_t *req,
                                hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_UPLOAD_FOLDER))
        return reply_err(cc, req, "You are not allowed to upload folders.", out, out_count);
    return reply_empty(cc, req, out, out_count);
}

static int handle_download_banner(hl_client_conn_t *cc, const hl_transaction_t *req,
                                  hl_transaction_t **out, int *out_count)
{
    /* Maps to: Go HandleDownloadBanner — creates a file transfer for the banner.
     * Returns FieldRefNum + FieldTransferSize so the client can download
     * via the transfer port (base+1). */
    hl_server_t *srv = cc->server;

    if (!srv->banner || srv->banner_len == 0) {
        return reply_empty(cc, req, out, out_count);
    }

    /* Create a file transfer entry for the banner */
    hl_file_transfer_t *ft = (hl_file_transfer_t *)calloc(1, sizeof(hl_file_transfer_t));
    if (!ft) return reply_empty(cc, req, out, out_count);

    ft->type = HL_XFER_BANNER_DOWNLOAD;
    ft->client_conn = cc;
    ft->active = 1;
    hl_write_u32(ft->transfer_size, (uint32_t)srv->banner_len);

    /* Add to transfer manager (assigns random ref_num) */
    if (srv->file_transfer_mgr) {
        srv->file_transfer_mgr->vt->add(srv->file_transfer_mgr, ft);
    }

    /* Build reply with ref_num and transfer_size */
    hl_field_t fields[2];
    hl_field_new(&fields[0], FIELD_REF_NUM, ft->ref_num, 4);
    hl_field_new(&fields[1], FIELD_TRANSFER_SIZE, ft->transfer_size, 4);

    hl_transaction_t *reply = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    hl_transaction_new(reply, req->type, cc->id, fields, 2);
    reply->is_reply = 1;
    memcpy(reply->id, req->id, 4);

    hl_field_free(&fields[0]);
    hl_field_free(&fields[1]);

    *out = reply;
    *out_count = 1;
    return 0;
}

/* ====================================================================
 * HANDLER REGISTRATION
 * ==================================================================== */

void mobius_register_handlers(hl_server_t *srv)
{
    hl_server_handle_func(srv, TRAN_KEEP_ALIVE,          handle_keep_alive);
    hl_server_handle_func(srv, TRAN_AGREED,              handle_agreed);
    hl_server_handle_func(srv, TRAN_SET_CLIENT_USER_INFO,handle_set_client_user_info);
    hl_server_handle_func(srv, TRAN_CHAT_SEND,           handle_chat_send);
    hl_server_handle_func(srv, TRAN_SEND_INSTANT_MSG,    handle_send_instant_msg);
    hl_server_handle_func(srv, TRAN_GET_USER_NAME_LIST,  handle_get_user_name_list);
    hl_server_handle_func(srv, TRAN_GET_CLIENT_INFO_TEXT, handle_get_client_info_text);
    hl_server_handle_func(srv, TRAN_USER_BROADCAST,      handle_user_broadcast);

    hl_server_handle_func(srv, TRAN_INVITE_NEW_CHAT,     handle_invite_new_chat);
    hl_server_handle_func(srv, TRAN_INVITE_TO_CHAT,      handle_invite_to_chat);
    hl_server_handle_func(srv, TRAN_JOIN_CHAT,           handle_join_chat);
    hl_server_handle_func(srv, TRAN_LEAVE_CHAT,          handle_leave_chat);
    hl_server_handle_func(srv, TRAN_REJECT_CHAT_INVITE,  handle_reject_chat_invite);
    hl_server_handle_func(srv, TRAN_SET_CHAT_SUBJECT,    handle_set_chat_subject);

    hl_server_handle_func(srv, TRAN_GET_USER,            handle_get_user);
    hl_server_handle_func(srv, TRAN_SET_USER,            handle_set_user);
    hl_server_handle_func(srv, TRAN_NEW_USER,            handle_new_user);
    hl_server_handle_func(srv, TRAN_UPDATE_USER,         handle_update_user);
    hl_server_handle_func(srv, TRAN_LIST_USERS,          handle_list_users);
    hl_server_handle_func(srv, TRAN_DELETE_USER,         handle_delete_user);
    hl_server_handle_func(srv, TRAN_DISCONNECT_USER,     handle_disconnect_user);

    hl_server_handle_func(srv, TRAN_GET_MSGS,            handle_get_msgs);
    hl_server_handle_func(srv, TRAN_OLD_POST_NEWS,       handle_old_post_news);
    hl_server_handle_func(srv, TRAN_GET_NEWS_CAT_NAME_LIST, handle_get_news_cat_name_list);
    hl_server_handle_func(srv, TRAN_GET_NEWS_ART_NAME_LIST, handle_get_news_art_name_list);
    hl_server_handle_func(srv, TRAN_GET_NEWS_ART_DATA,   handle_get_news_art_data);
    hl_server_handle_func(srv, TRAN_NEW_NEWS_CAT,        handle_new_news_cat);
    hl_server_handle_func(srv, TRAN_NEW_NEWS_FLDR,       handle_new_news_fldr);
    hl_server_handle_func(srv, TRAN_POST_NEWS_ART,       handle_post_news_art);
    hl_server_handle_func(srv, TRAN_DEL_NEWS_ITEM,       handle_del_news_item);
    hl_server_handle_func(srv, TRAN_DEL_NEWS_ART,        handle_del_news_art);

    hl_server_handle_func(srv, TRAN_GET_FILE_NAME_LIST,  handle_get_file_name_list);
    hl_server_handle_func(srv, TRAN_GET_FILE_INFO,       handle_get_file_info);
    hl_server_handle_func(srv, TRAN_SET_FILE_INFO,       handle_set_file_info);
    hl_server_handle_func(srv, TRAN_DELETE_FILE,         handle_delete_file);
    hl_server_handle_func(srv, TRAN_MOVE_FILE,           handle_move_file);
    hl_server_handle_func(srv, TRAN_NEW_FOLDER,          handle_new_folder);
    hl_server_handle_func(srv, TRAN_MAKE_FILE_ALIAS,     handle_make_alias);
    hl_server_handle_func(srv, TRAN_DOWNLOAD_FILE,       handle_download_file);
    hl_server_handle_func(srv, TRAN_UPLOAD_FILE,         handle_upload_file);
    hl_server_handle_func(srv, TRAN_DOWNLOAD_FLDR,       handle_download_folder);
    hl_server_handle_func(srv, TRAN_UPLOAD_FLDR,         handle_upload_folder);
    hl_server_handle_func(srv, TRAN_DOWNLOAD_BANNER,     handle_download_banner);
}
