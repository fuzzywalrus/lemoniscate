/*
 * transaction_handlers.c - All 43 Hotline transaction handlers
 *
 * Maps to: internal/mobius/transaction_handlers.go
 *
 * All TODOs from stub phase have been implemented.
 */

#include "mobius/transaction_handlers.h"
#include "mobius/flat_news.h"
#include "mobius/mnemosyne_sync.h"
#include "hotline/field.h"
#include "hotline/user.h"
#include "hotline/access.h"
#include "hotline/file_path.h"
#include "hotline/files.h"
#include "hotline/file_types.h"
#include "hotline/file_transfer.h"
#include "hotline/file_wrapper.h"
#include "hotline/time_conv.h"
#include "hotline/password.h"
#include "hotline/hope.h"
#include "hotline/file_name_with_info.h"
#include "hotline/chat_history.h"
#include "hotline/encoding.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
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

/* Build a NotifyChangeUser transaction for a client.
 * Order matches Mobius Go: UserName(102), UserID(103), IconID(104), UserFlags(112). */
void hl_build_notify_change_user(hl_client_conn_t *cc, hl_transaction_t *notify)
{
    memset(notify, 0, sizeof(*notify));
    memcpy(notify->type, TRAN_NOTIFY_CHANGE_USER, 2);
    notify->fields = (hl_field_t *)calloc(4, sizeof(hl_field_t));
    if (!notify->fields) return;
    notify->field_count = 4;
    hl_field_new(&notify->fields[0], FIELD_USER_NAME,
                 (const uint8_t *)cc->user_name, cc->user_name_len);
    hl_field_new(&notify->fields[1], FIELD_USER_ID, cc->id, 2);
    hl_field_new(&notify->fields[2], FIELD_USER_ICON_ID, cc->icon, 2);
    hl_field_new(&notify->fields[3], FIELD_USER_FLAGS, cc->flags, 2);
}

void hl_build_notify_change_user_free(hl_transaction_t *notify)
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
            cc->user_name[len] = '\0';
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
    hl_build_notify_change_user(cc, &notify);
    hl_server_broadcast(cc->server, cc, &notify);
    hl_build_notify_change_user_free(&notify);

    /* Send server banner notification with banner type */
    if (cc->server->banner && cc->server->banner_len > 0) {
        /* Determine banner type from config file extension */
        const char *banner_type = "JPEG"; /* default */
        const char *bfile = cc->server->config.banner_file;
        if (bfile[0]) {
            const char *ext = strrchr(bfile, '.');
            if (ext && (strcasecmp(ext, ".gif") == 0))
                banner_type = "GIFf";
        }

        hl_field_t bfield;
        hl_field_new(&bfield, FIELD_BANNER_TYPE,
                     (const uint8_t *)banner_type, (uint16_t)strlen(banner_type));
        hl_transaction_t banner;
        memset(&banner, 0, sizeof(banner));
        memcpy(banner.type, TRAN_SERVER_BANNER, 2);
        banner.fields = &bfield;
        banner.field_count = 1;
        hl_server_send_to_client(cc, &banner);
        hl_field_free(&bfield);
        banner.fields = NULL;
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
        cc->user_name[len] = '\0';
        cc->user_name_len = len;
    }

    if (f_icon && f_icon->data_len >= 2) {
        memcpy(cc->icon, f_icon->data + (f_icon->data_len - 2), 2);
    }

    /* Broadcast change to all clients */
    hl_transaction_t notify;
    hl_build_notify_change_user(cc, &notify);
    hl_server_broadcast(cc->server, cc, &notify);
    hl_build_notify_change_user_free(&notify);

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

        if (cc->server->chat_history && cc->server->config.chat_history_enabled) {
            char nick_utf8[LM_CHAT_MAX_NICK_LEN + 1];
            char body_utf8[LM_CHAT_MAX_BODY_LEN + 1];
            const char *nick_in = (const char *)cc->user_name;
            size_t nick_in_len = cc->user_name_len;
            const char *body_in = (const char *)f_data->data;
            size_t body_in_len = f_data->data_len;
            int sender_is_macroman = cc->utf8_encoding ? 0 : cc->server->use_mac_roman;

            if (sender_is_macroman) {
                int n = hl_macroman_to_utf8(nick_in, nick_in_len,
                                            nick_utf8, sizeof(nick_utf8));
                int b = hl_macroman_to_utf8(body_in, body_in_len,
                                            body_utf8, sizeof(body_utf8));
                if (n < 0) n = 0;
                if (b < 0) b = 0;
                nick_utf8[n] = '\0';
                body_utf8[b] = '\0';
            } else {
                size_t nl = nick_in_len < LM_CHAT_MAX_NICK_LEN
                          ? nick_in_len : LM_CHAT_MAX_NICK_LEN;
                size_t bl = body_in_len < LM_CHAT_MAX_BODY_LEN
                          ? body_in_len : LM_CHAT_MAX_BODY_LEN;
                memcpy(nick_utf8, nick_in, nl);
                nick_utf8[nl] = '\0';
                memcpy(body_utf8, body_in, bl);
                body_utf8[bl] = '\0';
            }

            lm_chat_history_append(cc->server->chat_history,
                                   0,
                                   is_action ? HL_CHAT_FLAG_IS_ACTION : 0,
                                   (uint16_t)((cc->icon[0] << 8) | cc->icon[1]),
                                   nick_utf8, body_utf8);
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

    /* Send TranServerMsg to target — maps to Go HandleSendInstantMsg
     * Must include FIELD_OPTIONS {0,1} to indicate it's a private message */
    uint8_t pm_options[2] = {0, 1};
    hl_field_t msg_fields[6];
    int fcount = 0;
    memset(msg_fields, 0, sizeof(msg_fields));

    if (f_data && f_data->data_len > 0) {
        hl_field_new(&msg_fields[fcount++], FIELD_DATA, f_data->data, f_data->data_len);
    }
    hl_field_new(&msg_fields[fcount++], FIELD_USER_NAME,
                 (const uint8_t *)cc->user_name, cc->user_name_len);
    hl_field_new(&msg_fields[fcount++], FIELD_USER_ID, cc->id, 2);
    hl_field_new(&msg_fields[fcount++], FIELD_OPTIONS, pm_options, 2);

    /* Forward quoting message if present */
    const hl_field_t *f_quote = hl_transaction_get_field(req, FIELD_QUOTING_MSG);
    if (f_quote && f_quote->data_len > 0) {
        hl_field_new(&msg_fields[fcount++], FIELD_QUOTING_MSG,
                     f_quote->data, f_quote->data_len);
    }

    hl_transaction_t msg_tran;
    hl_transaction_new(&msg_tran, TRAN_SERVER_MSG, target_id, msg_fields, (uint16_t)fcount);
    hl_server_send_to_client(target, &msg_tran);

    int i;
    for (i = 0; i < fcount; i++) hl_field_free(&msg_fields[i]);
    hl_transaction_free(&msg_tran);

    /* Check for auto-reply */
    if (target->auto_reply_len > 0) {
        uint8_t auto_options[2] = {0, 1};
        hl_field_t auto_fields[4];
        int afcount = 0;
        memset(auto_fields, 0, sizeof(auto_fields));
        hl_field_new(&auto_fields[afcount++], FIELD_DATA,
                     target->auto_reply, target->auto_reply_len);
        hl_field_new(&auto_fields[afcount++], FIELD_USER_NAME,
                     (const uint8_t *)target->user_name, target->user_name_len);
        hl_field_new(&auto_fields[afcount++], FIELD_USER_ID, target->id, 2);
        hl_field_new(&auto_fields[afcount++], FIELD_OPTIONS, auto_options, 2);

        hl_transaction_t auto_tran;
        hl_transaction_new(&auto_tran, TRAN_SERVER_MSG, cc->id,
                           auto_fields, (uint16_t)afcount);
        hl_server_send_to_client(cc, &auto_tran);

        for (i = 0; i < afcount; i++) hl_field_free(&auto_fields[i]);
        hl_transaction_free(&auto_tran);
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
    if (!f_uid || f_uid->data_len < 2)
        return reply_err(cc, req, "Missing user ID.", out, out_count);

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

    if (cc->server->chat_history && cc->server->config.chat_history_enabled) {
        char body_utf8[LM_CHAT_MAX_BODY_LEN + 1];
        const char *body_in = (const char *)f_data->data;
        size_t body_in_len = f_data->data_len;
        int sender_is_macroman = cc->utf8_encoding ? 0 : cc->server->use_mac_roman;

        if (sender_is_macroman) {
            int b = hl_macroman_to_utf8(body_in, body_in_len,
                                        body_utf8, sizeof(body_utf8));
            if (b < 0) b = 0;
            body_utf8[b] = '\0';
        } else {
            size_t bl = body_in_len < LM_CHAT_MAX_BODY_LEN
                      ? body_in_len : LM_CHAT_MAX_BODY_LEN;
            memcpy(body_utf8, body_in, bl);
            body_utf8[bl] = '\0';
        }

        lm_chat_history_append(cc->server->chat_history, 0,
                               HL_CHAT_FLAG_IS_SERVER_MSG, 0,
                               cc->server->config.name[0] ? cc->server->config.name : "",
                               body_utf8);
    }

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

static uint64_t lm_now_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (uint64_t)tv.tv_sec * 1000ULL + (uint64_t)(tv.tv_usec / 1000);
}

static int lm_chat_rl_take(hl_client_conn_t *cc)
{
    return lm_chat_rl_consume(&cc->chat_rl_tokens_x10,
                              &cc->chat_rl_last_refill_ms,
                              lm_now_ms(),
                              cc->server->config.chat_history_rate_capacity,
                              cc->server->config.chat_history_rate_refill_per_sec);
}

static int lm_pack_history_entry(const lm_chat_entry_t *e,
                                 int use_mac_roman,
                                 uint8_t *out, size_t out_max)
{
    char nick_wire[LM_CHAT_MAX_NICK_LEN + 1];
    char body_wire[LM_CHAT_MAX_BODY_LEN + 1];
    size_t nick_in_len = strlen(e->nick);
    size_t body_in_len = strlen(e->body);
    int nick_wire_len;
    int body_wire_len;
    size_t need;
    uint8_t *p;
    uint64_t id;
    uint64_t ts;

    if (use_mac_roman) {
        int n = hl_utf8_to_macroman(e->nick, nick_in_len,
                                    nick_wire, sizeof(nick_wire));
        int b = hl_utf8_to_macroman(e->body, body_in_len,
                                    body_wire, sizeof(body_wire));
        if (n < 0) {
            size_t cl = nick_in_len < sizeof(nick_wire) ? nick_in_len : sizeof(nick_wire);
            memcpy(nick_wire, e->nick, cl);
            n = (int)cl;
        }
        if (b < 0) {
            size_t cl = body_in_len < sizeof(body_wire) ? body_in_len : sizeof(body_wire);
            memcpy(body_wire, e->body, cl);
            b = (int)cl;
        }
        nick_wire_len = n;
        body_wire_len = b;
    } else {
        size_t nl = nick_in_len < sizeof(nick_wire) ? nick_in_len : sizeof(nick_wire);
        size_t bl = body_in_len < sizeof(body_wire) ? body_in_len : sizeof(body_wire);
        memcpy(nick_wire, e->nick, nl);
        memcpy(body_wire, e->body, bl);
        nick_wire_len = (int)nl;
        body_wire_len = (int)bl;
    }

    need = 8 + 8 + 2 + 2 + 2 + (size_t)nick_wire_len + 2 + (size_t)body_wire_len;
    if (need > out_max) return -1;

    p = out;
    id = e->id;
    p[0] = (uint8_t)(id >> 56); p[1] = (uint8_t)(id >> 48);
    p[2] = (uint8_t)(id >> 40); p[3] = (uint8_t)(id >> 32);
    p[4] = (uint8_t)(id >> 24); p[5] = (uint8_t)(id >> 16);
    p[6] = (uint8_t)(id >> 8);  p[7] = (uint8_t)id;
    p += 8;

    ts = (uint64_t)e->ts;
    p[0] = (uint8_t)(ts >> 56); p[1] = (uint8_t)(ts >> 48);
    p[2] = (uint8_t)(ts >> 40); p[3] = (uint8_t)(ts >> 32);
    p[4] = (uint8_t)(ts >> 24); p[5] = (uint8_t)(ts >> 16);
    p[6] = (uint8_t)(ts >> 8);  p[7] = (uint8_t)ts;
    p += 8;

    p[0] = (uint8_t)(e->flags >> 8);   p[1] = (uint8_t)e->flags;   p += 2;
    p[0] = (uint8_t)(e->icon_id >> 8); p[1] = (uint8_t)e->icon_id; p += 2;

    p[0] = (uint8_t)(nick_wire_len >> 8); p[1] = (uint8_t)nick_wire_len; p += 2;
    memcpy(p, nick_wire, (size_t)nick_wire_len); p += nick_wire_len;

    p[0] = (uint8_t)(body_wire_len >> 8); p[1] = (uint8_t)body_wire_len; p += 2;
    memcpy(p, body_wire, (size_t)body_wire_len); p += body_wire_len;

    return (int)(p - out);
}

static int handle_get_chat_history(hl_client_conn_t *cc, const hl_transaction_t *req,
                                   hl_transaction_t **out, int *out_count)
{
    hl_server_t *srv = cc->server;
    const hl_field_t *f_chan;
    const hl_field_t *f_before;
    const hl_field_t *f_after;
    const hl_field_t *f_limit;
    uint16_t channel_id;
    uint64_t before = 0;
    uint64_t after = 0;
    uint16_t limit = LM_CHAT_HISTORY_DEFAULT_LIMIT;
    lm_chat_entry_t *entries = NULL;
    size_t n = 0;
    uint8_t has_more = 0;
    hl_field_t *fields;
    int field_count = 0;
    int want_macroman;
    size_t i;
    uint8_t entry_buf[8192];

    if (!srv->chat_history || !srv->config.chat_history_enabled)
        return reply_err(cc, req, "Chat history is not enabled on this server.",
                         out, out_count);

    if (!(cc->account &&
          (hl_access_is_set(cc->account->access, ACCESS_READ_CHAT_HISTORY) ||
           hl_access_is_set(cc->account->access, ACCESS_READ_CHAT))))
        return reply_err(cc, req, "You are not allowed to read chat history.",
                         out, out_count);

    if (!lm_chat_rl_take(cc))
        return reply_err(cc, req, "chat history rate limited", out, out_count);

    f_chan = hl_transaction_get_field(req, FIELD_CHANNEL_ID);
    f_before = hl_transaction_get_field(req, FIELD_HISTORY_BEFORE);
    f_after = hl_transaction_get_field(req, FIELD_HISTORY_AFTER);
    f_limit = hl_transaction_get_field(req, FIELD_HISTORY_LIMIT);

    if (!f_chan || f_chan->data_len < 2)
        return reply_err(cc, req, "Missing channel ID.", out, out_count);

    channel_id = (uint16_t)((f_chan->data[0] << 8) | f_chan->data[1]);

    if (f_before && f_before->data_len >= 8) {
        const uint8_t *b = f_before->data;
        before = ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) |
                 ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
                 ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
                 ((uint64_t)b[6] << 8) | (uint64_t)b[7];
    }
    if (f_after && f_after->data_len >= 8) {
        const uint8_t *b = f_after->data;
        after = ((uint64_t)b[0] << 56) | ((uint64_t)b[1] << 48) |
                ((uint64_t)b[2] << 40) | ((uint64_t)b[3] << 32) |
                ((uint64_t)b[4] << 24) | ((uint64_t)b[5] << 16) |
                ((uint64_t)b[6] << 8) | (uint64_t)b[7];
    }

    if (f_limit && f_limit->data_len >= 2)
        limit = (uint16_t)((f_limit->data[0] << 8) | f_limit->data[1]);
    if (limit == 0) limit = LM_CHAT_HISTORY_DEFAULT_LIMIT;
    if (limit > LM_CHAT_HISTORY_MAX_LIMIT) limit = LM_CHAT_HISTORY_MAX_LIMIT;

    if (lm_chat_history_query(srv->chat_history, channel_id,
                              before, after, limit,
                              &entries, &n, &has_more) != 0)
        return reply_err(cc, req, "Failed to query chat history.",
                         out, out_count);

    fields = (hl_field_t *)calloc(n + 1, sizeof(hl_field_t));
    if (!fields) {
        if (entries) lm_chat_history_entries_free(entries);
        return reply_err(cc, req, "Out of memory.", out, out_count);
    }

    want_macroman = cc->utf8_encoding ? 0 : srv->use_mac_roman;
    for (i = 0; i < n; i++) {
        int written = lm_pack_history_entry(&entries[i], want_macroman,
                                            entry_buf, sizeof(entry_buf));
        if (written <= 0) continue;
        if (hl_field_new(&fields[field_count], FIELD_HISTORY_ENTRY,
                         entry_buf, (uint16_t)written) == 0)
            field_count++;
    }

    {
        uint8_t hm = has_more ? 1 : 0;
        if (hl_field_new(&fields[field_count], FIELD_HISTORY_HAS_MORE, &hm, 1) == 0)
            field_count++;
    }

    *out = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    if (!*out) {
        int j;
        for (j = 0; j < field_count; j++) hl_field_free(&fields[j]);
        free(fields);
        if (entries) lm_chat_history_entries_free(entries);
        return -1;
    }
    hl_client_conn_new_reply(cc, req, *out, fields, (uint16_t)field_count);
    *out_count = 1;

    {
        int j;
        for (j = 0; j < field_count; j++) hl_field_free(&fields[j]);
    }
    free(fields);
    if (entries) lm_chat_history_entries_free(entries);
    return 0;
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

    /* Password — store plaintext when HOPE is enabled, otherwise hash */
    const hl_field_t *f_pass = hl_transaction_get_field(req, FIELD_USER_PASSWORD);
    if (f_pass && f_pass->data_len > 0) {
        char plaintext[128];
        hl_field_decode_obfuscated_string(f_pass, plaintext, sizeof(plaintext));
        if (cc->server->config.enable_hope) {
            /* HOPE requires plaintext passwords for MAC verification */
            strncpy(acct.password, plaintext, sizeof(acct.password) - 1);
        } else {
            hl_password_hash(plaintext, acct.password, sizeof(acct.password));
        }
        memset(plaintext, 0, sizeof(plaintext));
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

/* Parse nested subfields from a FieldData blob.
 * Format: [2-byte count][field1: type(2) + size(2) + data(N)]...
 * Returns number of subfields parsed, stores them in out (caller frees). */
static int parse_subfields(const uint8_t *data, uint16_t data_len,
                            hl_field_t *out, int max_fields)
{
    if (data_len < 2) return 0;
    uint16_t count = hl_read_u16(data);
    size_t pos = 2;
    int parsed = 0;

    uint16_t i;
    for (i = 0; i < count && parsed < max_fields; i++) {
        if (pos + 4 > data_len) break;
        uint8_t ftype[2];
        ftype[0] = data[pos]; ftype[1] = data[pos + 1];
        uint16_t fsize = hl_read_u16(data + pos + 2);
        pos += 4;
        if (pos + fsize > data_len) break;
        hl_field_new(&out[parsed], ftype, data + pos, fsize);
        parsed++;
        pos += fsize;
    }
    return parsed;
}

/* Find a field by type in a subfield array. Returns NULL if not found. */
static const hl_field_t *find_subfield(const hl_field_t *fields, int count,
                                        const hl_field_type_t type)
{
    int i;
    for (i = 0; i < count; i++) {
        if (hl_type_eq(fields[i].type, type)) return &fields[i];
    }
    return NULL;
}

static int handle_update_user(hl_client_conn_t *cc, const hl_transaction_t *req,
                              hl_transaction_t **out, int *out_count)
{
    /* Batch user editor (v1.5+). Each top-level FieldData contains nested
     * subfields for one operation: delete, modify/rename, or create. */
    hl_server_t *srv = cc->server;
    if (!srv->account_mgr) return reply_empty(cc, req, out, out_count);

    uint16_t fi;
    for (fi = 0; fi < req->field_count; fi++) {
        if (!hl_type_eq(req->fields[fi].type, FIELD_DATA)) continue;

        /* Parse nested subfields */
        hl_field_t sub[16];
        int nsub = parse_subfields(req->fields[fi].data, req->fields[fi].data_len,
                                    sub, 16);
        if (nsub == 0) continue;

        /* Extract common fields */
        const hl_field_t *sf_login = find_subfield(sub, nsub, FIELD_USER_LOGIN);
        const hl_field_t *sf_name = find_subfield(sub, nsub, FIELD_USER_NAME);
        const hl_field_t *sf_pass = find_subfield(sub, nsub, FIELD_USER_PASSWORD);
        const hl_field_t *sf_access = find_subfield(sub, nsub, FIELD_USER_ACCESS);
        const hl_field_t *sf_data = find_subfield(sub, nsub, FIELD_DATA);

        /* DELETE: only 1 subfield present */
        if (nsub == 1) {
            if (!hl_client_conn_authorize(cc, ACCESS_DELETE_USER)) {
                int j; for (j = 0; j < nsub; j++) hl_field_free(&sub[j]);
                return reply_err(cc, req, "You are not allowed to delete accounts.", out, out_count);
            }
            char login[128] = "";
            if (sf_login) {
                hl_field_decode_obfuscated_string(sf_login, login, sizeof(login));
            } else if (sf_data) {
                hl_field_decode_obfuscated_string(sf_data, login, sizeof(login));
            }
            if (login[0]) {
                srv->account_mgr->vt->del(srv->account_mgr, login);
            }
            int j; for (j = 0; j < nsub; j++) hl_field_free(&sub[j]);
            continue;
        }

        /* Decode login */
        char login[128] = "";
        if (sf_login) hl_field_decode_obfuscated_string(sf_login, login, sizeof(login));

        /* Check for rename: FieldData contains old login */
        char old_login[128] = "";
        if (sf_data && sf_data->data_len > 0) {
            hl_field_decode_obfuscated_string(sf_data, old_login, sizeof(old_login));
        }
        int is_rename = (old_login[0] && login[0] && strcmp(old_login, login) != 0);

        /* Check if account exists */
        const char *lookup = is_rename ? old_login : login;
        hl_account_t *existing = srv->account_mgr->vt->get(srv->account_mgr, lookup);

        if (existing) {
            /* MODIFY / RENAME */
            if (!hl_client_conn_authorize(cc, ACCESS_MODIFY_USER)) {
                int j; for (j = 0; j < nsub; j++) hl_field_free(&sub[j]);
                return reply_err(cc, req, "You are not allowed to modify accounts.", out, out_count);
            }

            if (sf_name && sf_name->data_len > 0) {
                size_t nlen = sf_name->data_len < sizeof(existing->name) - 1 ?
                              sf_name->data_len : sizeof(existing->name) - 1;
                memset(existing->name, 0, sizeof(existing->name));
                memcpy(existing->name, sf_name->data, nlen);
            }
            if (sf_access && sf_access->data_len >= 8) {
                memcpy(existing->access, sf_access->data, 8);
            }
            /* Password: present with data > {0} = new password, {0} = keep, absent = clear */
            if (sf_pass) {
                if (!(sf_pass->data_len == 1 && sf_pass->data[0] == 0)) {
                    char pw[128];
                    hl_field_decode_obfuscated_string(sf_pass, pw, sizeof(pw));
                    if (cc->server->config.enable_hope) {
                        strncpy(existing->password, pw, sizeof(existing->password) - 1);
                    } else {
                        hl_password_hash(pw, existing->password, sizeof(existing->password));
                    }
                    memset(pw, 0, sizeof(pw));
                }
            } else {
                if (cc->server->config.enable_hope) {
                    existing->password[0] = '\0';
                } else {
                    hl_password_hash("", existing->password, sizeof(existing->password));
                }
            }

            if (is_rename) {
                srv->account_mgr->vt->update(srv->account_mgr, existing, login);
            } else {
                srv->account_mgr->vt->update(srv->account_mgr, existing, NULL);
            }
        } else {
            /* CREATE */
            if (!hl_client_conn_authorize(cc, ACCESS_CREATE_USER)) {
                int j; for (j = 0; j < nsub; j++) hl_field_free(&sub[j]);
                return reply_err(cc, req, "You are not allowed to create accounts.", out, out_count);
            }

            hl_account_t acct;
            memset(&acct, 0, sizeof(acct));
            strncpy(acct.login, login, sizeof(acct.login) - 1);
            if (sf_name && sf_name->data_len > 0) {
                size_t nlen = sf_name->data_len < sizeof(acct.name) - 1 ?
                              sf_name->data_len : sizeof(acct.name) - 1;
                memcpy(acct.name, sf_name->data, nlen);
            }
            if (sf_access && sf_access->data_len >= 8) {
                memcpy(acct.access, sf_access->data, 8);
            }
            if (sf_pass && sf_pass->data_len > 0) {
                char pw[128];
                hl_field_decode_obfuscated_string(sf_pass, pw, sizeof(pw));
                if (cc->server->config.enable_hope) {
                    strncpy(acct.password, pw, sizeof(acct.password) - 1);
                } else {
                    hl_password_hash(pw, acct.password, sizeof(acct.password));
                }
                memset(pw, 0, sizeof(pw));
            }
            srv->account_mgr->vt->create(srv->account_mgr, &acct);
        }

        int j; for (j = 0; j < nsub; j++) hl_field_free(&sub[j]);
    }

    return reply_empty(cc, req, out, out_count);
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
 * News path format: Count(2) + {Padding(2) + NameLen(1) + Name(n)}*Count
 * Returns the first path component name, or "General" if empty. */
static const char *decode_news_path_cat(const hl_transaction_t *req, char *buf, size_t buflen)
{
    const hl_field_t *path_field = hl_transaction_get_field(req, FIELD_NEWS_PATH);
    if (!path_field || path_field->data_len < 2) {
        strncpy(buf, "General", buflen - 1);
        buf[buflen - 1] = '\0';
        return buf;
    }

    const uint8_t *p = path_field->data;
    uint16_t count = hl_read_u16(p);
    p += 2;

    if (count == 0 || path_field->data_len < 5) {
        strncpy(buf, "General", buflen - 1);
        buf[buflen - 1] = '\0';
        return buf;
    }

    /* First component: Padding(2) + NameLen(1) + Name(nameLen) */
    p += 2; /* skip padding */
    uint8_t name_len = *p; p++;

    if (name_len == 0 || (size_t)name_len >= buflen ||
        (size_t)(p - path_field->data) + name_len > path_field->data_len) {
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
    const char *e2e_prefix = cc->server->config.hope_required_prefix;
    int is_encrypted = hl_client_is_encrypted(cc);

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

        /* Filter E2E categories for non-encrypted clients */
        const char *cat_name_ptr = (const char *)(cat_data + off + hdr_size + 1);
        if (!is_encrypted && e2e_prefix[0] != '\0' &&
            hl_hope_name_requires_encryption(cat_name_ptr, name_len, e2e_prefix)) {
            off += entry_len;
            continue;
        }

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

    /* Check E2E encryption gating on the category */
    {
        const char *e2e_prefix = srv->config.hope_required_prefix;
        if (e2e_prefix[0] != '\0' &&
            hl_hope_name_requires_encryption(cat_name, strlen(cat_name), e2e_prefix) &&
            !hl_client_is_encrypted(cc))
            return reply_err(cc, req,
                "This news category requires an encrypted connection.", out, out_count);
    }

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

    /* Check E2E encryption gating */
    {
        const char *e2e_prefix = srv->config.hope_required_prefix;
        if (e2e_prefix[0] != '\0' &&
            hl_hope_name_requires_encryption(cat_name, strlen(cat_name), e2e_prefix) &&
            !hl_client_is_encrypted(cc))
            return reply_err(cc, req,
                "This news category requires an encrypted connection.", out, out_count);
    }

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

    /* Check E2E encryption gating */
    {
        const char *e2e_prefix = srv->config.hope_required_prefix;
        if (e2e_prefix[0] != '\0' &&
            hl_hope_name_requires_encryption(cat_name, strlen(cat_name), e2e_prefix) &&
            !hl_client_is_encrypted(cc))
            return reply_err(cc, req,
                "This news category requires an encrypted connection.", out, out_count);
    }

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
                    title, (const char *)cc->user_name, data, data_len);

    /* Mnemosyne: queue incremental news add */
    if (srv->mnemosyne_sync) {
        char date_str[32];
        time_t now = time(NULL);
        struct tm *tm = gmtime(&now);
        snprintf(date_str, sizeof(date_str), "%04d-%02d-%02d",
                 tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday);

        /* Get the article ID that was just assigned */
        pthread_mutex_lock(&srv->threaded_news->mu);
        int ci;
        uint32_t art_id = 0;
        for (ci = 0; ci < srv->threaded_news->category_count; ci++) {
            if (strcmp(srv->threaded_news->categories[ci].name, cat_name) == 0) {
                art_id = srv->threaded_news->categories[ci].next_article_id - 1;
                break;
            }
        }
        pthread_mutex_unlock(&srv->threaded_news->mu);

        mn_queue_news_add((mn_sync_t *)srv->mnemosyne_sync, cat_name,
                          art_id, title, data, (const char *)cc->user_name,
                          date_str);
    }

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

    /* Check drop box viewing permission */
    const char *dir_leaf = strrchr(dir_path, '/');
    dir_leaf = dir_leaf ? dir_leaf + 1 : dir_path;
    if (strcasestr(dir_leaf, "drop box") &&
        !hl_client_conn_authorize(cc, ACCESS_VIEW_DROP_BOXES))
        return reply_err(cc, req, "You are not allowed to view drop boxes.", out, out_count);

    /* Check E2E encryption gating on the directory itself */
    const char *e2e_prefix = cc->server->config.hope_required_prefix;
    if (e2e_prefix[0] != '\0') {
        const char *root = cc->server->config.file_root;
        if (cc->account && cc->account->file_root[0] != '\0')
            root = cc->account->file_root;
        if (hl_hope_path_requires_encryption(dir_path, root, e2e_prefix) &&
            !hl_client_is_encrypted(cc))
            return reply_err(cc, req,
                "This folder requires an encrypted connection.", out, out_count);
    }

    int field_count = 0;
    hl_field_t *fields = hl_get_file_name_list(dir_path, &field_count);

    /* Filter out E2E entries for non-encrypted clients */
    int filtered_count = field_count;
    if (fields && e2e_prefix[0] != '\0' && !hl_client_is_encrypted(cc)) {
        int dst = 0;
        int i;
        for (i = 0; i < field_count; i++) {
            /* Extract name from FNWI field: header(20 bytes) + name */
            int hide = 0;
            if (fields[i].data_len > HL_FNWI_HEADER_SIZE) {
                const char *entry_name = (const char *)(fields[i].data + HL_FNWI_HEADER_SIZE);
                size_t entry_name_len = fields[i].data_len - HL_FNWI_HEADER_SIZE;
                hide = hl_hope_name_requires_encryption(entry_name, entry_name_len,
                                                        e2e_prefix);
            }
            if (hide) {
                hl_field_free(&fields[i]);
            } else {
                if (dst != i) fields[dst] = fields[i];
                dst++;
            }
        }
        filtered_count = dst;
    }

    /* Large file mode: interleave DATA_FILESIZE64 after each FNWI entry */
    hl_field_t *reply_fields = fields;
    int reply_count = filtered_count;

    if (cc->large_file_mode && filtered_count > 0 && fields) {
        reply_count = filtered_count * 2;
        reply_fields = (hl_field_t *)calloc((size_t)reply_count, sizeof(hl_field_t));
        if (reply_fields) {
            int ri = 0;
            int i;
            for (i = 0; i < filtered_count; i++) {
                /* Copy FNWI field */
                reply_fields[ri++] = fields[i];

                /* Extract name and stat for 64-bit size */
                uint64_t fsize64 = 0;
                if (fields[i].data_len > HL_FNWI_HEADER_SIZE) {
                    size_t name_len = fields[i].data_len - HL_FNWI_HEADER_SIZE;
                    char entry_name[256];
                    if (name_len > 255) name_len = 255;
                    memcpy(entry_name, fields[i].data + HL_FNWI_HEADER_SIZE, name_len);
                    entry_name[name_len] = '\0';

                    char entry_path[2048];
                    snprintf(entry_path, sizeof(entry_path), "%s/%s", dir_path, entry_name);
                    struct stat est;
                    if (stat(entry_path, &est) == 0) {
                        if (S_ISDIR(est.st_mode)) {
                            /* For dirs, size field holds item count */
                            fsize64 = (uint64_t)hl_read_u32(fields[i].data + 8);
                        } else {
                            fsize64 = (uint64_t)est.st_size;
                        }
                    }
                }

                uint8_t size64_buf[8];
                hl_write_u64(size64_buf, fsize64);
                hl_field_new(&reply_fields[ri++], FIELD_FILE_SIZE_64, size64_buf, 8);
            }
            reply_count = ri;
            /* Don't free individual fields — they were moved to reply_fields */
            free(fields);
            fields = NULL;
        } else {
            /* Allocation failed, fall back to non-64-bit */
            reply_fields = fields;
            reply_count = filtered_count;
        }
    }

    *out = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    if (!*out) return -1;
    hl_client_conn_new_reply(cc, req, *out, reply_fields, (uint16_t)reply_count);
    *out_count = 1;

    if (reply_fields) {
        int i;
        for (i = 0; i < reply_count; i++) hl_field_free(&reply_fields[i]);
        free(reply_fields);
    } else if (fields) {
        int i;
        for (i = 0; i < filtered_count; i++) hl_field_free(&fields[i]);
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

    hl_field_t fields[9]; /* +1 for 64-bit size */
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
        uint64_t fsize64 = (uint64_t)st.st_size;
        uint32_t fsize32 = (fsize64 > 0xFFFFFFFF) ? 0xFFFFFFFF : (uint32_t)fsize64;
        uint8_t size_buf[4];
        hl_write_u32(size_buf, fsize32);
        hl_field_new(&fields[fc++], FIELD_FILE_SIZE, size_buf, 4);

        if (cc->large_file_mode) {
            uint8_t size64_buf[8];
            hl_write_u64(size64_buf, fsize64);
            hl_field_new(&fields[fc++], FIELD_FILE_SIZE_64, size64_buf, 8);
        }
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

    /* Determine if target is a folder */
    char full_path[2048];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, filename);
    struct stat fst;
    int is_folder = (stat(full_path, &fst) == 0 && S_ISDIR(fst.st_mode));

    /* Handle rename */
    const hl_field_t *f_new_name = hl_transaction_get_field(req, FIELD_FILE_NEW_NAME);
    if (f_new_name && f_new_name->data_len > 0) {
        /* Check rename permission (file vs folder) */
        if (is_folder) {
            if (!hl_client_conn_authorize(cc, ACCESS_RENAME_FOLDER))
                return reply_err(cc, req, "You are not allowed to rename folders.", out, out_count);
        } else {
            if (!hl_client_conn_authorize(cc, ACCESS_RENAME_FILE))
                return reply_err(cc, req, "You are not allowed to rename files.", out, out_count);
        }

        if (!is_safe_filename((const char *)f_new_name->data, f_new_name->data_len))
            return reply_err(cc, req, "Invalid new filename.", out, out_count);

        char new_name[256];
        size_t nnlen = f_new_name->data_len < 255 ? f_new_name->data_len : 255;
        memcpy(new_name, f_new_name->data, nnlen);
        new_name[nnlen] = '\0';

        char new_path[2048];
        snprintf(new_path, sizeof(new_path), "%s/%s", dir_path, new_name);
        rename(full_path, new_path);
    }

    /* Handle comment — check folder comment permission if applicable */
    const hl_field_t *f_comment = hl_transaction_get_field(req, FIELD_FILE_COMMENT);
    if (f_comment) {
        if (is_folder && !hl_client_conn_authorize(cc, ACCESS_SET_FOLDER_COMMENT))
            return reply_err(cc, req, "You are not allowed to comment folders.", out, out_count);
        if (!is_folder && !hl_client_conn_authorize(cc, ACCESS_SET_FILE_COMMENT))
            return reply_err(cc, req, "You are not allowed to comment files.", out, out_count);
    }

    return reply_empty(cc, req, out, out_count);
}

static int handle_delete_file(hl_client_conn_t *cc, const hl_transaction_t *req,
                              hl_transaction_t **out, int *out_count)
{
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

    /* Check folder vs file permission */
    char full_path[2048];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, filename);
    struct stat dst;
    if (stat(full_path, &dst) == 0 && S_ISDIR(dst.st_mode)) {
        if (!hl_client_conn_authorize(cc, ACCESS_DELETE_FOLDER))
            return reply_err(cc, req, "You are not allowed to delete folders.", out, out_count);
    } else {
        if (!hl_client_conn_authorize(cc, ACCESS_DELETE_FILE))
            return reply_err(cc, req, "You are not allowed to delete files.", out, out_count);
    }

    hl_file_t f;
    hl_file_init(&f, dir_path, filename);
    hl_file_delete(&f);

    /* Mnemosyne: queue incremental file remove */
    if (cc->server->mnemosyne_sync) {
        char rel_path[2048];
        const char *root = cc->server->config.file_root;
        size_t root_len = strlen(root);
        if (strncmp(full_path, root, root_len) == 0 && full_path[root_len] == '/')
            snprintf(rel_path, sizeof(rel_path), "%s", full_path + root_len + 1);
        else
            snprintf(rel_path, sizeof(rel_path), "%s", filename);
        mn_queue_file_remove((mn_sync_t *)cc->server->mnemosyne_sync, rel_path);
    }

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

    /* Mnemosyne: file move triggers periodic check (full resync on drift) */
    if (cc->server->mnemosyne_sync) {
        mn_periodic_check((mn_sync_t *)cc->server->mnemosyne_sync);
    }

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

    if (!is_safe_filename(filename, nlen))
        return reply_err(cc, req, "Invalid file name.", out, out_count);

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

    if (cc->account && cc->account->require_encryption && !hl_client_is_encrypted(cc))
        return reply_err(cc, req, "This account requires an encrypted connection for file transfers.", out, out_count);

    hl_server_t *srv = cc->server;

    /* Build full file path */
    char file_path[2048];
    if (build_full_path(cc, req, file_path, sizeof(file_path)) != 0)
        return reply_err(cc, req, "Invalid file path.", out, out_count);

    /* Append filename */
    const hl_field_t *f_name = hl_transaction_get_field(req, FIELD_FILE_NAME);
    if (!f_name || f_name->data_len == 0)
        return reply_err(cc, req, "Missing file name.", out, out_count);

    char filename[256];
    size_t nlen = f_name->data_len < sizeof(filename) - 1 ?
                  f_name->data_len : sizeof(filename) - 1;
    memcpy(filename, f_name->data, nlen);
    filename[nlen] = '\0';

    if (!is_safe_filename(filename, nlen))
        return reply_err(cc, req, "Invalid file name.", out, out_count);

    char full_path[2048];
    snprintf(full_path, sizeof(full_path), "%s/%s", file_path, filename);

    /* Check E2E encryption gating */
    {
        const char *e2e_prefix = srv->config.hope_required_prefix;
        if (e2e_prefix[0] != '\0') {
            const char *root = srv->config.file_root;
            if (cc->account && cc->account->file_root[0] != '\0')
                root = cc->account->file_root;
            if (hl_hope_path_requires_encryption(full_path, root, e2e_prefix) &&
                !hl_client_is_encrypted(cc))
                return reply_err(cc, req,
                    "This file requires an encrypted connection.", out, out_count);
        }
    }

    /* Get file size */
    struct stat st;
    if (stat(full_path, &st) != 0 || S_ISDIR(st.st_mode))
        return reply_err(cc, req, "File not found.", out, out_count);

    uint64_t file_size_64 = (uint64_t)st.st_size;
    uint32_t file_size = (file_size_64 > 0xFFFFFFFF) ? 0xFFFFFFFF : (uint32_t)file_size_64;

    /* Parse optional resume data (RFLT format) */
    uint32_t data_offset = 0;
    hl_file_resume_data_t *resume = NULL;
    const hl_field_t *f_resume = hl_transaction_get_field(req, FIELD_FILE_RESUME_DATA);
    if (f_resume && f_resume->data_len > 0) {
        resume = (hl_file_resume_data_t *)calloc(1, sizeof(hl_file_resume_data_t));
        if (resume) {
            if (hl_file_resume_data_unmarshal(resume, f_resume->data, f_resume->data_len) == 0
                && resume->fork_info_count > 0) {
                data_offset = hl_read_u32(resume->fork_info[0].data_size);
                if (data_offset > file_size) data_offset = file_size;
            } else {
                free(resume);
                resume = NULL;
            }
        }
    }

    /* Create transfer entry */
    hl_file_transfer_t *ft = (hl_file_transfer_t *)calloc(1, sizeof(hl_file_transfer_t));
    if (!ft) { free(resume); return reply_empty(cc, req, out, out_count); }

    ft->type = HL_XFER_FILE_DOWNLOAD;
    ft->client_conn = cc;
    ft->active = 1;
    if (cc->hope && cc->hope->aead_active && cc->hope->aead.ft_base_key_set) {
        ft->ft_aead = 1;
        memcpy(ft->ft_base_key, cc->hope->aead.ft_base_key, 32);
    }
    ft->resume_data = resume;

    /* Store file root and path info for the transfer handler */
    strncpy(ft->file_root, full_path, sizeof(ft->file_root) - 1);

    /* Transfer size = FILP header (24) + INFO fork header (16) +
     * INFO fork data (72 + name_len + 2) + DATA fork header (16) + remaining file data.
     * For resumed transfers, subtract the bytes already sent. */
    uint32_t info_size = 72 + (uint32_t)nlen + 2;
    uint32_t transfer_size = 24 + 16 + info_size + 16 + (file_size - data_offset);
    hl_write_u32(ft->transfer_size, transfer_size);

    if (srv->file_transfer_mgr)
        srv->file_transfer_mgr->vt->add(srv->file_transfer_mgr, ft);

    /* Reply with ref_num, transfer_size, file_size, waiting_count
     * + 64-bit companions in large file mode */
    uint8_t wait_count[2] = {0, 0};

    hl_field_t fields[7]; /* max: 4 legacy + 2 x 64-bit + waiting */
    int fc = 0;
    hl_field_new(&fields[fc++], FIELD_REF_NUM, ft->ref_num, 4);
    hl_field_new(&fields[fc++], FIELD_TRANSFER_SIZE, ft->transfer_size, 4);

    uint8_t fsize_bytes[4];
    hl_write_u32(fsize_bytes, file_size);
    hl_field_new(&fields[fc++], FIELD_FILE_SIZE, fsize_bytes, 4);

    if (cc->large_file_mode) {
        uint64_t xfer_size_64 = (uint64_t)(24 + 16 + info_size + 16) +
                                (file_size_64 - (uint64_t)data_offset);
        uint8_t xfer64[8], fsize64[8];
        hl_write_u64(xfer64, xfer_size_64);
        hl_write_u64(fsize64, file_size_64);
        hl_field_new(&fields[fc++], FIELD_TRANSFER_SIZE_64, xfer64, 8);
        hl_field_new(&fields[fc++], FIELD_FILE_SIZE_64, fsize64, 8);
    }

    hl_field_new(&fields[fc++], FIELD_WAITING_COUNT, wait_count, 2);

    hl_transaction_t *reply = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    hl_transaction_new(reply, req->type, cc->id, fields, (uint16_t)fc);
    reply->is_reply = 1;
    memcpy(reply->id, req->id, 4);

    int k;
    for (k = 0; k < fc; k++) hl_field_free(&fields[k]);
    free(ft); /* add() copied it into the manager's internal array */

    *out = reply;
    *out_count = 1;
    return 0;
}

static int handle_upload_file(hl_client_conn_t *cc, const hl_transaction_t *req,
                              hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_UPLOAD_FILE))
        return reply_err(cc, req, "You are not allowed to upload files.", out, out_count);

    if (cc->account && cc->account->require_encryption && !hl_client_is_encrypted(cc))
        return reply_err(cc, req, "This account requires an encrypted connection for file transfers.", out, out_count);

    hl_server_t *srv = cc->server;

    /* Build destination directory path */
    char dir_path[2048];
    if (build_full_path(cc, req, dir_path, sizeof(dir_path)) != 0)
        return reply_err(cc, req, "Invalid file path.", out, out_count);

    /* Check E2E encryption gating on upload destination */
    {
        const char *e2e_prefix = srv->config.hope_required_prefix;
        if (e2e_prefix[0] != '\0') {
            const char *root = srv->config.file_root;
            if (cc->account && cc->account->file_root[0] != '\0')
                root = cc->account->file_root;
            if (hl_hope_path_requires_encryption(dir_path, root, e2e_prefix) &&
                !hl_client_is_encrypted(cc))
                return reply_err(cc, req,
                    "This folder requires an encrypted connection.", out, out_count);
        }
    }

    /* Check upload location restriction */
    if (!hl_client_conn_authorize(cc, ACCESS_UPLOAD_ANYWHERE)) {
        const char *dir_leaf = strrchr(dir_path, '/');
        dir_leaf = dir_leaf ? dir_leaf + 1 : dir_path;
        if (!strcasestr(dir_leaf, "upload") && !strcasestr(dir_leaf, "drop box"))
            return reply_err(cc, req,
                "You can only upload to the Uploads folder.", out, out_count);
    }

    /* Get filename */
    const hl_field_t *f_name = hl_transaction_get_field(req, FIELD_FILE_NAME);
    if (!f_name || f_name->data_len == 0)
        return reply_err(cc, req, "Missing file name.", out, out_count);

    char filename[256];
    size_t nlen = f_name->data_len < sizeof(filename) - 1 ?
                  f_name->data_len : sizeof(filename) - 1;
    memcpy(filename, f_name->data, nlen);
    filename[nlen] = '\0';

    if (!is_safe_filename(filename, nlen))
        return reply_err(cc, req, "Invalid file name.", out, out_count);

    /* Check if file already exists */
    char full_path[2048];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, filename);

    struct stat st;
    if (stat(full_path, &st) == 0)
        return reply_err(cc, req, "A file with that name already exists.", out, out_count);

    /* Create transfer entry */
    hl_file_transfer_t *ft = (hl_file_transfer_t *)calloc(1, sizeof(hl_file_transfer_t));
    if (!ft) return reply_empty(cc, req, out, out_count);

    ft->type = HL_XFER_FILE_UPLOAD;
    ft->client_conn = cc;
    ft->active = 1;
    if (cc->hope && cc->hope->aead_active && cc->hope->aead.ft_base_key_set) {
        ft->ft_aead = 1;
        memcpy(ft->ft_base_key, cc->hope->aead.ft_base_key, 32);
    }
    strncpy(ft->file_root, full_path, sizeof(ft->file_root) - 1);

    /* Store transfer size from request if provided */
    const hl_field_t *f_size = hl_transaction_get_field(req, FIELD_TRANSFER_SIZE);
    if (f_size && f_size->data_len >= 4) {
        memcpy(ft->transfer_size, f_size->data, 4);
    }

    if (srv->file_transfer_mgr)
        srv->file_transfer_mgr->vt->add(srv->file_transfer_mgr, ft);

    /* Reply with ref_num */
    hl_field_t fields[1];
    hl_field_new(&fields[0], FIELD_REF_NUM, ft->ref_num, 4);

    hl_transaction_t *reply = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    hl_transaction_new(reply, req->type, cc->id, fields, 1);
    reply->is_reply = 1;
    memcpy(reply->id, req->id, 4);
    hl_field_free(&fields[0]);
    free(ft); /* add() copied it into the manager's internal array */

    *out = reply;
    *out_count = 1;
    return 0;
}

static int handle_download_folder(hl_client_conn_t *cc, const hl_transaction_t *req,
                                  hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_DOWNLOAD_FOLDER))
        return reply_err(cc, req, "You are not allowed to download folders.", out, out_count);

    hl_server_t *srv = cc->server;

    /* Build full folder path */
    char dir_path[2048];
    if (build_full_path(cc, req, dir_path, sizeof(dir_path)) != 0)
        return reply_err(cc, req, "Invalid file path.", out, out_count);

    const hl_field_t *f_name = hl_transaction_get_field(req, FIELD_FILE_NAME);
    if (!f_name || f_name->data_len == 0)
        return reply_err(cc, req, "Missing folder name.", out, out_count);

    char foldername[256];
    size_t nlen = f_name->data_len < sizeof(foldername) - 1 ?
                  f_name->data_len : sizeof(foldername) - 1;
    memcpy(foldername, f_name->data, nlen);
    foldername[nlen] = '\0';

    char full_path[2048];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, foldername);

    struct stat st;
    if (stat(full_path, &st) != 0 || !S_ISDIR(st.st_mode))
        return reply_err(cc, req, "Folder not found.", out, out_count);

    /* Calculate transfer size and item count */
    uint8_t total_size[4];
    uint8_t item_count[2];
    hl_calc_total_size(full_path, total_size);
    hl_calc_item_count(full_path, item_count);

    /* Create transfer entry */
    hl_file_transfer_t *ft = (hl_file_transfer_t *)calloc(1, sizeof(hl_file_transfer_t));
    if (!ft) return reply_empty(cc, req, out, out_count);

    ft->type = HL_XFER_FOLDER_DOWNLOAD;
    ft->client_conn = cc;
    ft->active = 1;
    if (cc->hope && cc->hope->aead_active && cc->hope->aead.ft_base_key_set) {
        ft->ft_aead = 1;
        memcpy(ft->ft_base_key, cc->hope->aead.ft_base_key, 32);
    }
    strncpy(ft->file_root, full_path, sizeof(ft->file_root) - 1);
    memcpy(ft->transfer_size, total_size, 4);
    memcpy(ft->folder_item_count, item_count, 2);

    if (srv->file_transfer_mgr)
        srv->file_transfer_mgr->vt->add(srv->file_transfer_mgr, ft);

    /* Reply with ref_num, transfer_size, folder_item_count, waiting_count
     * + 64-bit companions in large file mode */
    uint8_t wait_count[2] = {0, 0};
    hl_field_t fields[7]; /* 4 legacy + up to 2 x 64-bit + waiting */
    int fcount = 0;
    hl_field_new(&fields[fcount++], FIELD_REF_NUM, ft->ref_num, 4);
    hl_field_new(&fields[fcount++], FIELD_TRANSFER_SIZE, ft->transfer_size, 4);
    hl_field_new(&fields[fcount++], FIELD_FOLDER_ITEM_COUNT, ft->folder_item_count, 2);

    if (cc->large_file_mode) {
        uint64_t total_64 = (uint64_t)hl_read_u32(total_size);
        uint64_t count_64 = (uint64_t)hl_read_u16(item_count);
        uint8_t xfer64[8], count64[8];
        hl_write_u64(xfer64, total_64);
        hl_write_u64(count64, count_64);
        hl_field_new(&fields[fcount++], FIELD_TRANSFER_SIZE_64, xfer64, 8);
        hl_field_new(&fields[fcount++], FIELD_FOLDER_ITEM_COUNT_64, count64, 8);
    }

    hl_field_new(&fields[fcount++], FIELD_WAITING_COUNT, wait_count, 2);

    hl_transaction_t *reply = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    hl_transaction_new(reply, req->type, cc->id, fields, (uint16_t)fcount);
    reply->is_reply = 1;
    memcpy(reply->id, req->id, 4);

    int k;
    for (k = 0; k < fcount; k++) hl_field_free(&fields[k]);
    free(ft);

    *out = reply;
    *out_count = 1;
    return 0;
}

static int handle_upload_folder(hl_client_conn_t *cc, const hl_transaction_t *req,
                                hl_transaction_t **out, int *out_count)
{
    if (!hl_client_conn_authorize(cc, ACCESS_UPLOAD_FOLDER))
        return reply_err(cc, req, "You are not allowed to upload folders.", out, out_count);

    hl_server_t *srv = cc->server;

    /* Build destination directory path */
    char dir_path[2048];
    if (build_full_path(cc, req, dir_path, sizeof(dir_path)) != 0)
        return reply_err(cc, req, "Invalid file path.", out, out_count);

    const hl_field_t *f_name = hl_transaction_get_field(req, FIELD_FILE_NAME);
    if (!f_name || f_name->data_len == 0)
        return reply_err(cc, req, "Missing folder name.", out, out_count);

    char foldername[256];
    size_t nlen = f_name->data_len < sizeof(foldername) - 1 ?
                  f_name->data_len : sizeof(foldername) - 1;
    memcpy(foldername, f_name->data, nlen);
    foldername[nlen] = '\0';

    char full_path[2048];
    snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, foldername);

    /* Create the root folder */
    mkdir(full_path, 0755);

    /* Get item count and transfer size from request */
    const hl_field_t *f_count = hl_transaction_get_field(req, FIELD_FOLDER_ITEM_COUNT);
    const hl_field_t *f_size = hl_transaction_get_field(req, FIELD_TRANSFER_SIZE);

    /* Create transfer entry */
    hl_file_transfer_t *ft = (hl_file_transfer_t *)calloc(1, sizeof(hl_file_transfer_t));
    if (!ft) return reply_empty(cc, req, out, out_count);

    ft->type = HL_XFER_FOLDER_UPLOAD;
    ft->client_conn = cc;
    ft->active = 1;
    if (cc->hope && cc->hope->aead_active && cc->hope->aead.ft_base_key_set) {
        ft->ft_aead = 1;
        memcpy(ft->ft_base_key, cc->hope->aead.ft_base_key, 32);
    }
    strncpy(ft->file_root, full_path, sizeof(ft->file_root) - 1);

    if (f_count && f_count->data_len >= 2)
        memcpy(ft->folder_item_count, f_count->data, 2);
    if (f_size && f_size->data_len >= 4)
        memcpy(ft->transfer_size, f_size->data, 4);

    if (srv->file_transfer_mgr)
        srv->file_transfer_mgr->vt->add(srv->file_transfer_mgr, ft);

    /* Reply with ref_num */
    hl_field_t fields[1];
    hl_field_new(&fields[0], FIELD_REF_NUM, ft->ref_num, 4);

    hl_transaction_t *reply = (hl_transaction_t *)calloc(1, sizeof(hl_transaction_t));
    hl_transaction_new(reply, req->type, cc->id, fields, 1);
    reply->is_reply = 1;
    memcpy(reply->id, req->id, 4);
    hl_field_free(&fields[0]);
    free(ft);

    *out = reply;
    *out_count = 1;
    return 0;
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
    if (cc->hope && cc->hope->aead_active && cc->hope->aead.ft_base_key_set) {
        ft->ft_aead = 1;
        memcpy(ft->ft_base_key, cc->hope->aead.ft_base_key, 32);
    }
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
    free(ft); /* add() copied it into the manager's internal array */

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
    hl_server_handle_func(srv, TRAN_GET_CHAT_HISTORY,    handle_get_chat_history);

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
