/*
 * time_conv.h - Hotline 8-byte time format
 *
 * Hotline time: Year(2) + Milliseconds(2, unused) + SecondsFromJan1(4)
 * All big-endian.
 */

#ifndef HOTLINE_TIME_CONV_H
#define HOTLINE_TIME_CONV_H

#include "hotline/types.h"
#include <time.h>

typedef uint8_t hl_time_t[8];

/*
 * hl_time_from_timet - Convert a time_t to Hotline 8-byte format.
 */
void hl_time_from_timet(hl_time_t out, time_t t);

/*
 * hl_time_to_timet - Convert Hotline 8-byte format to time_t.
 */
time_t hl_time_to_timet(const hl_time_t in);

#endif /* HOTLINE_TIME_CONV_H */
