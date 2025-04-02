/**
 * Compatibility layer for krep tests
 * Provides 6-parameter wrappers around 8-parameter search functions
 */

#ifndef TEST_COMPAT_H
#define TEST_COMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include "../krep.h"

#ifdef TESTING
/* Wrapper functions with 6 parameters that call the 8-parameter versions */

/* Boyer-Moore-Horspool compatibility wrapper */
static inline uint64_t boyer_moore_search_compat(
    const char *text, size_t text_len,
    const char *pattern, size_t pattern_len,
    bool case_sensitive, size_t report_limit_offset)
{
    return boyer_moore_search(text, text_len, pattern, pattern_len, 
                             case_sensitive, report_limit_offset,
                             false, NULL);
}

/* Knuth-Morris-Pratt compatibility wrapper */
static inline uint64_t kmp_search_compat(
    const char *text, size_t text_len,
    const char *pattern, size_t pattern_len,
    bool case_sensitive, size_t report_limit_offset)
{
    return kmp_search(text, text_len, pattern, pattern_len, 
                     case_sensitive, report_limit_offset,
                     false, NULL);
}

/* Rabin-Karp compatibility wrapper */
static inline uint64_t rabin_karp_search_compat(
    const char *text, size_t text_len,
    const char *pattern, size_t pattern_len,
    bool case_sensitive, size_t report_limit_offset)
{
    return rabin_karp_search(text, text_len, pattern, pattern_len, 
                            case_sensitive, report_limit_offset,
                            false, NULL);
}

/* SIMD SSE4.2 compatibility wrapper */
#ifdef __SSE4_2__
static inline uint64_t simd_sse42_search_compat(
    const char *text, size_t text_len,
    const char *pattern, size_t pattern_len,
    bool case_sensitive, size_t report_limit_offset)
{
    return simd_sse42_search(text, text_len, pattern, pattern_len, 
                            case_sensitive, report_limit_offset,
                            false, NULL);
}
#endif

/* AVX2 compatibility wrapper */
#ifdef __AVX2__
static inline uint64_t simd_avx2_search_compat(
    const char *text, size_t text_len,
    const char *pattern, size_t pattern_len,
    bool case_sensitive, size_t report_limit_offset)
{
    return simd_avx2_search(text, text_len, pattern, pattern_len, 
                           case_sensitive, report_limit_offset,
                           false, NULL);
}
#endif

/* NEON compatibility wrapper */
#ifdef __ARM_NEON
static inline uint64_t neon_search_compat(
    const char *text, size_t text_len,
    const char *pattern, size_t pattern_len,
    bool case_sensitive, size_t report_limit_offset)
{
    return neon_search(text, text_len, pattern, pattern_len, 
                      case_sensitive, report_limit_offset,
                      false, NULL);
}
#endif

/* Redefine the function names to use the compatibility versions */
#define boyer_moore_search boyer_moore_search_compat
#define kmp_search kmp_search_compat
#define rabin_karp_search rabin_karp_search_compat
#ifdef __SSE4_2__
#define simd_sse42_search simd_sse42_search_compat
#endif
#ifdef __AVX2__
#define simd_avx2_search simd_avx2_search_compat
#endif
#ifdef __ARM_NEON
#define neon_search neon_search_compat
#endif

#endif /* TESTING */

#endif /* TEST_COMPAT_H */
