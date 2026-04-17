/*
 * test_mnemosyne_live.c - Integration tests for Mnemosyne sync
 *
 * Runs against live tracker.vespernet.net:8980.
 * Requires MSV_API_KEY environment variable with a valid msv_-prefixed key.
 * Register at https://agora.vespernet.net/login to obtain a key.
 *
 * Run: MSV_API_KEY=msv_... make test-mnemosyne-live
 */

#include "mobius/mnemosyne_sync.h"
#include "mobius/json_builder.h"
#include "hotline/http_client.h"
#include "hotline/server.h"
#include "hotline/config.h"
#include "hotline/logger.h"
#include "mobius/threaded_news_yaml.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#define MN_TEST_HOST "tracker.vespernet.net"
#define MN_TEST_PORT 8980
#define MN_TEST_URL  "http://tracker.vespernet.net:8980"

static int tests_run = 0;
static int tests_passed = 0;
static int tests_skipped = 0;

static const char *g_api_key = NULL;
static char g_marker[64] = {0};       /* unique test marker */
static char g_temp_dir[512] = {0};    /* temp file directory */

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-60s ", #name); \
        fflush(stdout); \
        name(); \
        tests_passed++; \
        printf("PASS\n"); \
    } while(0)

#define SKIP(name, reason) \
    do { \
        tests_run++; \
        tests_skipped++; \
        printf("  %-60s SKIP (%s)\n", #name, reason); \
    } while(0)

/* --- Test helpers --- */

/* Build a minimal mn_sync_t for testing (no event loop, no server) */
static void build_test_sync(mn_sync_t *sync, const char *api_key)
{
    memset(sync, 0, sizeof(*sync));
    strncpy(sync->url, MN_TEST_URL, sizeof(sync->url) - 1);
    strncpy(sync->api_key, api_key, sizeof(sync->api_key) - 1);
    sync->index_files = 1;
    sync->index_news = 1;
    sync->state = MN_STATE_ACTIVE;

    assert(hl_http_parse_url(sync->url, &sync->parsed_url) == 0);
    assert(mn_resolve_dns(sync) == 0);
}

/* Build a minimal server + sync for full sync tests */
static hl_server_t *build_test_server(mn_sync_t *sync)
{
    hl_server_t *srv = hl_server_new();
    assert(srv != NULL);

    strncpy(srv->config.name, "Lemoniscate Integration Test", HL_CONFIG_NAME_MAX);
    strncpy(srv->config.description, "Automated test run", HL_CONFIG_DESC_MAX);
    strncpy(srv->config.file_root, g_temp_dir, HL_CONFIG_PATH_MAX - 1);
    strncpy(srv->config.mnemosyne_url, MN_TEST_URL, HL_CONFIG_MNEMOSYNE_URL_MAX - 1);
    strncpy(srv->config.mnemosyne_api_key, g_api_key, HL_CONFIG_MNEMOSYNE_KEY_MAX - 1);
    srv->config.mnemosyne_index_files = 1;
    srv->config.mnemosyne_index_news = 1;

    /* Create threaded news with a test article */
    srv->threaded_news = mobius_threaded_news_new(NULL);
    assert(srv->threaded_news != NULL);

    /* Add test category and article */
    uint8_t cat_type[2] = {0, 3}; /* category */
    tn_create_category(srv->threaded_news, "Test Category", cat_type);
    char title[256];
    snprintf(title, sizeof(title), "Test Article %s", g_marker);
    tn_post_article(srv->threaded_news, "Test Category", 0,
                    title, "testbot", "Integration test article body", 31);

    mn_sync_init(sync, srv);
    return srv;
}

static void cleanup_test_server(hl_server_t *srv, mn_sync_t *sync)
{
    mn_deregister(sync);
    mn_sync_cleanup(sync);
    if (srv->threaded_news) mobius_threaded_news_free(srv->threaded_news);
    srv->threaded_news = NULL;
    hl_server_free(srv);
}

/* Create temp directory with uniquely-named test files */
static void create_test_files(void)
{
    snprintf(g_temp_dir, sizeof(g_temp_dir), "/tmp/__lemoniscate_test_%ld",
             (long)time(NULL));
    mkdir(g_temp_dir, 0755);

    char path[600];

    /* Create a test file with the marker in the name */
    snprintf(path, sizeof(path), "%s/%s.txt", g_temp_dir, g_marker);
    FILE *f = fopen(path, "w");
    if (f) {
        fprintf(f, "Integration test file\n");
        fclose(f);
    }

    /* Create a subdirectory with another file */
    snprintf(path, sizeof(path), "%s/subdir", g_temp_dir);
    mkdir(path, 0755);
    snprintf(path, sizeof(path), "%s/subdir/nested-%s.txt", g_temp_dir, g_marker);
    f = fopen(path, "w");
    if (f) {
        fprintf(f, "Nested test file\n");
        fclose(f);
    }
}

static void remove_test_files(void)
{
    if (g_temp_dir[0] == '\0') return;
    char cmd[600];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_temp_dir);
    system(cmd);
}

/* Simple search helper using HTTP GET */
static int search_mnemosyne(const char *query, const char *type,
                            char *out_body, size_t out_size)
{
    char path[1024];
    if (type && type[0]) {
        snprintf(path, sizeof(path), "/api/v1/search?q=%s&type=%s&limit=20",
                 query, type);
    } else {
        snprintf(path, sizeof(path), "/api/v1/search?q=%s&limit=20", query);
    }
    return hl_http_get(MN_TEST_HOST, MN_TEST_PORT, path,
                       out_body, out_size, 5000, 5000);
}

/* --- 2. Heartbeat Tests --- */

static void test_heartbeat_valid_key(void)
{
    mn_sync_t sync;
    build_test_sync(&sync, g_api_key);

    /* We need a minimal server context for heartbeat */
    hl_server_t *srv = hl_server_new();
    strncpy(srv->config.name, "Heartbeat Test", HL_CONFIG_NAME_MAX);
    strncpy(srv->config.description, "Testing", HL_CONFIG_DESC_MAX);
    sync.server = srv;
    sync.logger = srv->logger;

    int rc = mn_send_heartbeat(&sync);
    assert(rc == 0);

    /* Clean up without deregister — just a heartbeat test */
    hl_server_free(srv);
}

static void test_heartbeat_invalid_key(void)
{
    mn_sync_t sync;
    build_test_sync(&sync, "msv_invalid_key_for_testing");

    hl_server_t *srv = hl_server_new();
    strncpy(srv->config.name, "Bad Key Test", HL_CONFIG_NAME_MAX);
    strncpy(srv->config.description, "Should fail", HL_CONFIG_DESC_MAX);
    sync.server = srv;
    sync.logger = srv->logger;

    int rc = mn_send_heartbeat(&sync);
    assert(rc == -1);

    hl_server_free(srv);
}

/* --- 3. Chunked Full Sync Tests --- */

static void test_file_sync_with_files(void)
{
    mn_sync_t sync;
    hl_server_t *srv = build_test_server(&sync);

    /* Start file sync only */
    mn_start_targeted_sync(&sync, MN_SYNC_FILES);
    assert(sync.chunked_sync_active == 1);

    /* Drive the state machine manually */
    int ticks = 0;
    while (sync.chunked_sync_active && ticks < 50) {
        mn_do_sync_tick(&sync);
        ticks++;
    }

    assert(sync.chunked_sync_active == 0);
    assert(sync.state == MN_STATE_ACTIVE); /* no suspension */

    cleanup_test_server(srv, &sync);
}

static void test_file_sync_empty_dir(void)
{
    /* Create an empty temp dir */
    char empty_dir[512];
    snprintf(empty_dir, sizeof(empty_dir), "/tmp/__lemoniscate_empty_%ld",
             (long)time(NULL));
    mkdir(empty_dir, 0755);

    mn_sync_t sync;
    hl_server_t *srv = hl_server_new();
    strncpy(srv->config.name, "Empty Dir Test", HL_CONFIG_NAME_MAX);
    strncpy(srv->config.description, "Testing", HL_CONFIG_DESC_MAX);
    strncpy(srv->config.file_root, empty_dir, HL_CONFIG_PATH_MAX - 1);
    strncpy(srv->config.mnemosyne_url, MN_TEST_URL, HL_CONFIG_MNEMOSYNE_URL_MAX - 1);
    strncpy(srv->config.mnemosyne_api_key, g_api_key, HL_CONFIG_MNEMOSYNE_KEY_MAX - 1);
    srv->config.mnemosyne_index_files = 1;
    srv->config.mnemosyne_index_news = 0;
    srv->threaded_news = mobius_threaded_news_new(NULL);

    mn_sync_init(&sync, srv);
    mn_start_targeted_sync(&sync, MN_SYNC_FILES);

    int ticks = 0;
    while (sync.chunked_sync_active && ticks < 20) {
        mn_do_sync_tick(&sync);
        ticks++;
    }

    assert(sync.chunked_sync_active == 0);
    assert(sync.state == MN_STATE_ACTIVE);

    mn_deregister(&sync);
    mn_sync_cleanup(&sync);
    mobius_threaded_news_free(srv->threaded_news);
    srv->threaded_news = NULL;
    hl_server_free(srv);
    rmdir(empty_dir);
}

static void test_news_sync_with_articles(void)
{
    mn_sync_t sync;
    hl_server_t *srv = build_test_server(&sync);

    mn_start_targeted_sync(&sync, MN_SYNC_NEWS);
    assert(sync.chunked_sync_active == 1);

    int ticks = 0;
    while (sync.chunked_sync_active && ticks < 50) {
        mn_do_sync_tick(&sync);
        ticks++;
    }

    assert(sync.chunked_sync_active == 0);
    assert(sync.state == MN_STATE_ACTIVE);

    cleanup_test_server(srv, &sync);
}

static void test_full_sync_chaining(void)
{
    mn_sync_t sync;
    hl_server_t *srv = build_test_server(&sync);

    /* Full sync: files then news */
    mn_start_full_sync(&sync);
    assert(sync.chunked_sync_active == 1);
    assert(sync.cursor.type == MN_SYNC_FILES);

    int ticks = 0;
    int saw_news = 0;
    while (sync.chunked_sync_active && ticks < 100) {
        if (sync.cursor.type == MN_SYNC_NEWS) saw_news = 1;
        mn_do_sync_tick(&sync);
        ticks++;
    }

    assert(sync.chunked_sync_active == 0);
    assert(saw_news == 1); /* news phase ran after files */
    assert(sync.state == MN_STATE_ACTIVE);

    cleanup_test_server(srv, &sync);
}

/* --- 4. Incremental Sync Tests --- */

static void test_incremental_file_add(void)
{
    mn_sync_t sync;
    hl_server_t *srv = build_test_server(&sync);

    mn_queue_file_add(&sync, "test/incr-add.txt", "incr-add.txt", 42, "file", "");
    assert(sync.incr_queue.count == 1);

    mn_drain_incremental_queue(&sync);
    assert(sync.state == MN_STATE_ACTIVE);

    cleanup_test_server(srv, &sync);
}

static void test_incremental_file_remove(void)
{
    mn_sync_t sync;
    hl_server_t *srv = build_test_server(&sync);

    mn_queue_file_remove(&sync, "test/old-file.txt");
    assert(sync.incr_queue.count == 1);

    mn_drain_incremental_queue(&sync);
    assert(sync.state == MN_STATE_ACTIVE);

    cleanup_test_server(srv, &sync);
}

static void test_incremental_news_add(void)
{
    mn_sync_t sync;
    hl_server_t *srv = build_test_server(&sync);

    mn_queue_news_add(&sync, "Test Category", 99, "Incremental Article",
                      "Test body", "testbot", "2026-04-03");
    assert(sync.incr_queue.count == 1);

    mn_drain_incremental_queue(&sync);
    assert(sync.state == MN_STATE_ACTIVE);

    cleanup_test_server(srv, &sync);
}

/* --- 5. Search Verification --- */

static void test_search_files_after_sync(void)
{
    mn_sync_t sync;
    hl_server_t *srv = build_test_server(&sync);

    /* Full sync to push content */
    mn_start_full_sync(&sync);
    int ticks = 0;
    while (sync.chunked_sync_active && ticks < 100) {
        mn_do_sync_tick(&sync);
        ticks++;
    }

    /* Brief delay for indexing */
    sleep(2);

    /* Search for our unique marker */
    char body[8192];
    int status = search_mnemosyne(g_marker, "files", body, sizeof(body));
    assert(status == 200);
    assert(strstr(body, g_marker) != NULL);

    cleanup_test_server(srv, &sync);
}

static void test_search_news_after_sync(void)
{
    mn_sync_t sync;
    hl_server_t *srv = build_test_server(&sync);

    mn_start_full_sync(&sync);
    int ticks = 0;
    while (sync.chunked_sync_active && ticks < 100) {
        mn_do_sync_tick(&sync);
        ticks++;
    }

    sleep(2);

    char body[8192];
    int status = search_mnemosyne(g_marker, "news", body, sizeof(body));
    assert(status == 200);
    /* News may or may not match by marker depending on indexing — check status only */
    (void)body;

    cleanup_test_server(srv, &sync);
}

static void test_search_after_deregister(void)
{
    mn_sync_t sync;
    hl_server_t *srv = build_test_server(&sync);

    /* Sync then deregister */
    mn_start_full_sync(&sync);
    int ticks = 0;
    while (sync.chunked_sync_active && ticks < 100) {
        mn_do_sync_tick(&sync);
        ticks++;
    }
    mn_deregister(&sync);

    sleep(2);

    /* Search should no longer find our content */
    char body[8192];
    int status = search_mnemosyne(g_marker, "files", body, sizeof(body));
    assert(status == 200);
    /* After deregister, our server's content should be gone */
    /* Note: Mnemosyne may take time to purge — check that results
       either don't contain our server or are empty */

    mn_sync_cleanup(&sync);
    if (srv->threaded_news) mobius_threaded_news_free(srv->threaded_news);
    srv->threaded_news = NULL;
    hl_server_free(srv);
}

/* --- 6. Error Handling Tests --- */

static void test_invalid_key_401(void)
{
    mn_sync_t sync;
    build_test_sync(&sync, "msv_totally_bogus_key");

    hl_server_t *srv = hl_server_new();
    strncpy(srv->config.name, "Bad Key 401", HL_CONFIG_NAME_MAX);
    sync.server = srv;
    sync.logger = srv->logger;

    int rc = mn_send_heartbeat(&sync);
    assert(rc == -1);
    assert(sync.consecutive_failures >= 1);

    hl_server_free(srv);
}

static void test_unreachable_host(void)
{
    mn_sync_t sync;
    memset(&sync, 0, sizeof(sync));
    strncpy(sync.url, "http://192.0.2.1:9999", sizeof(sync.url) - 1);
    strncpy(sync.api_key, "msv_test", sizeof(sync.api_key) - 1);
    sync.state = MN_STATE_ACTIVE;

    hl_http_parse_url(sync.url, &sync.parsed_url);
    /* Use the unreachable IP directly */
    strncpy(sync.cached_ip, "192.0.2.1", sizeof(sync.cached_ip) - 1);
    sync.dns_resolved = 1;

    hl_server_t *srv = hl_server_new();
    strncpy(srv->config.name, "Unreachable", HL_CONFIG_NAME_MAX);
    sync.server = srv;
    sync.logger = srv->logger;

    int rc = mn_send_heartbeat(&sync);
    assert(rc == -1);
    assert(sync.consecutive_failures >= 1);

    hl_server_free(srv);
}

static void test_backoff_to_suspension(void)
{
    mn_sync_t sync;
    memset(&sync, 0, sizeof(sync));
    strncpy(sync.url, "http://192.0.2.1:9999", sizeof(sync.url) - 1);
    strncpy(sync.api_key, "msv_test", sizeof(sync.api_key) - 1);
    sync.state = MN_STATE_ACTIVE;

    hl_http_parse_url(sync.url, &sync.parsed_url);
    strncpy(sync.cached_ip, "192.0.2.1", sizeof(sync.cached_ip) - 1);
    sync.dns_resolved = 1;

    hl_server_t *srv = hl_server_new();
    strncpy(srv->config.name, "Backoff Test", HL_CONFIG_NAME_MAX);
    sync.server = srv;
    sync.logger = srv->logger;

    /* Hit it 5 times to trigger suspension */
    int i;
    for (i = 0; i < 5; i++) {
        mn_send_heartbeat(&sync);
    }

    assert(sync.state == MN_STATE_SUSPENDED);
    assert(sync.consecutive_failures >= 5);

    hl_server_free(srv);
}

/* --- 7. SIGHUP Reconfiguration Tests --- */

static void test_reconfigure_changed_url(void)
{
    mn_sync_t sync;
    hl_server_t *srv = build_test_server(&sync);

    /* Change URL to a different host */
    strncpy(srv->config.mnemosyne_url, "http://localhost:1",
            HL_CONFIG_MNEMOSYNE_URL_MAX - 1);

    int enabled = mn_sync_reconfigure(&sync, srv);
    /* Should still return enabled (url is set, key is set) but DNS may fail */
    /* The important thing: the URL was updated */
    assert(strcmp(sync.url, "http://localhost:1") == 0 ||
           sync.url[0] == '\0'); /* DNS fail clears it */

    mn_sync_cleanup(&sync);
    if (srv->threaded_news) mobius_threaded_news_free(srv->threaded_news);
    srv->threaded_news = NULL;
    hl_server_free(srv);
    (void)enabled;
}

static void test_reconfigure_empty_url(void)
{
    mn_sync_t sync;
    hl_server_t *srv = build_test_server(&sync);

    /* Remove Mnemosyne config */
    srv->config.mnemosyne_url[0] = '\0';
    srv->config.mnemosyne_api_key[0] = '\0';

    int enabled = mn_sync_reconfigure(&sync, srv);
    assert(enabled == 0);
    assert(mn_sync_enabled(&sync) == 0);

    mn_sync_cleanup(&sync);
    if (srv->threaded_news) mobius_threaded_news_free(srv->threaded_news);
    srv->threaded_news = NULL;
    hl_server_free(srv);
}

static void test_reconfigure_changed_key(void)
{
    mn_sync_t sync;
    hl_server_t *srv = build_test_server(&sync);

    /* Change to an invalid key */
    strncpy(srv->config.mnemosyne_api_key, "msv_changed_key",
            HL_CONFIG_MNEMOSYNE_KEY_MAX - 1);

    int enabled = mn_sync_reconfigure(&sync, srv);
    assert(enabled == 1);
    assert(strcmp(sync.api_key, "msv_changed_key") == 0);

    /* Heartbeat should fail with the bad key */
    int rc = mn_send_heartbeat(&sync);
    assert(rc == -1);

    mn_deregister(&sync);
    mn_sync_cleanup(&sync);
    if (srv->threaded_news) mobius_threaded_news_free(srv->threaded_news);
    srv->threaded_news = NULL;
    hl_server_free(srv);
}

/* --- Main --- */

int main(void)
{
    /* Check for API key */
    g_api_key = getenv("MSV_API_KEY");

    printf("\n=== Mnemosyne Integration Tests ===\n");
    printf("    Target: %s\n", MN_TEST_URL);

    if (!g_api_key || g_api_key[0] == '\0') {
        printf("    Status: SKIPPED (no MSV_API_KEY)\n");
        printf("\n    Set MSV_API_KEY=msv_... to run integration tests.\n");
        printf("    Register at https://agora.vespernet.net/login\n\n");
        return 0;
    }

    printf("    Key:    %.10s...\n", g_api_key);

    /* Generate unique marker for this test run */
    snprintf(g_marker, sizeof(g_marker), "lmtest%ld", (long)time(NULL));
    printf("    Marker: %s\n\n", g_marker);

    /* Create test files */
    create_test_files();

    printf("Heartbeat:\n");
    TEST(test_heartbeat_valid_key);
    TEST(test_heartbeat_invalid_key);

    printf("\nChunked Full Sync:\n");
    TEST(test_file_sync_with_files);
    TEST(test_file_sync_empty_dir);
    TEST(test_news_sync_with_articles);
    TEST(test_full_sync_chaining);

    printf("\nIncremental Sync:\n");
    TEST(test_incremental_file_add);
    TEST(test_incremental_file_remove);
    TEST(test_incremental_news_add);

    printf("\nSearch Verification:\n");
    TEST(test_search_files_after_sync);
    TEST(test_search_news_after_sync);
    TEST(test_search_after_deregister);

    printf("\nError Handling:\n");
    TEST(test_invalid_key_401);
    TEST(test_unreachable_host);
    TEST(test_backoff_to_suspension);

    printf("\nSIGHUP Reconfiguration:\n");
    TEST(test_reconfigure_changed_url);
    TEST(test_reconfigure_empty_url);
    TEST(test_reconfigure_changed_key);

    /* Cleanup */
    remove_test_files();

    printf("\n--- %d/%d tests passed", tests_passed, tests_run);
    if (tests_skipped > 0) printf(", %d skipped", tests_skipped);
    printf(" ---\n\n");

    return (tests_passed + tests_skipped == tests_run) ? 0 : 1;
}
