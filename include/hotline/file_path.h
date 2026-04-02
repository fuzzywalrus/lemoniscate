/*
 * file_path.h - Hotline protocol file path encoding/decoding
 *
 * Wire format for a path:
 *   ItemCount(2) + { Reserved(2) + Len(1) + Name(variable) }...
 */

#ifndef HOTLINE_FILE_PATH_H
#define HOTLINE_FILE_PATH_H

#include "hotline/types.h"

#define HL_FILE_PATH_ITEM_MIN_LEN 3
#define HL_FILE_PATH_MAX_ITEMS    32
#define HL_FILE_PATH_NAME_MAX     255

typedef struct {
    uint8_t  name[HL_FILE_PATH_NAME_MAX + 1];
    uint8_t  name_len;
} hl_file_path_item_t;

typedef struct {
    hl_file_path_item_t items[HL_FILE_PATH_MAX_ITEMS];
    uint16_t            item_count;
} hl_file_path_t;

/*
 * hl_file_path_deserialize - Parse a file path from wire bytes.
 * Returns 0 on success, -1 on error.
 */
int hl_file_path_deserialize(hl_file_path_t *fp, const uint8_t *buf, size_t buf_len);

/*
 * hl_file_path_serialize - Encode a file path to wire bytes.
 * Returns bytes written, or -1 on error.
 */
int hl_file_path_serialize(const hl_file_path_t *fp, uint8_t *buf, size_t buf_len);

/*
 * hl_file_path_to_platform - Convert file path to a platform path string.
 * Joins items with '/' separator, prepends root.
 * out must be at least out_len bytes.
 * Returns 0 on success, -1 on error.
 */
int hl_file_path_to_platform(const hl_file_path_t *fp, const char *root,
                             char *out, size_t out_len);

/*
 * hl_file_path_from_string - Build a file path from a "/" separated string.
 */
int hl_file_path_from_string(hl_file_path_t *fp, const char *path);

/*
 * hl_is_safe_path_component - Reject path components that could escape the
 * file root (".", "..", or containing '/' or '\\').
 * Returns 1 if safe, 0 if unsafe.
 */
int hl_is_safe_path_component(const char *name, uint8_t len);

#endif /* HOTLINE_FILE_PATH_H */
