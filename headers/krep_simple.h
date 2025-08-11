/* krep_simple.h - Simple Windows DLL header
 *
 * Author: Based on krep by Davide Santangelo
 * Year: 2025
 */

#ifndef KREP_SIMPLE_H
#define KREP_SIMPLE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef _WIN32
    #ifdef BUILDING_DLL
        #define KREP_API __declspec(dllexport)
    #else
        #define KREP_API __declspec(dllimport)
    #endif
#else
    #define KREP_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* Data structures */
typedef struct {
    size_t start_offset;
    size_t end_offset;
} match_position_t;

typedef struct {
    match_position_t *positions;
    uint64_t count;
    uint64_t capacity;
} match_result_t;

typedef struct {
    const char *pattern;
    size_t pattern_len;
    bool case_sensitive;
    bool whole_word;
    size_t max_count;
} search_params_simple_t;

/* API Functions */

/**
 * @brief Initialize a match result structure
 * @param initial_capacity Initial capacity for storing matches
 * @return Pointer to match_result_t or NULL on error
 */
KREP_API match_result_t* match_result_init(uint64_t initial_capacity);

/**
 * @brief Add a match to the result structure
 * @param result The match result structure
 * @param start_offset Start position of the match
 * @param end_offset End position of the match
 * @return true on success, false on error
 */
KREP_API bool match_result_add(match_result_t *result, size_t start_offset, size_t end_offset);

/**
 * @brief Free a match result structure
 * @param result The match result structure to free
 */
KREP_API void match_result_free(match_result_t *result);

/**
 * @brief Search for a pattern in a string buffer
 * @param params Search parameters
 * @param text Text buffer to search in
 * @param text_len Length of text buffer
 * @param result Match result structure (can be NULL for count-only)
 * @return Number of matches found
 */
KREP_API uint64_t search_string_simple(const search_params_simple_t *params, const char *text, size_t text_len, match_result_t *result);

/**
 * @brief Simple search function for basic usage
 * @param pattern Pattern to search for
 * @param pattern_len Length of pattern
 * @param text Text to search in
 * @param text_len Length of text
 * @param case_sensitive Whether search is case-sensitive
 * @param whole_word Whether to match whole words only
 * @param result Match result structure (can be NULL for count-only)
 * @return Number of matches found
 */
KREP_API uint64_t search_buffer(const char *pattern, size_t pattern_len, const char *text, size_t text_len, bool case_sensitive, bool whole_word, match_result_t *result);

/**
 * @brief Get DLL version string
 * @return Version string
 */
KREP_API const char* get_version(void);

#ifdef __cplusplus
}
#endif

#endif /* KREP_SIMPLE_H */