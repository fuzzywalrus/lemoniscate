/*
 * test_access.c - Tests for hl_access_classify (colored-nicknames)
 *
 * Verifies the three branches: admin exact match, guest exact match,
 * any divergence -> HL_CLASS_CUSTOM.
 */

#include "hotline/access.h"
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

static void test_admin_exact_match(void)
{
    hl_access_bitmap_t access;
    memcpy(access, ADMIN_ACCESS_TEMPLATE, sizeof(access));
    assert(hl_access_classify(access) == HL_CLASS_ADMIN);
}

static void test_guest_exact_match(void)
{
    hl_access_bitmap_t access;
    memcpy(access, GUEST_ACCESS_TEMPLATE, sizeof(access));
    assert(hl_access_classify(access) == HL_CLASS_GUEST);
}

static void test_empty_is_custom(void)
{
    hl_access_bitmap_t access = {0, 0, 0, 0, 0, 0, 0, 0};
    assert(hl_access_classify(access) == HL_CLASS_CUSTOM);
}

static void test_admin_minus_one_bit_is_custom(void)
{
    hl_access_bitmap_t access;
    memcpy(access, ADMIN_ACCESS_TEMPLATE, sizeof(access));
    /* Clear bit 0 (ACCESS_DELETE_FILE) — admin template with one bit off
     * should fall to CUSTOM. This is the fragility the design accepts. */
    hl_access_clear(access, 0);
    assert(hl_access_classify(access) == HL_CLASS_CUSTOM);
}

static void test_guest_plus_one_bit_is_custom(void)
{
    hl_access_bitmap_t access;
    memcpy(access, GUEST_ACCESS_TEMPLATE, sizeof(access));
    /* Add ACCESS_UPLOAD_FILE (bit 1) — a guest with upload power isn't
     * the canonical guest anymore. */
    hl_access_set(access, 1);
    assert(hl_access_classify(access) == HL_CLASS_CUSTOM);
}

static void test_all_bits_set_is_custom(void)
{
    /* All 64 bits — does NOT match ADMIN_ACCESS_TEMPLATE because that
     * template excludes bit 19 (unused) and bit 56 (chat history, not
     * in GUI's permission list). */
    hl_access_bitmap_t access = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    assert(hl_access_classify(access) == HL_CLASS_CUSTOM);
}

int main(void)
{
    printf("Running access tests:\n");
    TEST(test_admin_exact_match);
    TEST(test_guest_exact_match);
    TEST(test_empty_is_custom);
    TEST(test_admin_minus_one_bit_is_custom);
    TEST(test_guest_plus_one_bit_is_custom);
    TEST(test_all_bits_set_is_custom);
    printf("\n%d/%d tests passed.\n", tests_passed, tests_run);
    return (tests_passed == tests_run) ? 0 : 1;
}
