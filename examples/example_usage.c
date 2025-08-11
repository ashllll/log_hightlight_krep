/* example_usage.c - Example of using krep_simple.dll
 *
 * Compile on Windows with:
 *   gcc example_usage.c -L. -lkrep_simple -o example_usage.exe
 *
 * Make sure krep_simple.dll is in the same directory or in PATH
 */

#include <stdio.h>
#include <string.h>
#include "krep_simple.h"

int main() {
    printf("krep Simple DLL Example\n");
    printf("Version: %s\n\n", get_version());
    
    // Test string
    const char *text = "This is a test string with test patterns and more test cases.";
    const char *pattern = "test";
    
    printf("Searching for '%s' in:\n\"%s\"\n\n", pattern, text);
    
    // Initialize result storage
    match_result_t *result = match_result_init(10);
    if (!result) {
        printf("Failed to initialize match result\n");
        return 1;
    }
    
    // Perform case-sensitive search
    printf("1. Case-sensitive search:\n");
    uint64_t matches = search_buffer(pattern, strlen(pattern), text, strlen(text), 
                                   true, false, result);
    printf("Found %llu matches:\n", (unsigned long long)matches);
    for (uint64_t i = 0; i < result->count; i++) {
        size_t start = result->positions[i].start_offset;
        size_t end = result->positions[i].end_offset;
        printf("  Match %llu: position %zu-%zu\n", (unsigned long long)(i+1), start, end-1);
    }
    
    // Clear previous results
    result->count = 0;
    
    // Perform case-insensitive search
    printf("\n2. Case-insensitive search for 'TEST':\n");
    matches = search_buffer("TEST", 4, text, strlen(text), false, false, result);
    printf("Found %llu matches:\n", (unsigned long long)matches);
    for (uint64_t i = 0; i < result->count; i++) {
        size_t start = result->positions[i].start_offset;
        size_t end = result->positions[i].end_offset;
        printf("  Match %llu: position %zu-%zu\n", (unsigned long long)(i+1), start, end-1);
    }
    
    // Clear previous results
    result->count = 0;
    
    // Perform whole-word search
    printf("\n3. Whole-word search for 'test':\n");
    matches = search_buffer("test", 4, text, strlen(text), true, true, result);
    printf("Found %llu whole-word matches:\n", (unsigned long long)matches);
    for (uint64_t i = 0; i < result->count; i++) {
        size_t start = result->positions[i].start_offset;
        size_t end = result->positions[i].end_offset;
        printf("  Match %llu: position %zu-%zu\n", (unsigned long long)(i+1), start, end-1);
    }
    
    // Test with search parameters structure
    printf("\n4. Using search_params_simple_t structure:\n");
    search_params_simple_t params = {
        .pattern = "is",
        .pattern_len = 2,
        .case_sensitive = false,
        .whole_word = true,
        .max_count = 2  // Limit to first 2 matches
    };
    
    result->count = 0; // Clear previous results
    matches = search_string_simple(&params, text, strlen(text), result);
    printf("Found %llu matches for whole-word 'is' (max 2):\n", (unsigned long long)matches);
    for (uint64_t i = 0; i < result->count; i++) {
        size_t start = result->positions[i].start_offset;
        size_t end = result->positions[i].end_offset;
        printf("  Match %llu: position %zu-%zu\n", (unsigned long long)(i+1), start, end-1);
    }
    
    // Count-only search (no position tracking)
    printf("\n5. Count-only search (no position storage):\n");
    matches = search_buffer("t", 1, text, strlen(text), false, false, NULL);
    printf("Found %llu occurrences of 't' (case-insensitive)\n", (unsigned long long)matches);
    
    // Cleanup
    match_result_free(result);
    
    printf("\nDLL test completed successfully!\n");
    return 0;
}