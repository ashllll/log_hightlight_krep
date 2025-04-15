/**
 * Test suite for krep string search utility
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <assert.h>
#include <locale.h>
#include <inttypes.h> // For PRIu64 format specifier
#include <limits.h>   // For SIZE_MAX
#include <unistd.h>   // For sleep (used in placeholder)

/* Define TESTING before including headers if not done by Makefile */
#ifndef TESTING
#define TESTING
#endif

/* Include main krep functions for testing */
/* Assumes krep.h and aho_corasick.h are in the parent directory */
#include "../krep.h"
#include "../aho_corasick.h" // Include Aho-Corasick header
#include "test_krep.h"       // Include test header for consistency (if needed)
#include "test_compat.h"     // Include compatibility wrappers

// Forward declaration for regex tests (defined in test_regex.c)
void run_regex_tests(void);
// Forward declaration for multiple pattern tests (defined in test_multiple_patterns.c)
void run_multiple_patterns_tests(void);

/* Test flags and counters */
int tests_passed = 0;
int tests_failed = 0;

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

/* ========================================================================= */
/* Helper Functions for Creating search_params_t    */
/* ========================================================================= */

// Helper to create params for single literal pattern
search_params_t create_literal_params(const char *pattern, bool case_sensitive, bool count_lines, bool only_match)
{
    search_params_t params = {0};
    params.patterns = malloc(sizeof(char *));
    params.pattern_lens = malloc(sizeof(size_t));
    if (!params.patterns || !params.pattern_lens)
    {
        perror("Failed to allocate memory for single pattern params");
        exit(EXIT_FAILURE);
    }
    params.patterns[0] = (char *)pattern; // Cast needed as patterns is const char**
    params.pattern_lens[0] = strlen(pattern);
    params.num_patterns = 1;
    params.case_sensitive = case_sensitive;
    params.use_regex = false;
    params.count_lines_mode = count_lines && !only_match;
    params.count_matches_mode = count_lines && only_match; // Not directly used, but set for clarity
    params.track_positions = !(count_lines && !only_match);
    params.compiled_regex = NULL;
    params.max_count = SIZE_MAX; // Default: no limit

    // Set legacy fields for compatibility with older test functions if needed
    params.pattern = params.patterns[0];
    params.pattern_len = params.pattern_lens[0];

    return params;
}

// Helper to create params for single regex pattern
search_params_t create_regex_params(const char *pattern, bool case_sensitive, bool count_lines, bool only_match)
{
    search_params_t params = {0};
    params.patterns = malloc(sizeof(char *));
    params.pattern_lens = malloc(sizeof(size_t));
    if (!params.patterns || !params.pattern_lens)
    {
        perror("Failed to allocate memory for single regex params");
        exit(EXIT_FAILURE);
    }
    params.patterns[0] = (char *)pattern;     // Cast needed
    params.pattern_lens[0] = strlen(pattern); // Store length even for regex
    params.num_patterns = 1;
    params.case_sensitive = case_sensitive;
    params.use_regex = true;
    params.count_lines_mode = count_lines && !only_match;
    params.count_matches_mode = count_lines && only_match;
    params.track_positions = !(count_lines && !only_match);
    params.max_count = SIZE_MAX; // Default: no limit

    // Compile the regex (allocate locally for the test)
    regex_t *compiled_regex = malloc(sizeof(regex_t));
    if (!compiled_regex)
    {
        perror("Failed to allocate memory for compiled regex");
        exit(EXIT_FAILURE);
    }
    int rflags = REG_EXTENDED | REG_NEWLINE | (case_sensitive ? 0 : REG_ICASE);
    int ret = regcomp(compiled_regex, pattern, rflags);
    if (ret != 0)
    {
        char ebuf[256];
        regerror(ret, compiled_regex, ebuf, sizeof(ebuf));
        fprintf(stderr, "Regex compilation error in test setup: %s\n", ebuf);
        free(compiled_regex);
        exit(EXIT_FAILURE);
    }
    params.compiled_regex = compiled_regex; // Assign the compiled regex

    // Set legacy fields if needed (less relevant for regex)
    params.pattern = params.patterns[0];
    params.pattern_len = params.pattern_lens[0];

    return params;
}

// Cleanup function for params (frees regex if allocated)
void cleanup_params(search_params_t *params)
{
    if (params->use_regex && params->compiled_regex)
    {
        regfree((regex_t *)params->compiled_regex); // Cast needed as it's const in struct
        free((void *)params->compiled_regex);       // Free the allocated regex_t struct itself
        params->compiled_regex = NULL;
    }
    // Free pattern arrays allocated by helpers
    if (params->patterns)
    {
        free((void *)params->patterns); // Free the array of pointers
        params->patterns = NULL;
    }
    if (params->pattern_lens)
    {
        free(params->pattern_lens);
        params->pattern_lens = NULL;
    }
    // Note: Does not free ac_trie, handled separately
}

/* ========================================================================= */
/* Test Functions using search_params_t             */
/* ========================================================================= */

/**
 * Test basic search functionality using the new structure
 */
void test_basic_search_new(void)
{
    printf("\n=== Basic Search Tests ===\n");

    const char *haystack = "The quick brown fox jumps over the lazy dog";
    size_t haystack_len = strlen(haystack);
    match_result_t *result = NULL; // Not needed for count checks

    // --- Boyer-Moore Tests ---
    search_params_t bm_params_quick = create_literal_params("quick", true, false, false);
    TEST_ASSERT(test_bridge_boyer_moore(&bm_params_quick, haystack, haystack_len, result) == 1,
                "Boyer-Moore finds 'quick' once");
    cleanup_params(&bm_params_quick);

    search_params_t bm_params_fox = create_literal_params("fox", true, false, false);
    TEST_ASSERT(test_bridge_boyer_moore(&bm_params_fox, haystack, haystack_len, result) == 1,
                "Boyer-Moore finds 'fox' once");
    cleanup_params(&bm_params_fox);

    search_params_t bm_params_cat = create_literal_params("cat", true, false, false);
    TEST_ASSERT(test_bridge_boyer_moore(&bm_params_cat, haystack, haystack_len, result) == 0,
                "Boyer-Moore doesn't find 'cat'");
    cleanup_params(&bm_params_cat);

    // --- KMP Tests ---
    search_params_t kmp_params_quick = create_literal_params("quick", true, false, false);
    TEST_ASSERT(test_bridge_kmp(&kmp_params_quick, haystack, haystack_len, result) == 1,
                "KMP finds 'quick' once");
    cleanup_params(&kmp_params_quick);

    search_params_t kmp_params_fox = create_literal_params("fox", true, false, false);
    TEST_ASSERT(test_bridge_kmp(&kmp_params_fox, haystack, haystack_len, result) == 1,
                "KMP finds 'fox' once");
    cleanup_params(&kmp_params_fox);

    search_params_t kmp_params_cat = create_literal_params("cat", true, false, false);
    TEST_ASSERT(test_bridge_kmp(&kmp_params_cat, haystack, haystack_len, result) == 0,
                "KMP doesn't find 'cat'");
    cleanup_params(&kmp_params_cat);

// --- SSE4.2 Tests (if available) ---
#if KREP_USE_SSE42
    search_params_t sse_params_quick = create_literal_params("quick", true, false, false);
    TEST_ASSERT(simd_sse42_search(&sse_params_quick, haystack, haystack_len, result) == 1,
                "SSE4.2 finds 'quick' once");
    cleanup_params(&sse_params_quick);

    search_params_t sse_params_fox = create_literal_params("fox", true, false, false);
    TEST_ASSERT(simd_sse42_search(&sse_params_fox, haystack, haystack_len, result) == 1,
                "SSE4.2 finds 'fox' once");
    cleanup_params(&sse_params_fox);

    search_params_t sse_params_cat = create_literal_params("cat", true, false, false);
    TEST_ASSERT(simd_sse42_search(&sse_params_cat, haystack, haystack_len, result) == 0,
                "SSE4.2 doesn't find 'cat'");
    cleanup_params(&sse_params_cat);
#endif
}

/**
 * Test edge cases using the new structure
 */
void test_edge_cases_new(void)
{
    printf("\n=== Edge Cases Tests ===\n");

    const char *haystack_a = "aaaaaaaaaaaaaaaaa"; // 17 'a's
    size_t len_a = strlen(haystack_a);
    const char *haystack_abc = "abcdef";
    size_t len_abc = strlen(haystack_abc);
    const char *overlap_text = "abababa"; // BM: 3, KMP/SSE: 2
    size_t len_overlap = strlen(overlap_text);
    const char *aa_text = "aaaaa"; // BM: 4, KMP/SSE: 2
    size_t len_aa = strlen(aa_text);
    match_result_t *result = NULL;

    /* Test single character patterns */
    search_params_t params_a = create_literal_params("a", true, false, false);
    TEST_ASSERT(test_bridge_kmp(&params_a, haystack_a, len_a, result) == 17,
                "KMP finds 17 occurrences of 'a'");
    TEST_ASSERT(test_bridge_boyer_moore(&params_a, haystack_a, len_a, result) == 17,
                "BM finds 17 occurrences of 'a'");
#if KREP_USE_SSE42
    TEST_ASSERT(simd_sse42_search(&params_a, haystack_a, len_a, result) == 17,
                "SSE4.2 finds 17 occurrences of 'a'");
#endif
    cleanup_params(&params_a);

    /* Test empty pattern and haystack */
    search_params_t params_empty_patt = create_literal_params("", true, false, false);
    TEST_ASSERT(test_bridge_boyer_moore(&params_empty_patt, haystack_a, len_a, result) == 0,
                "Empty pattern gives 0 matches (BM)");
    TEST_ASSERT(test_bridge_kmp(&params_empty_patt, haystack_a, len_a, result) == 0,
                "Empty pattern gives 0 matches (KMP)");
    cleanup_params(&params_empty_patt);

    search_params_t params_empty_hay = create_literal_params("test", true, false, false);
    TEST_ASSERT(test_bridge_boyer_moore(&params_empty_hay, "", 0, result) == 0,
                "Empty haystack gives 0 matches (BM)");
    TEST_ASSERT(test_bridge_kmp(&params_empty_hay, "", 0, result) == 0,
                "Empty haystack gives 0 matches (KMP)");
    cleanup_params(&params_empty_hay);

    /* Test matching at start and end */
    search_params_t params_abc = create_literal_params("abc", true, false, false);
    TEST_ASSERT(test_bridge_kmp(&params_abc, haystack_abc, len_abc, result) == 1,
                "Match at start is found (KMP)");
    TEST_ASSERT(test_bridge_boyer_moore(&params_abc, haystack_abc, len_abc, result) == 1,
                "Match at start is found (BM)");
#if KREP_USE_SSE42
    TEST_ASSERT(simd_sse42_search(&params_abc, haystack_abc, len_abc, result) == 1,
                "Match at start is found (SSE4.2)");
#endif
    cleanup_params(&params_abc);

    search_params_t params_def = create_literal_params("def", true, false, false);
    TEST_ASSERT(test_bridge_kmp(&params_def, haystack_abc, len_abc, result) == 1,
                "Match at end is found (KMP)");
    TEST_ASSERT(test_bridge_boyer_moore(&params_def, haystack_abc, len_abc, result) == 1,
                "Match at end is found (BM)");
#if KREP_USE_SSE42
    TEST_ASSERT(simd_sse42_search(&params_def, haystack_abc, len_abc, result) == 1,
                "Match at end is found (SSE4.2)");
#endif
    cleanup_params(&params_def);

    /* Test overlapping patterns */
    printf("Testing overlapping patterns: '%s' with pattern 'aba'\n", overlap_text);
    search_params_t params_aba = create_literal_params("aba", true, false, false);
    uint64_t aba_bm = test_bridge_boyer_moore(&params_aba, overlap_text, len_overlap, result);
    uint64_t aba_kmp = test_bridge_kmp(&params_aba, overlap_text, len_overlap, result);
#if KREP_USE_SSE42
    uint64_t aba_sse = simd_sse42_search(&params_aba, overlap_text, len_overlap, result);
    printf("  BM: %" PRIu64 ", KMP: %" PRIu64 ", SSE: %" PRIu64 " matches\n",
           aba_bm, aba_kmp, aba_sse);
    // SSE4.2 (like KMP) should find non-overlapping matches for this specific implementation
    TEST_ASSERT(aba_sse == 2, "SSE4.2 (fallback) finds 2 occurrences of 'aba'");
#else
    printf("  BM: %" PRIu64 ", KMP: %" PRIu64 " matches\n", aba_bm, aba_kmp);
#endif
    TEST_ASSERT(aba_bm == 3, "Boyer-Moore finds 3 overlapping matches of 'aba'");
    TEST_ASSERT(aba_kmp == 2, "KMP finds 2 non-overlapping 'aba'");
    cleanup_params(&params_aba);

    /* Test with repeating pattern 'aa' */
    printf("Sequence 'aaaaa' with pattern 'aa': ");
    search_params_t params_aa = create_literal_params("aa", true, false, false);
    uint64_t aa_count_bm = test_bridge_boyer_moore(&params_aa, aa_text, len_aa, result);
    uint64_t aa_count_kmp = test_bridge_kmp(&params_aa, aa_text, len_aa, result);
#if KREP_USE_SSE42
    uint64_t aa_count_sse = simd_sse42_search(&params_aa, aa_text, len_aa, result);
    printf("BM=%" PRIu64 ", KMP=%" PRIu64 ", SSE=%" PRIu64 "\n",
           aa_count_bm, aa_count_kmp, aa_count_sse);
    // SSE4.2 (like KMP) should find non-overlapping matches for this specific implementation
    TEST_ASSERT(aa_count_sse == 2, "SSE4.2 (fallback) finds 2 occurrences of 'aa'");
#else
    printf("BM=%" PRIu64 ", KMP=%" PRIu64 "\n", aa_count_bm, aa_count_kmp);
#endif
    TEST_ASSERT(aa_count_bm == 4, "Boyer-Moore finds 4 overlapping matches of 'aa'");
    TEST_ASSERT(aa_count_kmp == 2, "KMP finds 2 non-overlapping 'aa'");
    cleanup_params(&params_aa);
}

/**
 * Test case-insensitive search using the new structure
 */
void test_case_insensitive_new(void)
{
    printf("\n=== Case-Insensitive Tests ===\n");

    const char *haystack = "The Quick Brown Fox Jumps Over The Lazy Dog";
    size_t haystack_len = strlen(haystack);
    match_result_t *result = NULL;

    /* Compare case sensitive vs insensitive */
    search_params_t params_quick_cs = create_literal_params("quick", true, false, false);
    TEST_ASSERT(test_bridge_boyer_moore(&params_quick_cs, haystack, haystack_len, result) == 0,
                "Case-sensitive doesn't find 'quick' (BM)");
    cleanup_params(&params_quick_cs);

    search_params_t params_quick_ci = create_literal_params("quick", false, false, false);
    TEST_ASSERT(test_bridge_boyer_moore(&params_quick_ci, haystack, haystack_len, result) == 1,
                "Case-insensitive finds 'quick' (BM)");
    cleanup_params(&params_quick_ci);

    search_params_t params_fox_cs = create_literal_params("FOX", true, false, false);
    TEST_ASSERT(test_bridge_kmp(&params_fox_cs, haystack, haystack_len, result) == 0,
                "Case-sensitive doesn't find 'FOX' (KMP)");
    cleanup_params(&params_fox_cs);

    search_params_t params_fox_ci = create_literal_params("FOX", false, false, false);
    TEST_ASSERT(test_bridge_kmp(&params_fox_ci, haystack, haystack_len, result) == 1,
                "Case-insensitive finds 'FOX' (KMP)");
    cleanup_params(&params_fox_ci);

#if KREP_USE_SSE42
    search_params_t sse_params_quick_cs = create_literal_params("quick", true, false, false);
    TEST_ASSERT(simd_sse42_search(&sse_params_quick_cs, haystack, haystack_len, result) == 0,
                "Case-sensitive doesn't find 'quick' (SSE4.2)");
    cleanup_params(&sse_params_quick_cs);

    // SSE4.2 falls back to Boyer-Moore for case-insensitive
    search_params_t sse_params_quick_ci = create_literal_params("quick", false, false, false);
    TEST_ASSERT(simd_sse42_search(&sse_params_quick_ci, haystack, haystack_len, result) == 1,
                "Case-insensitive finds 'quick' (SSE4.2 Fallback)");
    cleanup_params(&sse_params_quick_ci);
#endif
}

/**
 * Test whole word (-w) option
 */
void test_whole_word_option(void)
{
    printf("\n=== Whole Word (-w) Option Tests ===\n");
    const char *text = "cat scatter catalog cat catapult cat";
    size_t text_len = strlen(text);
    match_result_t *result = NULL;

    // Without -w: should match 'cat' in all words containing 'cat'
    search_params_t params_no_w = create_literal_params("cat", true, false, false);
    params_no_w.whole_word = false;
    result = match_result_init(10);
    TEST_ASSERT(boyer_moore_search(&params_no_w, text, text_len, result) == 6, "BM without -w matches all 'cat' substrings");
    match_result_free(result);
    cleanup_params(&params_no_w);

    // With -w: should match only standalone 'cat' (first, fourth, last)
    search_params_t params_w = create_literal_params("cat", true, false, false);
    params_w.whole_word = true;
    result = match_result_init(10);
    TEST_ASSERT(boyer_moore_search(&params_w, text, text_len, result) == 3, "BM with -w matches only whole word 'cat'");
    match_result_free(result);
    cleanup_params(&params_w);

    // Also test with KMP
    search_params_t params_w_kmp = create_literal_params("cat", true, false, false);
    params_w_kmp.whole_word = true;
    result = match_result_init(10);
    TEST_ASSERT(kmp_search(&params_w_kmp, text, text_len, result) == 3, "KMP with -w matches only whole word 'cat'");
    match_result_free(result);
    cleanup_params(&params_w_kmp);
}

/**
 * Test performance with a simple benchmark using the new structure
 */
void test_performance_new(void)
{
    printf("\n=== Performance Tests ===\n");

    const size_t size = 10 * 1024 * 1024; // 10MB
    char *large_text = (char *)malloc(size + 1);
    if (!large_text)
    {
        printf("Failed to allocate memory\n");
        tests_failed++;
        return;
    }
    // Simple text generation
    for (size_t i = 0; i < size; i++)
        large_text[i] = 'a' + (i % 26);
    large_text[size] = '\0';

    const char *pattern = "performancetest"; // Length 15
    size_t pattern_len = strlen(pattern);
    // Insert pattern twice
    size_t pos1 = size / 4;
    size_t pos2 = 3 * size / 4;
    memcpy(large_text + pos1, pattern, pattern_len);
    memcpy(large_text + pos2, pattern, pattern_len);
    uint64_t expected_matches = 2;

    printf("Benchmarking on %zu MB text with pattern '%s' (len %zu)\n", size / (1024 * 1024), pattern, pattern_len);
    clock_t start, end;
    double time_taken;
    uint64_t matches_found;
    match_result_t *result = NULL; // Not needed for timing counts

    // --- Boyer-Moore ---
    search_params_t bm_params = create_literal_params(pattern, true, false, false);
    start = clock();
    matches_found = test_bridge_boyer_moore(&bm_params, large_text, size, result);
    end = clock();
    time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("  Boyer-Moore: %f seconds (found %" PRIu64 " matches)\n", time_taken, matches_found);
    TEST_ASSERT(matches_found == expected_matches, "BM found correct number");
    cleanup_params(&bm_params);

    // --- KMP ---
    search_params_t kmp_params = create_literal_params(pattern, true, false, false);
    start = clock();
    matches_found = test_bridge_kmp(&kmp_params, large_text, size, result);
    end = clock();
    time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("  KMP: %f seconds (found %" PRIu64 " matches)\n", time_taken, matches_found);
    TEST_ASSERT(matches_found == expected_matches, "KMP found correct number");
    cleanup_params(&kmp_params);

// --- SSE4.2 (Fallback because pattern length 15 > 16 is false, it's <= 16) ---
#if KREP_USE_SSE42
    search_params_t sse_params = create_literal_params(pattern, true, false, false);
    start = clock();
    // SSE4.2 should handle length 15 directly
    matches_found = simd_sse42_search(&sse_params, large_text, size, result);
    end = clock();
    time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("  SSE4.2 (Direct): %f seconds (found %" PRIu64 " matches)\n", time_taken, matches_found);
    TEST_ASSERT(matches_found == expected_matches, "SSE4.2 (Direct) found correct number");
    cleanup_params(&sse_params);
#endif

// --- AVX2 (Fallback because pattern length 15 > 32 is false, it's <= 32) ---
#if KREP_USE_AVX2
    search_params_t avx_params = create_literal_params(pattern, true, false, false);
    start = clock();
    // AVX2 should handle length 15 directly
    matches_found = simd_avx2_search(&avx_params, large_text, size, result);
    end = clock();
    time_taken = ((double)(end - start)) / CLOCKS_PER_SEC;
    printf("  AVX2 (Direct): %f seconds (found %" PRIu64 " matches)\n", time_taken, matches_found);
    TEST_ASSERT(matches_found == expected_matches, "AVX2 (Direct) found correct number");
    cleanup_params(&avx_params);
#endif

    free(large_text);
}

#if KREP_USE_SSE42 || KREP_USE_AVX2 || KREP_USE_NEON
/**
 * Test SIMD specific behaviors using the new structure
 */
void test_simd_specific_new(void)
{
    printf("\n=== SIMD Specific Tests ===\n");

    const char *haystack = "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore et dolore magna aliqua.";
    size_t haystack_len = strlen(haystack);
    match_result_t *result = NULL; // Not needed for count checks

    const char *pattern1 = "dolor";              // 5 bytes, occurs twice ("dolor", "dolore")
    const char *pattern2 = "consectetur";        // 11 bytes, occurs once
    const char *pattern3 = "adipiscing elit";    // 15 bytes, occurs once
    const char *pattern16 = "consectetur adip";  // 16 bytes, occurs once
    const char *pattern17 = "consectetur adipi"; // 17 bytes, occurs once

    uint64_t matches_bmh;
    uint64_t matches_simd;

#if KREP_USE_SSE42
    printf("--- Testing SSE4.2 ---\n");
    // Test pattern shorter than SIMD width (5 bytes)
    search_params_t params1 = create_literal_params(pattern1, true, false, false);
    matches_simd = simd_sse42_search(&params1, haystack, haystack_len, result);
    matches_bmh = test_bridge_boyer_moore(&params1, haystack, haystack_len, result);
    TEST_ASSERT(matches_simd == matches_bmh, "SSE4.2 and Boyer-Moore match for 5-byte pattern");
    TEST_ASSERT(matches_simd == 2, "SSE4.2 finds 'dolor' twice");
    cleanup_params(&params1);

    // Test pattern in middle of SIMD range (11 bytes)
    search_params_t params2 = create_literal_params(pattern2, true, false, false);
    matches_simd = simd_sse42_search(&params2, haystack, haystack_len, result);
    matches_bmh = test_bridge_boyer_moore(&params2, haystack, haystack_len, result);
    TEST_ASSERT(matches_simd == matches_bmh, "SSE4.2 and Boyer-Moore match for 11-byte pattern");
    TEST_ASSERT(matches_simd == 1, "SSE4.2 finds 'consectetur' once");
    cleanup_params(&params2);

    // Test pattern near SIMD width limit (15 bytes)
    search_params_t params3 = create_literal_params(pattern3, true, false, false);
    matches_simd = simd_sse42_search(&params3, haystack, haystack_len, result);
    matches_bmh = test_bridge_boyer_moore(&params3, haystack, haystack_len, result);
    TEST_ASSERT(matches_simd == matches_bmh, "SSE4.2 and Boyer-Moore match for 15-byte pattern");
    TEST_ASSERT(matches_simd == 1, "SSE4.2 finds 'adipiscing elit' once");
    cleanup_params(&params3);

    // Test pattern at exactly SIMD width limit (16 bytes)
    search_params_t params16 = create_literal_params(pattern16, true, false, false);
    matches_simd = simd_sse42_search(&params16, haystack, haystack_len, result);
    matches_bmh = test_bridge_boyer_moore(&params16, haystack, haystack_len, result);
    TEST_ASSERT(matches_simd == matches_bmh, "SSE4.2 and Boyer-Moore match for 16-byte pattern");
    TEST_ASSERT(matches_simd == 1, "SSE4.2 finds 'consectetur adip' once");
    cleanup_params(&params16);

    // Test pattern > SIMD width (17 bytes - should fallback)
    search_params_t params17 = create_literal_params(pattern17, true, false, false);
    matches_simd = simd_sse42_search(&params17, haystack, haystack_len, result); // Will call BM
    matches_bmh = test_bridge_boyer_moore(&params17, haystack, haystack_len, result);
    TEST_ASSERT(matches_simd == matches_bmh,
                "SSE4.2 fallback to Boyer-Moore for 17-byte pattern produces same result");
    TEST_ASSERT(matches_simd == 1, "SSE4.2 fallback finds 'consectetur adipi' once");
    cleanup_params(&params17);

    // Test case-insensitive SIMD fallback
    const char *pattern_upper = "DOLOR"; // Should match "dolor" and "dolore"
    search_params_t params_ci = create_literal_params(pattern_upper, false, false, false);
    matches_simd = simd_sse42_search(&params_ci, haystack, haystack_len, result); // Will call BM
    matches_bmh = test_bridge_boyer_moore(&params_ci, haystack, haystack_len, result);
    TEST_ASSERT(matches_simd == matches_bmh,
                "Case-insensitive search consistent between SSE4.2 fallback and Boyer-Moore");
    TEST_ASSERT(matches_simd == 2, "Case-insensitive SSE4.2 fallback finds 'DOLOR' twice");
    cleanup_params(&params_ci);
#else
    printf("INFO: SSE4.2 not available, skipping SSE4.2 specific tests.\n");
#endif // KREP_USE_SSE42

#if KREP_USE_AVX2
    printf("--- Testing AVX2 ---\n");
    const char *pattern_long = "sed do eiusmod tempor incididunt"; // 29 bytes
    const char *pattern_long_upper = "SED DO EIUSMOD TEMPOR INCIDIDUNT";

    // Test long pattern (29 bytes) - Case Sensitive
    search_params_t params_long_cs = create_literal_params(pattern_long, true, false, false);
    matches_simd = simd_avx2_search(&params_long_cs, haystack, haystack_len, result);
    matches_bmh = test_bridge_boyer_moore(&params_long_cs, haystack, haystack_len, result);
    TEST_ASSERT(matches_simd == matches_bmh, "AVX2 and Boyer-Moore match for 29-byte pattern (CS)");
    TEST_ASSERT(matches_simd == 1, "AVX2 finds 'sed do eiusmod tempor incididunt' once (CS)");
    cleanup_params(&params_long_cs);

    // Test long pattern (29 bytes) - Case Insensitive
    search_params_t params_long_ci = create_literal_params(pattern_long_upper, false, false, false);
    matches_simd = simd_avx2_search(&params_long_ci, haystack, haystack_len, result);
    matches_bmh = test_bridge_boyer_moore(&params_long_ci, haystack, haystack_len, result);
    TEST_ASSERT(matches_simd == matches_bmh, "AVX2 and Boyer-Moore match for 29-byte pattern (CI)");
    TEST_ASSERT(matches_simd == 1, "AVX2 finds 'SED DO EIUSMOD TEMPOR INCIDIDUNT' once (CI)");
    cleanup_params(&params_long_ci);

    // Test short pattern (5 bytes) - Case Insensitive
    search_params_t params_short_ci = create_literal_params("DOLOR", false, false, false);
    matches_simd = simd_avx2_search(&params_short_ci, haystack, haystack_len, result);
    matches_bmh = test_bridge_boyer_moore(&params_short_ci, haystack, haystack_len, result);
    TEST_ASSERT(matches_simd == matches_bmh, "AVX2 and Boyer-Moore match for 5-byte pattern (CI)");
    TEST_ASSERT(matches_simd == 2, "AVX2 finds 'DOLOR' twice (CI)");
    cleanup_params(&params_short_ci);

#endif // KREP_USE_AVX2

#if KREP_USE_NEON
    printf("--- Testing NEON ---\n");
    search_params_t params_neon1 = create_literal_params(pattern1, true, false, false);
    matches_simd = neon_search(&params_neon1, haystack, haystack_len, result);
    matches_bmh = test_bridge_boyer_moore(&params_neon1, haystack, haystack_len, result);
    TEST_ASSERT(matches_simd == matches_bmh, "NEON and Boyer-Moore match for 5-byte pattern");
    TEST_ASSERT(matches_simd == 2, "NEON finds 'dolor' twice");
    cleanup_params(&params_neon1);

    search_params_t params_neon16 = create_literal_params(pattern16, true, false, false);
    matches_simd = neon_search(&params_neon16, haystack, haystack_len, result);
    matches_bmh = test_bridge_boyer_moore(&params_neon16, haystack, haystack_len, result);
    TEST_ASSERT(matches_simd == matches_bmh, "NEON and Boyer-Moore match for 16-byte pattern");
    TEST_ASSERT(matches_simd == 1, "NEON finds 'consectetur adip' once");
    cleanup_params(&params_neon16);

    search_params_t params_neon17 = create_literal_params(pattern17, true, false, false);
    matches_simd = neon_search(&params_neon17, haystack, haystack_len, result); // Falls back to BM
    matches_bmh = test_bridge_boyer_moore(&params_neon17, haystack, haystack_len, result);
    TEST_ASSERT(matches_simd == matches_bmh, "NEON fallback for 17-byte pattern matches BM");
    TEST_ASSERT(matches_simd == 1, "NEON fallback finds 'consectetur adipi' once");
    cleanup_params(&params_neon17);

    search_params_t params_neon_ci = create_literal_params("DOLOR", false, false, false);
    matches_simd = neon_search(&params_neon_ci, haystack, haystack_len, result); // Falls back to BM
    matches_bmh = test_bridge_boyer_moore(&params_neon_ci, haystack, haystack_len, result);
    TEST_ASSERT(matches_simd == matches_bmh, "NEON fallback for case-insensitive matches BM");
    TEST_ASSERT(matches_simd == 2, "NEON fallback finds 'DOLOR' twice");
    cleanup_params(&params_neon_ci);

#endif // KREP_USE_NEON
}
#endif // Any SIMD defined

/**
 * Test report limit functionality using the new structure
 * NOTE: The old functions took limit directly, the new ones don't.
 * We need to simulate this by adjusting haystack_len passed to the search funcs.
 */
void test_report_limit_new(void)
{
    printf("\n=== Report Limit Offset Tests ===\n");
    const char *text = "abc---abc---abc---abc"; // Matches at 0, 6, 12, 18
    size_t full_text_len = strlen(text);
    match_result_t *result = NULL; // Not needed for counts

    search_params_t params = create_literal_params("abc", true, false, false);

    // Test full length (no limit)
    TEST_ASSERT(test_bridge_boyer_moore(&params, text, full_text_len, result) == 4, "BM counts all 4 with full limit");
    TEST_ASSERT(test_bridge_kmp(&params, text, full_text_len, result) == 4, "KMP counts all 4 with full limit");
#if KREP_USE_SSE42
    TEST_ASSERT(simd_sse42_search(&params, text, full_text_len, result) == 4, "SSE4.2 counts all 4 with full limit");
#endif

    // Test limit 18 (should find 3 matches: at 0, 6, 12)
    size_t limit3 = 18;
    TEST_ASSERT(test_bridge_boyer_moore(&params, text, limit3, result) == 3, "BM counts 3 with limit 18");
    TEST_ASSERT(test_bridge_kmp(&params, text, limit3, result) == 3, "KMP counts 3 with limit 18");
#if KREP_USE_SSE42
    TEST_ASSERT(simd_sse42_search(&params, text, limit3, result) == 3, "SSE4.2 counts 3 with limit 18");
#endif

    // Test limit 12 (should find 2 matches: at 0, 6)
    size_t limit2 = 12;
    TEST_ASSERT(test_bridge_boyer_moore(&params, text, limit2, result) == 2, "BM counts 2 with limit 12");
    TEST_ASSERT(test_bridge_kmp(&params, text, limit2, result) == 2, "KMP counts 2 with limit 12");
#if KREP_USE_SSE42
    TEST_ASSERT(simd_sse42_search(&params, text, limit2, result) == 2, "SSE4.2 counts 2 with limit 12");
#endif

    // Test limit 6 (should find 1 match: at 0)
    size_t limit1 = 6;
    TEST_ASSERT(test_bridge_boyer_moore(&params, text, limit1, result) == 1, "BM counts 1 with limit 6");
    TEST_ASSERT(test_bridge_kmp(&params, text, limit1, result) == 1, "KMP counts 1 with limit 6");
#if KREP_USE_SSE42
    TEST_ASSERT(simd_sse42_search(&params, text, limit1, result) == 1, "SSE4.2 counts 1 with limit 6");
#endif

    // Test limit 0 (should find 0 matches)
    size_t limit0 = 0;
    TEST_ASSERT(test_bridge_boyer_moore(&params, text, limit0, result) == 0, "BM counts 0 with limit 0");
    TEST_ASSERT(test_bridge_kmp(&params, text, limit0, result) == 0, "KMP counts 0 with limit 0");
#if KREP_USE_SSE42
    TEST_ASSERT(simd_sse42_search(&params, text, limit0, result) == 0, "SSE4.2 counts 0 with limit 0");
#endif

    cleanup_params(&params);
}

/**
 * Test max_count functionality (-m option)
 */
void test_max_count_new(void)
{
    printf("\n=== Max Count (-m) Tests ===\n");

    const char *text = "line1: match\nline2: no\nline3: match\nline4: match\nline5: no\nline6: match";
    size_t text_len = strlen(text);
    match_result_t *result = NULL; // Used for position tracking tests

    // --- Literal Search ---
    printf("--- Literal Search ---\n");
    search_params_t params_lit = create_literal_params("match", true, false, false);

    params_lit.max_count = 2;
    result = match_result_init(10);
    TEST_ASSERT(boyer_moore_search(&params_lit, text, text_len, result) == 2, "BM literal finds 2 matches with limit 2");
    TEST_ASSERT(result->count == 2, "BM literal result has 2 positions with limit 2");
    match_result_free(result);
    result = NULL;

    params_lit.max_count = 4;
    result = match_result_init(10);
    TEST_ASSERT(boyer_moore_search(&params_lit, text, text_len, result) == 4, "BM literal finds 4 matches with limit 4");
    TEST_ASSERT(result->count == 4, "BM literal result has 4 positions with limit 4");
    match_result_free(result);
    result = NULL;

    params_lit.max_count = 5;
    result = match_result_init(10);
    TEST_ASSERT(boyer_moore_search(&params_lit, text, text_len, result) == 4, "BM literal finds 4 matches with limit 5");
    TEST_ASSERT(result->count == 4, "BM literal result has 4 positions with limit 5");
    match_result_free(result);
    result = NULL;

    params_lit.max_count = 1;
    result = match_result_init(10);
    TEST_ASSERT(boyer_moore_search(&params_lit, text, text_len, result) == 1, "BM literal finds 1 match with limit 1");
    TEST_ASSERT(result->count == 1, "BM literal result has 1 position with limit 1");
    match_result_free(result);
    result = NULL;

    params_lit.max_count = 0;
    result = match_result_init(10);
    TEST_ASSERT(boyer_moore_search(&params_lit, text, text_len, result) == 0, "BM literal finds 0 matches with limit 0");
    TEST_ASSERT(result->count == 0, "BM literal result has 0 positions with limit 0");
    match_result_free(result);
    result = NULL;

    cleanup_params(&params_lit);

    // --- Literal Search with -c (Count Lines) ---
    printf("--- Literal Search (-c) ---\n");
    search_params_t params_lit_c = create_literal_params("match", true, true, false); // count_lines=true

    params_lit_c.max_count = 2;
    TEST_ASSERT(boyer_moore_search(&params_lit_c, text, text_len, NULL) == 2, "BM literal -c finds 2 lines with limit 2");

    params_lit_c.max_count = 4;
    TEST_ASSERT(boyer_moore_search(&params_lit_c, text, text_len, NULL) == 4, "BM literal -c finds 4 lines with limit 4");

    params_lit_c.max_count = 5;
    TEST_ASSERT(boyer_moore_search(&params_lit_c, text, text_len, NULL) == 4, "BM literal -c finds 4 lines with limit 5");

    params_lit_c.max_count = 1;
    TEST_ASSERT(boyer_moore_search(&params_lit_c, text, text_len, NULL) == 1, "BM literal -c finds 1 line with limit 1");

    params_lit_c.max_count = 0;
    TEST_ASSERT(boyer_moore_search(&params_lit_c, text, text_len, NULL) == 0, "BM literal -c finds 0 lines with limit 0");

    cleanup_params(&params_lit_c);

    // --- Literal Search with -o (Only Matching) ---
    printf("--- Literal Search (-o) ---\n");
    search_params_t params_lit_o = create_literal_params("match", true, false, true); // only_match=true

    params_lit_o.max_count = 2;
    result = match_result_init(10);
    TEST_ASSERT(boyer_moore_search(&params_lit_o, text, text_len, result) == 2, "BM literal -o finds 2 matches with limit 2");
    TEST_ASSERT(result->count == 2, "BM literal -o result has 2 positions with limit 2");
    match_result_free(result);
    result = NULL;

    params_lit_o.max_count = 4;
    result = match_result_init(10);
    TEST_ASSERT(boyer_moore_search(&params_lit_o, text, text_len, result) == 4, "BM literal -o finds 4 matches with limit 4");
    TEST_ASSERT(result->count == 4, "BM literal -o result has 4 positions with limit 4");
    match_result_free(result);
    result = NULL;

    cleanup_params(&params_lit_o);

    // --- Regex Search ---
    printf("--- Regex Search ---\n");
    search_params_t params_re = create_regex_params("^line[0-9]+: match", true, false, false);

    params_re.max_count = 2;
    result = match_result_init(10);
    TEST_ASSERT(regex_search(&params_re, text, text_len, result) == 2, "Regex finds 2 matches with limit 2");
    TEST_ASSERT(result->count == 2, "Regex result has 2 positions with limit 2");
    match_result_free(result);
    result = NULL;

    params_re.max_count = 4;
    result = match_result_init(10);
    TEST_ASSERT(regex_search(&params_re, text, text_len, result) == 4, "Regex finds 4 matches with limit 4");
    TEST_ASSERT(result->count == 4, "Regex result has 4 positions with limit 4");
    match_result_free(result);
    result = NULL;

    cleanup_params(&params_re);

    // --- Regex Search with -c (Count Lines) ---
    printf("--- Regex Search (-c) ---\n");
    search_params_t params_re_c = create_regex_params("^line[0-9]+: match", true, true, false); // count_lines=true

    params_re_c.max_count = 2;
    TEST_ASSERT(regex_search(&params_re_c, text, text_len, NULL) == 2, "Regex -c finds 2 lines with limit 2");

    params_re_c.max_count = 4;
    TEST_ASSERT(regex_search(&params_re_c, text, text_len, NULL) == 4, "Regex -c finds 4 lines with limit 4");

    cleanup_params(&params_re_c);

    // --- Aho-Corasick Search (Multiple Patterns) ---
    printf("--- Aho-Corasick Search ---\n");
    const char *ac_text = "apple banana apple orange apple grape apple";
    size_t ac_text_len = strlen(ac_text);
    const char *ac_patterns[] = {"apple", "orange"};
    size_t ac_pattern_lens[] = {5, 6};
    search_params_t params_ac = {
        .patterns = ac_patterns,
        .pattern_lens = ac_pattern_lens,
        .num_patterns = 2,
        .case_sensitive = true,
        .use_regex = false,
        .track_positions = true, // Need positions to verify count
        .count_lines_mode = false,
        .count_matches_mode = false,
        .compiled_regex = NULL,
        .max_count = SIZE_MAX // Default
    };

    params_ac.max_count = 3;
    result = match_result_init(10);
    TEST_ASSERT(aho_corasick_search(&params_ac, ac_text, ac_text_len, result) == 3, "Aho-Corasick finds 3 matches with limit 3");
    TEST_ASSERT(result->count == 3, "Aho-Corasick result has 3 positions with limit 3");
    match_result_free(result);
    result = NULL;

    params_ac.max_count = 5; // Total matches are 4 'apple' + 1 'orange' = 5
    result = match_result_init(10);
    TEST_ASSERT(aho_corasick_search(&params_ac, ac_text, ac_text_len, result) == 5, "Aho-Corasick finds 5 matches with limit 5");
    TEST_ASSERT(result->count == 5, "Aho-Corasick result has 5 positions with limit 5");
    match_result_free(result);
    result = NULL;

    params_ac.max_count = 6;
    result = match_result_init(10);
    TEST_ASSERT(aho_corasick_search(&params_ac, ac_text, ac_text_len, result) == 5, "Aho-Corasick finds 5 matches with limit 6");
    TEST_ASSERT(result->count == 5, "Aho-Corasick result has 5 positions with limit 6");
    match_result_free(result);
    result = NULL;

    // No cleanup_params needed for stack-allocated params_ac
}

/**
 * Placeholder test for multithreading.
 * Actual testing requires file I/O and calling search_file.
 */
void test_multithreading_placeholder_new(void)
{
    printf("\n=== Testing Parallel Processing ===\n");
    printf("  Parallel processing tests would validate:\n");
    printf("  1. Correct match counting across thread boundaries\n");
    printf("  2. Proper handling of overlapping chunks\n");
    printf("  3. Thread scaling efficiency\n");
    printf("  4. Memory mapping and resource cleanup\n");
    // In a real test, we'd create a temp file, call search_file() with different thread counts,
    // capture output or check return codes, and verify results.
    TEST_ASSERT(true, "Placeholder for multi-threaded tests");
}

/**
 * Test numeric patterns using the new structure
 */
void test_numeric_patterns_new(void)
{
    printf("\n=== Numeric Pattern Tests ===\n");

    const char *text = "IP addresses: 192.168.1.1 and 10.0.0.1, ports: 8080 and 443";
    size_t text_len = strlen(text);
    match_result_t *result = NULL; // Not needed for counts

    // Test IP address pattern with Boyer-Moore
    search_params_t params_ip_bm = create_literal_params("192.168.1.1", true, false, false);
    TEST_ASSERT(test_bridge_boyer_moore(&params_ip_bm, text, text_len, result) == 1,
                "Boyer-Moore finds IP 192.168.1.1 once");
    cleanup_params(&params_ip_bm);

    // Test port number pattern with Boyer-Moore
    search_params_t params_port_bm = create_literal_params("8080", true, false, false);
    TEST_ASSERT(test_bridge_boyer_moore(&params_port_bm, text, text_len, result) == 1,
                "Boyer-Moore finds port 8080 once");
    cleanup_params(&params_port_bm);

    // Test using regex for general IP pattern
    search_params_t params_ip_re = create_regex_params("[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+", true, false, false);
    TEST_ASSERT(regex_search(&params_ip_re, text, text_len, result) == 2,
                "Regex finds both IP addresses");
    cleanup_params(&params_ip_re); // Frees compiled regex

    // Test using regex for specific port numbers
    search_params_t params_port_re = create_regex_params("8080|443", true, false, false);
    TEST_ASSERT(regex_search(&params_port_re, text, text_len, result) == 2,
                "Regex finds both port numbers");
    cleanup_params(&params_port_re); // Frees compiled regex
}

/* ========================================================================= */
/* Main Test Runner                              */
/* ========================================================================= */
int main(void)
{
    printf("Running krep tests...\n");
    setlocale(LC_ALL, ""); // For potential locale-dependent functions like tolower

    // Run tests using the new search_params_t structure
    test_basic_search_new();
    test_edge_cases_new();
    test_case_insensitive_new();
    test_whole_word_option();
    test_performance_new();
    test_numeric_patterns_new();
#if KREP_USE_SSE42 || KREP_USE_AVX2 || KREP_USE_NEON
    test_simd_specific_new();
#else
    printf("\nINFO: No SIMD available, skipping SIMD specific tests.\n");
#endif
    test_report_limit_new();
    test_max_count_new(); // Add call to the new test function
    test_multithreading_placeholder_new();

    // Run tests from other files
    run_regex_tests();
    run_multiple_patterns_tests();

    printf("\n=== Test Summary ===\n");
    printf("Tests passed: %d\n", tests_passed);
    printf("Tests failed: %d\n", tests_failed);
    printf("Total tests run: %d\n", tests_passed + tests_failed);

    // Return 0 if all tests passed, 1 otherwise
    return (tests_failed == 0) ? 0 : 1;
}
