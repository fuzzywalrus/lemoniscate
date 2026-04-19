/*
 * test_account_color.c - Round-trip test for the Color key in account YAML
 *
 * Covers task 7.4 of openspec/changes/colored-nicknames. Creates a
 * temporary Users/ dir, writes accounts with and without Color via
 * the account manager, reloads a fresh manager against the same dir,
 * and asserts the nick_color field round-trips correctly.
 */

#include "mobius/yaml_account_manager.h"
#include "hotline/client_conn.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-55s ", #name); \
        name(); \
        tests_passed++; \
        printf("PASS\n"); \
    } while(0)

static const char *TMPDIR_PATH = "/tmp/colornicks_test";

static void setup_tmpdir(void)
{
    /* rm -rf and mkdir. */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s", TMPDIR_PATH, TMPDIR_PATH);
    int rc = system(cmd);
    (void)rc;
}

static hl_account_t make_account(const char *login, uint32_t nick_color)
{
    hl_account_t a;
    memset(&a, 0, sizeof(a));
    strncpy(a.login, login, sizeof(a.login) - 1);
    strncpy(a.name, login, sizeof(a.name) - 1);
    strncpy(a.password, "$2b$dummy", sizeof(a.password) - 1);
    /* Give it DownloadFile access so the permission block is non-empty */
    hl_access_set(a.access, 2 /* ACCESS_DOWNLOAD_FILE */);
    a.nick_color = nick_color;
    return a;
}

static void test_round_trip_with_color(void)
{
    setup_tmpdir();
    hl_account_mgr_t *mgr = mobius_yaml_account_mgr_new(TMPDIR_PATH);
    assert(mgr);

    hl_account_t a = make_account("golduser", 0x00FFD700u);
    assert(mgr->vt->create(mgr, &a) == 0);
    mobius_yaml_account_mgr_free(mgr);

    /* Reload from disk. */
    mgr = mobius_yaml_account_mgr_new(TMPDIR_PATH);
    assert(mgr);
    hl_account_t *loaded = mgr->vt->get(mgr, "golduser");
    assert(loaded);
    assert(loaded->nick_color == 0x00FFD700u);
    mobius_yaml_account_mgr_free(mgr);
}

static void test_round_trip_without_color(void)
{
    setup_tmpdir();
    hl_account_mgr_t *mgr = mobius_yaml_account_mgr_new(TMPDIR_PATH);
    assert(mgr);

    hl_account_t a = make_account("plainuser", 0u);
    assert(mgr->vt->create(mgr, &a) == 0);
    mobius_yaml_account_mgr_free(mgr);

    mgr = mobius_yaml_account_mgr_new(TMPDIR_PATH);
    assert(mgr);
    hl_account_t *loaded = mgr->vt->get(mgr, "plainuser");
    assert(loaded);
    /* No Color key should load as 0 (absent). */
    assert(loaded->nick_color == 0u);
    mobius_yaml_account_mgr_free(mgr);
}

static void test_color_absent_key_not_written(void)
{
    setup_tmpdir();
    hl_account_mgr_t *mgr = mobius_yaml_account_mgr_new(TMPDIR_PATH);
    assert(mgr);

    hl_account_t a = make_account("plainuser2", 0u);
    assert(mgr->vt->create(mgr, &a) == 0);
    mobius_yaml_account_mgr_free(mgr);

    /* Spot-check the written YAML — "Color:" should not appear. */
    char path[512];
    snprintf(path, sizeof(path), "%s/plainuser2.yaml", TMPDIR_PATH);
    FILE *f = fopen(path, "r");
    assert(f);
    char buf[4096] = {0};
    size_t n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    assert(n > 0);
    assert(strstr(buf, "Color:") == NULL);
}

static void test_color_value_exactly_three_channels(void)
{
    setup_tmpdir();
    hl_account_mgr_t *mgr = mobius_yaml_account_mgr_new(TMPDIR_PATH);
    assert(mgr);

    /* Alpha-ish high byte should be stripped at write/read. We only store
     * and serialize the low 24 bits (0x00RRGGBB). */
    hl_account_t a = make_account("highbyte", 0xFF123456u);
    assert(mgr->vt->create(mgr, &a) == 0);
    mobius_yaml_account_mgr_free(mgr);

    mgr = mobius_yaml_account_mgr_new(TMPDIR_PATH);
    assert(mgr);
    hl_account_t *loaded = mgr->vt->get(mgr, "highbyte");
    assert(loaded);
    assert(loaded->nick_color == 0x00123456u);
    mobius_yaml_account_mgr_free(mgr);
}

int main(void)
{
    printf("Running account color round-trip tests:\n");

    TEST(test_round_trip_with_color);
    TEST(test_round_trip_without_color);
    TEST(test_color_absent_key_not_written);
    TEST(test_color_value_exactly_three_channels);

    /* Cleanup */
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TMPDIR_PATH);
    int rc = system(cmd);
    (void)rc;

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
