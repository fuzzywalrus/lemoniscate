/*
 * chat.h - Private chat room manager
 *
 * Maps to: hotline/chat.go
 */

#ifndef HOTLINE_CHAT_H
#define HOTLINE_CHAT_H

#include "hotline/types.h"
#include <pthread.h>

/* Forward declaration */
#ifndef HL_CLIENT_CONN_TYPEDEF
#define HL_CLIENT_CONN_TYPEDEF
typedef struct hl_client_conn hl_client_conn_t;
#endif

typedef struct hl_chat_mgr hl_chat_mgr_t;

/* ChatManager vtable — maps to Go ChatManager interface */
typedef struct {
    /* Create a new private chat containing cc. Returns chat ID. */
    void (*new_chat)(hl_chat_mgr_t *self, hl_client_conn_t *cc, hl_chat_id_t out_id);

    /* Get chat subject */
    const char *(*get_subject)(hl_chat_mgr_t *self, const hl_chat_id_t id);

    /* Set chat subject */
    void (*set_subject)(hl_chat_mgr_t *self, const hl_chat_id_t id, const char *subject);

    /* Add a client to the chat */
    void (*join)(hl_chat_mgr_t *self, const hl_chat_id_t id, hl_client_conn_t *cc);

    /* Remove a client from the chat */
    void (*leave)(hl_chat_mgr_t *self, const hl_chat_id_t id, const hl_client_id_t client_id);

    /* Get sorted list of members. Returns malloc'd array, caller frees. */
    hl_client_conn_t **(*members)(hl_chat_mgr_t *self, const hl_chat_id_t id, int *out_count);
} hl_chat_mgr_vtable_t;

struct hl_chat_mgr {
    const hl_chat_mgr_vtable_t *vt;
};

/*
 * hl_mem_chat_mgr_new - Create an in-memory chat manager.
 * Maps to: Go NewMemChatManager()
 */
hl_chat_mgr_t *hl_mem_chat_mgr_new(void);

void hl_mem_chat_mgr_free(hl_chat_mgr_t *mgr);

#endif /* HOTLINE_CHAT_H */
