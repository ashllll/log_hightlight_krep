/**
 * Test suite for multiple pattern search capabilities in krep
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <assert.h>
#include <regex.h>
#include <inttypes.h> // For PRIu64 format specifier

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
#define TEST_ASSERT(condition, message)                 \
    do                                                  \
    {                                                   \
        if (condition)                                  \
        {                                               \
            tests_passed++;                             \
            printf("✓ %s\n", message);                  \
        }                                               \
        else                                            \
        {                                               \
            tests_failed++;                             \
            printf("✗ %s\n", message);                  \
            printf("  at %s:%d\n", __FILE__, __LINE__); \
        }                                               \
    } while (0)

/**
 * Test basic multiple pattern search functionality
 */
void test_basic_multipattern(void)
{
    printf("\n=== Basic Multiple Pattern Tests ===\n");

    const char *text = "The quick brown fox jumps over the lazy dog";
    size_t text_len = strlen(text);

    // Define multiple patterns
    const char *patterns[] = {"quick", "fox", "dog"};
    size_t pattern_lens[] = {5, 3, 3};
    size_t num_patterns = 3;

    // Create search parameters
    search_params_t params = {
        .patterns = patterns,
        .pattern_lens = pattern_lens,
        .num_patterns = num_patterns,
        .case_sensitive = true,
        .use_regex = false,
        .track_positions = false,
        .count_lines_mode = false,
        .compiled_regex = NULL};

    // Perform search - replace with compatibility wrapper parameters
    uint64_t matches = aho_corasick_search(
        text, text_len,
        patterns, pattern_lens, num_patterns,
        params.case_sensitive, SIZE_MAX);

    TEST_ASSERT(matches == 3, "Found all 3 patterns (quick, fox, dog) with Aho-Corasick");

    // Test with pattern not in text
    const char *patterns2[] = {"quick", "cat", "dog"};
    size_t pattern_lens2[] = {5, 3, 3};

    params.patterns = patterns2;
    params.pattern_lens = pattern_lens2;

    matches = aho_corasick_search(
        text, text_len,
        patterns2, pattern_lens2, num_patterns,
        params.case_sensitive, SIZE_MAX);

    TEST_ASSERT(matches == 2, "Found 2 patterns (quick, dog) correctly");
}

/**
 * Test case-insensitive multiple pattern search
 */
void test_case_insensitive_multipattern(void)
{
    printf("\n=== Case-Insensitive Multiple Pattern Tests ===\n");

    const char *text = "The Quick Brown Fox jumps over THE LAZY DOG";
    size_t text_len = strlen(text);

    // Define patterns (in lowercase)
    const char *patterns[] = {"quick", "fox", "dog"};
    size_t pattern_lens[] = {5, 3, 3};
    size_t num_patterns = 3;

    // Create search parameters
    search_params_t params = {
        .patterns = patterns,
        .pattern_lens = pattern_lens,
        .num_patterns = num_patterns,
        .case_sensitive = false, // case-insensitive
        .use_regex = false,
        .track_positions = false,
        .count_lines_mode = false,
        .compiled_regex = NULL};

    // Perform case-insensitive search - replace with compatibility wrapper parameters
    uint64_t matches = aho_corasick_search(
        text, text_len,
        patterns, pattern_lens, num_patterns,
        params.case_sensitive, SIZE_MAX);

    TEST_ASSERT(matches == 3, "Found all 3 patterns case-insensitively");

    // Compare with case-sensitive search
    params.case_sensitive = true;
    matches = aho_corasick_search(
        text, text_len,
        patterns, pattern_lens, num_patterns,
        params.case_sensitive, SIZE_MAX);

    TEST_ASSERT(matches == 1, "Found only 1 pattern with case sensitivity");
}

/**
 * Test position tracking with multiple patterns
 */
void test_position_tracking_multipattern(void)
{
    printf("\n=== Position Tracking with Multiple Patterns ===\n");

    const char *text = "apple orange banana apple";
    size_t text_len = strlen(text);

    // Define patterns
    const char *patterns[] = {"apple", "banana"};
    size_t pattern_lens[] = {5, 6};
    size_t num_patterns = 2;

    // Create search parameters with position tracking
    search_params_t params = {
        .patterns = patterns,
        .pattern_lens = pattern_lens,
        .num_patterns = num_patterns,
        .case_sensitive = true,
        .use_regex = false,
        .track_positions = true,
        .count_lines_mode = false,
        .compiled_regex = NULL};

    // Create result structure
    match_result_t *result = match_result_init(10);
    if (!result)
    {
        printf("Failed to create match_result\n");
        return;
    }

// To handle position tracking, temporarily undefine and redefine the macro
#undef aho_corasick_search
    uint64_t matches = aho_corasick_search(&params, text, text_len, result);
#define aho_corasick_search aho_corasick_search_compat

    TEST_ASSERT(matches == 3, "Found 3 pattern matches");
    TEST_ASSERT(result->count == 3, "Result contains 3 positions");

    // Verify match positions (if we have 3 results)
    if (result->count == 3)
    {
        // First "apple" at position 0-5
        TEST_ASSERT(result->positions[0].start_offset == 0, "First match starts at offset 0");
        TEST_ASSERT(result->positions[0].end_offset == 5, "First match ends at offset 5");

        // "banana" at position 13-19
        TEST_ASSERT(result->positions[1].start_offset == 13, "Second match starts at offset 13");
        TEST_ASSERT(result->positions[1].end_offset == 19, "Second match ends at offset 19");

        // Second "apple" at position 20-25
        TEST_ASSERT(result->positions[2].start_offset == 20, "Third match starts at offset 20");
        TEST_ASSERT(result->positions[2].end_offset == 25, "Third match ends at offset 25");
    }

    // Cleanup
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
        // Use the direct compatibility wrapper signature
        total_matches_individual += boyer_moore_search(
            text, text_size,
            patterns[p], pattern_lens[p],
            true, SIZE_MAX);
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
        .track_positions = false,
        .count_lines_mode = false,
        .compiled_regex = NULL};

    // Update the call to match compatibility wrapper
    uint64_t matches_combined = aho_corasick_search(
        text, text_size,
        patterns, pattern_lens, num_patterns,
        params.case_sensitive, SIZE_MAX);
    clock_t end_combined = clock();
    double time_combined = ((double)(end_combined - start_combined)) / CLOCKS_PER_SEC;

    // Report results
    printf("  Individual searches: %" PRIu64 " matches in %.6f seconds\n",
           total_matches_individual, time_individual);
    printf("  Combined search: %" PRIu64 " matches in %.6f seconds\n",
           matches_combined, time_combined);
    printf("  Speed improvement: %.2fx\n", time_individual / time_combined);

    TEST_ASSERT(total_matches_individual == matches_combined,
                "Both search methods found the same number of matches");

    // Cleanup
    free(text);
}

/**
 * Run all multiple pattern tests
 */
void run_multipattern_tests(void)
{
    printf("\n--- Running Multiple Pattern Tests ---\n");

    test_basic_multipattern();
    test_case_insensitive_multipattern();
    test_position_tracking_multipattern();
    test_multipattern_performance();

    printf("\n--- Completed Multiple Pattern Tests ---\n");
}
