/*
 * files.h - File listing and size calculation operations
 */

#ifndef HOTLINE_FILES_H
#define HOTLINE_FILES_H

#include "hotline/types.h"
#include "hotline/field.h"
#include "hotline/file_name_with_info.h"

/*
 * hl_get_file_name_list - Read directory and build FileNameWithInfo fields.
 *
 * Reads dir_path, creates a FieldFileNameWithInfo for each entry.
 * Returns a malloc'd array of fields, caller frees (hl_field_free each).
 * *out_count is set to number of fields.
 */
hl_field_t *hl_get_file_name_list(const char *dir_path, int *out_count);

/*
 * hl_calc_total_size - Sum file sizes in a directory tree.
 * Returns the total as a BE uint32 in out[4].
 */
void hl_calc_total_size(const char *dir_path, uint8_t out[4]);

/*
 * hl_calc_item_count - Count non-hidden files in a directory.
 * Returns the count as BE uint16 in out[2].
 */
void hl_calc_item_count(const char *dir_path, uint8_t out[2]);

/* 64-bit variants for large file extension */
void hl_calc_total_size_64(const char *dir_path, uint8_t out[8]);
void hl_calc_item_count_64(const char *dir_path, uint8_t out[8]);

#endif /* HOTLINE_FILES_H */
