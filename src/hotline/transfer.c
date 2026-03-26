/*
 * transfer.c - HTXF file transfer header
 *
 * Maps to: hotline/transfer.go
 */

#include "hotline/transfer.h"
#include <string.h>

int hl_transfer_header_parse(hl_transfer_header_t *t, const uint8_t *buf, size_t buf_len)
{
    if (buf_len < HL_TRANSFER_HEADER_SIZE) return -1;
    memset(t, 0, sizeof(*t));
    memcpy(t->protocol, buf, 4);
    memcpy(t->reference_number, buf + 4, 4);
    memcpy(t->data_size, buf + 8, 4);
    memcpy(t->flags, buf + 12, 4);

    /* If HTXF_FLAG_SIZE64 is set and we have 24 bytes, read 64-bit size */
    uint32_t flags = hl_read_u32(t->flags);
    if ((flags & HL_HTXF_FLAG_SIZE64) && buf_len >= HL_TRANSFER_HEADER_SIZE_64) {
        memcpy(t->data_size_64, buf + 16, 8);
    }

    return 0;
}

int hl_transfer_header_valid(const hl_transfer_header_t *t)
{
    return memcmp(t->protocol, HL_HTXF_PROTOCOL, 4) == 0;
}
