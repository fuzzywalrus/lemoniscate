/*
 * transfer.h - HTXF file transfer header
 *
 * Maps to: hotline/transfer.go
 *
 * Legacy wire format (16 bytes):
 *   Protocol(4 "HTXF") + RefNum(4) + DataSize(4) + Reserved(4)
 *
 * Large-file wire format (24 bytes, when HTXF_FLAG_SIZE64 set):
 *   Protocol(4 "HTXF") + RefNum(4) + DataSize32(4) + Flags(4) + DataSize64(8)
 */

#ifndef HOTLINE_TRANSFER_H
#define HOTLINE_TRANSFER_H

#include "hotline/types.h"

#define HL_TRANSFER_HEADER_SIZE    16
#define HL_TRANSFER_HEADER_SIZE_64 24

/* HTXF flags (bytes 12-15 when large-file extension is active) */
#define HL_HTXF_FLAG_LARGE_FILE  0x00000001  /* Large-file mode active */
#define HL_HTXF_FLAG_SIZE64      0x00000002  /* 8-byte length follows header */

static const uint8_t HL_HTXF_PROTOCOL[4] = {0x48, 0x54, 0x58, 0x46}; /* "HTXF" */

typedef struct {
    uint8_t protocol[4];        /* "HTXF" */
    uint8_t reference_number[4];/* Transfer reference */
    uint8_t data_size[4];       /* BE uint32 (legacy; 0 if > 4GiB) */
    uint8_t flags[4];           /* BE uint32 flags (was reserved/zeros) */
    uint8_t data_size_64[8];    /* BE uint64 (only when HTXF_FLAG_SIZE64) */
} hl_transfer_header_t;

/* Parse a transfer header from 16+ bytes. Returns 0 on success, -1 on error. */
int hl_transfer_header_parse(hl_transfer_header_t *t, const uint8_t *buf, size_t buf_len);

/* Validate that protocol field is "HTXF". Returns 1 if valid, 0 if not. */
int hl_transfer_header_valid(const hl_transfer_header_t *t);

#endif /* HOTLINE_TRANSFER_H */
