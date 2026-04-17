/*
 * test_runner.c - Unit tests for Phase 1: wire format foundation
 *
 * Simple assert-based tests. No external framework needed.
 * Mirrors Go test files: transaction_test.go, field_test.go, etc.
 */

#include "hotline/hotline.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0;
static int tests_passed = 0;

#define TEST(name) \
    do { \
        tests_run++; \
        printf("  %-50s ", #name); \
        name(); \
        tests_passed++; \
        printf("PASS\n"); \
    } while(0)

/* ===== Field tests ===== */

static void test_field_new_and_serialize(void)
{
    hl_field_t f;
    uint8_t data[] = {0x48, 0x65, 0x6C, 0x6C, 0x6F}; /* "Hello" */
    assert(hl_field_new(&f, FIELD_DATA, data, 5) == 0);

    assert(hl_type_eq(f.type, FIELD_DATA));
    assert(f.data_len == 5);
    assert(memcmp(f.data, data, 5) == 0);

    /* Serialize */
    uint8_t buf[64];
    int written = hl_field_serialize(&f, buf, sizeof(buf));
    assert(written == 9); /* 4 header + 5 data */

    /* Verify wire format: type(2) + size(2) + data(5) */
    assert(buf[0] == 0x00 && buf[1] == 0x65); /* FIELD_DATA */
    assert(buf[2] == 0x00 && buf[3] == 0x05); /* size = 5 */
    assert(memcmp(buf + 4, data, 5) == 0);

    hl_field_free(&f);
}

static void test_field_deserialize(void)
{
    /* Wire bytes: FIELD_USER_NAME(0x0066) + size(3) + "Bob" */
    uint8_t wire[] = {0x00, 0x66, 0x00, 0x03, 0x42, 0x6F, 0x62};

    hl_field_t f;
    int consumed = hl_field_deserialize(&f, wire, sizeof(wire));
    assert(consumed == 7);
    assert(hl_type_eq(f.type, FIELD_USER_NAME));
    assert(f.data_len == 3);
    assert(memcmp(f.data, "Bob", 3) == 0);

    hl_field_free(&f);
}

static void test_field_roundtrip(void)
{
    /* Create -> Serialize -> Deserialize -> Compare */
    hl_field_t f1, f2;
    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    assert(hl_field_new(&f1, FIELD_REF_NUM, data, 4) == 0);

    uint8_t buf[64];
    int written = hl_field_serialize(&f1, buf, sizeof(buf));
    assert(written == 8);

    int consumed = hl_field_deserialize(&f2, buf, (size_t)written);
    assert(consumed == 8);
    assert(hl_type_eq(f1.type, f2.type));
    assert(f1.data_len == f2.data_len);
    assert(memcmp(f1.data, f2.data, f1.data_len) == 0);

    hl_field_free(&f1);
    hl_field_free(&f2);
}

static void test_field_scan(void)
{
    /* Partial data — should request more */
    uint8_t partial[] = {0x00, 0x65, 0x00};
    assert(hl_field_scan(partial, 3) == 0);

    /* Complete field */
    uint8_t complete[] = {0x00, 0x65, 0x00, 0x02, 0xAA, 0xBB};
    assert(hl_field_scan(complete, 6) == 6);

    /* Field with trailing data */
    uint8_t extra[] = {0x00, 0x65, 0x00, 0x01, 0xFF, 0x00, 0x00};
    assert(hl_field_scan(extra, 7) == 5);
}

static void test_field_decode_int_2byte(void)
{
    hl_field_t f;
    uint8_t data[] = {0x00, 0x2A}; /* 42 */
    assert(hl_field_new(&f, FIELD_USER_ID, data, 2) == 0);

    assert(hl_field_decode_int(&f) == 42);
    hl_field_free(&f);
}

static void test_field_decode_int_4byte(void)
{
    /* Frogblast/Heildrun always send 4 bytes */
    hl_field_t f;
    uint8_t data[] = {0x00, 0x00, 0x01, 0x00}; /* 256 */
    assert(hl_field_new(&f, FIELD_USER_ID, data, 4) == 0);

    assert(hl_field_decode_int(&f) == 256);
    hl_field_free(&f);
}

static void test_field_obfuscated_string(void)
{
    /* "guest" obfuscated with 255-rotation */
    uint8_t cleartext[] = "guest";
    uint8_t obfuscated[5];
    size_t i;
    for (i = 0; i < 5; i++) obfuscated[i] = 255 - cleartext[i];

    hl_field_t f;
    assert(hl_field_new(&f, FIELD_USER_PASSWORD, obfuscated, 5) == 0);

    char decoded[16];
    hl_field_decode_obfuscated_string(&f, decoded, sizeof(decoded));
    assert(strcmp(decoded, "guest") == 0);

    hl_field_free(&f);
}

/* ===== Transaction tests ===== */

static void test_transaction_new(void)
{
    hl_field_t fields[1];
    hl_field_new(&fields[0], FIELD_DATA, (const uint8_t *)"Hi", 2);

    hl_transaction_t t;
    hl_client_id_t cid = {0x00, 0x01};
    assert(hl_transaction_new(&t, TRAN_CHAT_SEND, cid, fields, 1) == 0);

    assert(hl_type_eq(t.type, TRAN_CHAT_SEND));
    assert(t.field_count == 1);
    assert(t.fields[0].data_len == 2);

    /* ID should be non-zero (random) */
    assert(!(t.id[0] == 0 && t.id[1] == 0 && t.id[2] == 0 && t.id[3] == 0));

    hl_field_free(&fields[0]);
    hl_transaction_free(&t);
}

static void test_transaction_serialize_deserialize(void)
{
    /* Build a transaction with two fields */
    hl_field_t fields[2];
    hl_field_new(&fields[0], FIELD_DATA, (const uint8_t *)"Hello", 5);
    hl_field_new(&fields[1], FIELD_USER_NAME, (const uint8_t *)"Bob", 3);

    hl_transaction_t t1;
    hl_client_id_t cid = {0x00, 0x05};
    assert(hl_transaction_new(&t1, TRAN_CHAT_SEND, cid, fields, 2) == 0);

    /* Serialize */
    size_t wire_size = hl_transaction_wire_size(&t1);
    uint8_t *buf = (uint8_t *)malloc(wire_size);
    assert(buf != NULL);

    int written = hl_transaction_serialize(&t1, buf, wire_size);
    assert(written == (int)wire_size);

    /* Verify header structure */
    assert(buf[0] == 0);     /* flags */
    assert(buf[1] == 0);     /* is_reply */
    assert(buf[2] == 0x00 && buf[3] == 0x69); /* TRAN_CHAT_SEND = 105 */

    /* Deserialize */
    hl_transaction_t t2;
    int consumed = hl_transaction_deserialize(&t2, buf, (size_t)written);
    assert(consumed == written);

    assert(hl_type_eq(t1.type, t2.type));
    assert(memcmp(t1.id, t2.id, 4) == 0);
    assert(t2.field_count == 2);
    assert(t2.fields[0].data_len == 5);
    assert(memcmp(t2.fields[0].data, "Hello", 5) == 0);
    assert(t2.fields[1].data_len == 3);
    assert(memcmp(t2.fields[1].data, "Bob", 3) == 0);

    free(buf);
    hl_field_free(&fields[0]);
    hl_field_free(&fields[1]);
    hl_transaction_free(&t1);
    hl_transaction_free(&t2);
}

static void test_transaction_scan(void)
{
    /* Too small for header */
    uint8_t small[10] = {0};
    assert(hl_transaction_scan(small, 10) == 0);

    /* Build a minimal transaction header with TotalSize = 2 (just param count) */
    uint8_t header[22];
    memset(header, 0, 22);
    hl_write_u32(header + 12, 2); /* TotalSize = 2 */
    /* tran_len = 20 + 2 = 22 */
    assert(hl_transaction_scan(header, 22) == 22);

    /* Incomplete — need more data */
    assert(hl_transaction_scan(header, 21) == 0);
}

static void test_transaction_get_field(void)
{
    hl_field_t fields[2];
    hl_field_new(&fields[0], FIELD_DATA, (const uint8_t *)"test", 4);
    hl_field_new(&fields[1], FIELD_CHAT_ID, (const uint8_t *)"\x00\x00\x00\x01", 4);

    hl_transaction_t t;
    hl_client_id_t cid = {0, 0};
    hl_transaction_new(&t, TRAN_CHAT_SEND, cid, fields, 2);

    const hl_field_t *found = hl_transaction_get_field(&t, FIELD_CHAT_ID);
    assert(found != NULL);
    assert(found->data_len == 4);

    const hl_field_t *missing = hl_transaction_get_field(&t, FIELD_ERROR);
    assert(missing == NULL);

    hl_field_free(&fields[0]);
    hl_field_free(&fields[1]);
    hl_transaction_free(&t);
}

static void test_transaction_type_name(void)
{
    assert(strcmp(hl_transaction_type_name(TRAN_CHAT_SEND), "Send chat") == 0);
    assert(strcmp(hl_transaction_type_name(TRAN_KEEP_ALIVE), "Keepalive") == 0);
    assert(strcmp(hl_transaction_type_name(TRAN_LOGIN), "Log In") == 0);

    hl_tran_type_t unknown = {0xFF, 0xFF};
    assert(strcmp(hl_transaction_type_name(unknown), "Unknown") == 0);
}

/* ===== Handshake tests ===== */

static void test_handshake_parse_valid(void)
{
    hl_handshake_t h;
    assert(hl_handshake_parse(&h, HL_CLIENT_HANDSHAKE, 12) == 0);
    assert(hl_handshake_valid(&h));
}

static void test_handshake_parse_invalid(void)
{
    uint8_t bad[12] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                       0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    hl_handshake_t h;
    assert(hl_handshake_parse(&h, bad, 12) == 0);
    assert(!hl_handshake_valid(&h));
}

/* ===== User tests ===== */

static void test_user_roundtrip(void)
{
    hl_user_t u1;
    u1.id[0] = 0x00; u1.id[1] = 0x03;
    u1.icon[0] = 0x00; u1.icon[1] = 0x80;
    memset(u1.flags, 0, 2);
    strcpy(u1.name, "TestUser");
    u1.name_len = 8;

    uint8_t buf[64];
    int written = hl_user_serialize(&u1, buf, sizeof(buf));
    assert(written == 16); /* 8 header + 8 name */

    hl_user_t u2;
    int consumed = hl_user_deserialize(&u2, buf, (size_t)written);
    assert(consumed == 16);
    assert(memcmp(u1.id, u2.id, 2) == 0);
    assert(memcmp(u1.icon, u2.icon, 2) == 0);
    assert(u2.name_len == 8);
    assert(strcmp(u2.name, "TestUser") == 0);
}

static void test_user_flags(void)
{
    hl_user_flags_t flags = {0, 0};

    assert(!hl_user_flags_is_set(flags, HL_USER_FLAG_AWAY));
    hl_user_flags_set(flags, HL_USER_FLAG_AWAY, 1);
    assert(hl_user_flags_is_set(flags, HL_USER_FLAG_AWAY));

    hl_user_flags_set(flags, HL_USER_FLAG_ADMIN, 1);
    assert(hl_user_flags_is_set(flags, HL_USER_FLAG_ADMIN));
    assert(hl_user_flags_is_set(flags, HL_USER_FLAG_AWAY));

    hl_user_flags_set(flags, HL_USER_FLAG_AWAY, 0);
    assert(!hl_user_flags_is_set(flags, HL_USER_FLAG_AWAY));
    assert(hl_user_flags_is_set(flags, HL_USER_FLAG_ADMIN));
}

static void test_encode_string(void)
{
    uint8_t clear[] = "guest";
    uint8_t obfuscated[5];
    uint8_t back[5];

    hl_encode_string(clear, obfuscated, 5);
    /* 255-rotation: 'g'(103) -> 152, 'u'(117) -> 138, etc. */
    assert(obfuscated[0] == 152);

    hl_encode_string(obfuscated, back, 5);
    assert(memcmp(back, clear, 5) == 0);
}

/* ===== Access bitmap tests ===== */

static void test_access_bitmap(void)
{
    hl_access_bitmap_t bits;
    memset(bits, 0, sizeof(bits));

    assert(!hl_access_is_set(bits, ACCESS_DOWNLOAD_FILE));
    hl_access_set(bits, ACCESS_DOWNLOAD_FILE);
    assert(hl_access_is_set(bits, ACCESS_DOWNLOAD_FILE));

    /* Set a bit in a different byte */
    hl_access_set(bits, ACCESS_SEND_CHAT); /* bit 10 -> byte 1 */
    assert(hl_access_is_set(bits, ACCESS_SEND_CHAT));

    /* High bit */
    hl_access_set(bits, ACCESS_SEND_PRIV_MSG); /* bit 40 -> byte 5 */
    assert(hl_access_is_set(bits, ACCESS_SEND_PRIV_MSG));

    hl_access_clear(bits, ACCESS_DOWNLOAD_FILE);
    assert(!hl_access_is_set(bits, ACCESS_DOWNLOAD_FILE));
    assert(hl_access_is_set(bits, ACCESS_SEND_CHAT)); /* still set */
}

/* ===== Time conversion tests ===== */

static void test_time_roundtrip(void)
{
    time_t now = time(NULL);
    hl_time_t ht;
    hl_time_from_timet(ht, now);

    time_t back = hl_time_to_timet(ht);

    /* May differ by up to 3600s due to DST offset between Jan 1 and now.
     * This matches the Go behavior (time.Date with time.Local). */
    int diff = abs((int)(now - back));
    assert(diff <= 3600);
}

static void test_time_known_value(void)
{
    /* Test with a known date: 2024-01-15 12:00:00 (no DST ambiguity in January) */
    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = 2024 - 1900;
    tm.tm_mon  = 0;  /* January */
    tm.tm_mday = 15;
    tm.tm_hour = 12;
    tm.tm_isdst = -1;
    time_t t = mktime(&tm);

    hl_time_t ht;
    hl_time_from_timet(ht, t);

    /* Year should be 2024 */
    assert(hl_read_u16(ht) == 2024);

    /* Seconds: 14 days * 86400 + 12 hours * 3600 = 1252800 */
    assert(hl_read_u32(ht + 4) == 1252800);
}

/* ===== File resume data tests ===== */

static void test_file_resume_data_roundtrip(void)
{
    hl_fork_info_t forks[2];
    memcpy(forks[0].fork, FORK_TYPE_DATA, 4);
    hl_write_u32(forks[0].data_size, 1024);
    memset(forks[0].rsvd_a, 0, 4);
    memset(forks[0].rsvd_b, 0, 4);

    memcpy(forks[1].fork, FORK_TYPE_MACR, 4);
    hl_write_u32(forks[1].data_size, 256);
    memset(forks[1].rsvd_a, 0, 4);
    memset(forks[1].rsvd_b, 0, 4);

    hl_file_resume_data_t frd1;
    hl_file_resume_data_new(&frd1, forks, 2);

    uint8_t buf[128];
    int written = hl_file_resume_data_marshal(&frd1, buf, sizeof(buf));
    assert(written == 42 + 32); /* header + 2 fork infos */

    /* Verify RFLT magic */
    assert(memcmp(buf, FORMAT_RFLT, 4) == 0);

    hl_file_resume_data_t frd2;
    assert(hl_file_resume_data_unmarshal(&frd2, buf, (size_t)written) == 0);
    assert(frd2.fork_info_count == 2);
    assert(memcmp(frd2.fork_info[0].fork, FORK_TYPE_DATA, 4) == 0);
    assert(memcmp(frd2.fork_info[1].fork, FORK_TYPE_MACR, 4) == 0);
}

/* ===== Main ===== */

int main(void)
{
    printf("mobius-c Phase 1: Wire Format Tests\n");
    printf("====================================\n\n");

    printf("Field tests:\n");
    TEST(test_field_new_and_serialize);
    TEST(test_field_deserialize);
    TEST(test_field_roundtrip);
    TEST(test_field_scan);
    TEST(test_field_decode_int_2byte);
    TEST(test_field_decode_int_4byte);
    TEST(test_field_obfuscated_string);

    printf("\nTransaction tests:\n");
    TEST(test_transaction_new);
    TEST(test_transaction_serialize_deserialize);
    TEST(test_transaction_scan);
    TEST(test_transaction_get_field);
    TEST(test_transaction_type_name);

    printf("\nHandshake tests:\n");
    TEST(test_handshake_parse_valid);
    TEST(test_handshake_parse_invalid);

    printf("\nUser tests:\n");
    TEST(test_user_roundtrip);
    TEST(test_user_flags);
    TEST(test_encode_string);

    printf("\nAccess bitmap tests:\n");
    TEST(test_access_bitmap);

    printf("\nTime conversion tests:\n");
    TEST(test_time_roundtrip);
    TEST(test_time_known_value);

    printf("\nFile resume data tests:\n");
    TEST(test_file_resume_data_roundtrip);

    printf("\n====================================\n");
    printf("%d/%d tests passed\n", tests_passed, tests_run);

    return (tests_passed == tests_run) ? 0 : 1;
}
