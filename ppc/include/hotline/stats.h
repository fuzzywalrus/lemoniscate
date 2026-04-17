/*
 * stats.h - Server statistics counter
 *
 * Maps to: hotline/stats.go
 */

#ifndef HOTLINE_STATS_H
#define HOTLINE_STATS_H

#include <pthread.h>
#include <time.h>

/* Stat key constants — maps to Go const block in stats.go */
#define HL_STAT_CURRENTLY_CONNECTED  0
#define HL_STAT_DOWNLOADS_IN_PROGRESS 1
#define HL_STAT_UPLOADS_IN_PROGRESS  2
#define HL_STAT_WAITING_DOWNLOADS    3
#define HL_STAT_CONNECTION_PEAK      4
#define HL_STAT_CONNECTION_COUNTER   5
#define HL_STAT_DOWNLOAD_COUNTER     6
#define HL_STAT_UPLOAD_COUNTER       7
#define HL_STAT_COUNT                8  /* Total number of stat keys */

typedef struct {
    int             values[HL_STAT_COUNT]; /* Go: stats map[int]int */
    time_t          since;                 /* Go: since time.Time   */
    pthread_rwlock_t rwlock;               /* Go: mu sync.RWMutex   */
} hl_stats_t;

/* Create and initialize a stats struct. Maps to: Go NewStats() */
hl_stats_t *hl_stats_new(void);

/* Free a stats struct */
void hl_stats_free(hl_stats_t *s);

/* Increment one or more stat keys. Maps to: Go Stats.Increment() */
void hl_stats_increment(hl_stats_t *s, int key);

/* Decrement a stat key. Maps to: Go Stats.Decrement() */
void hl_stats_decrement(hl_stats_t *s, int key);

/* Set a stat key to a value. Maps to: Go Stats.Set() */
void hl_stats_set(hl_stats_t *s, int key, int val);

/* Get a stat value. Maps to: Go Stats.Get() */
int hl_stats_get(hl_stats_t *s, int key);

#endif /* HOTLINE_STATS_H */
