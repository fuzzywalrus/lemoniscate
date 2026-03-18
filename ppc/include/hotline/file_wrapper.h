/*
 * file_wrapper.h - Fork-aware file abstraction
 *
 * Maps to: hotline/file_wrapper.go
 *
 * Wraps a file with its associated data fork, resource fork (.rsrc_),
 * info fork (.info_), and incomplete marker (.incomplete).
 */

#ifndef HOTLINE_FILE_WRAPPER_H
#define HOTLINE_FILE_WRAPPER_H

#include "hotline/flattened_file_object.h"

#define HL_INCOMPLETE_SUFFIX ".incomplete"
#define HL_INFO_FORK_FMT     ".info_%s"
#define HL_RSRC_FORK_FMT     ".rsrc_%s"

typedef struct {
    char    name[256];              /* Filename */
    char    path[1024];             /* Directory path */
    char    data_path[1024];        /* Full path to data fork */
    char    rsrc_path[1024];        /* Full path to .rsrc_ sidecar */
    char    info_path[1024];        /* Full path to .info_ sidecar */
    char    incomplete_path[1024];  /* Full path to .incomplete file */
    int64_t data_offset;            /* Resume offset into data */
} hl_file_t;

/* Initialize a file wrapper from a directory path and filename. */
int hl_file_init(hl_file_t *f, const char *dir_path, const char *filename);

/* Get total transfer size (data + rsrc fork). Returns BE uint32 in out[4]. */
void hl_file_total_size(const hl_file_t *f, uint8_t out[4]);

/* Move all forks to a new directory. */
int hl_file_move(hl_file_t *f, const char *new_dir);

/* Delete all forks. */
int hl_file_delete(const hl_file_t *f);

#endif /* HOTLINE_FILE_WRAPPER_H */
