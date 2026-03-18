/*
 * file_name_with_info.c - File listing entry with metadata
 *
 * Maps to: hotline/file_name_with_info.go
 */

#include "hotline/file_name_with_info.h"
#include <string.h>

int hl_fnwi_serialize(const hl_file_name_with_info_t *f, uint8_t *buf, size_t buf_len)
{
    size_t needed = HL_FNWI_HEADER_SIZE + f->name_len;
    if (buf_len < needed) return -1;

    memcpy(buf, &f->header, HL_FNWI_HEADER_SIZE);
    hl_write_u16(buf + 18, f->name_len);
    memcpy(buf + HL_FNWI_HEADER_SIZE, f->name, f->name_len);

    return (int)needed;
}

int hl_fnwi_deserialize(hl_file_name_with_info_t *f, const uint8_t *buf, size_t buf_len)
{
    if (buf_len < HL_FNWI_HEADER_SIZE) return -1;

    memcpy(&f->header, buf, HL_FNWI_HEADER_SIZE);
    f->name_len = hl_read_u16(buf + 18);

    if (buf_len < (size_t)(HL_FNWI_HEADER_SIZE + f->name_len)) return -1;
    if (f->name_len > 255) return -1;

    memcpy(f->name, buf + HL_FNWI_HEADER_SIZE, f->name_len);
    f->name[f->name_len] = '\0';

    return HL_FNWI_HEADER_SIZE + f->name_len;
}
