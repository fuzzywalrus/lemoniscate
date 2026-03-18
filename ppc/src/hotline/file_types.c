/*
 * file_types.c - File type/creator code mappings
 *
 * Maps to: hotline/file_types.go
 */

#include "hotline/file_types.h"
#include <string.h>
#include <ctype.h>

const char HL_TYPE_FOLDER[5]    = "fldr";
const char HL_CREATOR_FOLDER[5] = "n/a ";

static const hl_file_type_entry_t file_type_table[] = {
    { ".sit",        "SIT!", "SIT!" },
    { ".pdf",        "PDF ", "CARO" },
    { ".gif",        "GIFf", "ogle" },
    { ".txt",        "TEXT", "ttxt" },
    { ".zip",        "ZIP ", "SITx" },
    { ".tgz",        "Gzip", "SITx" },
    { ".hqx",        "TEXT", "SITx" },
    { ".jpg",        "JPEG", "ogle" },
    { ".jpeg",       "JPEG", "ogle" },
    { ".img",        "rohd", "ddsk" },
    { ".sea",        "APPL", "aust" },
    { ".mov",        "MooV", "TVOD" },
    { ".incomplete", "HTft", "HTLC" },
    { NULL, "", "" }
};

static const hl_file_type_entry_t default_type = { NULL, "TEXT", "TTXT" };

const hl_file_type_entry_t *hl_file_type_from_filename(const char *filename)
{
    if (!filename) return &default_type;

    /* Find extension */
    const char *dot = strrchr(filename, '.');
    if (!dot) return &default_type;

    /* Case-insensitive comparison */
    const hl_file_type_entry_t *e;
    for (e = file_type_table; e->extension; e++) {
        if (strcasecmp(dot, e->extension) == 0) {
            return e;
        }
    }

    return &default_type;
}
