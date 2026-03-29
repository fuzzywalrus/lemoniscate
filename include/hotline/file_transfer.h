/*
 * file_transfer.h - File transfer management
 */

#ifndef HOTLINE_FILE_TRANSFER_H
#define HOTLINE_FILE_TRANSFER_H

#include "hotline/types.h"
#include "hotline/file_resume_data.h"
#include <pthread.h>

/* Transfer types */
#define HL_XFER_FILE_DOWNLOAD    0
#define HL_XFER_FILE_UPLOAD      1
#define HL_XFER_FOLDER_DOWNLOAD  2
#define HL_XFER_FOLDER_UPLOAD    3
#define HL_XFER_BANNER_DOWNLOAD  4

/* Folder download action constants */
#define HL_DL_FLDR_ACTION_SEND_FILE   1
#define HL_DL_FLDR_ACTION_RESUME_FILE 2
#define HL_DL_FLDR_ACTION_NEXT_FILE   3

/* Forward declaration */
#ifndef HL_CLIENT_CONN_TYPEDEF
#define HL_CLIENT_CONN_TYPEDEF
typedef struct hl_client_conn hl_client_conn_t;
#endif

typedef uint8_t hl_xfer_id_t[4];

#define HL_MAX_FILE_TRANSFERS 128

typedef struct {
    char                     file_root[1024];
    uint8_t                  file_name[256];
    uint16_t                 file_name_len;
    uint8_t                  file_path_data[1024]; /* Encoded wire path */
    uint16_t                 file_path_len;
    hl_xfer_id_t             ref_num;
    int                      type;   /* HL_XFER_* constant */
    uint8_t                  transfer_size[4]; /* BE uint32 */
    uint8_t                  folder_item_count[2]; /* BE uint16 */
    hl_file_resume_data_t   *resume_data;  /* NULL if not resuming */
    hl_client_conn_t        *client_conn;
    int                      active;
} hl_file_transfer_t;

/* FileTransferMgr */
typedef struct hl_xfer_mgr hl_xfer_mgr_t;

typedef struct {
    /* Add a transfer and assign a random ref_num. */
    void (*add)(hl_xfer_mgr_t *self, hl_file_transfer_t *ft);

    /* Get transfer by ref_num. Returns NULL if not found. */
    hl_file_transfer_t *(*get)(hl_xfer_mgr_t *self, const hl_xfer_id_t id);

    /* Remove transfer by ref_num. */
    void (*del)(hl_xfer_mgr_t *self, const hl_xfer_id_t id);
} hl_xfer_mgr_vtable_t;

struct hl_xfer_mgr {
    const hl_xfer_mgr_vtable_t *vt;
};

/* Create an in-memory file transfer manager. */
hl_xfer_mgr_t *hl_mem_xfer_mgr_new(void);
void hl_mem_xfer_mgr_free(hl_xfer_mgr_t *mgr);

#endif /* HOTLINE_FILE_TRANSFER_H */
