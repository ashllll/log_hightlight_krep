/**
 * Test suite for multiple pattern search functions
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <inttypes.h> // Add this include for PRIu64 macro

/* Define TESTING for test builds */
#ifndef TESTING
#define TESTING
#endif

/* Include the test headers */
#include "../krep.h"         // Main krep header
#include "../aho_corasick.h" // Aho-Corasick header
#include "test_krep.h"       // Test header

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

// --- Forward declarations for test functions within this file ---
void test_basic_aho_corasick(void);
void test_aho_corasick_case_insensitive(void);
void test_aho_corasick_edge_cases(void);
void test_position_tracking_multipattern(void);
void test_multipattern_performance(void);

/**
 * Test basic Aho-Corasick functionality
 */
void test_basic_aho_corasick(void)
{
    printf("\n=== Basic Aho-Corasick Tests ===\n");

    const char *text = "ushers";
    size_t text_len = strlen(text);
    const char *patterns[] = {"he", "she", "his", "hers"};
    size_t pattern_lens[] = {2, 3, 3, 4};
    size_t num_patterns = 4;

    // Create search params
    search_params_t params = {
        .patterns = patterns,
        .pattern_lens = pattern_lens,
        .num_patterns = num_patterns,
        .case_sensitive = true,
        .use_regex = false,
        .track_positions = false,   // Don't track positions for count check
        .count_lines_mode = false,  // Count matches
        .count_matches_mode = true, // Indicate intent
        .compiled_regex = NULL,
        .max_count = SIZE_MAX};

    // Call aho_corasick_search with the params struct
    uint64_t matches = aho_corasick_search(&params, text, text_len, NULL);

    // Expected matches: "he" (at index 1), "she" (at index 0), "hers" (at index 2)
    TEST_ASSERT(matches == 3, "Aho-Corasick finds 3 matches in 'ushers'");

    // Test with no matches
    const char *text2 = "xyz";
    size_t text2_len = strlen(text2);
    matches = aho_corasick_search(&params, text2, text2_len, NULL);
    TEST_ASSERT(matches == 0, "Aho-Corasick finds 0 matches in 'xyz'");
}

/**
 * Test Aho-Corasick case-insensitive search
 */
void test_aho_corasick_case_insensitive(void)
{
    printf("\n=== Aho-Corasick Case-Insensitive Tests ===\n");

    const char *text = "UsHeRs";
    size_t text_len = strlen(text);
    const char *patterns[] = {"he", "she", "his", "hers"};
    size_t pattern_lens[] = {2, 3, 3, 4};
    size_t num_patterns = 4;

    // Create search params for case-insensitive search
    search_params_t params = {
        .patterns = patterns,
        .pattern_lens = pattern_lens,
        .num_patterns = num_patterns,
        .case_sensitive = false, // Set to false
        .use_regex = false,
        .track_positions = false,   // Don't track positions for count check
        .count_lines_mode = false,  // Count matches
        .count_matches_mode = true, // Indicate intent
        .compiled_regex = NULL,
        .max_count = SIZE_MAX};

    // Call aho_corasick_search with the params struct
    uint64_t matches = aho_corasick_search(&params, text, text_len, NULL);

    // Expected matches: "he" (at index 1), "she" (at index 0), "hers" (at index 2)
    TEST_ASSERT(matches == 3, "Aho-Corasick finds 3 matches case-insensitively in 'UsHeRs'");

    // Test with different casing in patterns
    const char *patterns2[] = {"HE", "SHE", "HIS", "HERS"};
    search_params_t params2 = {
        .patterns = patterns2, // Use uppercase patterns
        .pattern_lens = pattern_lens,
        .num_patterns = num_patterns,
        .case_sensitive = false, // Still case-insensitive
        .use_regex = false,
        .track_positions = false,
        .count_lines_mode = false,
        .count_matches_mode = true,
        .compiled_regex = NULL,
        .max_count = SIZE_MAX};

    matches = aho_corasick_search(&params2, text, text_len, NULL);
    TEST_ASSERT(matches == 3, "Aho-Corasick finds 3 matches case-insensitively with uppercase patterns");
}

/**
 * Test Aho-Corasick edge cases
 */
void test_aho_corasick_edge_cases(void)
{
    printf("\n=== Aho-Corasick Edge Cases Tests ===\n");

    const char *text = "abc";
    size_t text_len = strlen(text);
    const char *patterns[] = {"a", "b", "c", "ab", "bc", "abc"};
    size_t pattern_lens[] = {1, 1, 1, 2, 2, 3};
    size_t num_patterns = 6;

    // Create search params
    search_params_t params = {
        .patterns = patterns,
        .pattern_lens = pattern_lens,
        .num_patterns = num_patterns,
        .case_sensitive = true,
        .use_regex = false,
        .track_positions = false,   // Don't track positions for count check
        .count_lines_mode = false,  // Count matches
        .count_matches_mode = true, // Indicate intent
        .compiled_regex = NULL,
        .max_count = SIZE_MAX};

    // Call aho_corasick_search with the params struct
    uint64_t matches = aho_corasick_search(&params, text, text_len, NULL);
    // Expected: "a", "ab", "abc", "b", "bc", "c" -> 6 matches
    TEST_ASSERT(matches == 6, "Aho-Corasick finds all overlapping patterns");

    // Test empty text
    matches = aho_corasick_search(&params, "", 0, NULL);
    TEST_ASSERT(matches == 0, "Aho-Corasick finds 0 matches in empty text");

    // Test empty patterns list
    search_params_t params_empty = {
        .patterns = NULL,
        .pattern_lens = NULL,
        .num_patterns = 0, // Set num_patterns to 0
        .case_sensitive = true,
        .use_regex = false,
        .track_positions = false,
        .count_lines_mode = false,
        .count_matches_mode = true,
        .compiled_regex = NULL,
        .max_count = SIZE_MAX};
    matches = aho_corasick_search(&params_empty, text, text_len, NULL);
    TEST_ASSERT(matches == 0, "Aho-Corasick finds 0 matches with empty pattern list");

    // Test patterns longer than text
    const char *patterns_long[] = {"abcd", "abcde"};
    size_t pattern_lens_long[] = {4, 5};
    search_params_t params_long = {
        .patterns = patterns_long,
        .pattern_lens = pattern_lens_long,
        .num_patterns = 2,
        .case_sensitive = true,
        .use_regex = false,
        .track_positions = false,
        .count_lines_mode = false,
        .count_matches_mode = true,
        .compiled_regex = NULL,
        .max_count = SIZE_MAX};
    matches = aho_corasick_search(&params_long, text, text_len, NULL);
    TEST_ASSERT(matches == 0, "Aho-Corasick finds 0 matches when patterns are longer than text");
}

/**
 * Test position tracking with multiple patterns
 */
void test_position_tracking_multipattern(void)
{
    printf("\n=== Position Tracking with Multiple Patterns ===\n");

    const char *text = "apple banana cherry";
    size_t text_len = strlen(text);
    const char *patterns[] = {"apple", "banana", "cherry"};
    size_t pattern_lens[] = {5, 6, 6};
    size_t num_patterns = 3;

    // Create search params with position tracking enabled
    search_params_t params = {
        .patterns = patterns,
        .pattern_lens = pattern_lens,
        .num_patterns = num_patterns,
        .case_sensitive = true,
        .use_regex = false,
        .track_positions = true, // Enable position tracking
        .count_lines_mode = false,
        .count_matches_mode = false,
        .compiled_regex = NULL,
        .max_count = SIZE_MAX};

    // Create result structure to collect positions
    match_result_t *result = match_result_init(10);
    if (!result)
    {
        fprintf(stderr, "Failed to create match_result in position tracking test\n");
        return;
    }

    // Perform the search
    uint64_t matches = aho_corasick_search(&params, text, text_len, result);

    // Verify number of matches
    TEST_ASSERT(matches == 3, "Found 3 pattern matches");
    TEST_ASSERT(result->count == 3, "Result contains 3 positions");

    // Clean up
    match_result_free(result);
}

/**
 * Performance comparison between single pattern and multiple pattern searches
 */
void test_multipattern_performance(void)
{
    printf("\n=== Multiple Pattern Performance Test ===\n");

    // Create a larger text buffer
    const size_t text_size = 1 * 1024 * 1024; // 1MB
    char *text = malloc(text_size + 1);
    if (!text)
    {
        printf("Failed to allocate memory for performance test\n");
        return;
    }

    // Fill the buffer with random text
    for (size_t i = 0; i < text_size; i++)
    {
        text[i] = 'a' + (i % 26); // a-z repeating pattern
    }
    text[text_size] = '\0';

    // Insert some patterns at known positions
    const char *patterns[] = {"pattern1", "pattern2", "pattern3", "pattern4", "pattern5"};
    size_t pattern_lens[] = {8, 8, 8, 8, 8};
    size_t num_patterns = 5;

    // Insert each pattern 10 times at evenly spaced intervals
    for (size_t p = 0; p < num_patterns; p++)
    {
        for (size_t i = 0; i < 10; i++)
        {
            size_t pos = (p * 10 + i + 1) * text_size / (num_patterns * 10 + 1);
            if (pos + pattern_lens[p] < text_size)
            {
                memcpy(text + pos, patterns[p], pattern_lens[p]);
            }
        }
    }

    printf("Testing with %zu MB text and %zu patterns...\n", text_size / (1024 * 1024), num_patterns);

    // Time individual searches for each pattern
    clock_t start_individual = clock();
    uint64_t total_matches_individual = 0;

    for (size_t p = 0; p < num_patterns; p++)
    {
        // Create params for individual Boyer-Moore search, configured for match counting
        search_params_t single_params = {
            .pattern = patterns[p],
            .pattern_len = pattern_lens[p],
            .case_sensitive = true,
            .use_regex = false,
            .track_positions = false,   // Don't track positions
            .count_lines_mode = false,  // Don't count lines
            .count_matches_mode = true, // Indicate intent to count matches
            .compiled_regex = NULL,
            .max_count = SIZE_MAX,
            // Assign multi-pattern fields for consistency
            .patterns = &patterns[p],
            .pattern_lens = &pattern_lens[p],
            .num_patterns = 1};
        // Call the actual boyer_moore_search function
        total_matches_individual += boyer_moore_search(&single_params, text, text_size, NULL);
    }
    clock_t end_individual = clock();
    double time_individual = ((double)(end_individual - start_individual)) / CLOCKS_PER_SEC;

    // Time combined search with Aho-Corasick
    clock_t start_combined = clock();
    search_params_t params = {
        .patterns = patterns,
        .pattern_lens = pattern_lens,
        .num_patterns = num_patterns,
        .case_sensitive = true,
        .use_regex = false,
        .track_positions = false,   // Don't track positions for combined count
        .count_lines_mode = false,  // Don't count lines
        .count_matches_mode = true, // Indicate intent to count matches
        .compiled_regex = NULL,
        .max_count = SIZE_MAX};

    // Call the actual aho_corasick_search function
    uint64_t matches_combined = aho_corasick_search(&params, text, text_size, NULL);
    clock_t end_combined = clock();
    double time_combined = ((double)(end_combined - start_combined)) / CLOCKS_PER_SEC;

    // Report results
    printf("  Individual searches: %" PRIu64 " matches in %.6f seconds\n",
           total_matches_individual, time_individual);
    printf("  Combined search: %" PRIu64 " matches in %.6f seconds\n",
           matches_combined, time_combined);
    // Avoid division by zero
    if (time_combined > 1e-9 && time_individual >= 0)
    {
        printf("  Speed improvement: %.2fx\n", time_individual / time_combined);
    }
    else if (time_individual < 1e-9 && time_combined < 1e-9)
    {
        printf("  Both searches too fast to calculate ratio.\n");
    }
    else if (time_combined < 1e-9)
    {
        printf("  Combined search too fast to calculate ratio.\n");
    }
    else
    {
        printf("  Could not calculate ratio.\n");
    }

    TEST_ASSERT(total_matches_individual == matches_combined,
                "Both search methods found the same number of matches");

    // Cleanup
    free(text);
}

/**
 * Run all multiple pattern tests
 */
void run_multiple_patterns_tests(void)
{
    printf("\n--- Running Multiple Pattern Tests ---\n");

    test_basic_aho_corasick();
    test_aho_corasick_case_insensitive();
    test_aho_corasick_edge_cases();
    test_position_tracking_multipattern();
    test_multipattern_performance();

    printf("\n--- Completed Multiple Pattern Tests ---\n");
}
