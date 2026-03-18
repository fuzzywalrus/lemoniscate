/*
 * threaded_news_yaml.h - YAML-based threaded news manager (stub)
 *
 * Maps to: internal/mobius/threaded_news.go
 *
 * Full implementation deferred to Phase 6 when news handlers are ported.
 * This stub provides the type definitions and constructor.
 */

#ifndef MOBIUS_THREADED_NEWS_YAML_H
#define MOBIUS_THREADED_NEWS_YAML_H

#include <stddef.h>

/* Opaque type — full implementation in Phase 6 */
typedef struct mobius_threaded_news mobius_threaded_news_t;

mobius_threaded_news_t *mobius_threaded_news_new(const char *filepath);
void mobius_threaded_news_free(mobius_threaded_news_t *tn);

#endif /* MOBIUS_THREADED_NEWS_YAML_H */
