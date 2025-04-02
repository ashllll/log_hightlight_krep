/**
 * Test header for krep string search utility
 */

#ifndef TEST_KREP_H
#define TEST_KREP_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <limits.h> // For SIZE_MAX

/* Function declarations from krep.c that we need for testing */
uint64_t boyer_moore_search(const char *text, size_t text_len,
                            const char *pattern, size_t pattern_len,
                            bool case_sensitive, size_t report_limit_offset);
uint64_t kmp_search(const char *text, size_t text_len,
                    const char *pattern, size_t pattern_len,
                    bool case_sensitive, size_t report_limit_offset);
uint64_t rabin_karp_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset);

#ifdef __SSE4_2__
uint64_t simd_sse42_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset);
#endif

#ifdef __AVX2__
uint64_t avx2_search(const char *text, size_t text_len,
                     const char *pattern, size_t pattern_len,
                     bool case_sensitive, size_t report_limit_offset);
#endif

#ifdef __ARM_NEON
uint64_t neon_search(const char *text, size_t text_len,
                     const char *pattern, size_t pattern_len,
                     bool case_sensitive, size_t report_limit_offset);
#endif

/**
 * @brief Regex-based search using POSIX regular expressions.
 *
 * @param text The text to search within
 * @param text_len Length of the text
 * @param compiled_regex Pointer to the pre-compiled regex object
 * @param report_limit_offset Maximum offset to report matches
 * @param track_positions If true, track and return match positions
 * @param result Optional pointer to store match positions (if track_positions is true)
 * @return uint64_t Number of matches found
 */
uint64_t regex_search(const char *text, size_t text_len,
                      const regex_t *compiled_regex,
                      size_t report_limit_offset,
                      bool track_positions,
                      match_result_t *result);

/* Declarations for regex test functions */
void test_basic_regex(void);
void test_complex_regex(void);
void test_regex_multiple_matches(void);
void test_regex_edge_cases(void);
void test_regex_overlapping(void);
void test_regex_report_limit(void);
void test_regex_vs_literal_performance(void);
void run_regex_tests(void);

#endif /* TEST_KREP_H */
