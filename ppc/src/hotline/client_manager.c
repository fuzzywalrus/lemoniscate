/*
 * client_manager.c - In-memory client connection manager
 *
 * Maps to: hotline/client_manager.go (MemClientMgr)
 *
 * Uses a simple linked list. For Hotline's scale (dozens of clients,
 * not thousands) this is more efficient than a hash table.
 */

#include "hotline/client_manager.h"
#include "hotline/client_conn.h"
#include <stdlib.h>
#include <string.h>
#include <libkern/OSAtomic.h>

/* Internal linked list node */
typedef struct client_node {
    hl_client_conn_t   *conn;
    struct client_node *next;
} client_node_t;

/* Concrete MemClientMgr — maps to Go MemClientMgr */
typedef struct {
    hl_client_mgr_t      base;          /* Must be first (vtable pointer) */
    client_node_t        *head;         /* Go: clients map[ClientID]*ClientConn */
    pthread_mutex_t       mu;           /* Go: mu sync.Mutex */
    volatile int32_t      next_id;      /* Go: nextClientID atomic.Uint32 */
} mem_client_mgr_t;

/* Forward declarations of vtable methods */
static hl_client_conn_t **mem_list(hl_client_mgr_t *self, int *out_count);
static hl_client_conn_t *mem_get(hl_client_mgr_t *self, hl_client_id_t id);
static void mem_add(hl_client_mgr_t *self, hl_client_conn_t *cc);
static void mem_del(hl_client_mgr_t *self, hl_client_id_t id);

static const hl_client_mgr_vtable_t mem_vtable = {
    .list = mem_list,
    .get  = mem_get,
    .add  = mem_add,
    .del  = mem_del
};

hl_client_mgr_t *hl_mem_client_mgr_new(void)
{
    mem_client_mgr_t *mgr = (mem_client_mgr_t *)calloc(1, sizeof(mem_client_mgr_t));
    if (!mgr) return NULL;

    mgr->base.vt = &mem_vtable;
    mgr->head = NULL;
    mgr->next_id = 0;
    pthread_mutex_init(&mgr->mu, NULL);

    return &mgr->base;
}

void hl_mem_client_mgr_free(hl_client_mgr_t *base)
{
    if (!base) return;
    mem_client_mgr_t *mgr = (mem_client_mgr_t *)base;

    pthread_mutex_lock(&mgr->mu);
    client_node_t *node = mgr->head;
    while (node) {
        client_node_t *next = node->next;
        free(node);
        node = next;
    }
    pthread_mutex_unlock(&mgr->mu);
    pthread_mutex_destroy(&mgr->mu);
    free(mgr);
}

/* Compare helper for sorting by client ID */
static int cmp_client_id(const void *a, const void *b)
{
    const hl_client_conn_t *ca = *(const hl_client_conn_t **)a;
    const hl_client_conn_t *cb = *(const hl_client_conn_t **)b;
    uint16_t ia = hl_read_u16(ca->id);
    uint16_t ib = hl_read_u16(cb->id);
    return (ia > ib) - (ia < ib);
}

/* This needs the full client_conn struct — we use an opaque access.
 * The struct must have hl_client_id_t id as a known-offset field.
 * We declare a minimal view here; the real struct is in client_conn.h. */

static hl_client_conn_t **mem_list(hl_client_mgr_t *self, int *out_count)
{
    /* Maps to: Go MemClientMgr.List() — returns sorted copy */
    mem_client_mgr_t *mgr = (mem_client_mgr_t *)self;
    pthread_mutex_lock(&mgr->mu);

    /* Count nodes */
    int count = 0;
    client_node_t *node;
    for (node = mgr->head; node; node = node->next) count++;

    if (count == 0) {
        pthread_mutex_unlock(&mgr->mu);
        *out_count = 0;
        return NULL;
    }

    /* Build array */
    hl_client_conn_t **arr = (hl_client_conn_t **)malloc(
        (size_t)count * sizeof(hl_client_conn_t *));
    if (!arr) {
        pthread_mutex_unlock(&mgr->mu);
        *out_count = 0;
        return NULL;
    }

    int i = 0;
    for (node = mgr->head; node; node = node->next) {
        arr[i++] = node->conn;
    }
    pthread_mutex_unlock(&mgr->mu);

    /* Sort by client ID (matches Go behavior) */
    qsort(arr, (size_t)count, sizeof(hl_client_conn_t *), cmp_client_id);

    *out_count = count;
    return arr;
}

static hl_client_conn_t *mem_get(hl_client_mgr_t *self, hl_client_id_t id)
{
    mem_client_mgr_t *mgr = (mem_client_mgr_t *)self;
    pthread_mutex_lock(&mgr->mu);

    client_node_t *node;
    for (node = mgr->head; node; node = node->next) {
        if (hl_type_eq(node->conn->id, id)) {
            pthread_mutex_unlock(&mgr->mu);
            return node->conn;
        }
    }

    pthread_mutex_unlock(&mgr->mu);
    return NULL;
}

static void mem_add(hl_client_mgr_t *self, hl_client_conn_t *cc)
{
    /* Maps to: Go MemClientMgr.Add() — assigns ID and adds to map */
    mem_client_mgr_t *mgr = (mem_client_mgr_t *)self;

    /* Atomic increment for thread-safe ID assignment.
     * Maps to: Go nextClientID.Add(1)
     * OSAtomicIncrement32 is available since 10.4 and not deprecated on Tiger. */
    int32_t new_id = OSAtomicIncrement32(&mgr->next_id);
    hl_write_u16(cc->id, (uint16_t)new_id);

    client_node_t *node = (client_node_t *)malloc(sizeof(client_node_t));
    if (!node) return;
    node->conn = cc;

    pthread_mutex_lock(&mgr->mu);
    node->next = mgr->head;
    mgr->head = node;
    pthread_mutex_unlock(&mgr->mu);
}

static void mem_del(hl_client_mgr_t *self, hl_client_id_t id)
{
    mem_client_mgr_t *mgr = (mem_client_mgr_t *)self;
    pthread_mutex_lock(&mgr->mu);

    client_node_t **pp = &mgr->head;
    while (*pp) {
        if (hl_type_eq((*pp)->conn->id, id)) {
            client_node_t *doomed = *pp;
            *pp = doomed->next;
            free(doomed);
            break;
        }
        pp = &(*pp)->next;
    }

    pthread_mutex_unlock(&mgr->mu);
}
