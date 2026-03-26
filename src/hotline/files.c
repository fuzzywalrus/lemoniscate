/*
 * files.c - File listing and size calculation
 *
 * Maps to: hotline/files.go
 */

#include "hotline/files.h"
#include "hotline/file_types.h"
#include <stdio.h>
#include <dirent.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <string.h>

#define MAX_DIR_ENTRIES 1024

hl_field_t *hl_get_file_name_list(const char *dir_path, int *out_count)
{
    /* Maps to: Go GetFileNameList() */
    DIR *dir = opendir(dir_path);
    if (!dir) {
        *out_count = 0;
        return NULL;
    }

    hl_field_t *fields = (hl_field_t *)calloc(MAX_DIR_ENTRIES, sizeof(hl_field_t));
    if (!fields) {
        closedir(dir);
        *out_count = 0;
        return NULL;
    }

    int count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL && count < MAX_DIR_ENTRIES) {
        /* Skip hidden files and . / .. */
        if (entry->d_name[0] == '.') continue;
        /* Skip sidecar files */
        if (strncmp(entry->d_name, ".info_", 6) == 0) continue;
        if (strncmp(entry->d_name, ".rsrc_", 6) == 0) continue;

        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        hl_file_name_with_info_t fnwi;
        memset(&fnwi, 0, sizeof(fnwi));

        /* Set name */
        fnwi.name_len = (uint16_t)strlen(entry->d_name);
        if (fnwi.name_len > 255) fnwi.name_len = 255;
        memcpy(fnwi.name, entry->d_name, fnwi.name_len);

        if (S_ISDIR(st.st_mode)) {
            /* Directory */
            memcpy(fnwi.header.type, HL_TYPE_FOLDER, 4);
            memcpy(fnwi.header.creator, HL_CREATOR_FOLDER, 4);

            /* Count items in subdirectory */
            DIR *subdir = opendir(full_path);
            if (subdir) {
                int item_count = 0;
                struct dirent *sub;
                while ((sub = readdir(subdir)) != NULL) {
                    if (sub->d_name[0] != '.') item_count++;
                }
                closedir(subdir);
                hl_write_u32(fnwi.header.file_size, (uint32_t)item_count);
            }
        } else {
            /* Regular file */
            const hl_file_type_entry_t *ft = hl_file_type_from_filename(entry->d_name);
            memcpy(fnwi.header.type, ft->type, 4);
            memcpy(fnwi.header.creator, ft->creator, 4);

            /* File size (capped at 4GiB) */
            uint32_t fsize = (st.st_size > 0xFFFFFFFF) ? 0xFFFFFFFF : (uint32_t)st.st_size;
            hl_write_u32(fnwi.header.file_size, fsize);
        }

        /* Serialize to a field */
        uint8_t buf[512];
        int written = hl_fnwi_serialize(&fnwi, buf, sizeof(buf));
        if (written > 0) {
            hl_field_new(&fields[count], FIELD_FILE_NAME_WITH_INFO,
                         buf, (uint16_t)written);
            count++;
        }
    }

    closedir(dir);
    *out_count = count;

    if (count == 0) {
        free(fields);
        return NULL;
    }

    return fields;
}

#define HL_MAX_RECURSE_DEPTH 64

static uint32_t calc_total_size_r(const char *dir_path, int depth)
{
    if (depth > HL_MAX_RECURSE_DEPTH) return 0;

    DIR *dir = opendir(dir_path);
    if (!dir) return 0;

    uint32_t total = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            total += calc_total_size_r(full_path, depth + 1);
        } else {
            total += (uint32_t)st.st_size;
        }
    }

    closedir(dir);
    return total;
}

void hl_calc_total_size(const char *dir_path, uint8_t out[4])
{
    hl_write_u32(out, calc_total_size_r(dir_path, 0));
}

/* 64-bit variant for large file support */
static uint64_t calc_total_size_r64(const char *dir_path, int depth)
{
    if (depth > HL_MAX_RECURSE_DEPTH) return 0;

    DIR *dir = opendir(dir_path);
    if (!dir) return 0;

    uint64_t total = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;

        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) != 0) continue;

        if (S_ISDIR(st.st_mode)) {
            total += calc_total_size_r64(full_path, depth + 1);
        } else {
            total += (uint64_t)st.st_size;
        }
    }

    closedir(dir);
    return total;
}

void hl_calc_total_size_64(const char *dir_path, uint8_t out[8])
{
    hl_write_u64(out, calc_total_size_r64(dir_path, 0));
}

static uint16_t calc_item_count_r(const char *dir_path, int depth)
{
    if (depth > HL_MAX_RECURSE_DEPTH) return 0;

    DIR *dir = opendir(dir_path);
    if (!dir) return 0;

    uint16_t count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        count++;

        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            count += calc_item_count_r(full_path, depth + 1);
        }
    }

    closedir(dir);
    return count;
}

void hl_calc_item_count(const char *dir_path, uint8_t out[2])
{
    /* Maps to: Go CalcItemCount() — recursive, returns count-1 */
    uint16_t count = calc_item_count_r(dir_path, 0);
    if (count > 0) count--; /* Subtract root */
    hl_write_u16(out, count);
}

/* 64-bit variant for large file support */
static uint64_t calc_item_count_r64(const char *dir_path, int depth)
{
    if (depth > HL_MAX_RECURSE_DEPTH) return 0;

    DIR *dir = opendir(dir_path);
    if (!dir) return 0;

    uint64_t count = 0;
    struct dirent *entry;

    while ((entry = readdir(dir)) != NULL) {
        if (entry->d_name[0] == '.') continue;
        count++;

        char full_path[2048];
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);

        struct stat st;
        if (stat(full_path, &st) == 0 && S_ISDIR(st.st_mode)) {
            count += calc_item_count_r64(full_path, depth + 1);
        }
    }

    closedir(dir);
    return count;
}

void hl_calc_item_count_64(const char *dir_path, uint8_t out[8])
{
    uint64_t count = calc_item_count_r64(dir_path, 0);
    if (count > 0) count--;
    hl_write_u64(out, count);
}
