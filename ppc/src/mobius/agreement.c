/*
 * agreement.c - Server agreement loader
 *
 * Maps to: internal/mobius/agreement.go
 */

#include "mobius/agreement.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) {
        fclose(f);
        *out_len = 0;
        return (char *)calloc(1, 1); /* empty string */
    }

    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t n = fread(buf, 1, (size_t)len, f);
    fclose(f);

    buf[n] = '\0';
    *out_len = n;
    return buf;
}

mobius_agreement_t *mobius_agreement_new(const char *path)
{
    mobius_agreement_t *a = (mobius_agreement_t *)calloc(1, sizeof(mobius_agreement_t));
    if (!a) return NULL;

    strncpy(a->file_path, path, sizeof(a->file_path) - 1);
    pthread_rwlock_init(&a->rwlock, NULL);

    a->data = read_file(path, &a->data_len);
    if (!a->data) {
        /* File not found is OK — empty agreement */
        a->data = (char *)calloc(1, 1);
        a->data_len = 0;
    }

    return a;
}

int mobius_agreement_reload(mobius_agreement_t *a)
{
    size_t new_len;
    char *new_data = read_file(a->file_path, &new_len);
    if (!new_data) return -1;

    pthread_rwlock_wrlock(&a->rwlock);
    free(a->data);
    a->data = new_data;
    a->data_len = new_len;
    pthread_rwlock_unlock(&a->rwlock);

    return 0;
}

const char *mobius_agreement_data(mobius_agreement_t *a, size_t *out_len)
{
    pthread_rwlock_rdlock(&a->rwlock);
    const char *d = a->data;
    if (out_len) *out_len = a->data_len;
    pthread_rwlock_unlock(&a->rwlock);
    return d;
}

void mobius_agreement_free(mobius_agreement_t *a)
{
    if (!a) return;
    if (a->data) free(a->data);
    pthread_rwlock_destroy(&a->rwlock);
    free(a);
}
