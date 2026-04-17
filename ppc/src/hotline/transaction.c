/*
 * transaction.c - Hotline protocol transaction implementation
 *
 * Maps to: hotline/transaction.go
 */

#include "hotline/transaction.h"
#include <stdlib.h>
#include <string.h>

/* 32-bit overflow check for size_t arithmetic.
 * On 64-bit, uint32_t + 20 can never overflow size_t. */
static inline int size_would_overflow(uint32_t val, size_t addend)
{
    return (sizeof(size_t) <= sizeof(uint32_t)) &&
           ((size_t)val > SIZE_MAX - addend);
}

/* Generate a random uint32 for transaction IDs.
 * Maps to: Go rand.Uint32() in NewTransaction
 * arc4random() is available on Tiger and needs no seeding or file I/O. */
static uint32_t random_u32(void)
{
    return arc4random();
}

int hl_transaction_new(hl_transaction_t *t, const hl_tran_type_t type,
                       const hl_client_id_t client_id,
                       const hl_field_t *fields, uint16_t field_count)
{
    memset(t, 0, sizeof(*t));
    memcpy(t->type, type, 2);
    memcpy(t->client_id, client_id, 2);

    /* Random transaction ID */
    hl_write_u32(t->id, random_u32());

    t->field_count = field_count;
    if (field_count > 0) {
        t->fields = (hl_field_t *)calloc(field_count, sizeof(hl_field_t));
        if (!t->fields) return -1;

        uint16_t i;
        for (i = 0; i < field_count; i++) {
            if (hl_field_new(&t->fields[i], fields[i].type,
                             fields[i].data, fields[i].data_len) < 0) {
                /* Cleanup on failure */
                uint16_t j;
                for (j = 0; j < i; j++) hl_field_free(&t->fields[j]);
                free(t->fields);
                t->fields = NULL;
                return -1;
            }
        }
    }

    return 0;
}

int hl_transaction_deserialize(hl_transaction_t *t,
                               const uint8_t *buf, size_t buf_len)
{
    /* Maps to: Go Transaction.Write() */
    if (buf_len < HL_TRAN_HEADER_LEN) return -1;

    uint32_t total_size = hl_read_u32(buf + 12);

    if (size_would_overflow(total_size, 20)) return -1;

    size_t tran_len = 20 + total_size; /* 20 = header without ParamCount */

    if (buf_len < tran_len) return -1;

    uint16_t param_count = hl_read_u16(buf + 20);
    if (param_count > HL_TRAN_MAX_FIELDS) return -1;

    memset(t, 0, sizeof(*t));
    t->flags    = buf[0];
    t->is_reply = buf[1];
    memcpy(t->type,        buf + 2,  2);
    memcpy(t->id,          buf + 4,  4);
    memcpy(t->error_code,  buf + 8,  4);
    memcpy(t->total_size,  buf + 12, 4);
    memcpy(t->data_size,   buf + 16, 4);
    memcpy(t->param_count, buf + 20, 2);

    /* Parse fields */
    if (param_count > 0) {
        t->fields = (hl_field_t *)calloc(param_count, sizeof(hl_field_t));
        if (!t->fields) return -1;

        size_t offset = HL_TRAN_HEADER_LEN;
        uint16_t i;
        for (i = 0; i < param_count; i++) {
            int consumed = hl_field_deserialize(&t->fields[i],
                                                buf + offset,
                                                tran_len - offset);
            if (consumed < 0) {
                /* Cleanup */
                uint16_t j;
                for (j = 0; j < i; j++) hl_field_free(&t->fields[j]);
                free(t->fields);
                t->fields = NULL;
                return -1;
            }
            offset += (size_t)consumed;
        }
        t->field_count = param_count;
    }

    return (int)tran_len;
}

uint32_t hl_transaction_payload_size(const hl_transaction_t *t)
{
    /* Maps to: Go Transaction.Size()
     * Payload = 2 (param count) + sum of field wire sizes */
    uint32_t size = 2; /* param count bytes */
    uint16_t i;
    for (i = 0; i < t->field_count; i++) {
        size += (uint32_t)hl_field_wire_size(&t->fields[i]);
    }
    return size;
}

size_t hl_transaction_wire_size(const hl_transaction_t *t)
{
    return 20 + hl_transaction_payload_size(t);
}

int hl_transaction_serialize(const hl_transaction_t *t,
                             uint8_t *buf, size_t buf_len)
{
    /* Maps to: Go Transaction.Read() */
    size_t needed = hl_transaction_wire_size(t);
    if (buf_len < needed) return -1;

    uint32_t payload = hl_transaction_payload_size(t);

    buf[0] = t->flags;
    buf[1] = t->is_reply;
    memcpy(buf + 2,  t->type, 2);
    memcpy(buf + 4,  t->id, 4);
    memcpy(buf + 8,  t->error_code, 4);
    hl_write_u32(buf + 12, payload);  /* TotalSize */
    hl_write_u32(buf + 16, payload);  /* DataSize (same as TotalSize per Go) */
    hl_write_u16(buf + 20, t->field_count);

    size_t offset = HL_TRAN_HEADER_LEN;
    uint16_t i;
    for (i = 0; i < t->field_count; i++) {
        int written = hl_field_serialize(&t->fields[i],
                                         buf + offset,
                                         buf_len - offset);
        if (written < 0) return -1;
        offset += (size_t)written;
    }

    return (int)needed;
}

int hl_transaction_scan(const uint8_t *buf, size_t buf_len)
{
    /* Maps to: Go transactionScanner */
    if (buf_len < 16) return 0; /* need at least bytes 12-16 for TotalSize */

    uint32_t total_size = hl_read_u32(buf + 12);

    if (size_would_overflow(total_size, 20)) return -1;

    size_t tran_len = 20 + total_size;

    if (tran_len > buf_len) return 0; /* need more data */

    return (int)tran_len;
}

const hl_field_t *hl_transaction_get_field(const hl_transaction_t *t,
                                           const hl_field_type_t type)
{
    /* Maps to: Go Transaction.GetField() */
    uint16_t i;
    for (i = 0; i < t->field_count; i++) {
        if (hl_type_eq(t->fields[i].type, type)) {
            return &t->fields[i];
        }
    }
    return NULL;
}

void hl_transaction_free(hl_transaction_t *t)
{
    if (t->fields) {
        uint16_t i;
        for (i = 0; i < t->field_count; i++) {
            hl_field_free(&t->fields[i]);
        }
        free(t->fields);
        t->fields = NULL;
    }
    t->field_count = 0;
}

/* Maps to: Go tranTypeNames map */
const char *hl_transaction_type_name(const hl_tran_type_t type)
{
    uint16_t code = hl_type_to_u16(type);
    switch (code) {
        case   0: return "Error";
        case 101: return "Get messages";
        case 102: return "New message";
        case 103: return "Post to message board";
        case 104: return "Server Message";
        case 105: return "Send chat";
        case 106: return "Receive chat";
        case 107: return "Log In";
        case 108: return "Send message";
        case 109: return "Show agreement";
        case 110: return "Disconnect user";
        case 111: return "Disconnect message";
        case 112: return "Invite to new chat";
        case 113: return "Invite to chat";
        case 114: return "Decline chat invite";
        case 115: return "Join chat";
        case 116: return "Leave chat";
        case 117: return "User change (chat)";
        case 118: return "User left (chat)";
        case 119: return "Chat subject changed";
        case 120: return "Set chat subject";
        case 121: return "Accept agreement";
        case 122: return "Server banner";
        case 200: return "Get file list";
        case 202: return "Download file";
        case 203: return "Upload file";
        case 204: return "Delete file";
        case 205: return "Create folder";
        case 206: return "Get file info";
        case 207: return "Set file info";
        case 208: return "Move file";
        case 209: return "Make file alias";
        case 210: return "Download folder";
        case 211: return "Download info";
        case 212: return "Download banner";
        case 213: return "Upload folder";
        case 300: return "Get user list";
        case 301: return "User change";
        case 302: return "User left";
        case 303: return "Get client info";
        case 304: return "Set client user info";
        case 348: return "List user accounts";
        case 349: return "Update user account";
        case 350: return "Create user account";
        case 351: return "Delete user account";
        case 352: return "Get user";
        case 353: return "Set user";
        case 354: return "User access";
        case 355: return "Send broadcast";
        case 370: return "Get news categories";
        case 371: return "Get news article list";
        case 380: return "Delete news item";
        case 381: return "Create news bundle";
        case 382: return "Create news category";
        case 400: return "Get news article";
        case 410: return "Create news article";
        case 411: return "Delete news article";
        case 500: return "Keepalive";
        case 700: return "Get chat history";
        default:  return "Unknown";
    }
}
