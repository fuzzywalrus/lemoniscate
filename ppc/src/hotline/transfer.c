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
    memcpy(t, buf, HL_TRANSFER_HEADER_SIZE);
    return 0;
}

int hl_transfer_header_valid(const hl_transfer_header_t *t)
{
    return memcmp(t->protocol, HL_HTXF_PROTOCOL, 4) == 0;
}
