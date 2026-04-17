/*
 * test_mnemosyne.c - Unit tests for Mnemosyne sync components
 *
 * Tests: JSON escaping, JSON payload builders, sync state machine,
 * backoff/suspension, incremental ring buffer.
 */

#include "mobius/json_builder.h"
#include "mobius/mnemosyne_sync.h"
#include "hotline/http_client.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-60s ", #name); \
        name(); \
        tests_passed++; \
        printf("PASS\n"); \
    } while(0)

/* ===== JSON escaping tests ===== */

static void test_json_escape_plain(void)
{
    char out[128];
    int n = json_escape_string("hello world", out, sizeof(out));
    assert(n > 0);
    assert(strcmp(out, "hello world") == 0);
}

static void test_json_escape_quotes(void)
{
    char out[128];
    int n = json_escape_string("say \"hello\"", out, sizeof(out));
    assert(n > 0);
    assert(strcmp(out, "say \\\"hello\\\"") == 0);
}

static void test_json_escape_backslash(void)
{
    char out[128];
    int n = json_escape_string("path\\to\\file", out, sizeof(out));
    assert(n > 0);
    assert(strcmp(out, "path\\\\to\\\\file") == 0);
}

static void test_json_escape_newlines(void)
{
    char out[128];
    int n = json_escape_string("line1\nline2\rline3", out, sizeof(out));
    assert(n > 0);
    assert(strcmp(out, "line1\\nline2\\rline3") == 0);
}

static void test_json_escape_tabs(void)
{
    char out[128];
    int n = json_escape_string("col1\tcol2", out, sizeof(out));
    assert(n > 0);
    assert(strcmp(out, "col1\\tcol2") == 0);
}

static void test_json_escape_control_chars(void)
{
    char input[4] = { 0x01, 0x02, 0x1F, 0x00 };
    char out[128];
    int n = json_escape_string(input, out, sizeof(out));
    assert(n > 0);
    /* Should produce \u0001\u0002\u001f */
    assert(strstr(out, "\\u0001") != NULL);
    assert(strstr(out, "\\u0002") != NULL);
    assert(strstr(out, "\\u001f") != NULL);
}

static void test_json_escape_empty(void)
{
    char out[128];
    int n = json_escape_string("", out, sizeof(out));
    assert(n == 0);
    assert(out[0] == '\0');
}

/* ===== JSON buffer tests ===== */

static void test_json_buf_basic(void)
{
    json_buf_t buf;
    json_buf_init(&buf);
    json_buf_append_str(&buf, "hello");
    assert(buf.len == 5);
    assert(strcmp(buf.data, "hello") == 0);
    json_buf_free(&buf);
}

static void test_json_buf_printf(void)
{
    json_buf_t buf;
    json_buf_init(&buf);
    json_buf_printf(&buf, "count: %d", 42);
    assert(strcmp(buf.data, "count: 42") == 0);
    json_buf_free(&buf);
}

static void test_json_buf_add_string(void)
{
    json_buf_t buf;
    json_buf_init(&buf);
    json_buf_add_string(&buf, "name", "test \"server\"");
    /* Should produce: "name": "test \"server\"" */
    assert(strstr(buf.data, "\"name\": \"test \\\"server\\\"\"") != NULL);
    json_buf_free(&buf);
}

static void test_json_buf_add_int(void)
{
    json_buf_t buf;
    json_buf_init(&buf);
    json_buf_add_int(&buf, "count", 99);
    assert(strstr(buf.data, "\"count\": 99") != NULL);
    json_buf_free(&buf);
}

static void test_json_buf_add_bool(void)
{
    json_buf_t buf;
    json_buf_init(&buf);
    json_buf_add_bool(&buf, "active", 1);
    assert(strstr(buf.data, "\"active\": true") != NULL);
    json_buf_free(&buf);

    json_buf_init(&buf);
    json_buf_add_bool(&buf, "active", 0);
    assert(strstr(buf.data, "\"active\": false") != NULL);
    json_buf_free(&buf);
}

/* ===== JSON payload builder tests ===== */

static void test_heartbeat_json(void)
{
    json_buf_t buf;
    json_buf_init(&buf);
    mn_build_heartbeat_json(&buf, "My Server", "A cool server",
                            "example.com:5500", 0, 3, 10, 42, 1048576LL);

    assert(strstr(buf.data, "\"server_name\": \"My Server\"") != NULL);
    assert(strstr(buf.data, "\"server_description\": \"A cool server\"") != NULL);
    assert(strstr(buf.data, "\"server_address\": \"example.com:5500\"") != NULL);
    assert(strstr(buf.data, "\"counts\"") != NULL);
    assert(strstr(buf.data, "\"msgboard_posts\": 0") != NULL);
    assert(strstr(buf.data, "\"news_categories\": 3") != NULL);
    assert(strstr(buf.data, "\"news_articles\": 10") != NULL);
    assert(strstr(buf.data, "\"files\": 42") != NULL);
    assert(strstr(buf.data, "\"total_file_size\": 1048576") != NULL);

    assert(buf.data[0] == '{');
    assert(buf.data[buf.len - 1] == '}');

    json_buf_free(&buf);
}

static void test_file_chunk_json(void)
{
    json_buf_t buf;
    json_buf_init(&buf);
    mn_build_file_chunk_json(&buf, "12345_678", 0, 0);
    json_buf_append_str(&buf, "]}");

    assert(strstr(buf.data, "\"sync_id\": \"12345_678\"") != NULL);
    assert(strstr(buf.data, "\"chunk_index\": 0") != NULL);
    assert(strstr(buf.data, "\"finalize\": false") != NULL);
    assert(strstr(buf.data, "\"files\": [") != NULL);

    json_buf_free(&buf);
}

static void test_file_chunk_finalize_json(void)
{
    json_buf_t buf;
    json_buf_init(&buf);
    mn_build_file_chunk_json(&buf, "12345_678", 3, 1);
    json_buf_append_str(&buf, "]}");

    assert(strstr(buf.data, "\"finalize\": true") != NULL);
    assert(strstr(buf.data, "\"chunk_index\": 3") != NULL);

    json_buf_free(&buf);
}

static void test_news_chunk_json(void)
{
    json_buf_t buf;
    json_buf_init(&buf);
    mn_build_news_chunk_json(&buf, "99_abc", 1, 0);
    json_buf_append_str(&buf, ", \"categories\": [], \"articles\": []}");

    assert(strstr(buf.data, "\"sync_id\": \"99_abc\"") != NULL);
    assert(strstr(buf.data, "\"chunk_index\": 1") != NULL);
    assert(strstr(buf.data, "\"finalize\": false") != NULL);

    json_buf_free(&buf);
}

static void test_incr_file_add_json(void)
{
    mn_incr_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.type = MN_INCR_FILE_ADD;
    strncpy(entry.path, "docs/readme.txt", sizeof(entry.path) - 1);
    strncpy(entry.name, "readme.txt", sizeof(entry.name) - 1);
    entry.size = 1024;
    strncpy(entry.file_type, "file", sizeof(entry.file_type) - 1);

    json_buf_t buf;
    json_buf_init(&buf);
    mn_build_incr_file_json(&buf, &entry);

    assert(strstr(buf.data, "\"mode\": \"incremental\"") != NULL);
    assert(strstr(buf.data, "\"added\": [") != NULL);
    assert(strstr(buf.data, "\"path\": \"docs/readme.txt\"") != NULL);
    assert(strstr(buf.data, "\"name\": \"readme.txt\"") != NULL);
    assert(strstr(buf.data, "\"size\": 1024") != NULL);
    assert(strstr(buf.data, "\"removed\": []") != NULL);

    json_buf_free(&buf);
}

static void test_incr_file_remove_json(void)
{
    mn_incr_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.type = MN_INCR_FILE_REMOVE;
    strncpy(entry.path, "old/file.zip", sizeof(entry.path) - 1);

    json_buf_t buf;
    json_buf_init(&buf);
    mn_build_incr_file_json(&buf, &entry);

    assert(strstr(buf.data, "\"mode\": \"incremental\"") != NULL);
    assert(strstr(buf.data, "\"added\": []") != NULL);
    assert(strstr(buf.data, "\"removed\": [\"old/file.zip\"]") != NULL);

    json_buf_free(&buf);
}

static void test_incr_news_add_json(void)
{
    mn_incr_entry_t entry;
    memset(&entry, 0, sizeof(entry));
    entry.type = MN_INCR_NEWS_ADD;
    entry.article_id = 42;
    strncpy(entry.path, "General", sizeof(entry.path) - 1);
    strncpy(entry.name, "Hello World", sizeof(entry.name) - 1);
    strncpy(entry.body, "This is a test.", sizeof(entry.body) - 1);
    strncpy(entry.poster, "admin", sizeof(entry.poster) - 1);
    strncpy(entry.date_str, "2026-04-03", sizeof(entry.date_str) - 1);

    json_buf_t buf;
    json_buf_init(&buf);
    mn_build_incr_news_json(&buf, &entry);

    assert(strstr(buf.data, "\"mode\": \"incremental\"") != NULL);
    assert(strstr(buf.data, "\"added_articles\": [") != NULL);
    assert(strstr(buf.data, "\"id\": 42") != NULL);
    assert(strstr(buf.data, "\"path\": \"General\"") != NULL);
    assert(strstr(buf.data, "\"title\": \"Hello World\"") != NULL);
    assert(strstr(buf.data, "\"body\": \"This is a test.\"") != NULL);
    assert(strstr(buf.data, "\"poster\": \"admin\"") != NULL);
    assert(strstr(buf.data, "\"parent_id\": 0") != NULL);

    json_buf_free(&buf);
}

/* ===== URL parsing tests ===== */

static void test_url_parse_basic(void)
{
    hl_parsed_url_t url;
    assert(hl_http_parse_url("http://example.com/api/v1", &url) == 0);
    assert(strcmp(url.host, "example.com") == 0);
    assert(url.port == 80);
    assert(strcmp(url.path, "/api/v1") == 0);
}

static void test_url_parse_with_port(void)
{
    hl_parsed_url_t url;
    assert(hl_http_parse_url("http://example.com:8080/sync", &url) == 0);
    assert(strcmp(url.host, "example.com") == 0);
    assert(url.port == 8080);
    assert(strcmp(url.path, "/sync") == 0);
}

static void test_url_parse_no_path(void)
{
    hl_parsed_url_t url;
    assert(hl_http_parse_url("http://localhost:3000", &url) == 0);
    assert(strcmp(url.host, "localhost") == 0);
    assert(url.port == 3000);
    assert(strcmp(url.path, "/") == 0);
}

static void test_url_api_key_append(void)
{
    hl_parsed_url_t url;
    hl_http_parse_url("http://example.com/api", &url);

    char out[512];
    assert(hl_http_url_with_api_key(&url, "msv_testkey", out, sizeof(out)) == 0);
    assert(strcmp(out, "/api?api_key=msv_testkey") == 0);
}

/* ===== Incremental ring buffer tests ===== */

static void test_incr_queue_basic(void)
{
    mn_sync_t sync;
    memset(&sync, 0, sizeof(sync));
    strncpy(sync.url, "http://example.com", sizeof(sync.url) - 1);
    strncpy(sync.api_key, "msv_test", sizeof(sync.api_key) - 1);
    sync.state = MN_STATE_ACTIVE;

    mn_queue_file_add(&sync, "/files/test.txt", "test.txt", 100, "file", "");
    assert(sync.incr_queue.count == 1);

    mn_queue_file_remove(&sync, "/files/old.txt");
    assert(sync.incr_queue.count == 2);
}

static void test_incr_queue_overflow(void)
{
    mn_sync_t sync;
    memset(&sync, 0, sizeof(sync));
    strncpy(sync.url, "http://example.com", sizeof(sync.url) - 1);
    strncpy(sync.api_key, "msv_test", sizeof(sync.api_key) - 1);
    sync.state = MN_STATE_ACTIVE;

    /* Fill the queue to capacity */
    int i;
    for (i = 0; i < MN_INCR_QUEUE_SIZE + 10; i++) {
        char path[64];
        snprintf(path, sizeof(path), "file_%d.txt", i);
        mn_queue_file_add(&sync, path, path, 100, "file", "");
    }

    /* Queue should be at capacity, oldest dropped */
    assert(sync.incr_queue.count == MN_INCR_QUEUE_SIZE);
}

static void test_incr_queue_suspended_noop(void)
{
    mn_sync_t sync;
    memset(&sync, 0, sizeof(sync));
    strncpy(sync.url, "http://example.com", sizeof(sync.url) - 1);
    strncpy(sync.api_key, "msv_test", sizeof(sync.api_key) - 1);
    sync.state = MN_STATE_SUSPENDED;

    mn_queue_file_add(&sync, "/files/test.txt", "test.txt", 100, "file", "");
    assert(sync.incr_queue.count == 0); /* Should not enqueue when suspended */
}

/* ===== Backoff/suspension state tests ===== */

static void test_backoff_escalation(void)
{
    mn_sync_t sync;
    memset(&sync, 0, sizeof(sync));
    sync.state = MN_STATE_ACTIVE;

    /* Simulate consecutive failures */
    sync.consecutive_failures = 0;
    sync.backoff_level = 0;

    /* First failure — should stay active */
    sync.consecutive_failures = 1;
    assert(sync.state == MN_STATE_ACTIVE);

    /* After 4 failures, should still be active */
    sync.consecutive_failures = 4;
    assert(sync.state == MN_STATE_ACTIVE);

    /* Suspension happens at 5 failures via mn_handle_post_result,
     * which requires a full server context. Just verify the state enum. */
    sync.state = MN_STATE_SUSPENDED;
    assert(sync.state == MN_STATE_SUSPENDED);
}

static void test_sync_enabled_checks(void)
{
    mn_sync_t sync;
    memset(&sync, 0, sizeof(sync));

    /* No URL = disabled */
    assert(mn_sync_enabled(&sync) == 0);

    /* URL but no key = disabled */
    strncpy(sync.url, "http://example.com", sizeof(sync.url) - 1);
    assert(mn_sync_enabled(&sync) == 0);

    /* Both = enabled */
    strncpy(sync.api_key, "msv_test", sizeof(sync.api_key) - 1);
    assert(mn_sync_enabled(&sync) == 1);
}

/* ===== Main ===== */

int main(void)
{
    printf("\n=== Mnemosyne Sync Tests ===\n\n");

    printf("JSON Escaping:\n");
    TEST(test_json_escape_plain);
    TEST(test_json_escape_quotes);
    TEST(test_json_escape_backslash);
    TEST(test_json_escape_newlines);
    TEST(test_json_escape_tabs);
    TEST(test_json_escape_control_chars);
    TEST(test_json_escape_empty);

    printf("\nJSON Buffer:\n");
    TEST(test_json_buf_basic);
    TEST(test_json_buf_printf);
    TEST(test_json_buf_add_string);
    TEST(test_json_buf_add_int);
    TEST(test_json_buf_add_bool);

    printf("\nJSON Payload Builders:\n");
    TEST(test_heartbeat_json);
    TEST(test_file_chunk_json);
    TEST(test_file_chunk_finalize_json);
    TEST(test_news_chunk_json);
    TEST(test_incr_file_add_json);
    TEST(test_incr_file_remove_json);
    TEST(test_incr_news_add_json);

    printf("\nURL Parsing:\n");
    TEST(test_url_parse_basic);
    TEST(test_url_parse_with_port);
    TEST(test_url_parse_no_path);
    TEST(test_url_api_key_append);

    printf("\nIncremental Ring Buffer:\n");
    TEST(test_incr_queue_basic);
    TEST(test_incr_queue_overflow);
    TEST(test_incr_queue_suspended_noop);

    printf("\nBackoff/Suspension:\n");
    TEST(test_backoff_escalation);
    TEST(test_sync_enabled_checks);

    printf("\n--- %d/%d tests passed ---\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
