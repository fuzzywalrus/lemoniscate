/*
 * flattened_file_object.c - FILP format for file transfers
 *
 * Maps to: hotline/flattened_file_object.go
 */

#include "hotline/flattened_file_object.h"
#include <string.h>

uint32_t hl_info_fork_data_size(const hl_info_fork_t *info)
{
    /* Maps to: Go FlatFileInformationFork.DataSize()
     * = 72 (fixed) + name_len + 2 (comment_size field) + comment_len */
    return HL_INFO_FORK_FIXED_SIZE + info->name_len + 2 + info->comment_len;
}

int hl_info_fork_serialize(const hl_info_fork_t *info, uint8_t *buf, size_t buf_len)
{
    uint32_t data_size = hl_info_fork_data_size(info);
    if (buf_len < data_size) return -1;

    /* Fixed fields (72 bytes) */
    memcpy(buf,      info->platform, 4);
    memcpy(buf + 4,  info->type_signature, 4);
    memcpy(buf + 8,  info->creator_signature, 4);
    memcpy(buf + 12, info->flags, 4);
    memcpy(buf + 16, info->platform_flags, 4);
    memcpy(buf + 20, info->rsvd, 32);
    memcpy(buf + 52, info->create_date, 8);
    memcpy(buf + 60, info->modify_date, 8);
    memcpy(buf + 68, info->name_script, 2);
    hl_write_u16(buf + 70, info->name_len);

    /* Name */
    memcpy(buf + 72, info->name, info->name_len);

    /* Comment size + comment */
    size_t pos = 72 + info->name_len;
    hl_write_u16(buf + pos, info->comment_len);
    pos += 2;
    if (info->comment_len > 0) {
        memcpy(buf + pos, info->comment, info->comment_len);
    }

    return (int)data_size;
}

int hl_info_fork_deserialize(hl_info_fork_t *info, const uint8_t *buf, size_t buf_len)
{
    /* Maps to: Go FlatFileInformationFork.Write() */
    if (buf_len < HL_INFO_FORK_FIXED_SIZE) return -1;

    memset(info, 0, sizeof(*info));
    memcpy(info->platform, buf, 4);
    memcpy(info->type_signature, buf + 4, 4);
    memcpy(info->creator_signature, buf + 8, 4);
    memcpy(info->flags, buf + 12, 4);
    memcpy(info->platform_flags, buf + 16, 4);
    memcpy(info->rsvd, buf + 20, 32);
    memcpy(info->create_date, buf + 52, 8);
    memcpy(info->modify_date, buf + 60, 8);
    memcpy(info->name_script, buf + 68, 2);

    info->name_len = hl_read_u16(buf + 70);
    if (info->name_len > 128) return -1;
    if (buf_len < (size_t)(72 + info->name_len + 2)) return -1;

    memcpy(info->name, buf + 72, info->name_len);
    info->name[info->name_len] = '\0';

    size_t pos = 72 + info->name_len;
    info->comment_len = hl_read_u16(buf + pos);
    pos += 2;

    if (info->comment_len > 255) info->comment_len = 255;
    if (buf_len >= pos + info->comment_len) {
        memcpy(info->comment, buf + pos, info->comment_len);
        info->comment[info->comment_len] = '\0';
        pos += info->comment_len;
    }

    return (int)pos;
}

int hl_ffo_serialize_header(const hl_flattened_file_object_t *ffo,
                            uint8_t *buf, size_t buf_len)
{
    /* Serialize: header(24) + info_fork_header(16) + info_fork_data + data_fork_header(16) */
    uint32_t info_data_size = hl_info_fork_data_size(&ffo->info_fork);
    size_t needed = HL_FLAT_FILE_HEADER_SIZE + HL_FORK_HEADER_SIZE +
                    info_data_size + HL_FORK_HEADER_SIZE;
    if (buf_len < needed) return -1;

    size_t pos = 0;

    /* FlatFileHeader */
    memcpy(buf + pos, &ffo->header, HL_FLAT_FILE_HEADER_SIZE);
    pos += HL_FLAT_FILE_HEADER_SIZE;

    /* Info fork header */
    memcpy(buf + pos, &ffo->info_fork_header, HL_FORK_HEADER_SIZE);
    pos += HL_FORK_HEADER_SIZE;

    /* Info fork data */
    int written = hl_info_fork_serialize(&ffo->info_fork, buf + pos, buf_len - pos);
    if (written < 0) return -1;
    pos += (size_t)written;

    /* Data fork header */
    memcpy(buf + pos, &ffo->data_fork_header, HL_FORK_HEADER_SIZE);
    pos += HL_FORK_HEADER_SIZE;

    return (int)pos;
}

int hl_ffo_deserialize(hl_flattened_file_object_t *ffo,
                       const uint8_t *buf, size_t buf_len)
{
    memset(ffo, 0, sizeof(*ffo));

    size_t pos = 0;

    /* FlatFileHeader */
    if (buf_len < HL_FLAT_FILE_HEADER_SIZE) return -1;
    memcpy(&ffo->header, buf, HL_FLAT_FILE_HEADER_SIZE);
    pos += HL_FLAT_FILE_HEADER_SIZE;

    /* Info fork header */
    if (buf_len - pos < HL_FORK_HEADER_SIZE) return -1;
    memcpy(&ffo->info_fork_header, buf + pos, HL_FORK_HEADER_SIZE);
    pos += HL_FORK_HEADER_SIZE;

    /* Info fork data */
    uint32_t info_size = hl_read_u32(ffo->info_fork_header.data_size);
    if (buf_len - pos < info_size) return -1;
    int consumed = hl_info_fork_deserialize(&ffo->info_fork, buf + pos, info_size);
    if (consumed < 0) return -1;
    pos += info_size;

    /* Data fork header */
    if (buf_len - pos < HL_FORK_HEADER_SIZE) return -1;
    memcpy(&ffo->data_fork_header, buf + pos, HL_FORK_HEADER_SIZE);
    pos += HL_FORK_HEADER_SIZE;

    return (int)pos;
}
