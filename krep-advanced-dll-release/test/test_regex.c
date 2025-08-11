/**
 * Test suite for regex search capabilities in krep
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <assert.h>
#include <regex.h>
#include <inttypes.h> // For PRIu64 format specifier
#include <limits.h>   // For SIZE_MAX

/* Define TESTING before including headers */
#ifndef TESTING
#define TESTING
#endif

/* Include main krep functions for testing */
#include "../krep.h"     // Main krep header
#include "test_krep.h"   // Test header
#include "test_compat.h" // Compatibility wrappers

/* Test flags and counters */
extern int tests_passed;
extern int tests_failed;

/**
 * Basic test assertion with reporting
 */
#define TEST_ASSERT(condition, message)      \
    do                                       \
    {                                        \
        if (condition)                       \
        {                                    \
            printf("✓ PASS: %s\n", message); \
            tests_passed++;                  \
        }                                    \
        else                                 \
        {                                    \
            printf("✗ FAIL: %s\n", message); \
            tests_failed++;                  \
        }                                    \
    } while (0)

/**
 * Helper function to create a compiled regex
 */
static regex_t create_regex(const char *pattern, bool case_sensitive)
{
    regex_t regex_obj;
    // REG_NEWLINE allows '.' to not match newline, and '^'/'$' to match start/end of lines
    int flags = REG_EXTENDED | REG_NEWLINE;
    if (!case_sensitive)
        flags |= REG_ICASE; // Add case-insensitivity flag if needed
    int rc = regcomp(&regex_obj, pattern, flags);

    // Handle compilation errors
    if (rc != 0)
    {
        char error_buf[256];
        regerror(rc, &regex_obj, error_buf, sizeof(error_buf));
        fprintf(stderr, "Regex compilation error: %s for pattern '%s'\n", error_buf, pattern);
        exit(1); // Exit if compilation fails, as tests depend on it
    }

    return regex_obj;
}

/**
 * Test basic regex functionality
 */
void test_basic_regex(void)
{
    printf("\n=== Basic Regex Tests ===\n");

    const char *haystack = "The quick brown fox jumps over the lazy dog";
    size_t haystack_len = strlen(haystack);

    // Simple character patterns
    regex_t re1 = create_regex("fox", true); // Case-sensitive
    regex_t re2 = create_regex("cat", true); // Case-sensitive

    // Use compatibility wrapper for simple count tests
    uint64_t matches_fox = regex_search_compat(haystack, haystack_len, &re1, SIZE_MAX);
    uint64_t matches_cat = regex_search_compat(haystack, haystack_len, &re2, SIZE_MAX);

    TEST_ASSERT(matches_fox == 1, "Regex finds 'fox' once");
    TEST_ASSERT(matches_cat == 0, "Regex doesn't find 'cat'");

    // Simple character class patterns
    regex_t re3 = create_regex("[qjx]", true); // Case-sensitive match q, j, or x
    uint64_t matches_qjx = regex_search_compat(haystack, haystack_len, &re3, SIZE_MAX);
    TEST_ASSERT(matches_qjx == 3, "Regex finds characters [qjx] three times");

    // Free compiled regex objects
    regfree(&re1);
    regfree(&re2);
    regfree(&re3);
}

/**
 * Test more complex regex patterns
 */
void test_complex_regex(void)
{
    printf("\n=== Complex Regex Tests ===\n");

    const char *haystack = "Hello 123, hello 456, HELLO 789!";
    size_t haystack_len = strlen(haystack);

    // Word boundaries and case-insensitive
    regex_t re1 = create_regex("hello", false); // Case-insensitive
    uint64_t matches_hello = regex_search_compat(haystack, haystack_len, &re1, SIZE_MAX);
    TEST_ASSERT(matches_hello == 3, "Case-insensitive regex finds 'hello' three times");

    // Digit sequences
    regex_t re2 = create_regex("[0-9]+", true); // Match one or more digits
    uint64_t matches_digits = regex_search_compat(haystack, haystack_len, &re2, SIZE_MAX);
    TEST_ASSERT(matches_digits == 3, "Regex finds three digit sequences");

    // More complex pattern with alternation and groups
    const char *text = "apple orange banana apple grape orange";
    size_t text_len = strlen(text);
    regex_t re3 = create_regex("(apple|orange)", true); // Match "apple" or "orange"
    uint64_t matches_fruits = regex_search_compat(text, text_len, &re3, SIZE_MAX);
    TEST_ASSERT(matches_fruits == 4, "Regex finds 'apple' or 'orange' four times");

    // Free compiled regex objects
    regfree(&re1);
    regfree(&re2);
    regfree(&re3);
}

/**
 * Test regex with multiple matches in various scenarios
 */
void test_regex_multiple_matches(void)
{
    printf("\n=== Regex Multiple Matches Tests ===\n");

    const char *text1 = "aaa bbb aaa bbb aaa";
    size_t text1_len = strlen(text1);
    regex_t re1 = create_regex("aaa", true);
    uint64_t matches_aaa = regex_search_compat(text1, text1_len, &re1, SIZE_MAX);
    TEST_ASSERT(matches_aaa == 3, "Regex finds 'aaa' three times");

    const char *text2 = "abababababa";
    size_t text2_len = strlen(text2);
    regex_t re2 = create_regex("ababa", true);
    uint64_t matches_ababa = regex_search_compat(text2, text2_len, &re2, SIZE_MAX);
    TEST_ASSERT(matches_ababa == 2, "Regex finds 'ababa' twice (non-overlapping)");

    const char *text3 = "Line 1: apple\nLine 2: orange\nLine 3: apple\nLine 4: banana";
    size_t text3_len = strlen(text3);
    regex_t re3 = create_regex("^Line [0-9]+: (apple|orange)$", true);
    uint64_t matches_lines = regex_search_compat(text3, text3_len, &re3, SIZE_MAX);
    TEST_ASSERT(matches_lines == 3, "Regex finds three lines with 'apple' or 'orange'");

    regfree(&re1);
    regfree(&re2);
    regfree(&re3);
}

/**
 * Test regex edge cases
 */
void test_regex_edge_cases(void)
{
    printf("\n=== Regex Edge Cases Tests ===\n");

    const char *empty = "";
    size_t empty_len = 0;

    regex_t re1 = create_regex(".", true);
    uint64_t matches_dot = regex_search_compat(empty, empty_len, &re1, SIZE_MAX);
    TEST_ASSERT(matches_dot == 0, "Regex '.' doesn't match in empty string");

    regex_t re2 = create_regex("^$", true);
    uint64_t matches_empty = regex_search_compat(empty, empty_len, &re2, SIZE_MAX);
    TEST_ASSERT(matches_empty == 1, "Regex '^$' matches empty string once");

    const char *short_text = "abc";
    size_t short_len = strlen(short_text);
    regex_t re3 = create_regex("abcdef", true);
    uint64_t matches_long = regex_search_compat(short_text, short_len, &re3, SIZE_MAX);
    TEST_ASSERT(matches_long == 0, "Regex longer than text doesn't match");

    const char *complex_text = "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaab";
    size_t complex_len = strlen(complex_text);
    regex_t re4 = create_regex("a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*a*b", true);
    uint64_t matches_backtrack = regex_search_compat(complex_text, complex_len, &re4, SIZE_MAX);
    TEST_ASSERT(matches_backtrack == 1, "Complex regex with backtracking matches correctly");

    regfree(&re1);
    regfree(&re2);
    regfree(&re3);
    regfree(&re4);
}

/**
 * Test regex overlapping patterns (POSIX regex should find non-overlapping matches)
 */
void test_regex_overlapping(void)
{
    printf("\n=== Regex Overlapping Tests ===\n");

    const char *text1 = "aaaa";
    size_t text1_len = strlen(text1);
    regex_t re1 = create_regex("aa", true);
    uint64_t matches_aa = regex_search_compat(text1, text1_len, &re1, SIZE_MAX);
    TEST_ASSERT(matches_aa == 2, "Regex finds 'aa' twice (non-overlapping)");

    const char *text2 = "ababababa";
    size_t text2_len = strlen(text2);
    regex_t re2 = create_regex("aba", true);
    uint64_t matches_aba = regex_search_compat(text2, text2_len, &re2, SIZE_MAX);
    TEST_ASSERT(matches_aba == 2, "Regex finds 2 non-overlapping 'aba' patterns");
    printf("  Found %" PRIu64 " instances of 'aba' in '%s'\n", matches_aba, text2);

    const char *text3 = "abcdef";
    size_t text3_len = strlen(text3);
    regex_t re3 = create_regex("^|$", true);
    uint64_t matches_bounds = regex_search_compat(text3, text3_len, &re3, SIZE_MAX);

    bool valid_count = (matches_bounds == 1 || matches_bounds == 2);
    TEST_ASSERT(valid_count, "Regex finds correct zero-width assertions count");
    printf("  Found %" PRIu64 " zero-width assertions\n", matches_bounds);

    regfree(&re1);
    regfree(&re2);
    regfree(&re3);
}

/**
 * Test regex with max_count limit (using search_params_t)
 */
void test_regex_report_limit(void)
{
    printf("\n=== Regex Max Count Limit Tests ===\n");

    const char *text = "aaa bbb aaa ccc aaa ddd aaa";
    size_t text_len = strlen(text);
    match_result_t *result = NULL;

    search_params_t params = create_regex_params("aaa", true, false, false);

    params.max_count = SIZE_MAX;
    uint64_t matches_full = regex_search(&params, text, text_len, result);
    TEST_ASSERT(matches_full == 4, "Regex finds all 4 'aaa' with no limit");

    params.max_count = 3;
    uint64_t matches_limit3 = regex_search(&params, text, text_len, result);
    TEST_ASSERT(matches_limit3 == 3, "Regex finds 3 'aaa' with limit 3");

    params.max_count = 2;
    uint64_t matches_limit2 = regex_search(&params, text, text_len, result);
    TEST_ASSERT(matches_limit2 == 2, "Regex finds 2 'aaa' with limit 2");

    params.max_count = 1;
    uint64_t matches_limit1 = regex_search(&params, text, text_len, result);
    TEST_ASSERT(matches_limit1 == 1, "Regex finds 1 'aaa' with limit 1");

    params.max_count = 0;
    uint64_t matches_limit0 = regex_search(&params, text, text_len, result);
    TEST_ASSERT(matches_limit0 == 0, "Regex finds 0 with limit 0");

    params.track_positions = true;
    params.max_count = 2;
    result = match_result_init(5);
    uint64_t matches_limit2_track = regex_search(&params, text, text_len, result);
    TEST_ASSERT(matches_limit2_track == 2, "Regex finds 2 'aaa' with limit 2 (tracking)");
    TEST_ASSERT(result->count == 2, "Result has 2 positions with limit 2 (tracking)");
    match_result_free(result);
    result = NULL;

    cleanup_params(&params);
}

/**
 * Compare performance of regex vs literal search (optional benchmark)
 */
void test_regex_vs_literal_performance(void)
{
    printf("\n=== Regex vs. Literal Performance Tests ===\n");

    const size_t size = 100 * 1024;
    char *large_text = (char *)malloc(size + 1);
    if (!large_text)
    {
        perror("Failed to allocate memory for large_text in performance test");
        tests_failed++;
        return;
    }

    size_t expected_literal_count = 0;
    for (size_t i = 0; i < size; i++)
    {
        if ((i + 1) % 1000 == 0)
        {
            large_text[i] = 'b';
            expected_literal_count++;
        }
        else
        {
            large_text[i] = 'a';
        }
    }

    if (size > 0)
    {
        if (large_text[0] != 'b')
        {
            expected_literal_count++;
        }
        large_text[0] = 'b';
    }

    large_text[size] = '\0';

    const char *pattern = "b";
    size_t pattern_len = 1;

    printf("Comparing performance for %zu KB text with single-char pattern '%s'...\n", size / 1024, pattern);
    printf("Expected matches: %zu\n", expected_literal_count);

    clock_t start_lit = clock();

    search_params_t bm_params = {0};
    bm_params.patterns = malloc(sizeof(char *));
    bm_params.pattern_lens = malloc(sizeof(size_t));

    if (!bm_params.patterns || !bm_params.pattern_lens)
    {
        perror("Failed to allocate memory for bm_params fields in performance test");
        if (bm_params.patterns)
            free((void *)bm_params.patterns);
        if (bm_params.pattern_lens)
            free(bm_params.pattern_lens);
        free(large_text);
        tests_failed++;
        return;
    }
    bm_params.patterns[0] = pattern;
    bm_params.pattern_lens[0] = pattern_len;
    bm_params.num_patterns = 1;

    bm_params.pattern = bm_params.patterns[0];
    bm_params.pattern_len = bm_params.pattern_lens[0];

    bm_params.case_sensitive = true;
    bm_params.use_regex = false;
    bm_params.count_lines_mode = false;
    bm_params.count_matches_mode = true;
    bm_params.track_positions = false;
    bm_params.whole_word = false;
    bm_params.compiled_regex = NULL;
    bm_params.ac_trie = NULL;
    bm_params.max_count = SIZE_MAX;

    uint64_t literal_count = boyer_moore_search(&bm_params, large_text, size, NULL);

    clock_t end_lit = clock();
    double literal_time = ((double)(end_lit - start_lit)) / CLOCKS_PER_SEC;
    printf("  Literal search (BM): %f seconds (found %" PRIu64 " matches)\n", literal_time, literal_count);
    TEST_ASSERT(literal_count == expected_literal_count, "Literal search found correct count");
    cleanup_params(&bm_params);

    regex_t re = create_regex(pattern, true);
    clock_t start_re = clock();
    uint64_t regex_matches = regex_search_compat(large_text, size, &re, SIZE_MAX);
    clock_t end_re = clock();
    double regex_time = ((double)(end_re - start_re)) / CLOCKS_PER_SEC;
    printf("  Regex search: %f seconds (found %" PRIu64 " matches)\n", regex_time, regex_matches);
    TEST_ASSERT(regex_matches == expected_literal_count, "Regex search found correct count");

    regfree(&re);
    free(large_text);

    if (literal_time > 1e-9 && regex_time > 1e-9)
    {
        printf("  Literal search was %.2fx faster than regex search.\n", regex_time / literal_time);
    }
}

/**
 * Test regex with line extraction (for testing match_result)
 */
void test_regex_line_extraction(void)
{
    printf("\n=== Regex Line Extraction Tests ===\n");

    const char *text = "example: first match\nother line\nexample: second match";
    size_t text_len = strlen(text);

    search_params_t params = {
        .pattern = "example:.*",
        .pattern_len = strlen("example:.*"),
        .case_sensitive = true,
        .use_regex = true,
        .track_positions = true,
        .count_lines_mode = false,
        .count_matches_mode = false,
        .compiled_regex = NULL,
        .max_count = SIZE_MAX};
    params.patterns = (const char **)&params.pattern;
    params.pattern_lens = &params.pattern_len;
    params.num_patterns = 1;

    regex_t regex_obj;
    int flags = REG_EXTENDED | REG_NEWLINE;
    int rc = regcomp(&regex_obj, params.pattern, flags);
    if (rc != 0)
    {
        char errbuf[256];
        regerror(rc, &regex_obj, errbuf, sizeof(errbuf));
        fprintf(stderr, "Failed to compile regex in line extraction test: %s\n", errbuf);
        tests_failed++;
        return;
    }
    params.compiled_regex = &regex_obj;

    match_result_t *result = match_result_init(10);
    if (!result)
    {
        fprintf(stderr, "Failed to create match_result in line extraction test\n");
        regfree(&regex_obj);
        tests_failed++;
        return;
    }

    uint64_t match_count = regex_search(&params, text, text_len, result);

    TEST_ASSERT(match_count == 2, "Regex found 2 matches");
    TEST_ASSERT(result->count == 2, "Result contains 2 positions");

    if (result->count == 2)
    {
        TEST_ASSERT(result->positions[0].start_offset == 0, "First match starts at offset 0");
        TEST_ASSERT(result->positions[0].end_offset == 20, "First match ends at offset 20");

        TEST_ASSERT(result->positions[1].start_offset == 32, "Second match starts at offset 32");
        TEST_ASSERT(result->positions[1].end_offset == 53, "Second match ends at offset 53");
    }

    match_result_free(result);
    regfree(&regex_obj);
}

/**
 * Run all regex tests
 */
void run_regex_tests(void)
{
    printf("\n--- Running Regex Tests ---\n");

    test_basic_regex();
    test_complex_regex();
    test_regex_multiple_matches();
    test_regex_edge_cases();
    test_regex_overlapping();
    test_regex_report_limit();
    test_regex_vs_literal_performance();
    test_regex_line_extraction();

    printf("\n--- Completed Regex Tests ---\n");
}
