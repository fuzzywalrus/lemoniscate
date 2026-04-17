/*
 * file_types.h - File type/creator code mappings
 *
 * Maps to: hotline/file_types.go
 */

#ifndef HOTLINE_FILE_TYPES_H
#define HOTLINE_FILE_TYPES_H

#include <stddef.h>

typedef struct {
    const char *extension;
    const char type[5];     /* 4-char type + NUL */
    const char creator[5];  /* 4-char creator + NUL */
} hl_file_type_entry_t;

/* Get type/creator for a filename based on extension.
 * Returns pointer to a static entry, or the default (TEXT/TTXT). */
const hl_file_type_entry_t *hl_file_type_from_filename(const char *filename);

/* Directory type/creator */
extern const char HL_TYPE_FOLDER[5];    /* "fldr" */
extern const char HL_CREATOR_FOLDER[5]; /* "n/a " */

#endif /* HOTLINE_FILE_TYPES_H */
