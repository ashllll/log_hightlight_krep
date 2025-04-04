/**
 * Compatibility layer for krep tests
 * Provides wrappers around search functions for tests that
 * don't need line counting or position tracking features.
 */

#ifndef TEST_COMPAT_H
#define TEST_COMPAT_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <limits.h>  // For SIZE_MAX
#include <regex.h>   // For regex_t
#include "../krep.h" // Include main header for the full function declarations

#ifdef TESTING
/*
 * Wrapper functions calling the full search implementations with default
 * arguments for line counting and position tracking parameters.
 * These wrappers effectively simulate the older function signatures.
 */

/* Boyer-Moore-Horspool compatibility wrapper */
static inline uint64_t boyer_moore_search_compat(
    const char *text, size_t text_len,
    const char *pattern, size_t pattern_len,
    bool case_sensitive, size_t report_limit_offset)
{
    // Dummy state variables for line counting (not used by caller)
    uint64_t dummy_line_count = 0;
    size_t dummy_last_line_start = SIZE_MAX;
    size_t dummy_last_line_end = 0;
    // Call the 11-parameter version (MODIFIED: Pass 11 args)
    return boyer_moore_search(text, text_len, pattern, pattern_len,
                              case_sensitive, report_limit_offset,
                              false, &dummy_line_count, &dummy_last_line_start, &dummy_last_line_end, // Pass dummy end offset
                              false, NULL);                                                           // track_positions, result
}

/* Knuth-Morris-Pratt compatibility wrapper */
static inline uint64_t kmp_search_compat(
    const char *text, size_t text_len,
    const char *pattern, size_t pattern_len,
    bool case_sensitive, size_t report_limit_offset)
{
    // Dummy state variables
    uint64_t dummy_line_count = 0;
    size_t dummy_last_line_start = SIZE_MAX;
    size_t dummy_last_line_end = 0;
    // Call the 11-parameter version (MODIFIED: Pass 11 args)
    return kmp_search(text, text_len, pattern, pattern_len,
                      case_sensitive, report_limit_offset,
                      false, &dummy_line_count, &dummy_last_line_start, &dummy_last_line_end, // Pass dummy end offset
                      false, NULL);                                                           // track_positions, result
}

/* Regex compatibility wrapper */
static inline uint64_t regex_search_compat(
    const char *text, size_t text_len,
    const regex_t *compiled_regex,
    size_t report_limit_offset)
{
    // Dummy state variables
    uint64_t dummy_line_count = 0;
    size_t dummy_last_line_start = SIZE_MAX;
    size_t dummy_last_line_end = 0;
    // Call the 9-parameter version (MODIFIED: Pass 9 args)
    return regex_search(text, text_len, compiled_regex, report_limit_offset,
                        false, &dummy_line_count, &dummy_last_line_start, &dummy_last_line_end, // Pass dummy end offset
                        false, NULL);                                                           // track_positions, result
}

/* SIMD SSE4.2 compatibility wrapper */
#ifdef __SSE4_2__
static inline uint64_t simd_sse42_search_compat(
    const char *text, size_t text_len,
    const char *pattern, size_t pattern_len,
    bool case_sensitive, size_t report_limit_offset)
{
    // Dummy state variables
    uint64_t dummy_line_count = 0;
    size_t dummy_last_line_start = SIZE_MAX;
    size_t dummy_last_line_end = 0;
    // Call the 11-parameter version (MODIFIED: Pass 11 args)
    return simd_sse42_search(text, text_len, pattern, pattern_len,
                             case_sensitive, report_limit_offset,
                             false, &dummy_line_count, &dummy_last_line_start, &dummy_last_line_end, // Pass dummy end offset
                             false, NULL);                                                           // track_positions, result
}
#endif

/* AVX2 compatibility wrapper */
#ifdef __AVX2__
static inline uint64_t simd_avx2_search_compat(
    const char *text, size_t text_len,
    const char *pattern, size_t pattern_len,
    bool case_sensitive, size_t report_limit_offset)
{
    // Dummy state variables
    uint64_t dummy_line_count = 0;
    size_t dummy_last_line_start = SIZE_MAX;
    size_t dummy_last_line_end = 0;
    // Call the 11-parameter version (MODIFIED: Pass 11 args)
    return simd_avx2_search(text, text_len, pattern, pattern_len,
                            case_sensitive, report_limit_offset,
                            false, &dummy_line_count, &dummy_last_line_start, &dummy_last_line_end, // Pass dummy end offset
                            false, NULL);                                                           // track_positions, result
}
#endif

/* NEON compatibility wrapper */
#ifdef __ARM_NEON
static inline uint64_t neon_search_compat(
    const char *text, size_t text_len,
    const char *pattern, size_t pattern_len,
    bool case_sensitive, size_t report_limit_offset)
{
    // Dummy state variables
    uint64_t dummy_line_count = 0;
    size_t dummy_last_line_start = SIZE_MAX;
    size_t dummy_last_line_end = 0;
    // Call the 11-parameter version (MODIFIED: Pass 11 args)
    return neon_search(text, text_len, pattern, pattern_len,
                       case_sensitive, report_limit_offset,
                       false, &dummy_line_count, &dummy_last_line_start, &dummy_last_line_end, // Pass dummy end offset
                       false, NULL);                                                           // track_positions, result
}
#endif

/* Redefine the function names to use the compatibility versions */
#define boyer_moore_search boyer_moore_search_compat
#define kmp_search kmp_search_compat
#define regex_search regex_search_compat // Add redefine for regex
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