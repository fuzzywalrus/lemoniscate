/*
 * flat_news.h - Flat message board (MessageBoard.txt)
 *
 * Maps to: internal/mobius/news.go (FlatNews)
 *
 * Stores news posts as a flat file. New posts are prepended.
 */

#ifndef MOBIUS_FLAT_NEWS_H
#define MOBIUS_FLAT_NEWS_H

#include <stddef.h>
#include <pthread.h>

typedef struct {
    char           *data;       /* Go: data []byte */
    size_t          data_len;
    char            file_path[1024];
    pthread_mutex_t mu;
} mobius_flat_news_t;

/*
 * mobius_flat_news_new - Load message board from file.
 * Maps to: Go NewFlatNews()
 */
mobius_flat_news_t *mobius_flat_news_new(const char *path);

/* Reload from disk */
int mobius_flat_news_reload(mobius_flat_news_t *fn);

/* Get current data (caller must NOT free). Thread-safe read. */
const char *mobius_flat_news_data(mobius_flat_news_t *fn, size_t *out_len);

/* Prepend new data (new post). Maps to: Go FlatNews.Write() */
int mobius_flat_news_prepend(mobius_flat_news_t *fn, const char *data, size_t len);

void mobius_flat_news_free(mobius_flat_news_t *fn);

#endif /* MOBIUS_FLAT_NEWS_H */
