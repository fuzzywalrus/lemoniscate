/*
 * agreement.h - Server agreement loader
 *
 * Maps to: internal/mobius/agreement.go
 *
 * Loads Agreement.txt from the config directory.
 * Plain text file, no YAML.
 */

#ifndef MOBIUS_AGREEMENT_H
#define MOBIUS_AGREEMENT_H

#include <stddef.h>
#include <pthread.h>

typedef struct {
    char           *data;       /* Go: data []byte */
    size_t          data_len;
    char            file_path[1024]; /* Go: filePath string */
    pthread_rwlock_t rwlock;    /* Go: mu sync.RWMutex */
} mobius_agreement_t;

/*
 * mobius_agreement_new - Load agreement from file.
 * Maps to: Go NewAgreement()
 * Returns NULL on failure.
 */
mobius_agreement_t *mobius_agreement_new(const char *path);

/* Reload agreement from disk. Maps to: Go Agreement.Reload() */
int mobius_agreement_reload(mobius_agreement_t *a);

/* Get agreement data (read-locked). Caller must NOT free the returned pointer. */
const char *mobius_agreement_data(mobius_agreement_t *a, size_t *out_len);

void mobius_agreement_free(mobius_agreement_t *a);

#endif /* MOBIUS_AGREEMENT_H */
