/*
 * time_conv.c - Hotline 8-byte time format implementation
 *
 * Maps to: hotline/time.go
 */

#include "hotline/time_conv.h"
#include <string.h>

void hl_time_from_timet(hl_time_t out, time_t t)
{
    /* Maps to: Go NewTime()
     * Format: Year(2) + Milliseconds(2, zeros) + SecondsFromJan1(4) */
    struct tm *tm = localtime(&t);
    if (!tm) {
        memset(out, 0, 8);
        return;
    }

    uint16_t year = (uint16_t)(tm->tm_year + 1900);
    /* tm_yday is days since Jan 1 (0-365), tm_hour/min/sec are time of day */
    uint32_t seconds = (uint32_t)(tm->tm_yday * 86400 +
                                   tm->tm_hour * 3600 +
                                   tm->tm_min * 60 +
                                   tm->tm_sec);

    hl_write_u16(out, year);
    out[2] = 0; /* milliseconds high */
    out[3] = 0; /* milliseconds low */
    hl_write_u32(out + 4, seconds);
}

time_t hl_time_to_timet(const hl_time_t in)
{
    /* Maps to: Go Time.Time() */
    uint16_t year = hl_read_u16(in);
    uint32_t seconds = hl_read_u32(in + 4);

    struct tm tm;
    memset(&tm, 0, sizeof(tm));
    tm.tm_year = year - 1900;
    tm.tm_mon  = 0;  /* January */
    tm.tm_mday = 1;
    tm.tm_isdst = -1; /* let mktime figure it out */

    time_t start_of_year = mktime(&tm);
    return start_of_year + (time_t)seconds;
}
