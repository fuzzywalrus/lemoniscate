/*
 * stats.c - Server statistics counter
 *
 * Maps to: hotline/stats.go
 */

#include "hotline/stats.h"
#include <stdlib.h>
#include <string.h>

hl_stats_t *hl_stats_new(void)
{
    hl_stats_t *s = (hl_stats_t *)calloc(1, sizeof(hl_stats_t));
    if (!s) return NULL;

    pthread_rwlock_init(&s->rwlock, NULL);
    s->since = time(NULL);
    return s;
}

void hl_stats_free(hl_stats_t *s)
{
    if (!s) return;
    pthread_rwlock_destroy(&s->rwlock);
    free(s);
}

void hl_stats_increment(hl_stats_t *s, int key)
{
    if (key < 0 || key >= HL_STAT_COUNT) return;
    pthread_rwlock_wrlock(&s->rwlock);
    s->values[key]++;
    pthread_rwlock_unlock(&s->rwlock);
}

void hl_stats_decrement(hl_stats_t *s, int key)
{
    if (key < 0 || key >= HL_STAT_COUNT) return;
    pthread_rwlock_wrlock(&s->rwlock);
    s->values[key]--;
    pthread_rwlock_unlock(&s->rwlock);
}

void hl_stats_set(hl_stats_t *s, int key, int val)
{
    if (key < 0 || key >= HL_STAT_COUNT) return;
    pthread_rwlock_wrlock(&s->rwlock);
    s->values[key] = val;
    pthread_rwlock_unlock(&s->rwlock);
}

int hl_stats_get(hl_stats_t *s, int key)
{
    if (key < 0 || key >= HL_STAT_COUNT) return 0;
    pthread_rwlock_rdlock(&s->rwlock);
    int val = s->values[key];
    pthread_rwlock_unlock(&s->rwlock);
    return val;
}
