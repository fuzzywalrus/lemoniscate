/*
 * test_chat_history.c - Unit tests for chat history storage.
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
#include <unistd.h>

static char g_test_dir[1024];

static void setup_dir(void)
{
    char cmd[1200];
    snprintf(g_test_dir, sizeof(g_test_dir),
             "/tmp/lm_chat_test_%d", (int)getpid());
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_test_dir);
    (void)system(cmd);
    if (mkdir(g_test_dir, 0755) != 0 && errno != EEXIST) {
        fprintf(stderr, "mkdir %s failed\n", g_test_dir);
        exit(1);
    }
}

static void teardown_dir(void)
{
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", g_test_dir);
    (void)system(cmd);
}

static lm_chat_history_t *open_default(void)
{
    lm_chat_history_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.enabled = 1;
    return lm_chat_history_open(g_test_dir, &cfg);
}

static void test_latest_query(void)
{
    lm_chat_history_t *h;
    lm_chat_entry_t *entries = NULL;
    size_t n = 0;
    uint8_t has_more = 0;
    int i;

    setup_dir();
    h = open_default();
    assert(h);

    for (i = 1; i <= 100; i++) {
        char body[32];
        snprintf(body, sizeof(body), "msg #%d", i);
        assert(lm_chat_history_append(h, 0, 0, 128, "user", body) == (uint64_t)i);
    }

    assert(lm_chat_history_query(h, 0, 0, 0, 20, &entries, &n, &has_more) == 0);
    assert(n == 20);
    assert(has_more == 1);
    assert(entries[0].id == 81);
    assert(entries[19].id == 100);

    lm_chat_history_entries_free(entries);
    lm_chat_history_close(h);
    teardown_dir();
}

static void test_before_after_queries(void)
{
    lm_chat_history_t *h;
    lm_chat_entry_t *entries = NULL;
    size_t n = 0;
    uint8_t has_more = 0;
    int i;

    setup_dir();
    h = open_default();
    assert(h);

    for (i = 1; i <= 50; i++) {
        char body[16];
        snprintf(body, sizeof(body), "%d", i);
        assert(lm_chat_history_append(h, 0, 0, 0, "u", body) == (uint64_t)i);
    }

    assert(lm_chat_history_query(h, 0, 30, 0, 10, &entries, &n, &has_more) == 0);
    assert(n == 10);
    assert(has_more == 1);
    assert(entries[0].id == 20);
    assert(entries[9].id == 29);
    lm_chat_history_entries_free(entries);

    entries = NULL;
    n = 0;
    has_more = 0;
    assert(lm_chat_history_query(h, 0, 0, 30, 10, &entries, &n, &has_more) == 0);
    assert(n == 10);
    assert(has_more == 1);
    assert(entries[0].id == 31);
    assert(entries[9].id == 40);

    lm_chat_history_entries_free(entries);
    lm_chat_history_close(h);
    teardown_dir();
}

static void test_persistence_and_tombstone(void)
{
    lm_chat_history_t *h;
    lm_chat_entry_t *entries = NULL;
    size_t n = 0;
    uint8_t has_more = 0;
    uint64_t id1;
    uint64_t id2;

    setup_dir();
    h = open_default();
    assert(h);
    id1 = lm_chat_history_append(h, 0, 0, 100, "alice", "one");
    id2 = lm_chat_history_append(h, 0, 0, 100, "bob", "two");
    assert(id1 == 1);
    assert(id2 == 2);
    assert(lm_chat_history_tombstone(h, id1) == 0);
    lm_chat_history_close(h);

    h = open_default();
    assert(h);
    assert(lm_chat_history_count(h, 0) == 2);
    assert(lm_chat_history_query(h, 0, 0, 0, 10, &entries, &n, &has_more) == 0);
    assert(n == 2);
    assert(entries[0].id == 1);
    assert(entries[0].flags & HL_CHAT_FLAG_IS_DELETED);
    assert(entries[0].body[0] == '\0');
    assert(strcmp(entries[1].body, "two") == 0);

    lm_chat_history_entries_free(entries);
    lm_chat_history_close(h);
    teardown_dir();
}

static void test_prune_and_rate_limit(void)
{
    lm_chat_history_t *h;
    lm_chat_history_config_t cfg;
    lm_chat_entry_t *entries = NULL;
    size_t n = 0;
    uint8_t has_more = 0;
    int i;
    uint16_t tokens = 0;
    uint64_t last_ms = 0;

    setup_dir();
    memset(&cfg, 0, sizeof(cfg));
    cfg.enabled = 1;
    cfg.max_msgs = 10;
    h = lm_chat_history_open(g_test_dir, &cfg);
    assert(h);

    for (i = 1; i <= 25; i++) {
        char body[16];
        snprintf(body, sizeof(body), "%d", i);
        lm_chat_history_append(h, 0, 0, 0, "u", body);
    }
    assert(lm_chat_history_prune(h) == 0);
    assert(lm_chat_history_count(h, 0) <= 10);
    assert(lm_chat_history_query(h, 0, 0, 0, 20, &entries, &n, &has_more) == 0);
    assert(n > 0);
    assert(entries[0].id >= 16);
    lm_chat_history_entries_free(entries);
    lm_chat_history_close(h);
    teardown_dir();

    assert(lm_chat_rl_consume(&tokens, &last_ms, 1000, 2, 1) == 1);
    assert(lm_chat_rl_consume(&tokens, &last_ms, 1001, 2, 1) == 1);
    assert(lm_chat_rl_consume(&tokens, &last_ms, 1002, 2, 1) == 0);
    assert(lm_chat_rl_consume(&tokens, &last_ms, 2002, 2, 1) == 1);
}

int main(void)
{
    test_latest_query();
    test_before_after_queries();
    test_persistence_and_tombstone();
    test_prune_and_rate_limit();
    printf("test_chat_history: PASS\n");
    return 0;
}
