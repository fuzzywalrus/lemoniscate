/*
 * flat_news.c - Flat message board (MessageBoard.txt)
 *
 * Maps to: internal/mobius/news.go (FlatNews)
 */

#include "mobius/flat_news.h"
#include "mobius/jsonl_message_board.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Check if this is a JSONL backend */
static int is_jsonl(mobius_flat_news_t *fn)
{
    size_t len = strlen(fn->file_path);
    return (len > 6 && strcmp(fn->file_path + len - 6, ".jsonl") == 0);
}

/* Forward declaration */
int mobius_jsonl_prepend(mobius_flat_news_t *fn, const char *data, size_t len);

static char *read_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        *out_len = 0;
        return (char *)calloc(1, 1);
    }

    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (len <= 0) {
        fclose(f);
        *out_len = 0;
        return (char *)calloc(1, 1);
    }

    char *buf = (char *)malloc((size_t)len + 1);
    if (!buf) { fclose(f); return NULL; }

    size_t n = fread(buf, 1, (size_t)len, f);
    fclose(f);

    buf[n] = '\0';
    *out_len = n;
    return buf;
}

mobius_flat_news_t *mobius_flat_news_new(const char *path)
{
    mobius_flat_news_t *fn = (mobius_flat_news_t *)calloc(1, sizeof(mobius_flat_news_t));
    if (!fn) return NULL;

    strncpy(fn->file_path, path, sizeof(fn->file_path) - 1);
    pthread_mutex_init(&fn->mu, NULL);

    fn->data = read_file(path, &fn->data_len);
    return fn;
}

int mobius_flat_news_reload(mobius_flat_news_t *fn)
{
    size_t new_len;
    char *new_data = read_file(fn->file_path, &new_len);
    if (!new_data) return -1;

    pthread_mutex_lock(&fn->mu);
    free(fn->data);
    fn->data = new_data;
    fn->data_len = new_len;
    pthread_mutex_unlock(&fn->mu);

    return 0;
}

const char *mobius_flat_news_data(mobius_flat_news_t *fn, size_t *out_len)
{
    pthread_mutex_lock(&fn->mu);
    const char *d = fn->data;
    if (out_len) *out_len = fn->data_len;
    pthread_mutex_unlock(&fn->mu);
    return d;
}

int mobius_flat_news_prepend(mobius_flat_news_t *fn, const char *data, size_t len)
{
    /* Route to JSONL handler if applicable */
    if (is_jsonl(fn))
        return mobius_jsonl_prepend(fn, data, len);

    /* Maps to: Go FlatNews.Write() — prepends new data */
    pthread_mutex_lock(&fn->mu);

    size_t new_total = len + fn->data_len;
    char *new_data = (char *)malloc(new_total + 1);
    if (!new_data) {
        pthread_mutex_unlock(&fn->mu);
        return -1;
    }

    memcpy(new_data, data, len);
    memcpy(new_data + len, fn->data, fn->data_len);
    new_data[new_total] = '\0';

    free(fn->data);
    fn->data = new_data;
    fn->data_len = new_total;

    /* Write to disk */
    FILE *f = fopen(fn->file_path, "wb");
    if (f) {
        fwrite(fn->data, 1, fn->data_len, f);
        fclose(f);
    }

    pthread_mutex_unlock(&fn->mu);
    return 0;
}

void mobius_flat_news_free(mobius_flat_news_t *fn)
{
    if (!fn) return;
    if (fn->data) free(fn->data);
    pthread_mutex_destroy(&fn->mu);
    free(fn);
}
