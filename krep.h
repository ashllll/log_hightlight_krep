/**
 * Header file for krep - A high-performance string search utility
 *
 * Author: Davide Santangelo
 * Version: 0.2.3
 * Year: 2025
 */

#ifndef KREP_H
#define KREP_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> // For size_t

/* --- Public API --- */

/**
 * @brief Searches for a pattern within a file using memory mapping and adaptive threading.
 *
 * @param filename Path to the file to search. Use "-" for stdin (currently not implemented).
 * @param pattern The null-terminated string pattern to search for.
 * @param pattern_len The length of the pattern (calculated if 0, but prefer passing it).
 * @param case_sensitive If true, perform case-sensitive search; otherwise, case-insensitive.
 * @param count_only If true, only print the total count of matches.
 * @param thread_count Requested number of threads (0 or negative for default, adjusted automatically).
 * @return 0 on success, non-zero on error.
 */
int search_file(const char *filename, const char *pattern, size_t pattern_len, bool case_sensitive,
                bool count_only, int thread_count);

/**
 * @brief Searches for a pattern within a given string (single-threaded).
 *
 * @param pattern The null-terminated string pattern to search for.
 * @param pattern_len The length of the pattern.
 * @param text The null-terminated string to search within.
 * @param case_sensitive If true, perform case-sensitive search; otherwise, case-insensitive.
 * @return 0 on success (match count printed), non-zero on error.
 */
int search_string(const char *pattern, size_t pattern_len, const char *text, bool case_sensitive);

/* --- Internal Search Algorithm Declarations (Exposed for potential direct use/testing) --- */
/* Note: These functions now include a report_limit_offset parameter. Matches starting
   at or after this offset (relative to the start of 'text') are NOT counted. */

/**
 * @brief Boyer-Moore-Horspool search algorithm implementation.
 */
uint64_t boyer_moore_search(const char *text, size_t text_len,
                            const char *pattern, size_t pattern_len,
                            bool case_sensitive, size_t report_limit_offset);

/**
 * @brief Knuth-Morris-Pratt (KMP) search algorithm implementation.
 */
uint64_t kmp_search(const char *text, size_t text_len,
                    const char *pattern, size_t pattern_len,
                    bool case_sensitive, size_t report_limit_offset);

/**
 * @brief Rabin-Karp search algorithm implementation.
 */
uint64_t rabin_karp_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset);

/* --- SIMD Placeholders/Implementations --- */
#ifdef __SSE4_2__
/**
 * @brief SIMD-accelerated search using SSE4.2 intrinsics (e.g., PCMPESTRI).
 * Best suited for patterns up to 16 bytes.
 */
uint64_t simd_sse42_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset);
#endif

#ifdef __AVX2__
/**
 * @brief SIMD-accelerated search using AVX2 intrinsics. (Placeholder)
 * Potentially faster than SSE4.2, suitable for patterns up to 32 bytes.
 * Falls back currently.
 */
uint64_t simd_avx2_search(const char *text, size_t text_len,
                          const char *pattern, size_t pattern_len,
                          bool case_sensitive, size_t report_limit_offset);
#endif

#ifdef __ARM_NEON // Placeholder for potential NEON implementation
/**
 * @brief SIMD-accelerated search using ARM NEON intrinsics. (Placeholder)
 */
uint64_t neon_search(const char *text, size_t text_len,
                     const char *pattern, size_t pattern_len,
                     bool case_sensitive, size_t report_limit_offset);
#endif

#endif /* KREP_H */
