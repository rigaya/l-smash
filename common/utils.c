/*****************************************************************************
 * utils.c
 *****************************************************************************
 * Copyright (C) 2010-2017 L-SMASH project
 *
 * Authors: Yusuke Nakamura <muken.the.vfrmaniac@gmail.com>
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 *****************************************************************************/

/* This file is available under an ISC license. */

#include "internal.h" /* must be placed first */

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <nmmintrin.h>

const void * (*lsmash_memmem)(const void *haystack, size_t haystack_len, const void *needle,
	size_t needle_len) = 0;

static const void * lsmash_memmem_naive(const void *haystack, size_t haystack_len, const void *needle,
	size_t needle_len)
{
	const char *begin;
	const char *const last_possible
		= (const char *)haystack + haystack_len - needle_len;
	if (needle_len == 0)
		return (void *)haystack;
	if (haystack_len < needle_len)
		return NULL;
	for (begin = (const char *)haystack; begin <= last_possible; ++begin)
		if (begin[0] == ((const char *)needle)[0] &&
			!memcmp((const void *)&begin[1],
			(const void *)((const char *)needle + 1),
				needle_len - 1))
			return (void *)begin;

	return NULL;
}

static const void *lsmash_memmem_sse42(const void *haystack, size_t haystack_len, const void *needle_,
	size_t needle_len) {
	if (needle_len > 16)
		return lsmash_memmem_naive(haystack, haystack_len, needle_, needle_len);
	if (haystack_len < needle_len)
		return NULL;
	enum { IMM8 = _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ORDERED | _SIDD_LEAST_SIGNIFICANT };
	char tmp_buf[16]; memcpy(tmp_buf, needle_, needle_len);
	const __m128i needle = _mm_loadu_si128((const __m128i*)tmp_buf);
	int offset;
	for (offset = 0; offset + 16 <= haystack_len; offset += 16 - needle_len + 1) {
		const __m128i data = _mm_loadu_si128((const __m128i*)((const uint8_t*)haystack + offset));
		auto r = _mm_cmpestri(needle, needle_len, data, 16, IMM8);
		if (r + needle_len <= 16) {
			return (const uint8_t*)haystack + offset + r;
		}
	}
	auto remain = haystack_len - offset;
	memcpy(tmp_buf, (const uint8_t*)haystack + offset, remain);
	const __m128i data = _mm_loadu_si128((const __m128i*)tmp_buf);
	auto r = _mm_cmpestri(needle, needle_len, data, remain, IMM8);
	if (r + needle_len <= remain) {
		return (const uint8_t*)haystack + offset + r;
	}
	return NULL;
}

void lsmash_init_sse() {
	if (!lsmash_memmem) {
		int cpuinfo[4];
		__cpuid(cpuinfo, 1);
		int sse42_available = (cpuinfo[2] & (1 << 20));
		lsmash_memmem = sse42_available ? lsmash_memmem_sse42 : lsmash_memmem_naive;
	}
}

/*---- type ----*/
double lsmash_fixed2double( int64_t value, int frac_width )
{
    return value / (double)(1ULL << frac_width);
}

float lsmash_int2float32( uint32_t value )
{
    return (union {uint32_t i; float f;}){value}.f;
}

double lsmash_int2float64( uint64_t value )
{
    return (union {uint64_t i; double d;}){value}.d;
}
/*---- ----*/

/*---- others ----*/
void lsmash_log
(
    const void      *class,
    lsmash_log_level level,
    const char      *message,
    ...
)
{
    /* Dereference lsmash_class_t pointer if 'class' is non-NULL. */
    lsmash_class_t *cls = class ? (lsmash_class_t *)*(intptr_t *)class : NULL;
    if( cls && cls->log_level_offset )
    {
        lsmash_log_level log_level = *(lsmash_log_level *)((int8_t *)class + cls->log_level_offset);
        if( level > log_level )
            return;
    }
    char *prefix;
    va_list args;
    va_start( args, message );
    switch( level )
    {
        case LSMASH_LOG_ERROR:
            prefix = "Error";
            break;
        case LSMASH_LOG_WARNING:
            prefix = "Warning";
            break;
        case LSMASH_LOG_INFO:
            prefix = "Info";
            break;
        default:
            prefix = "Unknown";
            break;
    }
    if( cls )
        fprintf( stderr, "[%s: %s]: ", cls->name, prefix );
    else
        fprintf( stderr, "[%s]: ", prefix );
    vfprintf( stderr, message, args );
    va_end( args );
}

void lsmash_log_refresh_line
(
    const void *class   /* unused, but for forward compatibility */
)
{
    /* Assume 80 characters per line. */
    fprintf( stderr, "%80c", '\r' );
}

void lsmash_ifprintf
(
    FILE       *fp,
    int         indent,
    const char *format, ...
)
{
    va_list args;
    va_start( args, format );
    if( indent <= 10 )
    {
        static const char *indent_string[] =
            {
                "",
                "    ",
                "        ",
                "            ",
                "                ",
                "                    ",
                "                        ",
                "                            ",
                "                                ",
                "                                    ",
                "                                        "
            };
        fprintf( fp, "%s", indent_string[indent] );
    }
    else
        for( int i = 0; i < indent; i++ )
            fprintf( fp, "    " );
    vfprintf( fp, format, args );
    va_end( args );
}

/* for qsort function */
int lsmash_compare_dts
(
    const lsmash_media_ts_t *a,
    const lsmash_media_ts_t *b
)
{
    int64_t diff = (int64_t)(a->dts - b->dts);
    return diff > 0 ? 1 : (diff == 0 ? 0 : -1);
}

int lsmash_compare_cts
(
    const lsmash_media_ts_t *a,
    const lsmash_media_ts_t *b
)
{
    int64_t diff = (int64_t)(a->cts - b->cts);
    return diff > 0 ? 1 : (diff == 0 ? 0 : -1);
}

#ifdef _WIN32
int lsmash_convert_ansi_to_utf8( const char *ansi, char *utf8, int length )
{
    int len0 = MultiByteToWideChar( CP_THREAD_ACP, 0, ansi, -1, 0, 0 );
    wchar_t *buff = lsmash_malloc( len0 * sizeof(wchar_t) );
    if( !buff )
        return 0;
    int len1 = MultiByteToWideChar( CP_THREAD_ACP, 0, ansi, -1, buff, len0 );
    if( len0 != len1 )
        goto convert_fail;
    len0 = WideCharToMultiByte( CP_UTF8, 0, buff, -1, 0, 0, 0, 0 );
    if( len0 > length - 1 )
        goto convert_fail;
    len1 = WideCharToMultiByte( CP_UTF8, 0, buff, -1, utf8, length, 0, 0 );
    lsmash_free( buff );
    if( len0 != len1 )
        return 0;
    return len1;
convert_fail:
    lsmash_free( buff );
    return 0;
}
#endif
