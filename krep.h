/* krep.h - Header file for krep utility
 *
 * Author: Davide Santangelo
 * Version: 0.3.7
 * Year: 2025
 */

#ifndef KREP_H
#define KREP_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h> // For size_t definition
#include <regex.h> // For regex_t type

/* --- ANSI Color Codes --- */
#define KREP_COLOR_MATCH "\033[1;31m" // Bold Red
#define KREP_COLOR_RESET "\033[0m"
#define KREP_COLOR_FILENAME "\033[35m"  // Magenta for filename
#define KREP_COLOR_SEPARATOR "\033[36m" // Cyan for separator (:)

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
int search_file(const char *filename, const char *pattern, size_t pattern_len, bool case_sensitive,
                bool count_only, int thread_count, bool use_regex);

int search_string(const char *pattern, size_t pattern_len, const char *text, bool case_sensitive, bool use_regex, bool count_only);

/* --- Match result management functions --- */
match_result_t *match_result_init(uint64_t initial_capacity);
bool match_result_add(match_result_t *result, size_t start_offset, size_t end_offset);
void match_result_free(match_result_t *result);

/**
 * @brief Print matching lines or only matched parts (-o). Handles highlighting and unique lines.
 *
 * @param filename Optional filename prefix. NULL if not needed.
 * @param text The original text buffer.
 * @param text_len Length of the text buffer.
 * @param result Pointer to the match_result_t structure containing positions.
 * @return The number of items printed (unique lines or matches).
 */
size_t print_matching_items(const char *filename, const char *text, size_t text_len, const match_result_t *result);

void print_usage(const char *program_name);

/* --- Internal Search Algorithm Declarations (Updated Signatures) --- */

/**
 * @brief Boyer-Moore-Horspool search algorithm implementation.
 * Returns total matches found. Optionally counts unique lines or tracks positions.
 *
 * @param text_start Pointer to the start of the text buffer.
 * @param text_len Length of text buffer.
 * @param pattern Pattern to search for.
 * @param pattern_len Length of pattern.
 * @param case_sensitive Case sensitivity flag.
 * @param report_limit_offset Offset limit for considering matches/lines.
 * @param count_lines_mode If true, count unique lines and store in line_match_count.
 * @param line_match_count Pointer to store unique line count (used if count_lines_mode is true).
 * @param last_counted_line_start Pointer to track the start offset of the last counted line.
 * @param track_positions If true (and not count_lines_mode), store match positions in result.
 * @param result Pointer to initialized match_result_t (required if track_positions is true).
 * @return uint64_t Total physical match count found within the report limit.
 */
uint64_t boyer_moore_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                            bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                            uint64_t *line_match_count, size_t *last_counted_line_start,
                            bool track_positions, match_result_t *result);

/**
 * @brief Knuth-Morris-Pratt (KMP) search algorithm implementation.
 * (Parameters and return value same as boyer_moore_search)
 */
uint64_t kmp_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                    bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                    uint64_t *line_match_count, size_t *last_counted_line_start,
                    bool track_positions, match_result_t *result);

/**
 * @brief Rabin-Karp search algorithm implementation.
 * (Parameters and return value same as boyer_moore_search)
 */
uint64_t rabin_karp_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                           uint64_t *line_match_count, size_t *last_counted_line_start,
                           bool track_positions, match_result_t *result);

/**
 * @brief Regex-based search using a pre-compiled POSIX regex.
 * (Parameters and return value similar to boyer_moore_search, uses compiled_regex instead of pattern/pattern_len)
 */
uint64_t regex_search(const char *text_start, size_t text_len, const regex_t *compiled_regex,
                      size_t report_limit_offset, bool count_lines_mode,
                      uint64_t *line_match_count, size_t *last_counted_line_start,
                      bool track_positions, match_result_t *result);

/* --- SIMD Placeholders/Implementations --- */
#ifdef __SSE4_2__
/**
 * @brief SIMD-accelerated search using SSE4.2 intrinsics (Fallback).
 * (Parameters and return value same as boyer_moore_search)
 */
uint64_t simd_sse42_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                           uint64_t *line_match_count, size_t *last_counted_line_start,
                           bool track_positions, match_result_t *result);
#endif

#ifdef __AVX2__
/**
 * @brief SIMD-accelerated search using AVX2 intrinsics (Fallback).
 * (Parameters and return value same as boyer_moore_search)
 */
uint64_t simd_avx2_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                          bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                          uint64_t *line_match_count, size_t *last_counted_line_start,
                          bool track_positions, match_result_t *result);
#endif

#ifdef __ARM_NEON
/**
 * @brief SIMD-accelerated search using ARM NEON intrinsics (Fallback).
 * (Parameters and return value same as boyer_moore_search)
 */
uint64_t neon_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                     bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                     uint64_t *line_match_count, size_t *last_counted_line_start,
                     bool track_positions, match_result_t *result);

// These were likely placeholders and don't match the updated signature, commenting out for now
// uint64_t neon_search_short(...);
// bool has_advanced_neon_features(void);
#endif

/* --- SIMD Constants (Consider moving if header gets too large) --- */
// #define SIMD_MAX_LEN_SSE42 16
// #define SIMD_MAX_LEN_AVX2 32
// #define SIMD_MAX_LEN_NEON 16
// #define SIMD_ALIGN_SSE 16
// #define SIMD_ALIGN_AVX 32
// #define SIMD_ALIGN_NEON 16

// static inline bool is_aligned_for_simd(const void *ptr, size_t alignment) {
//    return ((uintptr_t)ptr & (alignment - 1)) == 0;
// }

#endif /* KREP_H */
