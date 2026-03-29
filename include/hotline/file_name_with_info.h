/*
 * file_name_with_info.h - File listing entry with metadata
 *
 * Wire format: Header(20 bytes) + Name(variable)
 */

#ifndef HOTLINE_FILE_NAME_WITH_INFO_H
#define HOTLINE_FILE_NAME_WITH_INFO_H

#include "hotline/types.h"

#define HL_FNWI_HEADER_SIZE 20

typedef struct {
    uint8_t type[4];         /* Offset 0:  File type code */
    uint8_t creator[4];      /* Offset 4:  Creator code */
    uint8_t file_size[4];    /* Offset 8:  File size (BE uint32) */
    uint8_t rsvd[4];         /* Offset 12: Reserved */
    uint8_t name_script[2];  /* Offset 16: Name script */
    uint8_t name_size[2];    /* Offset 18: Name length (BE uint16) */
} hl_fnwi_header_t;

typedef struct {
    hl_fnwi_header_t header;
    uint8_t          name[256]; /* Variable length name */
    uint16_t         name_len;
} hl_file_name_with_info_t;

/* Serialize to wire format. Returns bytes written, -1 on error. */
int hl_fnwi_serialize(const hl_file_name_with_info_t *f, uint8_t *buf, size_t buf_len);

/* Deserialize from wire format. Returns bytes consumed, -1 on error. */
int hl_fnwi_deserialize(hl_file_name_with_info_t *f, const uint8_t *buf, size_t buf_len);

#endif /* HOTLINE_FILE_NAME_WITH_INFO_H */
