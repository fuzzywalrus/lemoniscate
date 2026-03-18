/*
 * file_store.c - OS filesystem implementation
 *
 * Maps to: hotline/file_store.go (OSFileStore)
 */

#include "hotline/file_store.h"
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>

typedef struct {
    hl_file_store_t base;
} os_file_store_t;

static int os_mkdir(hl_file_store_t *self, const char *path, mode_t mode)
{
    (void)self;
    return mkdir(path, mode);
}

static int os_stat(hl_file_store_t *self, const char *path, struct stat *buf)
{
    (void)self;
    return stat(path, buf);
}

static int os_remove(hl_file_store_t *self, const char *path)
{
    (void)self;
    return unlink(path);
}

static int os_remove_all(hl_file_store_t *self, const char *path)
{
    (void)self;
    /* Simple recursive remove via system() — adequate for Tiger */
    char cmd[2048];
    snprintf(cmd, sizeof(cmd), "rm -rf '%s'", path);
    return system(cmd);
}

static int os_rename(hl_file_store_t *self, const char *old_path, const char *new_path)
{
    (void)self;
    return rename(old_path, new_path);
}

static int os_symlink(hl_file_store_t *self, const char *old_name, const char *new_name)
{
    (void)self;
    return symlink(old_name, new_name);
}

static int os_open(hl_file_store_t *self, const char *path, int flags, mode_t mode)
{
    (void)self;
    return open(path, flags, mode);
}

static const hl_file_store_vtable_t os_vtable = {
    .mkdir      = os_mkdir,
    .stat       = os_stat,
    .remove     = os_remove,
    .remove_all = os_remove_all,
    .rename     = os_rename,
    .symlink    = os_symlink,
    .open       = os_open
};

hl_file_store_t *hl_os_file_store_new(void)
{
    os_file_store_t *fs = (os_file_store_t *)calloc(1, sizeof(os_file_store_t));
    if (!fs) return NULL;
    fs->base.vt = &os_vtable;
    return &fs->base;
}
