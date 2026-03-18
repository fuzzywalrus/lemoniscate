/*
 * threaded_news_yaml.c - YAML-based threaded news manager (stub)
 *
 * Maps to: internal/mobius/threaded_news.go
 *
 * Stub implementation — loads/saves will be implemented in Phase 6.
 */

#include "mobius/threaded_news_yaml.h"
#include <stdlib.h>
#include <string.h>

struct mobius_threaded_news {
    char file_path[1024];
    /* TODO: categories tree, articles, mutex */
};

mobius_threaded_news_t *mobius_threaded_news_new(const char *filepath)
{
    mobius_threaded_news_t *tn = (mobius_threaded_news_t *)calloc(
        1, sizeof(mobius_threaded_news_t));
    if (!tn) return NULL;

    strncpy(tn->file_path, filepath, sizeof(tn->file_path) - 1);
    /* TODO: load YAML and parse categories */

    return tn;
}

void mobius_threaded_news_free(mobius_threaded_news_t *tn)
{
    free(tn);
}
