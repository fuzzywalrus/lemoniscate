/*
 * encoding.c - MacRoman <-> UTF-8 via CoreFoundation
 *
 * Maps to: golang.org/x/text/encoding/charmap usage in hotline/server.go
 *
 * On Mac OS X Tiger, CoreFoundation provides CFStringConvertEncoding
 * with kCFStringEncodingMacRoman — the definitive MacRoman tables.
 */

#include "hotline/encoding.h"
#include <CoreFoundation/CoreFoundation.h>
#include <string.h>

int hl_macroman_to_utf8(const char *in, size_t in_len,
                        char *out, size_t out_len)
{
    if (out_len == 0) return -1; /* caller must provide at least 1 byte for NUL */

    if (in_len == 0) {
        out[0] = '\0';
        return 0;
    }

    CFStringRef str = CFStringCreateWithBytes(
        kCFAllocatorDefault,
        (const UInt8 *)in,
        (CFIndex)in_len,
        kCFStringEncodingMacRoman,
        false
    );
    if (!str) return -1;

    CFIndex used = 0;
    CFIndex converted = CFStringGetBytes(
        str,
        CFRangeMake(0, CFStringGetLength(str)),
        kCFStringEncodingUTF8,
        '?',     /* lossy byte */
        false,
        (UInt8 *)out,
        (CFIndex)(out_len - 1), /* leave room for NUL */
        &used
    );

    CFRelease(str);

    if (converted == 0 && in_len > 0) return -1;

    out[used] = '\0';
    return (int)used;
}

int hl_utf8_to_macroman(const char *in, size_t in_len,
                        char *out, size_t out_len)
{
    if (in_len == 0) {
        return 0;
    }

    CFStringRef str = CFStringCreateWithBytes(
        kCFAllocatorDefault,
        (const UInt8 *)in,
        (CFIndex)in_len,
        kCFStringEncodingUTF8,
        false
    );
    if (!str) return -1;

    CFIndex used = 0;
    CFIndex converted = CFStringGetBytes(
        str,
        CFRangeMake(0, CFStringGetLength(str)),
        kCFStringEncodingMacRoman,
        '?',
        false,
        (UInt8 *)out,
        (CFIndex)out_len,
        &used
    );

    CFRelease(str);

    if (converted == 0 && in_len > 0) return -1;

    return (int)used;
}
