/**
 * Header file for krep - A high-performance string search utility
 *
 * Author: Davide Santangelo
 * Version: 0.3.2
 * Year: 2025
 */

#ifndef KREP_H
#define KREP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> // For size_t
#include <regex.h>  // Needed for regex_t type

/* --- Match tracking structure --- */
typedef struct
{
   size_t start_offset; // Starting position of the match
   size_t end_offset;   // Ending position of the match (exclusive)
} match_position_t;

typedef struct
{
   match_position_t *positions; // Dynamically resizing array of match positions
   uint64_t count;              // Number of matches found and stored
   uint64_t capacity;           // Current allocated capacity of the positions array
} match_result_t;

/* --- Public API --- */

/**
 * @brief Searches for a pattern within a file using memory mapping and adaptive threading.
 *
 * @param filename Path to the file to search. Use "-" for stdin (currently not implemented).
 * @param pattern The null-terminated string pattern to search for or regex pattern.
 * @param pattern_len The length of the pattern (used for non-regex).
 * @param case_sensitive If true, perform case-sensitive search; otherwise, case-insensitive.
 * @param count_only If true, only print the total count of matches to stdout.
 * @param thread_count Requested number of threads (0 or negative for default, adjusted automatically).
 * @param use_regex If true, treat the pattern as an extended POSIX regular expression.
 * @return 0 on success, non-zero on error. Match count or lines are printed to stdout.
 */
int search_file(const char *filename, const char *pattern, size_t pattern_len, bool case_sensitive,
                bool count_only, int thread_count, bool use_regex);

/**
 * @brief Searches for a pattern within a given string (single-threaded).
 *
 * @param pattern The null-terminated string pattern or regex pattern.
 * @param pattern_len The length of the pattern (used for non-regex).
 * @param text The null-terminated string to search within.
 * @param case_sensitive If true, perform case-sensitive search; otherwise, case-insensitive.
 * @param use_regex If true, treat the pattern as an extended POSIX regular expression.
 * @param count_only If true, only print the count of matches found to stdout.
 * @return 0 on success, non-zero on error. Match count or lines are printed to stdout.
 */
int search_string(const char *pattern, size_t pattern_len, const char *text, bool case_sensitive, bool use_regex, bool count_only);

/* --- Match result management functions (potentially useful externally) --- */

/**
 * @brief Initialize a match result structure for storing match positions.
 *
 * @param initial_capacity Initial capacity for the positions array (e.g., 100).
 * @return match_result_t* Pointer to the initialized structure, or NULL on allocation failure.
 */
match_result_t *match_result_init(uint64_t initial_capacity);

/**
 * @brief Add a found match position to the result structure. Handles resizing.
 *
 * @param result Pointer to the match result structure.
 * @param start_offset Starting byte offset of the match within the searched text.
 * @param end_offset Ending byte offset (exclusive) of the match within the searched text.
 * @return bool True if the position was added successfully, false on allocation failure.
 */
bool match_result_add(match_result_t *result, size_t start_offset, size_t end_offset);

/**
 * @brief Free resources associated with a match result structure (the positions array and the struct itself).
 *
 * @param result Pointer to the match result structure to free.
 */
void match_result_free(match_result_t *result);

/**
 * @brief Print lines containing matches based on the positions stored in the result structure.
 *
 * @param text The original text buffer that was searched.
 * @param text_len Length of the text buffer.
 * @param result Pointer to the match_result_t structure containing match positions.
 */
void print_matching_lines(const char *text, size_t text_len, const match_result_t *result);

/* --- Internal Search Algorithm Declarations (Exposed for potential direct use/testing) --- */
/* Note: These functions now include a report_limit_offset parameter. Matches starting
   at or after this offset (relative to the start of 'text') are NOT counted by that specific call.
   This is primarily used for multi-threading to avoid double-counting matches at chunk boundaries. */

/**
 * @brief Boyer-Moore-Horspool search algorithm implementation.
 * Counts matches starting before report_limit_offset.
 */
uint64_t boyer_moore_search(const char *text, size_t text_len,
                            const char *pattern, size_t pattern_len,
                            bool case_sensitive, size_t report_limit_offset);

/**
 * @brief Knuth-Morris-Pratt (KMP) search algorithm implementation.
 * Counts matches starting before report_limit_offset.
 */
uint64_t kmp_search(const char *text, size_t text_len,
                    const char *pattern, size_t pattern_len,
                    bool case_sensitive, size_t report_limit_offset);

/**
 * @brief Rabin-Karp search algorithm implementation.
 * Counts matches starting before report_limit_offset.
 */
uint64_t rabin_karp_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset);

/**
 * @brief Regex-based search using a pre-compiled POSIX regex.
 * Counts matches starting before report_limit_offset. Optionally tracks positions.
 *
 * @param text The text to search within.
 * @param text_len Length of the text.
 * @param compiled_regex Pointer to the pre-compiled regex object (must be valid).
 * @param report_limit_offset Maximum offset to report matches (matches starting at or beyond this offset are ignored).
 * @param track_positions If true, track and store match positions in the result structure.
 * @param result Pointer to a match_result_t structure to store positions (required if track_positions is true, must be initialized). Can be NULL if track_positions is false.
 * @return uint64_t Number of matches found starting before report_limit_offset.
 */
uint64_t regex_search(const char *text, size_t text_len,
                      const regex_t *compiled_regex,
                      size_t report_limit_offset,
                      bool track_positions,
                      match_result_t *result);

/* --- SIMD Placeholders/Implementations --- */
#ifdef __SSE4_2__
/**
 * @brief SIMD-accelerated search using SSE4.2 intrinsics (e.g., PCMPESTRI). (Fallback)
 * Counts matches starting before report_limit_offset.
 */
uint64_t simd_sse42_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset);
#endif

#ifdef __AVX2__
/**
 * @brief SIMD-accelerated search using AVX2 intrinsics. (Fallback)
 * Counts matches starting before report_limit_offset.
 */
uint64_t simd_avx2_search(const char *text, size_t text_len,
                          const char *pattern, size_t pattern_len,
                          bool case_sensitive, size_t report_limit_offset);
#endif

#ifdef __ARM_NEON // Placeholder for potential NEON implementation
/**
 * @brief SIMD-accelerated search using ARM NEON intrinsics. (Fallback)
 * Counts matches starting before report_limit_offset.
 */
uint64_t neon_search(const char *text, size_t text_len,
                     const char *pattern, size_t pattern_len,
                     bool case_sensitive, size_t report_limit_offset);
#endif

#endif /* KREP_H */
