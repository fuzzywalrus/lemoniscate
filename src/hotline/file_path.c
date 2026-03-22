/*
 * file_path.c - Hotline protocol file path encoding/decoding
 *
 * Maps to: hotline/file_path.go
 */

#include "hotline/file_path.h"
#include <string.h>

/* Reject path components that could escape the file root */
int hl_is_safe_path_component(const char *name, uint8_t len)
{
    if (len == 0) return 0;
    /* Reject "." and ".." */
    if (len == 1 && name[0] == '.') return 0;
    if (len == 2 && name[0] == '.' && name[1] == '.') return 0;
    /* Reject components containing path separators */
    uint8_t i;
    for (i = 0; i < len; i++) {
        if (name[i] == '/' || name[i] == '\\' || name[i] == '\0') return 0;
    }
    return 1;
}

int hl_file_path_deserialize(hl_file_path_t *fp, const uint8_t *buf, size_t buf_len)
{
    /* Maps to: Go FilePath.Write() + fileItemScanner */
    memset(fp, 0, sizeof(*fp));

    if (buf_len < 2) return -1;

    uint16_t count = hl_read_u16(buf);
    if (count > HL_FILE_PATH_MAX_ITEMS) return -1;

    size_t offset = 2;
    uint16_t i;
    for (i = 0; i < count; i++) {
        if (offset + HL_FILE_PATH_ITEM_MIN_LEN > buf_len) return -1;

        /* Skip 2 reserved bytes, read 1 length byte */
        uint8_t name_len = buf[offset + 2];
        if (offset + 3 + name_len > buf_len) return -1;
        if (name_len > HL_FILE_PATH_NAME_MAX) return -1;

        /* Reject traversal attempts */
        if (!hl_is_safe_path_component((const char *)(buf + offset + 3), name_len))
            return -1;

        memcpy(fp->items[i].name, buf + offset + 3, name_len);
        fp->items[i].name[name_len] = '\0';
        fp->items[i].name_len = name_len;
        offset += 3 + name_len;
    }

    fp->item_count = count;
    return 0;
}

int hl_file_path_serialize(const hl_file_path_t *fp, uint8_t *buf, size_t buf_len)
{
    /* Maps to: Go EncodeFilePath() in files.go */
    size_t needed = 2; /* item count */
    uint16_t i;
    for (i = 0; i < fp->item_count; i++) {
        needed += 3 + fp->items[i].name_len; /* 2 reserved + 1 len + name */
    }
    if (buf_len < needed) return -1;

    hl_write_u16(buf, fp->item_count);
    size_t offset = 2;
    for (i = 0; i < fp->item_count; i++) {
        buf[offset] = 0x00;
        buf[offset + 1] = 0x00;
        buf[offset + 2] = fp->items[i].name_len;
        memcpy(buf + offset + 3, fp->items[i].name, fp->items[i].name_len);
        offset += 3 + fp->items[i].name_len;
    }

    return (int)needed;
}

int hl_file_path_to_platform(const hl_file_path_t *fp, const char *root,
                             char *out, size_t out_len)
{
    size_t pos = 0;

    if (root && root[0] != '\0') {
        size_t rlen = strlen(root);
        if (rlen >= out_len) return -1;
        memcpy(out, root, rlen);
        pos = rlen;
        if (pos > 0 && out[pos - 1] != '/') {
            if (pos >= out_len) return -1;
            out[pos++] = '/';
        }
    }

    uint16_t i;
    for (i = 0; i < fp->item_count; i++) {
        if (pos + fp->items[i].name_len + 1 >= out_len) return -1;
        memcpy(out + pos, fp->items[i].name, fp->items[i].name_len);
        pos += fp->items[i].name_len;
        if (i < fp->item_count - 1) {
            out[pos++] = '/';
        }
    }

    out[pos] = '\0';
    return 0;
}

int hl_file_path_from_string(hl_file_path_t *fp, const char *path)
{
    memset(fp, 0, sizeof(*fp));

    const char *p = path;
    if (*p == '/') p++; /* Skip leading slash */

    while (*p != '\0' && fp->item_count < HL_FILE_PATH_MAX_ITEMS) {
        const char *sep = strchr(p, '/');
        size_t seg_len = sep ? (size_t)(sep - p) : strlen(p);

        if (seg_len > 0 && seg_len <= HL_FILE_PATH_NAME_MAX) {
            memcpy(fp->items[fp->item_count].name, p, seg_len);
            fp->items[fp->item_count].name[seg_len] = '\0';
            fp->items[fp->item_count].name_len = (uint8_t)seg_len;
            fp->item_count++;
        }

        if (!sep) break;
        p = sep + 1;
    }

    return 0;
}
