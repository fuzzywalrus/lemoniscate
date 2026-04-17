/*
 * chat.c - In-memory private chat manager
 *
 * Maps to: hotline/chat.go (MemChatManager)
 */

#include "hotline/chat.h"
#include "hotline/client_conn.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

#define HL_CHAT_SUBJECT_MAX 256
#define HL_CHAT_MAX_MEMBERS  64
#define HL_CHAT_MAX_ROOMS    64

/* A single private chat room — maps to Go PrivateChat */
typedef struct {
    hl_chat_id_t      id;
    char              subject[HL_CHAT_SUBJECT_MAX];
    hl_client_conn_t *members[HL_CHAT_MAX_MEMBERS];
    hl_client_id_t    member_ids[HL_CHAT_MAX_MEMBERS]; /* parallel array for lookup */
    int               member_count;
    int               active;  /* 1 if slot is in use */
} private_chat_t;

/* Concrete MemChatManager */
typedef struct {
    hl_chat_mgr_t    base;
    private_chat_t   chats[HL_CHAT_MAX_ROOMS];
    pthread_mutex_t  mu;
} mem_chat_mgr_t;

/* Generate random chat ID */
static void random_chat_id(hl_chat_id_t out)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, out, 4);
        close(fd);
        if (n == 4) return;
    }
    /* Fallback */
    out[0] = (uint8_t)(rand() & 0xFF);
    out[1] = (uint8_t)(rand() & 0xFF);
    out[2] = (uint8_t)(rand() & 0xFF);
    out[3] = (uint8_t)(rand() & 0xFF);
}

/* Find chat by ID, returns NULL if not found */
static private_chat_t *find_chat(mem_chat_mgr_t *mgr, const hl_chat_id_t id)
{
    int i;
    for (i = 0; i < HL_CHAT_MAX_ROOMS; i++) {
        if (mgr->chats[i].active && memcmp(mgr->chats[i].id, id, 4) == 0) {
            return &mgr->chats[i];
        }
    }
    return NULL;
}

/* vtable methods */

static void mem_new_chat(hl_chat_mgr_t *self, hl_client_conn_t *cc, hl_chat_id_t out_id)
{
    mem_chat_mgr_t *mgr = (mem_chat_mgr_t *)self;
    pthread_mutex_lock(&mgr->mu);

    /* Find free slot */
    int i;
    for (i = 0; i < HL_CHAT_MAX_ROOMS; i++) {
        if (!mgr->chats[i].active) {
            private_chat_t *chat = &mgr->chats[i];
            memset(chat, 0, sizeof(*chat));
            chat->active = 1;
            random_chat_id(chat->id);
            memcpy(out_id, chat->id, 4);

            /* Add creator as first member */
            chat->members[0] = cc;
            memcpy(chat->member_ids[0], cc->id, 2);
            chat->member_count = 1;

            pthread_mutex_unlock(&mgr->mu);
            return;
        }
    }

    /* No free slots — return zero ID */
    memset(out_id, 0, 4);
    pthread_mutex_unlock(&mgr->mu);
}

static const char *mem_get_subject(hl_chat_mgr_t *self, const hl_chat_id_t id)
{
    mem_chat_mgr_t *mgr = (mem_chat_mgr_t *)self;
    pthread_mutex_lock(&mgr->mu);
    private_chat_t *chat = find_chat(mgr, id);
    const char *subj = chat ? chat->subject : "";
    pthread_mutex_unlock(&mgr->mu);
    return subj;
}

static void mem_set_subject(hl_chat_mgr_t *self, const hl_chat_id_t id, const char *subject)
{
    mem_chat_mgr_t *mgr = (mem_chat_mgr_t *)self;
    pthread_mutex_lock(&mgr->mu);
    private_chat_t *chat = find_chat(mgr, id);
    if (chat) {
        strncpy(chat->subject, subject, HL_CHAT_SUBJECT_MAX - 1);
        chat->subject[HL_CHAT_SUBJECT_MAX - 1] = '\0';
    }
    pthread_mutex_unlock(&mgr->mu);
}

static void mem_join(hl_chat_mgr_t *self, const hl_chat_id_t id, hl_client_conn_t *cc)
{
    mem_chat_mgr_t *mgr = (mem_chat_mgr_t *)self;
    pthread_mutex_lock(&mgr->mu);
    private_chat_t *chat = find_chat(mgr, id);
    if (chat && chat->member_count < HL_CHAT_MAX_MEMBERS) {
        int n = chat->member_count;
        chat->members[n] = cc;
        memcpy(chat->member_ids[n], cc->id, 2);
        chat->member_count++;
    }
    pthread_mutex_unlock(&mgr->mu);
}

static void mem_leave(hl_chat_mgr_t *self, const hl_chat_id_t id, const hl_client_id_t client_id)
{
    mem_chat_mgr_t *mgr = (mem_chat_mgr_t *)self;
    pthread_mutex_lock(&mgr->mu);
    private_chat_t *chat = find_chat(mgr, id);
    if (chat) {
        int i;
        for (i = 0; i < chat->member_count; i++) {
            if (hl_type_eq(chat->member_ids[i], client_id)) {
                /* Shift remaining members down */
                int j;
                for (j = i; j < chat->member_count - 1; j++) {
                    chat->members[j] = chat->members[j + 1];
                    memcpy(chat->member_ids[j], chat->member_ids[j + 1], 2);
                }
                chat->member_count--;

                /* Deactivate empty chat */
                if (chat->member_count == 0) {
                    chat->active = 0;
                }
                break;
            }
        }
    }
    pthread_mutex_unlock(&mgr->mu);
}

static hl_client_conn_t **mem_members(hl_chat_mgr_t *self, const hl_chat_id_t id, int *out_count)
{
    mem_chat_mgr_t *mgr = (mem_chat_mgr_t *)self;
    pthread_mutex_lock(&mgr->mu);
    private_chat_t *chat = find_chat(mgr, id);

    if (!chat || chat->member_count == 0) {
        pthread_mutex_unlock(&mgr->mu);
        *out_count = 0;
        return NULL;
    }

    hl_client_conn_t **arr = (hl_client_conn_t **)malloc(
        (size_t)chat->member_count * sizeof(hl_client_conn_t *));
    if (!arr) {
        pthread_mutex_unlock(&mgr->mu);
        *out_count = 0;
        return NULL;
    }

    memcpy(arr, chat->members, (size_t)chat->member_count * sizeof(hl_client_conn_t *));
    *out_count = chat->member_count;
    pthread_mutex_unlock(&mgr->mu);
    return arr;
}

static const hl_chat_mgr_vtable_t mem_chat_vtable = {
    .new_chat    = mem_new_chat,
    .get_subject = mem_get_subject,
    .set_subject = mem_set_subject,
    .join        = mem_join,
    .leave       = mem_leave,
    .members     = mem_members
};

hl_chat_mgr_t *hl_mem_chat_mgr_new(void)
{
    mem_chat_mgr_t *mgr = (mem_chat_mgr_t *)calloc(1, sizeof(mem_chat_mgr_t));
    if (!mgr) return NULL;

    mgr->base.vt = &mem_chat_vtable;
    pthread_mutex_init(&mgr->mu, NULL);
    return &mgr->base;
}

void hl_mem_chat_mgr_free(hl_chat_mgr_t *base)
{
    if (!base) return;
    mem_chat_mgr_t *mgr = (mem_chat_mgr_t *)base;
    pthread_mutex_destroy(&mgr->mu);
    free(mgr);
}
