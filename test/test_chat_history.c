/*
 * test_chat_history.c - Unit tests for chat history storage layer.
 *
 * Covers OpenSpec change "chat-history" section 11 tasks 11.1-11.9 + 11.11.
 * Tests 11.10 (rate limiter) and 11.12-11.13 (live integration) are not
 * unit tests — see openspec/changes/chat-history/tasks.md.
 */

#define _FILE_OFFSET_BITS 64
#define _POSIX_C_SOURCE 200809L

#include "hotline/chat_history.h"

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-55s ", #name); \
        fflush(stdout); \
        name(); \
        tests_passed++; \
        printf("PASS\n"); \
    } while(0)

/* --- Filesystem helpers --- */

static char g_test_dir[1024];

static void setup_dir(void)
{
    snprintf(g_test_dir, sizeof(g_test_dir),
             "/tmp/lm_chat_test_%d", (int)getpid());
    /* Best-effort cleanup of any prior run. */
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_test_dir);
    if (system(cmd) != 0) { /* ignore */ }
    if (mkdir(g_test_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "mkdir %s failed: %s\n", g_test_dir, strerror(errno));
        exit(1);
    }
}

static void teardown_dir(void)
{
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_test_dir);
    if (system(cmd) != 0) { /* ignore */ }
}

static lm_chat_history_t *open_default(void)
{
    lm_chat_history_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.enabled = 1;
    cfg.max_msgs = 0;
    cfg.max_days = 0;
    return lm_chat_history_open(g_test_dir, &cfg);
}

/* --- 11.1 append 1000, query latest 50, oldest-first, has_more=1 --- */

static void test_append_1000_query_latest_50(void)
{
    setup_dir();
    lm_chat_history_t *h = open_default();
    assert(h);

    int i;
    for (i = 1; i <= 1000; i++) {
        char body[64];
        snprintf(body, sizeof(body), "msg #%d", i);
        uint64_t id = lm_chat_history_append(h, 0, 0, 100, "user", body);
        assert(id == (uint64_t)i);
    }

    lm_chat_entry_t *entries = NULL;
    size_t n = 0;
    uint8_t has_more = 0;
    int rc = lm_chat_history_query(h, 0, 0, 0, 50, &entries, &n, &has_more);
    assert(rc == 0);
    assert(n == 50);
    assert(has_more == 1);
    /* Latest-50 → ids 951..1000, oldest-first. */
    assert(entries[0].id == 951);
    assert(entries[49].id == 1000);

    lm_chat_history_entries_free(entries);
    lm_chat_history_close(h);
    teardown_dir();
}

/* --- 11.2 BEFORE=50 limit=20 → ids 30..49, has_more=1 --- */

static void test_query_before(void)
{
    setup_dir();
    lm_chat_history_t *h = open_default();
    assert(h);
    int i;
    for (i = 1; i <= 100; i++) {
        char body[32]; snprintf(body, sizeof(body), "%d", i);
        lm_chat_history_append(h, 0, 0, 100, "u", body);
    }

    lm_chat_entry_t *entries = NULL;
    size_t n = 0;
    uint8_t has_more = 0;
    int rc = lm_chat_history_query(h, 0, /*before*/50, /*after*/0, 20,
                                    &entries, &n, &has_more);
    assert(rc == 0);
    assert(n == 20);
    assert(has_more == 1);
    assert(entries[0].id == 30);
    assert(entries[19].id == 49);

    lm_chat_history_entries_free(entries);
    lm_chat_history_close(h);
    teardown_dir();
}

/* --- 11.3 AFTER=50 limit=20 → ids 51..70, has_more=1 --- */

static void test_query_after(void)
{
    setup_dir();
    lm_chat_history_t *h = open_default();
    assert(h);
    int i;
    for (i = 1; i <= 100; i++) {
        char body[32]; snprintf(body, sizeof(body), "%d", i);
        lm_chat_history_append(h, 0, 0, 100, "u", body);
    }

    lm_chat_entry_t *entries = NULL;
    size_t n = 0;
    uint8_t has_more = 0;
    int rc = lm_chat_history_query(h, 0, /*before*/0, /*after*/50, 20,
                                    &entries, &n, &has_more);
    assert(rc == 0);
    assert(n == 20);
    assert(has_more == 1);
    assert(entries[0].id == 51);
    assert(entries[19].id == 70);

    lm_chat_history_entries_free(entries);
    lm_chat_history_close(h);
    teardown_dir();
}

/* --- 11.4 BEFORE=60 AFTER=20 limit=100 → ids 21..59, has_more=0 --- */

static void test_query_range(void)
{
    setup_dir();
    lm_chat_history_t *h = open_default();
    assert(h);
    int i;
    for (i = 1; i <= 100; i++) {
        char body[32]; snprintf(body, sizeof(body), "%d", i);
        lm_chat_history_append(h, 0, 0, 100, "u", body);
    }

    lm_chat_entry_t *entries = NULL;
    size_t n = 0;
    uint8_t has_more = 0;
    int rc = lm_chat_history_query(h, 0, /*before*/60, /*after*/20, 100,
                                    &entries, &n, &has_more);
    assert(rc == 0);
    assert(n == 39);          /* 21..59 inclusive */
    assert(has_more == 0);
    assert(entries[0].id == 21);
    assert(entries[38].id == 59);

    lm_chat_history_entries_free(entries);
    lm_chat_history_close(h);
    teardown_dir();
}

/* --- 11.5 empty file → zero entries, has_more=0 --- */

static void test_query_empty(void)
{
    setup_dir();
    lm_chat_history_t *h = open_default();
    assert(h);

    lm_chat_entry_t *entries = NULL;
    size_t n = 0;
    uint8_t has_more = 0;
    int rc = lm_chat_history_query(h, 0, 0, 0, 50, &entries, &n, &has_more);
    assert(rc == 0);
    assert(n == 0);
    assert(has_more == 0);
    assert(entries == NULL);

    lm_chat_history_close(h);
    teardown_dir();
}

/* --- 11.6 partial line at EOF → startup truncates to last good offset --- */

static void test_corrupt_truncate(void)
{
    setup_dir();

    /* Round 1: append three valid entries, close. */
    {
        lm_chat_history_t *h = open_default();
        assert(h);
        lm_chat_history_append(h, 0, 0, 100, "u", "one");
        lm_chat_history_append(h, 0, 0, 100, "u", "two");
        lm_chat_history_append(h, 0, 0, 100, "u", "three");
        lm_chat_history_close(h);
    }

    /* Append 50 bytes of garbage (no newline) directly. */
    char path[2048];
    snprintf(path, sizeof(path), "%s/ChatHistory/channel-0.jsonl", g_test_dir);
    FILE *f = fopen(path, "ab");
    assert(f);
    char garbage[50];
    memset(garbage, 'X', sizeof(garbage));
    fwrite(garbage, 1, sizeof(garbage), f);
    fclose(f);

    struct stat before;
    assert(stat(path, &before) == 0);

    /* Round 2: reopen — scan should truncate the garbage. */
    {
        lm_chat_history_t *h = open_default();
        assert(h);
        struct stat after;
        assert(stat(path, &after) == 0);
        assert(after.st_size < before.st_size);
        assert(lm_chat_history_count(h, 0) == 3);

        lm_chat_entry_t *entries = NULL;
        size_t n = 0;
        uint8_t has_more = 0;
        int rc = lm_chat_history_query(h, 0, 0, 0, 50, &entries, &n, &has_more);
        assert(rc == 0);
        assert(n == 3);
        assert(strcmp(entries[2].body, "three") == 0);
        lm_chat_history_entries_free(entries);
        lm_chat_history_close(h);
    }

    teardown_dir();
}

/* --- 11.7 tombstone clears body and sets the deleted flag --- */

static void test_tombstone(void)
{
    setup_dir();
    lm_chat_history_t *h = open_default();
    assert(h);
    uint64_t id1 = lm_chat_history_append(h, 0, 0, 100, "alice", "secret");
    uint64_t id2 = lm_chat_history_append(h, 0, 0, 100, "bob",   "kept");
    assert(id1 == 1 && id2 == 2);

    assert(lm_chat_history_tombstone(h, id1) == 0);

    lm_chat_entry_t *entries = NULL;
    size_t n = 0;
    uint8_t has_more = 0;
    int rc = lm_chat_history_query(h, 0, 0, 0, 50, &entries, &n, &has_more);
    assert(rc == 0);
    assert(n == 2);

    /* id 1 is tombstoned. */
    assert(entries[0].id == 1);
    assert(entries[0].flags & HL_CHAT_FLAG_IS_DELETED);
    assert(entries[0].body[0] == '\0');

    /* id 2 still intact. */
    assert(entries[1].id == 2);
    assert(strcmp(entries[1].body, "kept") == 0);

    lm_chat_history_entries_free(entries);
    lm_chat_history_close(h);
    teardown_dir();
}

/* --- 11.8 prune by count drops the oldest entries --- */

static void test_prune_by_count(void)
{
    setup_dir();
    lm_chat_history_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.enabled = 1;
    cfg.max_msgs = 100;       /* keep 100 most recent */
    cfg.max_days = 0;
    lm_chat_history_t *h = lm_chat_history_open(g_test_dir, &cfg);
    assert(h);

    int i;
    for (i = 1; i <= 250; i++) {
        char body[32]; snprintf(body, sizeof(body), "%d", i);
        lm_chat_history_append(h, 0, 0, 100, "u", body);
    }

    /* Pre-prune: 250 entries persisted. */
    assert(lm_chat_history_count(h, 0) == 250);

    assert(lm_chat_history_prune(h) == 0);

    /* After prune: ≤ max_msgs entries remain — implementation may keep some
     * slack to avoid rewriting on every append. */
    size_t after = lm_chat_history_count(h, 0);
    assert(after <= 100);
    assert(after > 0);

    /* Latest entry is still id 250. */
    lm_chat_entry_t *entries = NULL;
    size_t n = 0;
    uint8_t has_more = 0;
    int rc = lm_chat_history_query(h, 0, 0, 0, 1, &entries, &n, &has_more);
    assert(rc == 0);
    assert(n == 1);
    assert(entries[0].id == 250);
    lm_chat_history_entries_free(entries);

    lm_chat_history_close(h);
    teardown_dir();
}

/* --- 11.9 prune by age drops entries older than MaxDays --- */

static void test_prune_by_age(void)
{
    setup_dir();

    /* Hand-write a JSONL file with backdated timestamps. */
    char dir[1200];
    snprintf(dir, sizeof(dir), "%s/ChatHistory", g_test_dir);
    if (mkdir(dir, 0755) != 0 && errno != EEXIST) {
        perror("mkdir ChatHistory");
        exit(1);
    }
    char path[2048];
    snprintf(path, sizeof(path), "%s/channel-0.jsonl", dir);

    time_t now = time(NULL);
    time_t old_ts    = now - (10 * 86400);  /* 10 days old */
    time_t recent_ts = now - (1 * 3600);    /* 1 hour old */

    FILE *f = fopen(path, "wb");
    assert(f);
    fprintf(f, "{\"id\":1,\"ch\":0,\"ts\":%lld,\"flags\":0,\"icon\":0,"
               "\"nick\":\"u\",\"body\":\"old1\"}\n", (long long)old_ts);
    fprintf(f, "{\"id\":2,\"ch\":0,\"ts\":%lld,\"flags\":0,\"icon\":0,"
               "\"nick\":\"u\",\"body\":\"old2\"}\n", (long long)old_ts);
    fprintf(f, "{\"id\":3,\"ch\":0,\"ts\":%lld,\"flags\":0,\"icon\":0,"
               "\"nick\":\"u\",\"body\":\"recent\"}\n", (long long)recent_ts);
    fclose(f);

    lm_chat_history_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.enabled = 1;
    cfg.max_msgs = 0;
    cfg.max_days = 7;     /* drop entries older than 7 days */
    lm_chat_history_t *h = lm_chat_history_open(g_test_dir, &cfg);
    assert(h);
    assert(lm_chat_history_count(h, 0) == 3);

    assert(lm_chat_history_prune(h) == 0);

    /* Only the recent (id=3) entry survives. */
    assert(lm_chat_history_count(h, 0) == 1);

    lm_chat_entry_t *entries = NULL;
    size_t n = 0;
    uint8_t has_more = 0;
    int rc = lm_chat_history_query(h, 0, 0, 0, 50, &entries, &n, &has_more);
    assert(rc == 0);
    assert(n == 1);
    assert(entries[0].id == 3);
    assert(strcmp(entries[0].body, "recent") == 0);
    lm_chat_history_entries_free(entries);

    lm_chat_history_close(h);
    teardown_dir();
}

/* --- 11.11 encryption round-trip: write encrypted, read decrypted --- */

static void test_encryption_roundtrip(void)
{
    setup_dir();

    /* Write a 32-byte deterministic key file. */
    char key_path[1200];
    snprintf(key_path, sizeof(key_path), "%s/chat.key", g_test_dir);
    FILE *kf = fopen(key_path, "wb");
    assert(kf);
    uint8_t key[32];
    int i;
    for (i = 0; i < 32; i++) key[i] = (uint8_t)(0xA0 + i);
    fwrite(key, 1, 32, kf);
    fclose(kf);

    lm_chat_history_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.enabled = 1;
    strncpy(cfg.encryption_key_path, key_path, sizeof(cfg.encryption_key_path) - 1);

    lm_chat_history_t *h = lm_chat_history_open(g_test_dir, &cfg);
    assert(h);

    const char *plaintext = "this is a secret message";
    uint64_t id = lm_chat_history_append(h, 0, 0, 100, "alice", plaintext);
    assert(id == 1);

    /* On disk: body must NOT contain the plaintext (must be ENC: prefixed). */
    char path[2048];
    snprintf(path, sizeof(path), "%s/ChatHistory/channel-0.jsonl", g_test_dir);
    FILE *df = fopen(path, "rb");
    assert(df);
    char raw[8192] = {0};
    fread(raw, 1, sizeof(raw) - 1, df);
    fclose(df);
    assert(strstr(raw, plaintext) == NULL);
    assert(strstr(raw, "ENC:") != NULL);

    /* Query path decrypts and yields original plaintext. */
    lm_chat_entry_t *entries = NULL;
    size_t n = 0;
    uint8_t has_more = 0;
    int rc = lm_chat_history_query(h, 0, 0, 0, 50, &entries, &n, &has_more);
    assert(rc == 0);
    assert(n == 1);
    assert(entries[0].id == 1);
    assert(strcmp(entries[0].body, plaintext) == 0);
    lm_chat_history_entries_free(entries);

    lm_chat_history_close(h);

    /* Reopen with the same key, ensure round-trip survives close/reopen. */
    h = lm_chat_history_open(g_test_dir, &cfg);
    assert(h);
    rc = lm_chat_history_query(h, 0, 0, 0, 50, &entries, &n, &has_more);
    assert(rc == 0);
    assert(n == 1);
    assert(strcmp(entries[0].body, plaintext) == 0);
    lm_chat_history_entries_free(entries);
    lm_chat_history_close(h);

    teardown_dir();
}

/* --- 11.10 rate limiter empties at capacity, refills correctly --- */

static void test_rate_limiter(void)
{
    uint16_t tokens = 0;
    uint64_t last = 0;
    uint64_t now = 1000000;       /* arbitrary base; helper compares deltas */
    uint32_t cap = 20;
    uint32_t refill = 10;         /* tokens per second */
    int allowed_run1 = 0;
    int i;

    /* 25 rapid requests at the same timestamp: first 20 succeed, next 5 fail. */
    for (i = 0; i < 25; i++) {
        if (lm_chat_rl_consume(&tokens, &last, now, cap, refill))
            allowed_run1++;
    }
    assert(allowed_run1 == 20);
    assert(tokens == 0);

    /* Advance 500 ms: refill = 500ms × 10/sec = 5 tokens. Next 5 succeed; #6 fails. */
    now += 500;
    int allowed_run2 = 0;
    for (i = 0; i < 6; i++) {
        if (lm_chat_rl_consume(&tokens, &last, now, cap, refill))
            allowed_run2++;
    }
    assert(allowed_run2 == 5);

    /* Advance 5 seconds: refill clamps at capacity. */
    now += 5000;
    int allowed_run3 = 0;
    for (i = 0; i < 25; i++) {
        if (lm_chat_rl_consume(&tokens, &last, now, cap, refill))
            allowed_run3++;
    }
    assert(allowed_run3 == 20);   /* clamped to capacity, not 50 */

    /* Lazy-init: pristine state with last=0 yields a full bucket on
     * first call, so the very first request is allowed. */
    uint16_t pristine_tokens = 0;
    uint64_t pristine_last = 0;
    int first_call = lm_chat_rl_consume(&pristine_tokens, &pristine_last,
                                         123456, cap, refill);
    assert(first_call == 1);
    assert(pristine_last == 123456);
    assert(pristine_tokens == 19 * 10);  /* 20 cap, consumed 1 */
}

int main(void)
{
    printf("\n=== Chat History Storage Tests ===\n\n");

    TEST(test_append_1000_query_latest_50);
    TEST(test_query_before);
    TEST(test_query_after);
    TEST(test_query_range);
    TEST(test_query_empty);
    TEST(test_corrupt_truncate);
    TEST(test_tombstone);
    TEST(test_prune_by_count);
    TEST(test_prune_by_age);
    TEST(test_encryption_roundtrip);
    TEST(test_rate_limiter);

    printf("\n%d/%d tests passed\n\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
