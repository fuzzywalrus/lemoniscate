/*
 * test_nick_color.c - Tests for hl_nick_color_resolve (colored-nicknames cascade)
 *
 * Covers the 5-tier cascade: per-account YAML -> client-sent (if honored)
 * -> admin class default -> guest class default -> 0xFFFFFFFF (no color).
 */

#include "hotline/client_conn.h"
#include "hotline/access.h"
#include "hotline/config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

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

/* --- Fixture helpers --- */

static void init_config(hl_config_t *cfg, int delivery, int honor,
                        uint32_t admin_color, uint32_t guest_color)
{
    memset(cfg, 0, sizeof(*cfg));
    cfg->colored_nicknames.delivery = delivery;
    cfg->colored_nicknames.honor_client_colors = honor;
    cfg->colored_nicknames.default_admin_color = admin_color;
    cfg->colored_nicknames.default_guest_color = guest_color;
}

static void init_admin_account(hl_account_t *a, uint32_t nick_color)
{
    memset(a, 0, sizeof(*a));
    memcpy(a->access, ADMIN_ACCESS_TEMPLATE, sizeof(a->access));
    a->nick_color = nick_color;
}

static void init_guest_account(hl_account_t *a, uint32_t nick_color)
{
    memset(a, 0, sizeof(*a));
    memcpy(a->access, GUEST_ACCESS_TEMPLATE, sizeof(a->access));
    a->nick_color = nick_color;
}

static void init_conn(hl_client_conn_t *c, hl_account_t *account,
                      uint32_t client_sent_color)
{
    memset(c, 0, sizeof(*c));
    c->account = account;
    c->nick_color = client_sent_color;
}

/* --- Tier 1: per-account YAML wins --- */

static void test_per_account_admin_wins_over_class_default(void)
{
    hl_config_t cfg;
    hl_account_t a;
    hl_client_conn_t c;

    init_config(&cfg, HL_CN_DELIVERY_AUTO, 0, 0x00999999u, 0x00777777u);
    init_admin_account(&a, 0x00FFD700u); /* per-account gold */
    init_conn(&c, &a, 0u);

    assert(hl_nick_color_resolve(&c, &cfg) == 0x00FFD700u);
}

static void test_per_account_wins_over_client_sent(void)
{
    hl_config_t cfg;
    hl_account_t a;
    hl_client_conn_t c;

    init_config(&cfg, HL_CN_DELIVERY_AUTO, 1, 0xFFFFFFFFu, 0xFFFFFFFFu);
    init_admin_account(&a, 0x00AABBCCu);
    init_conn(&c, &a, 0x00112233u); /* client sent, but per-account wins */

    assert(hl_nick_color_resolve(&c, &cfg) == 0x00AABBCCu);
}

/* --- Tier 2: client-sent (when honored) --- */

static void test_client_sent_honored(void)
{
    hl_config_t cfg;
    hl_account_t a;
    hl_client_conn_t c;

    init_config(&cfg, HL_CN_DELIVERY_AUTO, 1, 0xFFFFFFFFu, 0xFFFFFFFFu);
    init_guest_account(&a, 0u); /* no per-account */
    init_conn(&c, &a, 0x00112233u);

    assert(hl_nick_color_resolve(&c, &cfg) == 0x00112233u);
}

static void test_client_sent_ignored_when_honor_off(void)
{
    hl_config_t cfg;
    hl_account_t a;
    hl_client_conn_t c;

    /* Admin class with default gold; honor_client_colors off; client sent teal.
     * Cascade must skip client-sent and use the admin default. */
    init_config(&cfg, HL_CN_DELIVERY_AUTO, 0, 0x00FFD700u, 0xFFFFFFFFu);
    init_admin_account(&a, 0u);
    init_conn(&c, &a, 0x00008080u);

    assert(hl_nick_color_resolve(&c, &cfg) == 0x00FFD700u);
}

/* --- Tier 3/4: class defaults --- */

static void test_admin_class_default(void)
{
    hl_config_t cfg;
    hl_account_t a;
    hl_client_conn_t c;

    init_config(&cfg, HL_CN_DELIVERY_AUTO, 0, 0x00FFD700u, 0x00999999u);
    init_admin_account(&a, 0u);
    init_conn(&c, &a, 0u);

    assert(hl_nick_color_resolve(&c, &cfg) == 0x00FFD700u);
}

static void test_guest_class_default(void)
{
    hl_config_t cfg;
    hl_account_t a;
    hl_client_conn_t c;

    init_config(&cfg, HL_CN_DELIVERY_AUTO, 0, 0x00FFD700u, 0x00999999u);
    init_guest_account(&a, 0u);
    init_conn(&c, &a, 0u);

    assert(hl_nick_color_resolve(&c, &cfg) == 0x00999999u);
}

/* --- Tier 5: no color fallthrough --- */

static void test_custom_class_no_color(void)
{
    hl_config_t cfg;
    hl_account_t a;
    hl_client_conn_t c;

    init_config(&cfg, HL_CN_DELIVERY_AUTO, 0, 0x00FFD700u, 0x00999999u);
    memset(&a, 0, sizeof(a));
    /* Custom access — all zero bits — doesn't match either template. */
    a.nick_color = 0u;
    init_conn(&c, &a, 0u);

    assert(hl_nick_color_resolve(&c, &cfg) == 0xFFFFFFFFu);
}

static void test_admin_with_no_class_default(void)
{
    hl_config_t cfg;
    hl_account_t a;
    hl_client_conn_t c;

    init_config(&cfg, HL_CN_DELIVERY_AUTO, 0, 0xFFFFFFFFu, 0xFFFFFFFFu);
    init_admin_account(&a, 0u);
    init_conn(&c, &a, 0u);

    assert(hl_nick_color_resolve(&c, &cfg) == 0xFFFFFFFFu);
}

static void test_null_conn_or_cfg(void)
{
    hl_config_t cfg;
    hl_client_conn_t c;
    init_config(&cfg, HL_CN_DELIVERY_AUTO, 1, 0x00FFD700u, 0x00999999u);
    memset(&c, 0, sizeof(c));

    assert(hl_nick_color_resolve(NULL, &cfg) == 0xFFFFFFFFu);
    assert(hl_nick_color_resolve(&c, NULL) == 0xFFFFFFFFu);
}

static void test_no_account_falls_through(void)
{
    hl_config_t cfg;
    hl_client_conn_t c;
    /* Admin defaults set, but the connection has no account. Cascade should
     * fall to 0xFFFFFFFF (no class lookup possible). */
    init_config(&cfg, HL_CN_DELIVERY_AUTO, 0, 0x00FFD700u, 0x00999999u);
    memset(&c, 0, sizeof(c));
    c.account = NULL;

    assert(hl_nick_color_resolve(&c, &cfg) == 0xFFFFFFFFu);
}

int main(void)
{
    printf("Running nick color cascade tests:\n");

    TEST(test_per_account_admin_wins_over_class_default);
    TEST(test_per_account_wins_over_client_sent);
    TEST(test_client_sent_honored);
    TEST(test_client_sent_ignored_when_honor_off);
    TEST(test_admin_class_default);
    TEST(test_guest_class_default);
    TEST(test_custom_class_no_color);
    TEST(test_admin_with_no_class_default);
    TEST(test_null_conn_or_cfg);
    TEST(test_no_account_falls_through);

    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
