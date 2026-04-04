/*
 * mnemosyne_sync.c - Mnemosyne indexing service sync protocol
 *
 * Implements the full sync lifecycle: heartbeat, chunked full sync,
 * incremental sync, drift detection, backoff/suspension, persistence.
 */

#include "mobius/mnemosyne_sync.h"
#include "mobius/jsonl_message_board.h"
#include "hotline/server.h"
#include "hotline/http_client.h"
#include "mobius/json_builder.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <dirent.h>
#include <sys/stat.h>
#include <netdb.h>
#include <arpa/inet.h>

/* --- Constants --- */

#define MN_CONNECT_TIMEOUT_MS  2000
#define MN_READ_TIMEOUT_MS     5000
#define MN_MAX_BACKOFF_LEVEL   4
#define MN_MAX_CONSECUTIVE_FAILURES 5
#define MN_STALE_CURSOR_SECONDS 3600  /* 1 hour */

static const int backoff_delays[] = { 2, 4, 8, 16, 32 };

/* --- Forward declarations --- */

static int mn_post(mn_sync_t *sync, const char *endpoint,
                   const char *body, size_t body_len);
static void mn_handle_post_result(mn_sync_t *sync, int status);
static void free_pending_dirs(mn_dir_entry_t *head);
static int count_files_recursive(const char *root);

/* --- sync_id generation --- */

static void generate_sync_id(char *out, size_t out_size)
{
    time_t now = time(NULL);
    unsigned int r = (unsigned int)rand();
    snprintf(out, out_size, "%ld_%u", (long)now, r);
}

/* --- Lifecycle --- */

void mn_sync_init(mn_sync_t *sync, hl_server_t *srv)
{
    memset(sync, 0, sizeof(*sync));
    sync->server = srv;
    sync->logger = srv->logger;
    sync->state = MN_STATE_ACTIVE;

    /* Copy config */
    strncpy(sync->url, srv->config.mnemosyne_url, sizeof(sync->url) - 1);
    strncpy(sync->api_key, srv->config.mnemosyne_api_key, sizeof(sync->api_key) - 1);
    sync->index_files = srv->config.mnemosyne_index_files;
    sync->index_news = srv->config.mnemosyne_index_news;

    if (!mn_sync_enabled(sync))
        return;

    /* Parse URL */
    if (hl_http_parse_url(sync->url, &sync->parsed_url) != 0) {
        hl_log_error(sync->logger, "Mnemosyne: invalid URL '%s' — sync disabled",
                     sync->url);
        sync->url[0] = '\0';
        return;
    }

    /* Build cursor file path */
    snprintf(sync->cursor_file_path, sizeof(sync->cursor_file_path),
             "%s/.mnemosyne_cursor", srv->config.file_root);

    /* Resolve DNS */
    if (mn_resolve_dns(sync) != 0) {
        hl_log_error(sync->logger,
                     "Mnemosyne: DNS resolution failed for '%s' — sync disabled",
                     sync->parsed_url.host);
        sync->url[0] = '\0';
        return;
    }

    hl_log_info(sync->logger, "Mnemosyne sync enabled: %s", sync->url);
}

int mn_sync_reconfigure(mn_sync_t *sync, hl_server_t *srv)
{
    /* Save old URL to detect changes */
    char old_url[HL_CONFIG_MNEMOSYNE_URL_MAX];
    strncpy(old_url, sync->url, sizeof(old_url) - 1);
    old_url[sizeof(old_url) - 1] = '\0';

    /* Cleanup old state */
    free_pending_dirs(sync->cursor.pending_dirs);
    sync->cursor.pending_dirs = NULL;

    /* Re-init with new config */
    mn_sync_state_t old_state = sync->state;
    int old_chunked = sync->chunked_sync_active;
    (void)old_state;
    (void)old_chunked;

    /* Copy new config */
    strncpy(sync->url, srv->config.mnemosyne_url, sizeof(sync->url) - 1);
    strncpy(sync->api_key, srv->config.mnemosyne_api_key, sizeof(sync->api_key) - 1);
    sync->index_files = srv->config.mnemosyne_index_files;
    sync->index_news = srv->config.mnemosyne_index_news;

    if (!mn_sync_enabled(sync)) {
        sync->chunked_sync_active = 0;
        return 0;
    }

    /* Re-parse URL */
    if (hl_http_parse_url(sync->url, &sync->parsed_url) != 0) {
        sync->url[0] = '\0';
        return 0;
    }

    /* Re-resolve DNS */
    mn_resolve_dns(sync);

    /* Reset state */
    sync->state = MN_STATE_ACTIVE;
    sync->consecutive_failures = 0;
    sync->backoff_level = 0;
    sync->chunked_sync_active = 0;
    memset(&sync->cursor, 0, sizeof(sync->cursor));
    memset(&sync->queue, 0, sizeof(sync->queue));

    return 1;
}

int mn_sync_enabled(const mn_sync_t *sync)
{
    return sync->url[0] != '\0' && sync->api_key[0] != '\0';
}

void mn_sync_cleanup(mn_sync_t *sync)
{
    free_pending_dirs(sync->cursor.pending_dirs);
    sync->cursor.pending_dirs = NULL;
}

/* --- DNS resolution --- */

int mn_resolve_dns(mn_sync_t *sync)
{
    struct addrinfo hints, *res;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(sync->parsed_url.host, NULL, &hints, &res) != 0) {
        sync->dns_resolved = 0;
        return -1;
    }

    struct sockaddr_in *sin = (struct sockaddr_in *)res->ai_addr;
    inet_ntop(AF_INET, &sin->sin_addr, sync->cached_ip, sizeof(sync->cached_ip));
    freeaddrinfo(res);
    sync->dns_resolved = 1;
    return 0;
}

/* --- HTTP POST helper --- */

static int mn_post(mn_sync_t *sync, const char *endpoint,
                   const char *body, size_t body_len)
{
    if (!sync->dns_resolved)
        return -1;

    /* Build full path with API key */
    char full_path[1024];
    char base_path[512];
    snprintf(base_path, sizeof(base_path), "%s%s",
             sync->parsed_url.path[0] == '/' &&
             strlen(sync->parsed_url.path) == 1 ? "" : sync->parsed_url.path,
             endpoint);

    /* Ensure path starts with / */
    if (base_path[0] != '/') {
        memmove(base_path + 1, base_path, strlen(base_path) + 1);
        base_path[0] = '/';
    }

    hl_parsed_url_t temp_url;
    memcpy(&temp_url, &sync->parsed_url, sizeof(temp_url));
    strncpy(temp_url.path, base_path, sizeof(temp_url.path) - 1);

    if (hl_http_url_with_api_key(&temp_url, sync->api_key,
                                  full_path, sizeof(full_path)) != 0)
        return -1;

    return hl_http_post_to_ip(sync->cached_ip, sync->parsed_url.host,
                               sync->parsed_url.port, full_path,
                               body, body_len, "application/json",
                               MN_CONNECT_TIMEOUT_MS, MN_READ_TIMEOUT_MS);
}

/* --- Backoff / failure handling --- */

static void mn_handle_post_result(mn_sync_t *sync, int status)
{
    if (status >= 200 && status < 300) {
        /* Success — reset backoff */
        if (sync->consecutive_failures > 0) {
            hl_log_info(sync->logger, "Mnemosyne: request succeeded after %d failures",
                        sync->consecutive_failures);
        }
        sync->consecutive_failures = 0;
        sync->backoff_level = 0;
        sync->next_retry_time = 0;
        return;
    }

    /* Failure */
    sync->consecutive_failures++;

    if (sync->consecutive_failures == 1) {
        hl_log_error(sync->logger, "Mnemosyne: request failed (status %d)", status);
    }

    /* Escalate backoff level */
    int new_level = sync->consecutive_failures - 1;
    if (new_level > MN_MAX_BACKOFF_LEVEL)
        new_level = MN_MAX_BACKOFF_LEVEL;

    if (new_level > sync->backoff_level) {
        sync->backoff_level = new_level;
        hl_log_error(sync->logger, "Mnemosyne: backoff level %d (delay %ds)",
                     sync->backoff_level, backoff_delays[sync->backoff_level]);
    }

    sync->next_retry_time = time(NULL) + backoff_delays[sync->backoff_level];

    /* Suspend after max failures */
    if (sync->consecutive_failures >= MN_MAX_CONSECUTIVE_FAILURES &&
        sync->state == MN_STATE_ACTIVE) {
        sync->state = MN_STATE_SUSPENDED;
        sync->chunked_sync_active = 0;

        /* Clear incremental queue */
        sync->incr_queue.head = 0;
        sync->incr_queue.tail = 0;
        sync->incr_queue.count = 0;

        /* Free pending dirs */
        free_pending_dirs(sync->cursor.pending_dirs);
        sync->cursor.pending_dirs = NULL;
        memset(&sync->cursor, 0, sizeof(sync->cursor));
        memset(&sync->queue, 0, sizeof(sync->queue));

        hl_log_error(sync->logger,
                     "Mnemosyne: sync suspended after %d failures — "
                     "will resume when heartbeat succeeds",
                     sync->consecutive_failures);
    }
}

/* --- JSON builders --- */

void mn_build_heartbeat_json(json_buf_t *buf, const char *server_name,
                             const char *description,
                             const char *server_address,
                             int msgboard_posts, int news_categories,
                             int news_articles, int files,
                             long long total_file_size)
{
    json_buf_append_str(buf, "{");
    json_buf_add_string(buf, "server_name", server_name);
    json_buf_append_str(buf, ", ");
    json_buf_add_string(buf, "server_description", description);
    json_buf_append_str(buf, ", ");
    json_buf_add_string(buf, "server_address", server_address);
    json_buf_append_str(buf, ", \"counts\": {");
    json_buf_add_int(buf, "msgboard_posts", msgboard_posts);
    json_buf_append_str(buf, ", ");
    json_buf_add_int(buf, "news_categories", news_categories);
    json_buf_append_str(buf, ", ");
    json_buf_add_int(buf, "news_articles", news_articles);
    json_buf_append_str(buf, ", ");
    json_buf_add_int(buf, "files", files);
    json_buf_append_str(buf, ", ");
    json_buf_printf(buf, "\"total_file_size\": %lld", total_file_size);
    json_buf_append_str(buf, "}}");
}

/* File chunk JSON with files array (populated by caller appending entries) */
void mn_build_file_chunk_json(json_buf_t *buf, const char *sync_id,
                              int chunk_index, int finalize)
{
    json_buf_append_str(buf, "{");
    json_buf_add_string(buf, "sync_id", sync_id);
    json_buf_append_str(buf, ", ");
    json_buf_add_int(buf, "chunk_index", chunk_index);
    json_buf_append_str(buf, ", ");
    json_buf_add_bool(buf, "finalize", finalize);
    json_buf_append_str(buf, ", \"files\": [");
    /* Caller appends file entries and closes with "]}" */
}

void mn_build_news_chunk_json(json_buf_t *buf, const char *sync_id,
                              int chunk_index, int finalize)
{
    json_buf_append_str(buf, "{");
    json_buf_add_string(buf, "sync_id", sync_id);
    json_buf_append_str(buf, ", ");
    json_buf_add_int(buf, "chunk_index", chunk_index);
    json_buf_append_str(buf, ", ");
    json_buf_add_bool(buf, "finalize", finalize);
    /* Caller adds categories/articles arrays and closes with "}" */
}

void mn_build_incr_file_json(json_buf_t *buf, const mn_incr_entry_t *entry)
{
    json_buf_append_str(buf, "{\"mode\": \"incremental\", ");
    if (entry->type == MN_INCR_FILE_ADD) {
        json_buf_append_str(buf, "\"added\": [{");
        json_buf_add_string(buf, "path", entry->path);
        json_buf_append_str(buf, ", ");
        json_buf_add_string(buf, "name", entry->name);
        json_buf_append_str(buf, ", ");
        json_buf_printf(buf, "\"size\": %llu", (unsigned long long)entry->size);
        json_buf_append_str(buf, ", ");
        json_buf_add_string(buf, "type", entry->file_type);
        json_buf_append_str(buf, "}], \"removed\": []}");
    } else {
        json_buf_append_str(buf, "\"added\": [], \"removed\": [\"");
        json_buf_append_escaped(buf, entry->path);
        json_buf_append_str(buf, "\"]}");
    }
}

void mn_build_incr_news_json(json_buf_t *buf, const mn_incr_entry_t *entry)
{
    json_buf_append_str(buf, "{\"mode\": \"incremental\", ");
    json_buf_append_str(buf, "\"added_categories\": [], \"removed_categories\": [], ");
    json_buf_append_str(buf, "\"added_articles\": [{");
    json_buf_printf(buf, "\"id\": %u, ", entry->article_id);
    json_buf_add_string(buf, "path", entry->path);
    json_buf_append_str(buf, ", ");
    json_buf_add_string(buf, "title", entry->name);
    json_buf_append_str(buf, ", ");
    json_buf_add_string(buf, "body", entry->body);
    json_buf_append_str(buf, ", ");
    json_buf_add_string(buf, "poster", entry->poster);
    json_buf_append_str(buf, ", ");
    json_buf_add_string(buf, "date", entry->date_str);
    json_buf_append_str(buf, ", ");
    json_buf_printf(buf, "\"parent_id\": 0");
    json_buf_append_str(buf, "}], \"removed_articles\": []}");
}

/* --- Heartbeat --- */

int mn_send_heartbeat(mn_sync_t *sync)
{
    if (!mn_sync_enabled(sync))
        return -1;

    hl_server_t *srv = sync->server;

    /* Count files recursively */
    int file_count = 0;
    long long total_file_size = 0;
    if (sync->index_files && srv->config.file_root[0]) {
        file_count = count_files_recursive(srv->config.file_root);
        /* TODO: accumulate total_file_size in count_files_recursive */
    }

    /* Count news */
    int news_categories = 0;
    int news_articles = 0;
    if (sync->index_news && srv->threaded_news) {
        pthread_mutex_lock(&srv->threaded_news->mu);
        news_categories = srv->threaded_news->category_count;
        int i;
        for (i = 0; i < srv->threaded_news->category_count; i++) {
            news_articles += srv->threaded_news->categories[i].article_count;
        }
        pthread_mutex_unlock(&srv->threaded_news->mu);
    }

    /* Update cached counts */
    sync->cached_file_count = file_count;
    sync->cached_news_count = news_articles;

    /* Count msgboard posts */
    int msgboard_posts = 0;
    if (srv->config.mnemosyne_index_msgboard && srv->flat_news) {
        msgboard_posts = mobius_jsonl_post_count(srv->flat_news);
    }

    /* Build server address string */
    char server_address[128];
    snprintf(server_address, sizeof(server_address), "%s:%d",
             sync->parsed_url.host, srv->port);

    /* Build JSON */
    json_buf_t buf;
    json_buf_init(&buf);
    mn_build_heartbeat_json(&buf, srv->config.name, srv->config.description,
                            server_address, msgboard_posts, news_categories,
                            news_articles, file_count, total_file_size);

    int status = mn_post(sync, "/api/v1/sync/heartbeat", buf.data, buf.len);
    json_buf_free(&buf);

    /* If heartbeat succeeds while suspended, recover */
    if (status >= 200 && status < 300 && sync->state == MN_STATE_SUSPENDED) {
        hl_log_info(sync->logger,
                    "Mnemosyne reachable again, resuming sync");
        sync->state = MN_STATE_ACTIVE;
        sync->consecutive_failures = 0;
        sync->backoff_level = 0;
        sync->next_retry_time = 0;

        /* Re-resolve DNS on recovery */
        mn_resolve_dns(sync);

        /* Start fresh full sync */
        mn_start_full_sync(sync);
        return 0;
    }

    if (sync->state != MN_STATE_SUSPENDED) {
        mn_handle_post_result(sync, status);
    }
    /* Don't log heartbeat failures during suspension */

    return (status >= 200 && status < 300) ? 0 : -1;
}

/* --- Directory walking helpers --- */

static void free_pending_dirs(mn_dir_entry_t *head)
{
    while (head) {
        mn_dir_entry_t *next = head->next;
        free(head);
        head = next;
    }
}

static mn_dir_entry_t *push_dir(mn_dir_entry_t *stack, const char *path)
{
    mn_dir_entry_t *entry = (mn_dir_entry_t *)calloc(1, sizeof(mn_dir_entry_t));
    if (!entry) return stack;
    strncpy(entry->path, path, sizeof(entry->path) - 1);
    entry->next = stack;
    return entry;
}

static mn_dir_entry_t *pop_dir(mn_dir_entry_t *stack, char *out_path, size_t out_size)
{
    if (!stack) return NULL;
    strncpy(out_path, stack->path, out_size - 1);
    out_path[out_size - 1] = '\0';
    mn_dir_entry_t *next = stack->next;
    free(stack);
    return next;
}

/* Count files recursively for drift detection */
static int count_files_recursive(const char *root)
{
    int count = 0;
    DIR *d = opendir(root);
    if (!d) return 0;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", root, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0) {
            if (S_ISDIR(st.st_mode)) {
                count += count_files_recursive(full_path);
            } else {
                count++;
            }
        }
    }
    closedir(d);
    return count;
}

/* --- Recursive file collector for full sync --- */

static void collect_files_recursive(json_buf_t *buf, const char *root,
                                    const char *rel_prefix, int *count,
                                    long long *total_size)
{
    DIR *d = opendir(root);
    if (!d) return;

    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        if (ent->d_name[0] == '.') continue;

        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", root, ent->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        char rel_path[1024];
        if (rel_prefix[0]) {
            snprintf(rel_path, sizeof(rel_path), "/%s/%s", rel_prefix, ent->d_name);
        } else {
            snprintf(rel_path, sizeof(rel_path), "/%s", ent->d_name);
        }

        if (*count > 0) json_buf_append_str(buf, ", ");

        /* Format modified time as ISO date */
        char modified[32] = "";
        struct tm *tm = gmtime(&st.st_mtime);
        if (tm) {
            snprintf(modified, sizeof(modified), "%04d-%02d-%02dT%02d:%02d:%02dZ",
                     tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                     tm->tm_hour, tm->tm_min, tm->tm_sec);
        }

        json_buf_append_str(buf, "{");
        json_buf_add_string(buf, "path", rel_path);
        json_buf_append_str(buf, ", ");
        json_buf_add_string(buf, "name", ent->d_name);
        json_buf_append_str(buf, ", ");
        json_buf_printf(buf, "\"size\": %lld", (long long)st.st_size);
        json_buf_append_str(buf, ", ");
        json_buf_add_string(buf, "type", S_ISDIR(st.st_mode) ? "fold" : "");
        json_buf_append_str(buf, ", ");
        json_buf_add_string(buf, "creator", "");
        json_buf_append_str(buf, ", ");
        json_buf_add_string(buf, "comment", "");
        json_buf_append_str(buf, ", ");
        json_buf_add_string(buf, "modified", modified);
        json_buf_append_str(buf, ", ");
        json_buf_add_bool(buf, "is_dir", S_ISDIR(st.st_mode));
        json_buf_append_str(buf, "}");
        (*count)++;
        if (!S_ISDIR(st.st_mode))
            *total_size += (long long)st.st_size;

        if (S_ISDIR(st.st_mode)) {
            char sub_rel[1024];
            if (rel_prefix[0]) {
                snprintf(sub_rel, sizeof(sub_rel), "%s/%s", rel_prefix, ent->d_name);
            } else {
                snprintf(sub_rel, sizeof(sub_rel), "%s", ent->d_name);
            }
            collect_files_recursive(buf, full_path, sub_rel, count, total_size);
        }
    }
    closedir(d);
}

/* --- Full sync (single POST per content type) --- */

static void do_full_file_sync(mn_sync_t *sync)
{
    hl_server_t *srv = sync->server;

    hl_log_info(sync->logger, "Mnemosyne: starting file sync (full mode)");

    json_buf_t buf;
    json_buf_init(&buf);
    json_buf_append_str(&buf, "{\"mode\": \"full\", \"files\": [");

    int count = 0;
    long long total_size = 0;
    if (srv->config.file_root[0]) {
        collect_files_recursive(&buf, srv->config.file_root, "", &count, &total_size);
    }

    json_buf_append_str(&buf, "]}");

    int status = mn_post(sync, "/api/v1/sync/files", buf.data, buf.len);
    json_buf_free(&buf);

    mn_handle_post_result(sync, status);
    if (status >= 200 && status < 300) {
        hl_log_info(sync->logger, "Mnemosyne: file sync complete (%d entries)", count);
        sync->cached_file_count = count;
    }
}

static void do_full_news_sync(mn_sync_t *sync)
{
    hl_server_t *srv = sync->server;

    if (!srv->threaded_news) return;

    hl_log_info(sync->logger, "Mnemosyne: starting news sync (full mode)");

    pthread_mutex_lock(&srv->threaded_news->mu);

    json_buf_t buf;
    json_buf_init(&buf);
    json_buf_append_str(&buf, "{\"mode\": \"full\", \"categories\": [");

    /* Categories */
    int i;
    for (i = 0; i < srv->threaded_news->category_count; i++) {
        if (i > 0) json_buf_append_str(&buf, ", ");
        char cat_path[256];
        snprintf(cat_path, sizeof(cat_path), "/%s",
                 srv->threaded_news->categories[i].name);
        json_buf_append_str(&buf, "{");
        json_buf_add_string(&buf, "path", cat_path);
        json_buf_append_str(&buf, ", ");
        json_buf_add_string(&buf, "name", srv->threaded_news->categories[i].name);
        json_buf_append_str(&buf, "}");
    }

    json_buf_append_str(&buf, "], \"articles\": [");

    /* Articles across all categories */
    int first = 1;
    int article_count = 0;
    for (i = 0; i < srv->threaded_news->category_count; i++) {
        tn_category_t *cat = &srv->threaded_news->categories[i];
        char cat_path[256];
        snprintf(cat_path, sizeof(cat_path), "/%s", cat->name);

        int j;
        for (j = 0; j < cat->article_count; j++) {
            tn_article_t *art = &cat->articles[j];
            if (!art->active) continue;

            if (!first) json_buf_append_str(&buf, ", ");
            first = 0;

            /* Convert Hotline date to ISO string */
            char date_str[32] = "1970-01-01T00:00:00Z";
            uint32_t secs = ((uint32_t)art->date[4] << 24) |
                            ((uint32_t)art->date[5] << 16) |
                            ((uint32_t)art->date[6] << 8) |
                             (uint32_t)art->date[7];
            if (secs > 0) {
                time_t unix_secs = (time_t)secs - 2082844800LL;
                struct tm *tm = gmtime(&unix_secs);
                if (tm) {
                    snprintf(date_str, sizeof(date_str),
                             "%04d-%02d-%02dT%02d:%02d:%02dZ",
                             tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                             tm->tm_hour, tm->tm_min, tm->tm_sec);
                }
            }

            json_buf_append_str(&buf, "{");
            json_buf_printf(&buf, "\"id\": %u, ", art->id);
            json_buf_add_string(&buf, "path", cat_path);
            json_buf_append_str(&buf, ", ");
            json_buf_add_string(&buf, "title", art->title);
            json_buf_append_str(&buf, ", ");
            json_buf_add_string(&buf, "poster", art->poster);
            json_buf_append_str(&buf, ", ");
            json_buf_add_string(&buf, "date", date_str);
            json_buf_append_str(&buf, ", ");
            json_buf_printf(&buf, "\"parent_id\": %u, ", art->parent_id);
            json_buf_add_string(&buf, "body", art->data);
            json_buf_append_str(&buf, "}");
            article_count++;
        }
    }

    json_buf_append_str(&buf, "]}");
    pthread_mutex_unlock(&srv->threaded_news->mu);

    int status = mn_post(sync, "/api/v1/sync/news", buf.data, buf.len);
    json_buf_free(&buf);

    mn_handle_post_result(sync, status);
    if (status >= 200 && status < 300) {
        hl_log_info(sync->logger, "Mnemosyne: news sync complete (%d articles)", article_count);
        sync->cached_news_count = article_count;
    }
}

static void do_full_msgboard_sync(mn_sync_t *sync)
{
    hl_server_t *srv = sync->server;
    if (!srv->flat_news) return;

    /* Only sync if JSONL backend (has structured posts) */
    mb_post_t *posts = NULL;
    int count = 0;
    if (mobius_jsonl_get_posts(srv->flat_news, &posts, &count) != 0 || count == 0) {
        mobius_jsonl_free_posts(posts, count);
        return;
    }

    hl_log_info(sync->logger, "Mnemosyne: starting msgboard sync (full mode)");

    json_buf_t buf;
    json_buf_init(&buf);
    json_buf_append_str(&buf, "{\"mode\": \"full\", \"posts\": [");

    int i;
    for (i = 0; i < count; i++) {
        if (i > 0) json_buf_append_str(&buf, ", ");
        json_buf_append_str(&buf, "{");
        json_buf_printf(&buf, "\"id\": %d, ", posts[i].id);
        json_buf_add_string(&buf, "nick", posts[i].nick);
        json_buf_append_str(&buf, ", ");
        json_buf_add_string(&buf, "login", posts[i].login);
        json_buf_append_str(&buf, ", ");
        json_buf_add_string(&buf, "body", posts[i].body ? posts[i].body : "");
        json_buf_append_str(&buf, ", ");
        json_buf_add_string(&buf, "timestamp", posts[i].ts);
        json_buf_append_str(&buf, "}");
    }

    json_buf_append_str(&buf, "]}");
    mobius_jsonl_free_posts(posts, count);

    int status = mn_post(sync, "/api/v1/sync/msgboard", buf.data, buf.len);
    json_buf_free(&buf);

    mn_handle_post_result(sync, status);
    if (status >= 200 && status < 300) {
        hl_log_info(sync->logger, "Mnemosyne: msgboard sync complete (%d posts)", count);
    }
}

void mn_start_full_sync(mn_sync_t *sync)
{
    if (!mn_sync_enabled(sync) || sync->state == MN_STATE_SUSPENDED)
        return;

    if (sync->index_files)
        do_full_file_sync(sync);

    if (sync->state == MN_STATE_SUSPENDED)
        return;

    if (sync->index_news)
        do_full_news_sync(sync);

    if (sync->state == MN_STATE_SUSPENDED)
        return;

    if (sync->server->config.mnemosyne_index_msgboard)
        do_full_msgboard_sync(sync);

    if (sync->state != MN_STATE_SUSPENDED)
        hl_log_info(sync->logger, "Mnemosyne: full sync complete");
}

void mn_start_targeted_sync(mn_sync_t *sync, mn_sync_type_t type)
{
    if (!mn_sync_enabled(sync) || sync->state == MN_STATE_SUSPENDED)
        return;

    switch (type) {
    case MN_SYNC_FILES:
        do_full_file_sync(sync);
        break;
    case MN_SYNC_NEWS:
        do_full_news_sync(sync);
        break;
    default:
        break;
    }
}

/* mn_do_sync_tick — no longer needed for full mode, but keep for API compat */
void mn_do_sync_tick(mn_sync_t *sync)
{
    (void)sync;
}

/* --- Periodic check (drift detection) --- */

void mn_periodic_check(mn_sync_t *sync)
{
    if (!mn_sync_enabled(sync) || sync->state == MN_STATE_SUSPENDED)
        return;

    /* Don't check during active chunked sync */
    if (sync->chunked_sync_active)
        return;

    hl_server_t *srv = sync->server;

    /* Count actual files */
    int actual_files = 0;
    if (sync->index_files && srv->config.file_root[0]) {
        actual_files = count_files_recursive(srv->config.file_root);
    }

    /* Count actual news */
    int actual_news = 0;
    if (sync->index_news && srv->threaded_news) {
        pthread_mutex_lock(&srv->threaded_news->mu);
        int i;
        for (i = 0; i < srv->threaded_news->category_count; i++) {
            actual_news += srv->threaded_news->categories[i].article_count;
        }
        pthread_mutex_unlock(&srv->threaded_news->mu);
    }

    /* Check for drift */
    if (sync->index_files && actual_files != sync->cached_file_count) {
        hl_log_info(sync->logger,
                    "Mnemosyne: file count drift detected (%d cached, %d actual)",
                    sync->cached_file_count, actual_files);
        sync->cached_file_count = actual_files;
        mn_start_targeted_sync(sync, MN_SYNC_FILES);
        return;
    }

    if (sync->index_news && actual_news != sync->cached_news_count) {
        hl_log_info(sync->logger,
                    "Mnemosyne: news count drift detected (%d cached, %d actual)",
                    sync->cached_news_count, actual_news);
        sync->cached_news_count = actual_news;
        mn_start_targeted_sync(sync, MN_SYNC_NEWS);
    }
}

/* --- Incremental sync queue --- */

static void incr_queue_push(mn_incr_queue_t *q, const mn_incr_entry_t *entry)
{
    if (q->count >= MN_INCR_QUEUE_SIZE) {
        /* Drop oldest */
        q->head = (q->head + 1) % MN_INCR_QUEUE_SIZE;
        q->count--;
    }
    memcpy(&q->entries[q->tail], entry, sizeof(*entry));
    q->tail = (q->tail + 1) % MN_INCR_QUEUE_SIZE;
    q->count++;
}

static int incr_queue_pop(mn_incr_queue_t *q, mn_incr_entry_t *out)
{
    if (q->count == 0)
        return 0;
    memcpy(out, &q->entries[q->head], sizeof(*out));
    q->head = (q->head + 1) % MN_INCR_QUEUE_SIZE;
    q->count--;
    return 1;
}

void mn_queue_file_add(mn_sync_t *sync, const char *path, const char *name,
                       uint64_t size, const char *file_type, const char *comment)
{
    if (!mn_sync_enabled(sync) || sync->state == MN_STATE_SUSPENDED)
        return;

    mn_incr_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.type = MN_INCR_FILE_ADD;
    strncpy(entry.path, path, sizeof(entry.path) - 1);
    strncpy(entry.name, name, sizeof(entry.name) - 1);
    entry.size = size;
    if (file_type) strncpy(entry.file_type, file_type, sizeof(entry.file_type) - 1);
    if (comment) strncpy(entry.comment, comment, sizeof(entry.comment) - 1);

    incr_queue_push(&sync->incr_queue, &entry);
}

void mn_queue_file_remove(mn_sync_t *sync, const char *path)
{
    if (!mn_sync_enabled(sync) || sync->state == MN_STATE_SUSPENDED)
        return;

    mn_incr_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.type = MN_INCR_FILE_REMOVE;
    strncpy(entry.path, path, sizeof(entry.path) - 1);

    incr_queue_push(&sync->incr_queue, &entry);
}

void mn_queue_news_add(mn_sync_t *sync, const char *cat_path,
                       uint32_t article_id, const char *title,
                       const char *body, const char *poster,
                       const char *date_str)
{
    if (!mn_sync_enabled(sync) || sync->state == MN_STATE_SUSPENDED)
        return;

    mn_incr_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.type = MN_INCR_NEWS_ADD;
    strncpy(entry.path, cat_path, sizeof(entry.path) - 1);
    entry.article_id = article_id;
    strncpy(entry.name, title, sizeof(entry.name) - 1);
    if (body) strncpy(entry.body, body, sizeof(entry.body) - 1);
    if (poster) strncpy(entry.poster, poster, sizeof(entry.poster) - 1);
    if (date_str) strncpy(entry.date_str, date_str, sizeof(entry.date_str) - 1);

    incr_queue_push(&sync->incr_queue, &entry);
}

void mn_drain_incremental_queue(mn_sync_t *sync)
{
    if (!mn_sync_enabled(sync) || sync->state == MN_STATE_SUSPENDED)
        return;

    /* Don't send incrementals during active chunked sync */
    if (sync->chunked_sync_active)
        return;

    mn_incr_entry_t entry;
    while (incr_queue_pop(&sync->incr_queue, &entry)) {
        json_buf_t buf;
        json_buf_init(&buf);

        const char *endpoint = NULL;
        switch (entry.type) {
        case MN_INCR_FILE_ADD:
        case MN_INCR_FILE_REMOVE:
            mn_build_incr_file_json(&buf, &entry);
            endpoint = "/api/v1/sync/files";
            break;
        case MN_INCR_NEWS_ADD:
            mn_build_incr_news_json(&buf, &entry);
            endpoint = "/api/v1/sync/news";
            break;
        }

        if (endpoint && buf.data) {
            int status = mn_post(sync, endpoint, buf.data, buf.len);
            mn_handle_post_result(sync, status);
        }

        json_buf_free(&buf);

        /* Stop if we got suspended */
        if (sync->state == MN_STATE_SUSPENDED)
            return;
    }
}

/* --- Deregistration --- */

void mn_deregister(mn_sync_t *sync)
{
    if (!mn_sync_enabled(sync))
        return;

    json_buf_t buf;
    json_buf_init(&buf);
    json_buf_append_str(&buf, "{}");

    int status = mn_post(sync, "/api/v1/sync/deregister", buf.data, buf.len);
    json_buf_free(&buf);

    if (status >= 200 && status < 300) {
        hl_log_info(sync->logger, "Mnemosyne: deregistered successfully");
    }
    /* Don't log failures on shutdown — we're exiting anyway */
}

/* --- Persistence --- */

void mn_save_cursor(mn_sync_t *sync)
{
    if (sync->cursor_file_path[0] == '\0')
        return;

    /* Write to temp file, then rename (atomic on POSIX) */
    char tmp_path[1050];
    snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", sync->cursor_file_path);

    FILE *f = fopen(tmp_path, "w");
    if (!f) return;

    fprintf(f, "type: %d\n", sync->cursor.type);
    fprintf(f, "sync_id: %s\n", sync->cursor.sync_id);
    fprintf(f, "chunk_index: %d\n", sync->cursor.chunk_index);
    fprintf(f, "cat_index: %d\n", sync->cursor.cat_index);
    fprintf(f, "cats_sent: %d\n", sync->cursor.cats_sent);
    fprintf(f, "timestamp: %ld\n", (long)time(NULL));
    fclose(f);

    rename(tmp_path, sync->cursor_file_path);
}

int mn_load_cursor(mn_sync_t *sync)
{
    if (sync->cursor_file_path[0] == '\0')
        return 0;

    FILE *f = fopen(sync->cursor_file_path, "r");
    if (!f) return 0;

    int type = 0;
    char sync_id[64] = {0};
    int chunk_index = 0;
    int cat_index = 0;
    int cats_sent = 0;
    long timestamp = 0;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "type: %d", &type) == 1) continue;
        if (sscanf(line, "sync_id: %63s", sync_id) == 1) continue;
        if (sscanf(line, "chunk_index: %d", &chunk_index) == 1) continue;
        if (sscanf(line, "cat_index: %d", &cat_index) == 1) continue;
        if (sscanf(line, "cats_sent: %d", &cats_sent) == 1) continue;
        if (sscanf(line, "timestamp: %ld", &timestamp) == 1) continue;
    }
    fclose(f);

    /* Discard stale cursors */
    if (timestamp > 0 && (time(NULL) - timestamp) > MN_STALE_CURSOR_SECONDS) {
        hl_log_info(sync->logger,
                    "Mnemosyne: discarding stale sync cursor (age > 1h)");
        remove(sync->cursor_file_path);
        return 0;
    }

    /* Restore cursor (but not pending_dirs — those need to be rebuilt) */
    sync->cursor.type = (mn_sync_type_t)type;
    strncpy(sync->cursor.sync_id, sync_id, sizeof(sync->cursor.sync_id) - 1);
    sync->cursor.chunk_index = chunk_index;
    sync->cursor.cat_index = cat_index;
    sync->cursor.cats_sent = cats_sent;

    hl_log_info(sync->logger,
                "Mnemosyne: loaded sync cursor (type=%d, chunk=%d)",
                type, chunk_index);

    /* Remove cursor file after loading */
    remove(sync->cursor_file_path);
    return 1;
}
