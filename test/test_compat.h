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
    // Apply report_limit_offset by limiting the text length if needed
    size_t effective_len = (report_limit_offset < text_len) ? report_limit_offset : text_len;

    // Create a search_params_t structure with the required parameters
    search_params_t params = {
        .pattern = pattern,
        .pattern_len = pattern_len,
        .case_sensitive = case_sensitive,
        .use_regex = false,
        .track_positions = false,
        .count_lines_mode = false,
        .compiled_regex = NULL};

    // Call the new function signature with the adjusted text length
    return boyer_moore_search(&params, text, effective_len, NULL);
}

/* Knuth-Morris-Pratt compatibility wrapper */
static inline uint64_t kmp_search_compat(
    const char *text, size_t text_len,
    const char *pattern, size_t pattern_len,
    bool case_sensitive, size_t report_limit_offset)
{
    // Apply report_limit_offset by limiting the text length if needed
    size_t effective_len = (report_limit_offset < text_len) ? report_limit_offset : text_len;

    // Create a search_params_t structure with the required parameters
    search_params_t params = {
        .pattern = pattern,
        .pattern_len = pattern_len,
        .case_sensitive = case_sensitive,
        .use_regex = false,
        .track_positions = false,
        .count_lines_mode = false,
        .compiled_regex = NULL};

    // Call the new function signature with the adjusted text length
    return kmp_search(&params, text, effective_len, NULL);
}

/* Regex compatibility wrapper */
static inline uint64_t regex_search_compat(
    const char *text, size_t text_len,
    const regex_t *compiled_regex,
    size_t report_limit_offset)
{
    // Apply report_limit_offset by limiting the text length if needed
    size_t effective_len = (report_limit_offset < text_len) ? report_limit_offset : text_len;

    // Create a search_params_t structure with the required parameters
    search_params_t params = {
        .pattern = NULL, // Not used for regex
        .pattern_len = 0,
        .case_sensitive = false,
        .use_regex = true,
        .track_positions = false,
        .count_lines_mode = false,
        .compiled_regex = compiled_regex};

    // Call the new function signature with the adjusted text length
    return regex_search(&params, text, effective_len, NULL);
}

/* SIMD SSE4.2 compatibility wrapper */
#ifdef __SSE4_2__
static inline uint64_t simd_sse42_search_compat(
    const char *text, size_t text_len,
    const char *pattern, size_t pattern_len,
    bool case_sensitive, size_t report_limit_offset)
{
    // Apply report_limit_offset by limiting the text length if needed
    size_t effective_len = (report_limit_offset < text_len) ? report_limit_offset : text_len;

    // Create a search_params_t structure with the required parameters
    search_params_t params = {
        .pattern = pattern,
        .pattern_len = pattern_len,
        .case_sensitive = case_sensitive,
        .use_regex = false,
        .track_positions = false,
        .count_lines_mode = false,
        .compiled_regex = NULL};

    // Call the new function signature with the adjusted text length
    return simd_sse42_search(&params, text, effective_len, NULL);
}
#endif

/* AVX2 compatibility wrapper */
#ifdef __AVX2__
static inline uint64_t simd_avx2_search_compat(
    const char *text, size_t text_len,
    const char *pattern, size_t pattern_len,
    bool case_sensitive, size_t report_limit_offset)
{
    // Apply report_limit_offset by limiting the text length if needed
    size_t effective_len = (report_limit_offset < text_len) ? report_limit_offset : text_len;

    // Create a search_params_t structure with the required parameters
    search_params_t params = {
        .pattern = pattern,
        .pattern_len = pattern_len,
        .case_sensitive = case_sensitive,
        .use_regex = false,
        .track_positions = false,
        .count_lines_mode = false,
        .compiled_regex = NULL};

    // Call the new function signature with the adjusted text length
    return simd_avx2_search(&params, text, effective_len, NULL);
}
#endif

/* NEON compatibility wrapper */
#ifdef __ARM_NEON
static inline uint64_t neon_search_compat(
    const char *text, size_t text_len,
    const char *pattern, size_t pattern_len,
    bool case_sensitive, size_t report_limit_offset)
{
    // Apply report_limit_offset by limiting the text length if needed
    size_t effective_len = (report_limit_offset < text_len) ? report_limit_offset : text_len;

    // Create a search_params_t structure with the required parameters
    search_params_t params = {
        .pattern = pattern,
        .pattern_len = pattern_len,
        .case_sensitive = case_sensitive,
        .use_regex = false,
        .track_positions = false,
        .count_lines_mode = false,
        .compiled_regex = NULL};

    // Call the new function signature with the adjusted text length
    return neon_search(&params, text, effective_len, NULL);
}
#endif

/* Aho-Corasick compatibility wrapper for multiple pattern search */
static inline uint64_t aho_corasick_search_compat(
    const char *text, size_t text_len,
    const char **patterns, size_t *pattern_lens, size_t num_patterns,
    bool case_sensitive, size_t report_limit_offset)
{
    // Apply report_limit_offset by limiting the text length if needed
    size_t effective_len = (report_limit_offset < text_len) ? report_limit_offset : text_len;

    // Create a search_params_t structure for multiple patterns
    search_params_t params = {
        .pattern = NULL,  // Not used for multiple patterns
        .pattern_len = 0, // Not used for multiple patterns
        .patterns = patterns,
        .pattern_lens = pattern_lens,
        .num_patterns = num_patterns,
        .case_sensitive = case_sensitive,
        .use_regex = false,
        .track_positions = false,
        .count_lines_mode = false,
        .compiled_regex = NULL};

    // Call the new function signature
    return aho_corasick_search(&params, text, effective_len, NULL);
}

/* memchr_short_search compatibility wrapper */
static inline uint64_t memchr_short_search_compat(
    const char *text, size_t text_len,
    const char *pattern, size_t pattern_len,
    bool case_sensitive, size_t report_limit_offset)
{
    // Apply report_limit_offset by limiting the text length if needed
    size_t effective_len = (report_limit_offset < text_len) ? report_limit_offset : text_len;

    // Create a search_params_t structure with the required parameters
    search_params_t params = {
        .pattern = pattern,
        .pattern_len = pattern_len,
        .case_sensitive = case_sensitive,
        .use_regex = false,
        .track_positions = false,
        .count_lines_mode = false,
        .compiled_regex = NULL};

    // Call the new function signature with the adjusted text length
    return memchr_short_search(&params, text, effective_len, NULL);
}

/*
 * Special functions for test_krep.c that accept search_params_t and extract fields
 * These have completely different names to avoid conflicts with the compat functions
 */

static inline uint64_t test_bridge_boyer_moore(
    const search_params_t *params,
    const char *text, size_t text_len,
    match_result_t *result)
{
    (void)result; // Suppress unused parameter warning

    // Extract the parameters we need from the params struct
    const char *pattern = params->pattern;
    size_t pattern_len = params->pattern_len;
    bool case_sensitive = params->case_sensitive;

    // Call the compatibility wrapper with SIZE_MAX for report_limit_offset
    return boyer_moore_search_compat(text, text_len, pattern, pattern_len,
                                     case_sensitive, SIZE_MAX);
}

static inline uint64_t test_bridge_kmp(
    const search_params_t *params,
    const char *text, size_t text_len,
    match_result_t *result)
{
    (void)result; // Suppress unused parameter warning

    // Extract the parameters we need from the params struct
    const char *pattern = params->pattern;
    size_t pattern_len = params->pattern_len;
    bool case_sensitive = params->case_sensitive;

    // Call the compatibility wrapper with SIZE_MAX for report_limit_offset
    return kmp_search_compat(text, text_len, pattern, pattern_len,
                             case_sensitive, SIZE_MAX);
}

/* Bridge function for regex_search with params struct */
static inline uint64_t test_bridge_regex(
    const search_params_t *params,
    const char *text, size_t text_len,
    match_result_t *result)
{
    (void)result; // Suppress unused parameter warning

    // Extract the regex from the params struct
    const regex_t *compiled_regex = params->compiled_regex;
    if (!compiled_regex)
    {
        // If no regex compiled, return 0
        return 0;
    }

    // Call the compatibility wrapper with SIZE_MAX for report_limit_offset
    return regex_search_compat(text, text_len, compiled_regex, SIZE_MAX);
}

/*
 * Conditional compilation based on which test file is using this header.
 * Define TEST_MULTIPLE_PATTERNS when compiling test_multiple_patterns.c
 * to get the old style function signatures.
 */

// Check if we're compiling test_multiple_patterns.c (identified by unique string)
#if defined(TEST_MULTIPLE_PATTERNS_FILE)
// For test_multiple_patterns.c - use original compatibility style (direct arguments)
#define boyer_moore_search boyer_moore_search_compat
#define kmp_search kmp_search_compat
#define regex_search regex_search_compat
#define aho_corasick_search aho_corasick_search_compat
#define memchr_short_search memchr_short_search_compat
#ifdef __SSE4_2__
#define simd_sse42_search simd_sse42_search_compat
#endif
#ifdef __AVX2__
#define simd_avx2_search simd_avx2_search_compat
#endif
#ifdef __ARM_NEON
#define neon_search neon_search_compat
#endif
#else
// For test_krep.c and other tests - use bridge functions that work with struct params
#define boyer_moore_search test_bridge_boyer_moore
#define kmp_search test_bridge_kmp
#define regex_search test_bridge_regex
#define aho_corasick_search aho_corasick_search_compat // No bridge function for this yet
#define memchr_short_search memchr_short_search_compat // No bridge function for this yet
#ifdef __SSE4_2__
#define simd_sse42_search simd_sse42_search_compat
#endif
#ifdef __AVX2__
#define simd_avx2_search simd_avx2_search_compat
#endif
#ifdef __ARM_NEON
#define neon_search neon_search_compat
#endif
#endif

#endif /* TESTING */

#endif /* TEST_COMPAT_H */