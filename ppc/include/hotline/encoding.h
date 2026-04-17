/*
 * encoding.h - MacRoman <-> UTF-8 text encoding
 *
 * Maps to: golang.org/x/text/encoding/charmap (Macintosh)
 *          Used in hotline/server.go, hotline/file_path.go, hotline/files.go
 *
 * On Tiger, uses CoreFoundation's CFStringConvertEncoding which has
 * the definitive MacRoman tables (PPC Macs are the native platform).
 */

#ifndef HOTLINE_ENCODING_H
#define HOTLINE_ENCODING_H

#include <stddef.h>

/*
 * hl_macroman_to_utf8 - Convert MacRoman bytes to UTF-8 string.
 * out must be at least out_len bytes. Returns bytes written (excluding NUL),
 * or -1 on error.
 */
int hl_macroman_to_utf8(const char *in, size_t in_len,
                        char *out, size_t out_len);

/*
 * hl_utf8_to_macroman - Convert UTF-8 string to MacRoman bytes.
 * out must be at least out_len bytes. Returns bytes written,
 * or -1 on error.
 */
int hl_utf8_to_macroman(const char *in, size_t in_len,
                        char *out, size_t out_len);

#endif /* HOTLINE_ENCODING_H */
