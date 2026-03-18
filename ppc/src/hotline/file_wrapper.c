/*
 * file_wrapper.c - Fork-aware file abstraction
 *
 * Maps to: hotline/file_wrapper.go
 */

#include "hotline/file_wrapper.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

int hl_file_init(hl_file_t *f, const char *dir_path, const char *filename)
{
    memset(f, 0, sizeof(*f));
    strncpy(f->name, filename, sizeof(f->name) - 1);
    strncpy(f->path, dir_path, sizeof(f->path) - 1);

    snprintf(f->data_path, sizeof(f->data_path), "%s/%s", dir_path, filename);
    snprintf(f->rsrc_path, sizeof(f->rsrc_path), "%s/" HL_RSRC_FORK_FMT, dir_path, filename);
    snprintf(f->info_path, sizeof(f->info_path), "%s/" HL_INFO_FORK_FMT, dir_path, filename);
    snprintf(f->incomplete_path, sizeof(f->incomplete_path), "%s/%s" HL_INCOMPLETE_SUFFIX,
             dir_path, filename);

    return 0;
}

void hl_file_total_size(const hl_file_t *f, uint8_t out[4])
{
    /* Maps to: Go File.TotalSize() */
    struct stat st;
    uint32_t total = 0;

    if (stat(f->data_path, &st) == 0) {
        total += (uint32_t)(st.st_size - f->data_offset);
    } else if (stat(f->incomplete_path, &st) == 0) {
        total += (uint32_t)(st.st_size - f->data_offset);
    }

    if (stat(f->rsrc_path, &st) == 0) {
        total += (uint32_t)st.st_size;
    }

    hl_write_u32(out, total);
}

static int move_if_exists(const char *old_path, const char *new_dir, const char *filename)
{
    struct stat st;
    if (stat(old_path, &st) != 0) return 0; /* Doesn't exist, OK */

    char new_path[2048];
    snprintf(new_path, sizeof(new_path), "%s/%s", new_dir, filename);
    return rename(old_path, new_path);
}

int hl_file_move(hl_file_t *f, const char *new_dir)
{
    /* Maps to: Go File.Move() */
    if (move_if_exists(f->data_path, new_dir, f->name) < 0) return -1;

    /* Move sidecar files (ignore not-found) */
    char rsrc_name[512], info_name[512], incomplete_name[512];
    snprintf(rsrc_name, sizeof(rsrc_name), HL_RSRC_FORK_FMT, f->name);
    snprintf(info_name, sizeof(info_name), HL_INFO_FORK_FMT, f->name);
    snprintf(incomplete_name, sizeof(incomplete_name), "%s" HL_INCOMPLETE_SUFFIX, f->name);

    move_if_exists(f->rsrc_path, new_dir, rsrc_name);
    move_if_exists(f->info_path, new_dir, info_name);
    move_if_exists(f->incomplete_path, new_dir, incomplete_name);

    /* Update paths */
    hl_file_init(f, new_dir, f->name);
    return 0;
}

int hl_file_delete(const hl_file_t *f)
{
    /* Maps to: Go File.Delete() */
    unlink(f->data_path);       /* OK if not found */
    unlink(f->rsrc_path);
    unlink(f->info_path);
    unlink(f->incomplete_path);
    return 0;
}
