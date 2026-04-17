/*
 * client_manager.h - Client connection manager
 *
 * Maps to: hotline/client_manager.go
 */

#ifndef HOTLINE_CLIENT_MANAGER_H
#define HOTLINE_CLIENT_MANAGER_H

#include "hotline/types.h"
#include <pthread.h>

/* Forward declaration — full struct in client_conn.h */
#ifndef HL_CLIENT_CONN_TYPEDEF
#define HL_CLIENT_CONN_TYPEDEF
typedef struct hl_client_conn hl_client_conn_t;
#endif

/* ClientManager vtable — maps to Go ClientManager interface */
typedef struct hl_client_mgr hl_client_mgr_t;

typedef struct {
    /* Returns a malloc'd array of pointers (caller frees the array, not the conns).
     * *out_count is set to the number of entries. Sorted by ID.
     * Maps to: Go ClientManager.List() */
    hl_client_conn_t **(*list)(hl_client_mgr_t *self, int *out_count);

    /* Get client by ID. Returns NULL if not found.
     * Maps to: Go ClientManager.Get() */
    hl_client_conn_t *(*get)(hl_client_mgr_t *self, hl_client_id_t id);

    /* Add client and assign an ID.
     * Maps to: Go ClientManager.Add() */
    void (*add)(hl_client_mgr_t *self, hl_client_conn_t *cc);

    /* Remove client by ID.
     * Maps to: Go ClientManager.Delete() */
    void (*del)(hl_client_mgr_t *self, hl_client_id_t id);
} hl_client_mgr_vtable_t;

struct hl_client_mgr {
    const hl_client_mgr_vtable_t *vt;
};

/*
 * hl_mem_client_mgr_new - Create an in-memory client manager.
 * Maps to: Go NewMemClientMgr()
 */
hl_client_mgr_t *hl_mem_client_mgr_new(void);

/* Free client manager (does NOT free the client connections themselves) */
void hl_mem_client_mgr_free(hl_client_mgr_t *mgr);

#endif /* HOTLINE_CLIENT_MANAGER_H */
