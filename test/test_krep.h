/**
 * Test header for krep string search utility
 */

#ifndef TEST_KREP_H
#define TEST_KREP_H

#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <limits.h> // For SIZE_MAX
#include <regex.h>  // Include for regex_t

/* Define TESTING to enable the compatibility wrappers if not already defined by Makefile */
#ifndef TESTING
#define TESTING
#endif

/* Include the main header AFTER defining TESTING potentially */
#include "../krep.h"

/*
 * Note: We use compatibility wrappers defined elsewhere (test_compat.h or krep.c)
 * for most search functions when TESTING is defined.
 * However, we might need the correct declaration for functions called directly
 * or if wrappers aren't used for everything.
 */

/**
 * @brief Regex-based search using POSIX regular expressions.
 * (Matching the updated signature from krep.h)
 */
uint64_t regex_search(const char *text_start, size_t text_len, const regex_t *compiled_regex,
                      size_t report_limit_offset, bool count_lines_mode,
                      uint64_t *line_match_count, size_t *last_counted_line_start,
                      bool track_positions, match_result_t *result);

/* Declarations for regex test functions */
void test_basic_regex(void);
void test_complex_regex(void);
void test_regex_multiple_matches(void);
void test_regex_edge_cases(void);
void test_regex_overlapping(void);
void test_regex_report_limit(void);
void test_regex_vs_literal_performance(void);
void test_regex_line_extraction(void); // Added declaration
void run_regex_tests(void);

#endif /* TEST_KREP_H */
