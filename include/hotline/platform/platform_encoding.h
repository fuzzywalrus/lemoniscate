/*
 * platform_encoding.h - Platform-specific encoding implementation selection
 *
 * The public API for MacRoman <-> UTF-8 conversion is in hotline/encoding.h.
 * The implementation is selected at compile time:
 *
 * macOS: CoreFoundation CFString API (encoding_cf.c)
 * Linux: Static 256-byte lookup table (encoding_table.c)
 *
 * Both implementations produce identical output for all 256 MacRoman
 * code points. This header exists for documentation; include
 * hotline/encoding.h for the public API.
 */

#ifndef HOTLINE_PLATFORM_ENCODING_H
#define HOTLINE_PLATFORM_ENCODING_H

/* Public API is in hotline/encoding.h — nothing additional needed here. */

#endif /* HOTLINE_PLATFORM_ENCODING_H */
