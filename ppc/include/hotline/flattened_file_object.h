/*
 * flattened_file_object.h - FILP format for file transfers
 *
 * Maps to: hotline/flattened_file_object.go
 *
 * Layout: FlatFileHeader(24) + InfoForkHeader(16) + InfoFork(72+) +
 *         DataForkHeader(16) + [optional RsrcForkHeader(16)]
 */

#ifndef HOTLINE_FLATTENED_FILE_OBJECT_H
#define HOTLINE_FLATTENED_FILE_OBJECT_H

#include "hotline/types.h"

#define HL_FLAT_FILE_HEADER_SIZE 24
#define HL_FORK_HEADER_SIZE      16
#define HL_INFO_FORK_FIXED_SIZE  72

/* FlatFileHeader — maps to Go FlatFileHeader */
typedef struct {
    uint8_t format[4];      /* "FILP" */
    uint8_t version[2];     /* {0x00, 0x01} */
    uint8_t rsvd[16];       /* zeros */
    uint8_t fork_count[2];  /* 2 or 3 (with resource fork) */
} hl_flat_file_header_t;

/* FlatFileForkHeader — maps to Go FlatFileForkHeader */
typedef struct {
    uint8_t fork_type[4];        /* "INFO", "DATA", or "MACR" */
    uint8_t compression_type[4]; /* zeros */
    uint8_t rsvd[4];            /* zeros */
    uint8_t data_size[4];       /* BE uint32 */
} hl_flat_fork_header_t;

/* FlatFileInformationFork — maps to Go FlatFileInformationFork */
typedef struct {
    uint8_t  platform[4];          /* "AMAC" or "MWIN" */
    uint8_t  type_signature[4];    /* File type code */
    uint8_t  creator_signature[4]; /* Creator code */
    uint8_t  flags[4];
    uint8_t  platform_flags[4];    /* Typically {0,0,1,0} */
    uint8_t  rsvd[32];
    uint8_t  create_date[8];       /* Hotline time format */
    uint8_t  modify_date[8];
    uint8_t  name_script[2];
    uint8_t  name_size[2];         /* BE uint16 */
    char     name[128];            /* Variable, max 128 */
    uint16_t name_len;             /* Convenience */
    uint8_t  comment_size[2];      /* BE uint16 */
    char     comment[256];         /* Variable */
    uint16_t comment_len;          /* Convenience */
} hl_info_fork_t;

/* Complete flattened file object */
typedef struct {
    hl_flat_file_header_t  header;
    hl_flat_fork_header_t  info_fork_header;
    hl_info_fork_t         info_fork;
    hl_flat_fork_header_t  data_fork_header;
    hl_flat_fork_header_t  rsrc_fork_header;  /* Optional (if fork_count == 3) */
} hl_flattened_file_object_t;

/* Calculate info fork data size (72 + name_len + 2 + comment_len) */
uint32_t hl_info_fork_data_size(const hl_info_fork_t *info);

/* Serialize info fork to wire bytes. Returns bytes written, -1 on error. */
int hl_info_fork_serialize(const hl_info_fork_t *info, uint8_t *buf, size_t buf_len);

/* Deserialize info fork from wire bytes. Returns bytes consumed, -1 on error. */
int hl_info_fork_deserialize(hl_info_fork_t *info, const uint8_t *buf, size_t buf_len);

/* Serialize the full FFO header + info section. Returns bytes written, -1 on error. */
int hl_ffo_serialize_header(const hl_flattened_file_object_t *ffo,
                            uint8_t *buf, size_t buf_len);

/* Deserialize from wire (header + info fork + data fork header). Returns bytes consumed. */
int hl_ffo_deserialize(hl_flattened_file_object_t *ffo,
                       const uint8_t *buf, size_t buf_len);

#endif /* HOTLINE_FLATTENED_FILE_OBJECT_H */
