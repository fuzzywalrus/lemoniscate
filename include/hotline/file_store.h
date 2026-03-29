/*
 * file_store.h - File system abstraction (vtable)
 *
 * Allows swapping real filesystem for testing mocks.
 */

#ifndef HOTLINE_FILE_STORE_H
#define HOTLINE_FILE_STORE_H

#include <sys/types.h>
#include <sys/stat.h>

typedef struct hl_file_store hl_file_store_t;

/* FileStore vtable */
typedef struct {
    int  (*mkdir)(hl_file_store_t *self, const char *path, mode_t mode);
    int  (*stat)(hl_file_store_t *self, const char *path, struct stat *buf);
    int  (*remove)(hl_file_store_t *self, const char *path);
    int  (*remove_all)(hl_file_store_t *self, const char *path);
    int  (*rename)(hl_file_store_t *self, const char *old_path, const char *new_path);
    int  (*symlink)(hl_file_store_t *self, const char *old_name, const char *new_name);
    /* Returns fd on success, -1 on error */
    int  (*open)(hl_file_store_t *self, const char *path, int flags, mode_t mode);
} hl_file_store_vtable_t;

struct hl_file_store {
    const hl_file_store_vtable_t *vt;
};

/*
 * hl_os_file_store_new - Create an OSFileStore (real filesystem).
 */
hl_file_store_t *hl_os_file_store_new(void);

#endif /* HOTLINE_FILE_STORE_H */
