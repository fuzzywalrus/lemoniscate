/*
 * mnemosyne_sync.h - Mnemosyne indexing service sync protocol
 *
 * Implements chunked full sync, incremental sync, heartbeat, drift detection,
 * exponential backoff, and sync state persistence for the Mnemosyne API.
 */

#ifndef MOBIUS_MNEMOSYNE_SYNC_H
#define MOBIUS_MNEMOSYNE_SYNC_H

#include "hotline/config.h"
#include "hotline/logger.h"
#include "hotline/http_client.h"
#include "mobius/json_builder.h"
#include "mobius/threaded_news_yaml.h"

#include <stdint.h>
#include <stddef.h>
#include <time.h>

/* --- Content type flags --- */

#define MN_CONTENT_FILES   0x01
#define MN_CONTENT_NEWS    0x02

/* --- Sync state --- */

typedef enum {
    MN_STATE_ACTIVE,     /* Normal operation */
    MN_STATE_SUSPENDED   /* After 5 consecutive failures */
} mn_sync_state_t;

/* --- Sync cursor (for chunked sync) --- */

typedef enum {
    MN_SYNC_NONE,
    MN_SYNC_FILES,
    MN_SYNC_NEWS
} mn_sync_type_t;

/* Directory stack entry for recursive file walk */
typedef struct mn_dir_entry {
    char                   path[1024]; /* relative path from file root */
    struct mn_dir_entry   *next;
} mn_dir_entry_t;

typedef struct {
    mn_sync_type_t  type;
    char            sync_id[64];
    int             chunk_index;

    /* File sync position */
    mn_dir_entry_t *pending_dirs;  /* stack of directories to visit */

    /* News sync position */
    int             cat_index;     /* current category index */
    int             cats_sent;     /* whether category chunk has been sent */
} mn_sync_cursor_t;

/* --- Sync queue (chains content types) --- */

#define MN_SYNC_QUEUE_MAX 4

typedef struct {
    mn_sync_type_t types[MN_SYNC_QUEUE_MAX];
    int            count;
    int            current;  /* index into types[] */
} mn_sync_queue_t;

/* --- Incremental sync ring buffer --- */

#define MN_INCR_QUEUE_SIZE 64

typedef enum {
    MN_INCR_FILE_ADD,
    MN_INCR_FILE_REMOVE,
    MN_INCR_NEWS_ADD
} mn_incr_type_t;

typedef struct {
    mn_incr_type_t type;
    char           path[512];     /* file path or category path */
    char           name[256];     /* file name or article title */
    uint64_t       size;          /* file size (files only) */
    char           file_type[64]; /* MIME-ish type (files only) */
    char           comment[256];  /* file comment (files only) */
    /* News article fields */
    uint32_t       article_id;
    char           body[4096];
    char           poster[64];
    char           date_str[32];
} mn_incr_entry_t;

typedef struct {
    mn_incr_entry_t entries[MN_INCR_QUEUE_SIZE];
    int             head;
    int             tail;
    int             count;
} mn_incr_queue_t;

/* --- Main Mnemosyne sync manager --- */

struct hl_server; /* forward decl — avoids circular include with server.h */

typedef struct {
    /* Configuration (cached from hl_config_t) */
    char            url[HL_CONFIG_MNEMOSYNE_URL_MAX];
    char            api_key[HL_CONFIG_MNEMOSYNE_KEY_MAX];
    int             index_files;
    int             index_news;

    /* Parsed URL */
    hl_parsed_url_t parsed_url;
    char            cached_ip[64];    /* DNS-cached IP address */
    int             dns_resolved;     /* 1 if cached_ip is valid */

    /* Sync state */
    mn_sync_state_t state;
    mn_sync_cursor_t cursor;
    mn_sync_queue_t  queue;
    int             chunked_sync_active; /* 1 if chunk tick timer should fire */

    /* Incremental queue */
    mn_incr_queue_t incr_queue;

    /* Backoff / resilience */
    int             consecutive_failures;
    int             backoff_level;      /* 0-4, maps to 2s,4s,8s,16s,32s */
    time_t          next_retry_time;

    /* Cached content counts (for heartbeat and drift detection) */
    int             cached_file_count;
    int             cached_news_count;

    /* Startup delay */
    int             startup_delay_done; /* 1 after 30s delay */
    int             startup_ticks;      /* count of heartbeat ticks since start */

    /* Persistence */
    char            cursor_file_path[1024];

    /* References (not owned) */
    struct hl_server *server;
    hl_logger_t    *logger;
} mn_sync_t;

/* --- Lifecycle --- */

/* Initialize sync manager. Call after config is loaded. */
void mn_sync_init(mn_sync_t *sync, struct hl_server *srv);

/* Reconfigure after SIGHUP. Returns 1 if sync should be enabled. */
int mn_sync_reconfigure(mn_sync_t *sync, struct hl_server *srv);

/* Check if Mnemosyne sync is configured and enabled */
int mn_sync_enabled(const mn_sync_t *sync);

/* Cleanup */
void mn_sync_cleanup(mn_sync_t *sync);

/* --- Heartbeat --- */

/* Send heartbeat to Mnemosyne. Returns 0 on success. */
int mn_send_heartbeat(mn_sync_t *sync);

/* --- Full sync --- */

/* Start a full sync of all enabled content types */
void mn_start_full_sync(mn_sync_t *sync);

/* Start a targeted sync of a single content type */
void mn_start_targeted_sync(mn_sync_t *sync, mn_sync_type_t type);

/* Process one chunk tick (called every 2 seconds during active sync) */
void mn_do_sync_tick(mn_sync_t *sync);

/* --- Periodic check --- */

/* Compare cached counts vs actual, trigger resync on drift */
void mn_periodic_check(mn_sync_t *sync);

/* --- Incremental sync --- */

/* Queue an incremental file add */
void mn_queue_file_add(mn_sync_t *sync, const char *path, const char *name,
                       uint64_t size, const char *file_type, const char *comment);

/* Queue an incremental file remove */
void mn_queue_file_remove(mn_sync_t *sync, const char *path);

/* Queue an incremental news article add */
void mn_queue_news_add(mn_sync_t *sync, const char *cat_path,
                       uint32_t article_id, const char *title,
                       const char *body, const char *poster,
                       const char *date_str);

/* Drain queued incremental events (called on timer tick) */
void mn_drain_incremental_queue(mn_sync_t *sync);

/* --- Deregistration --- */

/* Send deregister POST on shutdown */
void mn_deregister(mn_sync_t *sync);

/* --- Persistence --- */

/* Save sync cursor to disk */
void mn_save_cursor(mn_sync_t *sync);

/* Load sync cursor from disk (returns 1 if valid cursor found) */
int mn_load_cursor(mn_sync_t *sync);

/* --- DNS --- */

/* Resolve and cache the Mnemosyne hostname */
int mn_resolve_dns(mn_sync_t *sync);

/* --- JSON builders (used internally, exposed for testing) --- */

/* Build heartbeat JSON payload */
void mn_build_heartbeat_json(json_buf_t *buf, const char *server_name,
                             const char *description,
                             const char *server_address,
                             int msgboard_posts, int news_categories,
                             int news_articles, int files,
                             long long total_file_size);

/* Build file chunk JSON payload */
void mn_build_file_chunk_json(json_buf_t *buf, const char *sync_id,
                              int chunk_index, int finalize);

/* Build news chunk JSON payload (categories or articles) */
void mn_build_news_chunk_json(json_buf_t *buf, const char *sync_id,
                              int chunk_index, int finalize);

/* Build incremental file JSON */
void mn_build_incr_file_json(json_buf_t *buf, const mn_incr_entry_t *entry);

/* Build incremental news JSON */
void mn_build_incr_news_json(json_buf_t *buf, const mn_incr_entry_t *entry);

#endif /* MOBIUS_MNEMOSYNE_SYNC_H */
