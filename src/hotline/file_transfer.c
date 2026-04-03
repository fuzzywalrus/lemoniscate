/*
 * file_transfer.c - In-memory file transfer manager
 *
 * Maps to: hotline/file_transfer.go (MemFileTransferMgr)
 */

#include "hotline/file_transfer.h"
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>

typedef struct {
    hl_xfer_mgr_t       base;
    hl_file_transfer_t   transfers[HL_MAX_FILE_TRANSFERS];
    pthread_mutex_t      mu;
} mem_xfer_mgr_t;

static void random_ref_num(hl_xfer_id_t out)
{
    int fd = open("/dev/urandom", O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, out, 4);
        close(fd);
        if (n == 4) return;
    }
    out[0] = (uint8_t)(rand() & 0xFF);
    out[1] = (uint8_t)(rand() & 0xFF);
    out[2] = (uint8_t)(rand() & 0xFF);
    out[3] = (uint8_t)(rand() & 0xFF);
}

static void mem_add(hl_xfer_mgr_t *self, hl_file_transfer_t *ft)
{
    mem_xfer_mgr_t *mgr = (mem_xfer_mgr_t *)self;
    pthread_mutex_lock(&mgr->mu);

    /* Generate ref num */
    random_ref_num(ft->ref_num);
    ft->active = 1;

    /* Find free slot */
    int i;
    for (i = 0; i < HL_MAX_FILE_TRANSFERS; i++) {
        if (!mgr->transfers[i].active) {
            memcpy(&mgr->transfers[i], ft, sizeof(hl_file_transfer_t));
            break;
        }
    }

    pthread_mutex_unlock(&mgr->mu);
}

static hl_file_transfer_t *mem_get(hl_xfer_mgr_t *self, const hl_xfer_id_t id)
{
    mem_xfer_mgr_t *mgr = (mem_xfer_mgr_t *)self;
    pthread_mutex_lock(&mgr->mu);

    int i;
    for (i = 0; i < HL_MAX_FILE_TRANSFERS; i++) {
        if (mgr->transfers[i].active &&
            memcmp(mgr->transfers[i].ref_num, id, 4) == 0) {
            pthread_mutex_unlock(&mgr->mu);
            return &mgr->transfers[i];
        }
    }

    pthread_mutex_unlock(&mgr->mu);
    return NULL;
}

static void mem_del(hl_xfer_mgr_t *self, const hl_xfer_id_t id)
{
    mem_xfer_mgr_t *mgr = (mem_xfer_mgr_t *)self;
    pthread_mutex_lock(&mgr->mu);

    int i;
    for (i = 0; i < HL_MAX_FILE_TRANSFERS; i++) {
        if (mgr->transfers[i].active &&
            memcmp(mgr->transfers[i].ref_num, id, 4) == 0) {
            if (mgr->transfers[i].resume_data) {
                free(mgr->transfers[i].resume_data);
                mgr->transfers[i].resume_data = NULL;
            }
            mgr->transfers[i].active = 0;
            break;
        }
    }

    pthread_mutex_unlock(&mgr->mu);
}

static const hl_xfer_mgr_vtable_t mem_xfer_vtable = {
    .add = mem_add,
    .get = mem_get,
    .del = mem_del
};

hl_xfer_mgr_t *hl_mem_xfer_mgr_new(void)
{
    mem_xfer_mgr_t *mgr = (mem_xfer_mgr_t *)calloc(1, sizeof(mem_xfer_mgr_t));
    if (!mgr) return NULL;

    mgr->base.vt = &mem_xfer_vtable;
    pthread_mutex_init(&mgr->mu, NULL);
    return &mgr->base;
}

void hl_mem_xfer_mgr_free(hl_xfer_mgr_t *base)
{
    if (!base) return;
    mem_xfer_mgr_t *mgr = (mem_xfer_mgr_t *)base;
    int i;
    for (i = 0; i < HL_MAX_FILE_TRANSFERS; i++) {
        if (mgr->transfers[i].resume_data) {
            free(mgr->transfers[i].resume_data);
        }
    }
    pthread_mutex_destroy(&mgr->mu);
    free(mgr);
}
