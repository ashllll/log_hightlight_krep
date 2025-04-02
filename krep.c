/* krep - A high-performance string search utility
 *
 * Author: Davide Santangelo
 * Version: 0.3.3
 * Year: 2025
 *
 * Features:
 * - Multiple search algorithms (Boyer-Moore-Horspool, KMP, Rabin-Karp)
 * - Basic SIMD (SSE4.2) acceleration implemented (Currently Disabled - Fallback)
 * - Placeholders for AVX2 acceleration
 * - Memory-mapped file I/O for maximum throughput (Optimized flags)
 * - Multi-threaded parallel search for large files
 * - Case-sensitive and case-insensitive matching
 * - Direct string search in addition to file search
 * - Regular expression search support (POSIX, compiled once, fixed loop)
 * - Matching line printing with filename prefix and highlighting (single-thread only)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <ctype.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <pthread.h>
#include <inttypes.h>
#include <errno.h>
#include <limits.h> // For SIZE_MAX
#include <regex.h>  // For POSIX regex support

// Include SIMD intrinsics if available
#ifdef __SSE4_2__
#include <nmmintrin.h> // SSE4.2 intrinsics (for _mm_cmpestri)
// Define constant only when SSE4.2 is available and used
const size_t SIMD_MAX_LEN_SSE42 = 16;
#endif

#ifdef __AVX2__
#include <immintrin.h> // AVX2 intrinsics
// Define constant only when AVX2 is available and used (adjust value if needed)
const size_t SIMD_MAX_LEN_AVX2 = 32;
#endif

#include "krep.h" // Include own header

/* Constants */
#define MAX_PATTERN_LENGTH 1024
#define DEFAULT_THREAD_COUNT 4
#define MIN_FILE_SIZE_FOR_THREADS (1 * 1024 * 1024) // 1MB minimum for threading
// #define CHUNK_SIZE (16 * 1024 * 1024)            // Base chunk size - currently unused, calculated dynamically
#define VERSION "0.3.3"

// ANSI Color Codes for Highlighting
#define KREP_COLOR_MATCH "\033[1;31m" // Bold Red
#define KREP_COLOR_RESET "\033[0m"
#define KREP_COLOR_FILENAME "\033[35m"  // Magenta for filename
#define KREP_COLOR_SEPARATOR "\033[36m" // Cyan for separator (:)

// --- Global state for color output ---
static bool color_output_enabled = false; // Determined in main

// --- Global lookup table for fast lowercasing ---
static unsigned char lower_table[256];

// Initialize the lookup table once before main using a constructor
static void __attribute__((constructor)) init_lower_table(void)
{
    for (int i = 0; i < 256; i++)
    {
        lower_table[i] = tolower(i);
    }
}

/* --- Type definitions --- */
// (search_job_t remains the same)
typedef struct
{
    const char *file_data; // Memory-mapped file content pointer
    size_t file_size;      // Total size of the file
    size_t start_pos;      // Starting position for this thread's primary chunk
    size_t end_pos;        // Ending position for this thread's primary chunk
    const char *pattern;   // Search pattern string (still needed for non-regex)
    size_t pattern_len;    // Length of search pattern
    bool case_sensitive;   // Whether search is case-sensitive
    bool use_regex;        // Whether to use regex for search
    const regex_t *regex;  // Pointer to the pre-compiled regex (if use_regex is true)
    int thread_id;         // Thread identifier
    int total_threads;     // Total number of threads
    uint64_t local_count;  // Local match counter for this thread
} search_job_t;

/* --- Match result management functions --- */
// (match_result_init, match_result_add, match_result_free remain the same)
match_result_t *match_result_init(uint64_t initial_capacity)
{
    match_result_t *result = malloc(sizeof(match_result_t));
    if (!result)
        return NULL;

    // Ensure initial capacity is reasonable
    if (initial_capacity == 0)
        initial_capacity = 16;

    result->positions = malloc(initial_capacity * sizeof(match_position_t));
    if (!result->positions)
    {
        free(result);
        return NULL;
    }

    result->count = 0;
    result->capacity = initial_capacity;
    return result;
}

bool match_result_add(match_result_t *result, size_t start_offset, size_t end_offset)
{
    if (!result)
        return false;

    if (result->count >= result->capacity)
    {
        // Prevent excessively large allocations
        if (result->capacity > SIZE_MAX / 2)
        {
            fprintf(stderr, "Error: Match result capacity overflow potential.\n");
            return false;
        }
        uint64_t new_capacity = result->capacity * 2;

        match_position_t *new_positions = realloc(result->positions,
                                                  new_capacity * sizeof(match_position_t));
        if (!new_positions)
        {
            perror("Error reallocating match positions");
            return false; // Reallocation failed
        }

        result->positions = new_positions;
        result->capacity = new_capacity;
    }

    result->positions[result->count].start_offset = start_offset;
    result->positions[result->count].end_offset = end_offset;
    result->count++;
    return true;
}

void match_result_free(match_result_t *result)
{
    if (!result)
        return;

    if (result->positions)
        free(result->positions);

    free(result);
}

/* --- Line finding and printing functions --- */
// (find_line_start, find_line_end remain the same)
static size_t find_line_start(const char *text, size_t pos)
{
    while (pos > 0 && text[pos - 1] != '\n')
    {
        pos--;
    }
    return pos;
}

static size_t find_line_end(const char *text, size_t text_len, size_t pos)
{
    // Ensure pos starts within bounds
    if (pos >= text_len)
        return text_len;

    while (pos < text_len && text[pos] != '\n')
    {
        pos++;
    }
    return pos;
}

void print_matching_lines(const char *filename, const char *text, size_t text_len, const match_result_t *result)
{
    if (!result || !text || result->count == 0)
        return;

    // Special case for string search (when filename is NULL)
    bool is_string_search = (filename == NULL);

    // Keep track of lines we've already printed
    size_t *printed_lines_start = NULL;
    size_t *printed_lines_end = NULL;
    size_t printed_lines_count = 0;
    size_t printed_lines_capacity = 0;

    if (!is_string_search)
    {
        // Only allocate tracking arrays for file search (not needed for string search)
        printed_lines_capacity = 64; // Start with space for 64 lines
        printed_lines_start = malloc(printed_lines_capacity * sizeof(size_t));
        printed_lines_end = malloc(printed_lines_capacity * sizeof(size_t));

        if (!printed_lines_start || !printed_lines_end)
        {
            // Memory allocation failed, fall back to previous behavior
            free(printed_lines_start);
            free(printed_lines_end);
            printed_lines_start = printed_lines_end = NULL;
        }
    }

    for (uint64_t i = 0; i < result->count; i++)
    {
        size_t match_start = result->positions[i].start_offset;
        size_t match_end = result->positions[i].end_offset;

        // Basic validation of match offsets
        if (match_start >= text_len || match_end > text_len || match_start > match_end)
        {
            fprintf(stderr, "Warning: Skipping invalid match position (%zu, %zu) in print.\n", match_start, match_end);
            continue;
        }

        size_t line_start = find_line_start(text, match_start);
        size_t line_end = find_line_end(text, text_len, match_start);

        // For file search, check if this line has already been processed
        if (!is_string_search && printed_lines_start && printed_lines_end)
        {
            bool already_printed = false;
            for (size_t j = 0; j < printed_lines_count; j++)
            {
                if (line_start == printed_lines_start[j] && line_end == printed_lines_end[j])
                {
                    already_printed = true;
                    break;
                }
            }

            if (already_printed)
            {
                continue; // Skip this match, we've already printed this line
            }

            // Add this line to our tracking arrays
            if (printed_lines_count >= printed_lines_capacity)
            {
                // Need to expand arrays
                size_t new_capacity = printed_lines_capacity * 2;
                size_t *new_starts = realloc(printed_lines_start, new_capacity * sizeof(size_t));
                size_t *new_ends = realloc(printed_lines_end, new_capacity * sizeof(size_t));

                if (!new_starts || !new_ends)
                {
                    // Memory allocation failed, just continue with current capacity
                    // Some duplicates might be printed
                }
                else
                {
                    printed_lines_start = new_starts;
                    printed_lines_end = new_ends;
                    printed_lines_capacity = new_capacity;
                }
            }

            if (printed_lines_count < printed_lines_capacity)
            {
                printed_lines_start[printed_lines_count] = line_start;
                printed_lines_end[printed_lines_count] = line_end;
                printed_lines_count++;
            }
        }

        // Print filename prefix if provided
        if (filename)
        {
            if (color_output_enabled)
            {
                printf("%s%s%s%s", KREP_COLOR_FILENAME, filename, KREP_COLOR_SEPARATOR, ":");
            }
            else
            {
                printf("%s:", filename);
            }
        }

        // For string search, print match position
        if (is_string_search)
        {
            if (color_output_enabled)
            {
                printf("%s[%zu]%s ", KREP_COLOR_FILENAME, match_start, KREP_COLOR_RESET);
            }
            else
            {
                printf("[%zu] ", match_start);
            }
        }

        // Find all matches on this line for file search (to highlight all matches)
        if (!is_string_search)
        {
            // Print the entire line with all matches highlighted
            size_t current_pos = line_start;

            // Find all matches on this line
            for (uint64_t j = i; j < result->count; j++)
            {
                size_t m_start = result->positions[j].start_offset;
                size_t m_end = result->positions[j].end_offset;

                // Check if this match is on the current line
                if (m_start >= line_start && m_start < line_end)
                {
                    // Print text up to this match
                    printf("%.*s", (int)(m_start - current_pos), text + current_pos);

                    // Print the match with highlighting
                    if (color_output_enabled)
                        printf(KREP_COLOR_MATCH);
                    printf("%.*s", (int)(m_end - m_start), text + m_start);
                    if (color_output_enabled)
                        printf(KREP_COLOR_RESET);

                    current_pos = m_end;

                    // If we used a match after the current one in the loop, skip it in the main loop
                    if (j > i)
                    {
                        // Mark matches on the same line as "processed"
                        if (printed_lines_start && j < result->count)
                        {
                            size_t next_line_start = find_line_start(text, result->positions[j].start_offset);
                            size_t next_line_end = find_line_end(text, text_len, result->positions[j].start_offset);

                            if (next_line_start == line_start && next_line_end == line_end)
                            {
                                // This is a match on the same line, so we'll skip it in the main loop
                                // We don't increment i directly because that would mess up the loop
                                // But we've already processed this match
                            }
                        }
                    }
                }
            }

            // Print the rest of the line
            if (current_pos < line_end)
            {
                printf("%.*s", (int)(line_end - current_pos), text + current_pos);
            }
        }
        else
        {
            // For string search, just print the line with the current match highlighted
            printf("%.*s", (int)(match_start - line_start), text + line_start);

            if (color_output_enabled)
                printf(KREP_COLOR_MATCH);
            printf("%.*s", (int)(match_end - match_start), text + match_start);
            if (color_output_enabled)
                printf(KREP_COLOR_RESET);

            printf("%.*s", (int)(line_end - match_end), text + match_end);
        }

        // Print newline
        printf("\n");
    }

    // Free the tracking arrays
    if (printed_lines_start)
        free(printed_lines_start);
    if (printed_lines_end)
        free(printed_lines_end);
}

/* --- Forward declarations --- */
void print_usage(const char *program_name);
double get_time(void);
void prepare_bad_char_table(const char *pattern, size_t pattern_len,
                            int *bad_char_table, bool case_sensitive);
void *search_thread(void *arg);
// Updated declarations for search functions
uint64_t boyer_moore_search(const char *text, size_t text_len,
                            const char *pattern, size_t pattern_len,
                            bool case_sensitive, size_t report_limit_offset,
                            bool track_positions, match_result_t *result);
uint64_t kmp_search(const char *text, size_t text_len,
                    const char *pattern, size_t pattern_len,
                    bool case_sensitive, size_t report_limit_offset,
                    bool track_positions, match_result_t *result);
uint64_t rabin_karp_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset,
                           bool track_positions, match_result_t *result);
uint64_t regex_search(const char *text, size_t text_len,
                      const regex_t *compiled_regex,
                      size_t report_limit_offset,
                      bool track_positions,
                      match_result_t *result);

/* --- Utility Functions --- */
// (get_time, print_usage remain the same)
double get_time(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        // Consider logging instead of printing directly in a library function
        perror("Failed to get current time");
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void print_usage(const char *program_name)
{
    // Add --color option description
    printf("krep v%s - A high-performance string search utility\n\n", VERSION);
    printf("Usage: %s [OPTIONS] PATTERN FILE\n", program_name);
    printf("   or: %s [OPTIONS] -s PATTERN STRING_TO_SEARCH\n\n", program_name);
    printf("OPTIONS:\n");
    printf("  -i            Perform case-insensitive matching.\n");
    printf("  -c            Only print a count of matches found.\n");
    printf("  -r            Treat PATTERN as a regular expression.\n");
    printf("  -t NUM        Use NUM threads for file search (default: %d, auto-adjusts).\n", DEFAULT_THREAD_COUNT);
    printf("  -s            Search in STRING_TO_SEARCH instead of a FILE.\n");
    printf("  --color[=WHEN] Control color output ('always', 'never', 'auto'). Default: 'auto'.\n"); // TODO: Implement fully with getopt_long
    printf("  -v            Display version information and exit.\n");
    printf("  -h            Display this help message and exit.\n\n");
    printf("EXAMPLES:\n");
    printf("  %s \"search term\" input.log\n", program_name);
    printf("  %s -i -t 8 ERROR large_log.txt\n", program_name);
    printf("  %s -r \"[Ee]rror: .*\" system.log\n", program_name);
    printf("  %s -s \"pattern\" \"Search for pattern in this string.\"\n", program_name);
    printf("  echo \"Data stream\" | %s -c word -\n", program_name); // Example with stdin (needs file arg '-')
}

/* --- Boyer-Moore-Horspool Algorithm --- */
// (prepare_bad_char_table remains the same)
void prepare_bad_char_table(const char *pattern, size_t pattern_len,
                            int *bad_char_table, bool case_sensitive)
{
    // Initialize all shifts to pattern length
    for (int i = 0; i < 256; i++)
    {
        bad_char_table[i] = (int)pattern_len;
    }

    // Calculate shifts based on characters in the pattern (excluding the last char)
    for (size_t i = 0; i < pattern_len - 1; i++)
    {
        unsigned char c = (unsigned char)pattern[i];
        int shift = (int)(pattern_len - 1 - i);

        if (!case_sensitive)
        {
            // Apply shift for both lower and upper case if insensitive
            unsigned char lower_c = lower_table[c];
            bad_char_table[lower_c] = shift;
            // Only set uppercase if different from lowercase to avoid redundant work
            unsigned char upper_c = toupper(lower_c); // Note: toupper() expects int
            if (upper_c != lower_c)
            {
                bad_char_table[upper_c] = shift;
            }
        }
        else
        {
            // Case-sensitive: only set for the exact character
            bad_char_table[c] = shift;
        }
    }
}

// Modified to track positions
uint64_t boyer_moore_search(const char *text, size_t text_len,
                            const char *pattern, size_t pattern_len,
                            bool case_sensitive, size_t report_limit_offset,
                            bool track_positions, match_result_t *result)
{
    uint64_t match_count = 0;
    int bad_char_table[256];

    // Basic checks for validity
    if (pattern_len == 0 || text_len < pattern_len || report_limit_offset == 0)
    {
        return 0;
    }

    prepare_bad_char_table(pattern, pattern_len, bad_char_table, case_sensitive);

    size_t i = 0; // Current alignment position in text
    size_t limit = text_len - pattern_len;

    while (i <= limit)
    {
        size_t j = pattern_len - 1;
        size_t k = i + pattern_len - 1;

        while (true)
        {
            char text_char = text[k];
            char pattern_char = pattern[j];

            if (!case_sensitive)
            {
                text_char = lower_table[(unsigned char)text_char];
                pattern_char = lower_table[(unsigned char)pattern_char];
            }

            if (text_char != pattern_char)
            {
                unsigned char bad_char_from_text = (unsigned char)text[i + pattern_len - 1];
                int shift = case_sensitive ? bad_char_table[bad_char_from_text]
                                           : bad_char_table[lower_table[bad_char_from_text]];
                i += (shift > 0 ? shift : 1);
                break;
            }

            if (j == 0)
            {
                // Full match found!
                if (i < report_limit_offset)
                {
                    match_count++;
                    // Track position if requested
                    if (track_positions && result)
                    {
                        if (!match_result_add(result, i, i + pattern_len))
                        {
                            fprintf(stderr, "Warning: Failed to add match position (BMH).\n");
                        }
                    }
                }
                i++; // Shift by 1 for overlapping matches
                break;
            }
            j--;
            k--;
        }
    }
    return match_count;
}

/* --- Knuth-Morris-Pratt (KMP) Algorithm --- */
// (compute_lps_array remains the same)
static void compute_lps_array(const char *pattern, size_t pattern_len, int *lps, bool case_sensitive)
{
    size_t length = 0; // length of the previous longest prefix suffix
    lps[0] = 0;        // lps[0] is always 0
    size_t i = 1;

    // the loop calculates lps[i] for i = 1 to pattern_len-1
    while (i < pattern_len)
    {
        char char_i = pattern[i];
        char char_len = pattern[length];

        if (!case_sensitive)
        {
            char_i = lower_table[(unsigned char)char_i];
            char_len = lower_table[(unsigned char)char_len];
        }

        if (char_i == char_len)
        {
            length++;
            lps[i] = length;
            i++;
        }
        else
        {
            if (length != 0)
            {
                length = lps[length - 1];
            }
            else
            {
                lps[i] = 0;
                i++;
            }
        }
    }
}

// Modified to track positions
uint64_t kmp_search(const char *text, size_t text_len,
                    const char *pattern, size_t pattern_len,
                    bool case_sensitive, size_t report_limit_offset,
                    bool track_positions, match_result_t *result)
{
    uint64_t match_count = 0;

    if (pattern_len == 0 || text_len < pattern_len || report_limit_offset == 0)
    {
        return 0;
    }

    // Optimization for single character pattern
    if (pattern_len == 1)
    {
        char p_char = pattern[0];
        char p_lower = lower_table[(unsigned char)p_char];

        for (size_t i = 0; i < text_len && i < report_limit_offset; i++)
        {
            char text_char = text[i];
            bool match_found = false;
            if (case_sensitive)
            {
                if (text_char == p_char)
                    match_found = true;
            }
            else
            {
                if (lower_table[(unsigned char)text_char] == p_lower)
                    match_found = true;
            }
            if (match_found)
            {
                match_count++;
                if (track_positions && result)
                {
                    if (!match_result_add(result, i, i + 1))
                    {
                        fprintf(stderr, "Warning: Failed to add match position (KMP-1).\n");
                    }
                }
            }
        }
        return match_count;
    }

    int *lps = malloc(pattern_len * sizeof(int));
    if (!lps)
    {
        perror("malloc failed for LPS array");
        return 0;
    }
    compute_lps_array(pattern, pattern_len, lps, case_sensitive);

    size_t i = 0; // index for text[]
    size_t j = 0; // index for pattern[]
    while (i < text_len)
    {
        char text_char = text[i];
        char pattern_char = pattern[j];

        if (!case_sensitive)
        {
            text_char = lower_table[(unsigned char)text_char];
            pattern_char = lower_table[(unsigned char)pattern_char];
        }

        if (pattern_char == text_char)
        {
            i++;
            j++;
        }

        if (j == pattern_len)
        {
            size_t match_start_index = i - j;
            if (match_start_index < report_limit_offset)
            {
                match_count++;
                if (track_positions && result)
                {
                    if (!match_result_add(result, match_start_index, i))
                    { // i is end offset (exclusive)
                        fprintf(stderr, "Warning: Failed to add match position (KMP).\n");
                    }
                }
            }
            j = lps[j - 1];
        }
        else if (i < text_len && pattern_char != text_char)
        {
            if (j != 0)
            {
                j = lps[j - 1];
            }
            else
            {
                i = i + 1;
            }
        }
    }

    free(lps);
    return match_count;
}

/* --- Rabin-Karp Algorithm --- */
// Modified to track positions
uint64_t rabin_karp_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset,
                           bool track_positions, match_result_t *result)
{
    uint64_t match_count = 0;

    if (pattern_len == 0 || text_len < pattern_len || report_limit_offset == 0)
    {
        return 0;
    }

    // Use KMP for very short patterns where RK overhead isn't worth it
    if (pattern_len <= 4)
    {
        // Pass through the tracking parameters
        return kmp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, track_positions, result);
    }

    const uint64_t prime = 1000000007ULL;
    const uint64_t base = 256ULL;
    uint64_t pattern_hash = 0;
    uint64_t text_hash = 0;
    uint64_t h = 1;

    for (size_t i = 0; i < pattern_len - 1; i++)
    {
        h = (h * base) % prime;
    }

    for (size_t i = 0; i < pattern_len; i++)
    {
        char pc = pattern[i];
        char tc = text[i];
        if (!case_sensitive)
        {
            pc = lower_table[(unsigned char)pc];
            tc = lower_table[(unsigned char)tc];
        }
        pattern_hash = (base * pattern_hash + pc) % prime;
        text_hash = (base * text_hash + tc) % prime;
    }

    size_t limit = text_len - pattern_len;
    for (size_t i = 0; i <= limit; i++)
    {
        if (pattern_hash == text_hash)
        {
            bool match = true;
            for (size_t j = 0; j < pattern_len; j++)
            {
                char pc = pattern[j];
                char tc = text[i + j];
                if (!case_sensitive)
                {
                    pc = lower_table[(unsigned char)pc];
                    tc = lower_table[(unsigned char)tc];
                }
                if (tc != pc)
                {
                    match = false;
                    break;
                }
            }

            if (match)
            {
                if (i < report_limit_offset)
                {
                    match_count++;
                    if (track_positions && result)
                    {
                        if (!match_result_add(result, i, i + pattern_len))
                        {
                            fprintf(stderr, "Warning: Failed to add match position (RK).\n");
                        }
                    }
                }
            }
        }

        if (i < limit)
        {
            char leading_char = text[i];
            char trailing_char = text[i + pattern_len];

            if (!case_sensitive)
            {
                leading_char = lower_table[(unsigned char)leading_char];
                trailing_char = lower_table[(unsigned char)trailing_char];
            }
            text_hash = (base * (text_hash + prime - (h * leading_char) % prime)) % prime;
            text_hash = (text_hash + trailing_char) % prime;
        }
    }
    return match_count;
}

/* --- SIMD Search Implementations --- */
// NOTE: SIMD implementations also need modification to track positions if used for line printing.
// For now, they only return counts and fallback to BMH which now tracks positions.
#ifdef __SSE4_2__
uint64_t simd_sse42_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset,
                           bool track_positions, match_result_t *result) // Added params
{
    // fprintf(stderr, "Warning: SSE4.2 search temporarily disabled, falling back to Boyer-Moore.\n");
    // Fallback to BMH which now supports position tracking
    return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, track_positions, result);
}
#endif // __SSE4_2__

#ifdef __AVX2__
uint64_t simd_avx2_search(const char *text, size_t text_len,
                          const char *pattern, size_t pattern_len,
                          bool case_sensitive, size_t report_limit_offset,
                          bool track_positions, match_result_t *result) // Added params
{
    // fprintf(stderr, "Warning: AVX2 search not implemented, falling back.\n");
#ifdef __SSE4_2__
    if (pattern_len <= SIMD_MAX_LEN_SSE42)
    {
        // Fallback to SSE4.2 which falls back to BMH
        return simd_sse42_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, track_positions, result);
    }
    else
    {
        // Fallback to BMH
        return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, track_positions, result);
    }
#else
    // Fallback directly to BMH
    return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, track_positions, result);
#endif
}
#endif // __AVX2__

#ifdef __ARM_NEON
uint64_t neon_search(const char *text, size_t text_len,
                     const char *pattern, size_t pattern_len,
                     bool case_sensitive, size_t report_limit_offset,
                     bool track_positions, match_result_t *result) // Added params
{
    // fprintf(stderr, "Warning: NEON search not implemented, falling back to Boyer-Moore.\n");
    // Fallback to BMH
    return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, track_positions, result);
}
#endif // __ARM_NEON

/* --- Regex Search --- */
// (regex_search remains the same as fixed in previous step)
uint64_t regex_search(const char *text, size_t text_len,
                      const regex_t *compiled_regex,
                      size_t report_limit_offset,
                      bool track_positions,
                      match_result_t *result)
{
    uint64_t match_count = 0;
    regmatch_t match; // Structure to hold match offsets

    // Basic validation
    if (!compiled_regex || text_len == 0 || report_limit_offset == 0)
        return 0;

    const char *search_start = text;
    size_t current_offset = 0; // Offset from original text start to search_start

    // Loop while matches are found within the text bounds
    int exec_flags = 0;
    while (regexec(compiled_regex, search_start, 1, &match, exec_flags) == 0)
    {
        // Calculate absolute offsets
        size_t match_start_abs = current_offset + match.rm_so;
        size_t match_end_abs = current_offset + match.rm_eo;

        // Check if the match starts *before* the reporting limit
        if (match_start_abs < report_limit_offset)
        {
            match_count++;
            if (track_positions && result)
            {
                if (!match_result_add(result, match_start_abs, match_end_abs))
                {
                    fprintf(stderr, "Warning: Failed to add match position, results may be incomplete.\n");
                    // Optionally break or handle memory exhaustion
                }
            }
        }
        else
        {
            // Match starts at or after the limit for this chunk/call
            break;
        }

        // --- Corrected Advancement Logic ---
        regoff_t advance_offset_in_chunk = match.rm_eo; // Default: advance past the current match

        // Crucial fix for zero-length matches:
        // If the match was zero-length (rm_so == rm_eo) AND
        // it occurred exactly at the start of the current search buffer (rm_so == 0),
        // we must advance by at least one character to avoid an infinite loop.
        if (match.rm_so == match.rm_eo && match.rm_so == 0)
        {
            advance_offset_in_chunk = 1;
        }
        // Also handle the case where rm_eo somehow is not > rm_so, force advance by 1
        // This also covers the zero-length match case where rm_so > 0.
        else if (advance_offset_in_chunk <= match.rm_so)
        {
            advance_offset_in_chunk = match.rm_so + 1;
        }

        // Ensure advancement doesn't exceed remaining text length relative to search_start
        size_t remaining_len = text_len - current_offset;
        if ((size_t)advance_offset_in_chunk > remaining_len)
        {
            break; // Cannot advance further
        }

        // Advance pointers and offsets
        search_start += advance_offset_in_chunk;
        current_offset += advance_offset_in_chunk;

        // After the first iteration, don't match ^ at BOL or $ at EOL
        // unless REG_NEWLINE was specified during regcomp.
        exec_flags = REG_NOTBOL | REG_NOTEOL;

        // Optimization: if search_start reached end, break early
        if (current_offset >= text_len)
        {
            break;
        }
    }

    return match_count;
}

/* --- Multi-threading Logic --- */
// (search_thread remains the same - does not support position tracking yet)
void *search_thread(void *arg)
{
    search_job_t *job = (search_job_t *)arg;
    job->local_count = 0; // Initialize local count

    // Determine the actual buffer this thread needs to read (includes overlap)
    size_t buffer_abs_start = job->start_pos;
    size_t overlap = (job->use_regex || job->pattern_len == 0) ? 0 : job->pattern_len - 1;
    size_t buffer_abs_end = (job->thread_id == job->total_threads - 1)
                                ? job->end_pos // Last thread takes till the end
                                : job->end_pos + overlap;

    // Clamp buffer end to file size
    if (buffer_abs_end > job->file_size)
    {
        buffer_abs_end = job->file_size;
    }

    // Calculate pointer and length for the buffer this thread processes
    const char *buffer_ptr = job->file_data + buffer_abs_start;
    size_t buffer_len = (buffer_abs_end > buffer_abs_start) ? buffer_abs_end - buffer_abs_start : 0;

    // Determine the limit for *counting* matches (relative to buffer_ptr)
    size_t report_limit_offset = (job->end_pos > buffer_abs_start) ? job->end_pos - buffer_abs_start : 0;

    // Basic checks before searching
    if (buffer_len == 0 || report_limit_offset == 0)
    {
        return NULL;
    }
    if (!job->use_regex && buffer_len < job->pattern_len)
    {
        return NULL;
    }

    // --- Select and Execute Search Algorithm ---
    if (job->use_regex)
    {
        // Use regex search - position tracking disabled in threads
        job->local_count = regex_search(buffer_ptr, buffer_len,
                                        job->regex,
                                        report_limit_offset,
                                        false, // No position tracking
                                        NULL);
    }
    else
    {
        // --- Dynamic algorithm selection for non-regex searches ---
        const size_t KMP_THRESH = 3;
        const size_t RK_THRESH = 32;

        // Position tracking disabled in threads for non-regex too
        bool track_positions = false;
        match_result_t *result = NULL;

        if (job->pattern_len < KMP_THRESH)
        {
            job->local_count = kmp_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset, track_positions, result);
        }
#ifdef __AVX2__
        else if (job->pattern_len <= SIMD_MAX_LEN_AVX2)
        {
            job->local_count = simd_avx2_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset, track_positions, result);
        }
#elif defined(__SSE4_2__)
        else if (job->pattern_len <= SIMD_MAX_LEN_SSE42)
        {
            job->local_count = simd_sse42_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset, track_positions, result);
        }
#elif defined(__ARM_NEON)
        // else if (job->pattern_len <= NEON_MAX_LEN) {
        //      job->local_count = neon_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset, track_positions, result);
        // }
#endif
        else if (job->pattern_len > RK_THRESH)
        {
            job->local_count = rabin_karp_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset, track_positions, result);
        }
        else
        {
            job->local_count = boyer_moore_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset, track_positions, result);
        }
    }

    return NULL;
}

/* --- Public API Implementations --- */

/**
 * Search within a string (single-threaded).
 */
int search_string(const char *pattern, size_t pattern_len, const char *text, bool case_sensitive, bool use_regex, bool count_only)
{
    double start_time = get_time();
    size_t text_len = strlen(text);
    uint64_t match_count = 0;
    regex_t compiled_regex;
    bool regex_compiled = false;
    const char *algo_name = "Unknown";
    match_result_t *matches = NULL;
    bool track_positions = !count_only; // Track positions if not count_only

    // --- Input Validation --- (remains same)
    if (!pattern || pattern_len == 0)
    { /* ... */
        return 1;
    }
    if (!text)
    { /* ... */
        return 1;
    }
    if (text_len == 0)
    { /* ... */
        return 0;
    }
    if (!use_regex && pattern_len > text_len)
    { /* ... */
        return 0;
    }

    // Allocate results struct if tracking positions
    if (track_positions)
    {
        matches = match_result_init(100);
        if (!matches)
        {
            fprintf(stderr, "Error: Failed to allocate memory for match results.\n");
            return 1; // Early exit on allocation failure
        }
    }

    // --- Compile Regex (if needed) ---
    if (use_regex)
    {
        int regex_flags = REG_EXTENDED; // Don't use REG_NOSUB if tracking positions
        if (!case_sensitive)
            regex_flags |= REG_ICASE;
        // Note: REG_NOSUB removed implicitly if track_positions is true,
        // because we need match offsets (rm_so, rm_eo). If count_only is true,
        // track_positions is false, and regex_search ignores the NULL 'matches' pointer.

        int ret = regcomp(&compiled_regex, pattern, regex_flags);
        if (ret != 0)
        { /* ... error handling ... */
            match_result_free(matches);
            return 1;
        }
        regex_compiled = true;
        algo_name = "Regex (POSIX)";

        match_count = regex_search(text, text_len, &compiled_regex, text_len, track_positions, matches);
    }
    else // Not using regex
    {
        // --- Select and Execute Non-Regex Algorithm ---
        const size_t KMP_THRESH = 3;
        const size_t RK_THRESH = 32;
        size_t report_limit = text_len;

        // Call appropriate search function with tracking parameters
        if (pattern_len < KMP_THRESH)
        {
            match_count = kmp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
            algo_name = "KMP";
        }
#ifdef __AVX2__
        else if (pattern_len <= SIMD_MAX_LEN_AVX2)
        {
            match_count = simd_avx2_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
            algo_name = "AVX2 (Fallback)";
        }
#elif defined(__SSE4_2__)
        else if (pattern_len <= SIMD_MAX_LEN_SSE42)
        {
            match_count = simd_sse42_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
            algo_name = "SSE4.2 (Fallback)";
        }
#elif defined(__ARM_NEON)
        // else if (pattern_len <= NEON_MAX_LEN) { match_count = neon_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches); algo_name = "NEON (Fallback)"; }
#endif
        else if (pattern_len > RK_THRESH)
        {
            match_count = rabin_karp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
            algo_name = "Rabin-Karp";
        }
        else
        {
            match_count = boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
            algo_name = "Boyer-Moore-Horspool";
        }
    }

    // --- Report Results ---
    double end_time = get_time();
    double search_time = end_time - start_time;

    if (count_only)
    {
        printf("%" PRIu64 "\n", match_count);
    }
    else
    {
        // Print matching lines (using filename=NULL for string search)
        if (matches)
        {
            print_matching_lines(NULL, text, text_len, matches);
        }

        // Print summary details
        if (!matches || match_count == 0)
        { // If no lines printed or no matches
            printf("Found %" PRIu64 " matches\n", match_count);
        }
        else
        {
            printf("\n--- Total Matches: %" PRIu64 " ---\n", match_count);
        }

        printf("Search completed in %.4f seconds\n", search_time);
        printf("Search details:\n");
        printf("  - String length: %zu characters\n", text_len);
        printf("  - Pattern length: %zu characters\n", pattern_len);
        printf("  - Algorithm used: %s\n", algo_name);
        printf("  - %s search\n", case_sensitive ? "Case-sensitive" : "Case-insensitive");
        printf("  - %s pattern\n", use_regex ? "Regular expression" : "Literal");
    }

    // --- Cleanup ---
    if (regex_compiled)
        regfree(&compiled_regex);
    if (matches)
        match_result_free(matches);

    return 0; // Success
}

/**
 * Search within a file using memory mapping and adaptive threading.
 */
int search_file(const char *filename, const char *pattern, size_t pattern_len, bool case_sensitive,
                bool count_only, int thread_count, bool use_regex)
{
    double start_time = get_time();
    uint64_t total_match_count = 0;
    int fd = -1;
    char *file_data = MAP_FAILED;
    size_t file_size = 0;
    regex_t compiled_regex;
    bool regex_compiled = false;
    int result_code = 0;
    match_result_t *matches = NULL;
    const char *algo_name = "Unknown";
    bool track_positions = !count_only; // Track positions if not count_only

    // --- Input Validation --- (remains same)
    if (!filename)
    { /* ... */
        return 1;
    }
    if (!pattern || pattern_len == 0)
    { /* ... */
        return 1;
    }
    if (!use_regex && pattern_len > MAX_PATTERN_LENGTH)
    { /* ... */
        return 1;
    }
    if (strcmp(filename, "-") == 0)
    { /* ... */
        return 1;
    }

    // --- File Opening and Stat --- (remains same)
    fd = open(filename, O_RDONLY | O_CLOEXEC);
    if (fd == -1)
    { /* ... */
        return 1;
    }
    struct stat file_stat;
    if (fstat(fd, &file_stat) == -1)
    { /* ... */
        close(fd);
        return 1;
    }
    file_size = file_stat.st_size;
    if (file_size == 0)
    { /* ... */
        close(fd);
        return 0;
    }
    if (!use_regex && pattern_len > file_size)
    { /* ... */
        close(fd);
        return 0;
    }

    // --- Compile Regex (if needed) ---
    if (use_regex)
    {
        int regex_flags = REG_EXTENDED; // Don't use REG_NOSUB if tracking positions
        if (!case_sensitive)
            regex_flags |= REG_ICASE;
        // Note: REG_NOSUB removed implicitly if track_positions is true

        int ret = regcomp(&compiled_regex, pattern, regex_flags);
        if (ret != 0)
        { /* ... error handling ... */
            close(fd);
            return 1;
        }
        regex_compiled = true;
    }

    // --- Memory Mapping --- (remains same)
    int mmap_flags = MAP_PRIVATE;
    file_data = mmap(NULL, file_size, PROT_READ, mmap_flags, fd, 0);
    if (file_data == MAP_FAILED)
    { /* ... error handling ... */
        close(fd);
        if (regex_compiled)
            regfree(&compiled_regex);
        return 1;
    }
    if (madvise(file_data, file_size, MADV_SEQUENTIAL) != 0)
    {
        perror("Warning: madvise(MADV_SEQUENTIAL) failed");
    }
    close(fd);
    fd = -1;

    // --- Determine Thread Count --- (remains same)
    int cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);                                       /* ... */
    int actual_thread_count = (thread_count <= 0) ? DEFAULT_THREAD_COUNT : thread_count; /* ... */
    int max_reasonable_threads = cpu_cores * 2;                                          /* ... */
    if (actual_thread_count > max_reasonable_threads)
    {
        actual_thread_count = max_reasonable_threads;
    }
    if (!use_regex && pattern_len > 0)
    { /* ... */
    }
    if (actual_thread_count < 1)
        actual_thread_count = 1;

    // --- Decide Threading Strategy ---
    size_t dynamic_threshold = MIN_FILE_SIZE_FOR_THREADS * actual_thread_count;
    // Force single-thread if tracking positions (for now)
    bool use_multithreading = (file_size >= dynamic_threshold) && (actual_thread_count > 1) && !track_positions;
    const char *execution_mode = use_multithreading ? "Multi-threaded" : "Single-threaded";
    // Adjust actual_thread_count if forced single-threaded
    if (!use_multithreading && actual_thread_count > 1 && track_positions)
    {
        actual_thread_count = 1;
        fprintf(stderr, "Warning: Line printing forces single-threaded mode (-t ignored).\n");
    }

    // Allocate results struct if tracking positions (only happens if !use_multithreading)
    if (track_positions)
    {
        matches = match_result_init(100);
        if (!matches)
        {
            fprintf(stderr, "Error: Failed to allocate memory for match results.\n");
            result_code = 1;
            goto cleanup;
        }
    }

    if (!use_multithreading)
    {
        // --- Single-threaded Search ---
        size_t report_limit = file_size;

        if (use_regex)
        {
            total_match_count = regex_search(file_data, file_size, &compiled_regex, report_limit, track_positions, matches);
            algo_name = "Regex (POSIX)";
        }
        else
        {
            // Call appropriate non-regex search with tracking params
            const size_t KMP_THRESH = 3;
            const size_t RK_THRESH = 32;
            if (pattern_len < KMP_THRESH)
            {
                total_match_count = kmp_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
                algo_name = "KMP";
            }
#ifdef __AVX2__
            else if (pattern_len <= SIMD_MAX_LEN_AVX2)
            {
                total_match_count = simd_avx2_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
                algo_name = "AVX2 (Fallback)";
            }
#elif defined(__SSE4_2__)
            else if (pattern_len <= SIMD_MAX_LEN_SSE42)
            {
                total_match_count = simd_sse42_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
                algo_name = "SSE4.2 (Fallback)";
            }
#elif defined(__ARM_NEON)
            // else if (pattern_len <= NEON_MAX_LEN) { total_match_count = neon_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches); algo_name = "NEON (Fallback)"; }
#endif
            else if (pattern_len > RK_THRESH)
            {
                total_match_count = rabin_karp_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
                algo_name = "Rabin-Karp";
            }
            else
            {
                total_match_count = boyer_moore_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
                algo_name = "Boyer-Moore-Horspool";
            }
        }
    }
    else // Use Multi-threading (position tracking disabled here)
    {
        // --- Multi-threaded Search ---
        pthread_t *threads = malloc(actual_thread_count * sizeof(pthread_t));
        search_job_t *jobs = malloc(actual_thread_count * sizeof(search_job_t));
        bool *thread_valid = calloc(actual_thread_count, sizeof(bool));
        bool memory_error = (!threads || !jobs || !thread_valid);

        if (memory_error)
        {
            perror("Error allocating memory for threads/jobs/flags");
            result_code = 1;
            free(threads);
            free(jobs);
            free(thread_valid); // Free partially allocated
            goto cleanup;
        }

        // Divide work among threads - FIX: Reinstate chunk calculation logic
        size_t chunk_size = file_size / actual_thread_count;
        size_t current_pos = 0;
        int created_threads = 0; // Track successfully created threads

        for (int i = 0; i < actual_thread_count; i++)
        {
            // --- FIX: Calculate start/end pos using chunk_size and current_pos ---
            jobs[i].file_data = file_data;
            jobs[i].file_size = file_size;
            jobs[i].start_pos = current_pos;
            jobs[i].end_pos = (i == actual_thread_count - 1) ? file_size : current_pos + chunk_size;
            if (jobs[i].end_pos < current_pos)
                jobs[i].end_pos = current_pos; // Safeguard
            if (jobs[i].end_pos > file_size)
                jobs[i].end_pos = file_size; // Safeguard
            current_pos = jobs[i].end_pos;   // Update for next iteration
            // --- End FIX ---

            jobs[i].pattern = pattern;
            jobs[i].pattern_len = pattern_len;
            jobs[i].case_sensitive = case_sensitive;
            jobs[i].use_regex = use_regex;
            jobs[i].regex = use_regex ? &compiled_regex : NULL;
            jobs[i].thread_id = i;
            jobs[i].total_threads = actual_thread_count;
            jobs[i].local_count = 0;

            if (jobs[i].start_pos < jobs[i].end_pos)
            {
                if (pthread_create(&threads[i], NULL, search_thread, &jobs[i]) != 0)
                {
                    perror("Error creating search thread");
                    // FIX: Use created_threads for cleanup loop
                    for (int j = 0; j < created_threads; j++)
                    {
                        pthread_join(threads[j], NULL);
                        total_match_count += jobs[j].local_count;
                    }
                    result_code = 1;
                    free(threads);
                    free(jobs);
                    free(thread_valid); // Free memory
                    goto cleanup;
                }
                thread_valid[i] = true;
                created_threads++; // Increment counter for successfully created threads
            }
            else
            {
                thread_valid[i] = false;
            }
        } // End thread creation loop

        // Wait for threads and aggregate results
        for (int i = 0; i < actual_thread_count; i++)
        {
            if (thread_valid[i])
            {
                pthread_join(threads[i], NULL);
                total_match_count += jobs[i].local_count;
            }
        }

        free(threads);
        free(jobs);
        free(thread_valid);
        algo_name = "Multiple (Dynamic)";

    } // End multi-threaded block

    // --- Final Reporting ---
    if (result_code == 0)
    {
        // Print matching lines IF tracking was enabled and results exist
        if (track_positions && matches)
        {
            print_matching_lines(filename, file_data, file_size, matches);
        }

        double end_time = get_time();
        double search_time = end_time - start_time;
        double mb_per_sec = (search_time > 1e-9 && file_size > 0) ? (file_size / (1024.0 * 1024.0)) / search_time : 0.0;

        // Print count if count_only OR if lines were not printed/tracked
        if (count_only || !track_positions) // Use track_positions flag here
        {
            if (count_only)
            {
                printf("%" PRIu64 "\n", total_match_count);
            }
            else
            {
                // If lines weren't printed (because multi-threaded or non-regex), print summary count line
                printf("Found %" PRIu64 " matches in '%s'\n", total_match_count, filename);
            }
        }
        else if (track_positions && matches && total_match_count > 0)
        {
            // If lines *were* printed, print a final summary count
            printf("--- Total Matches: %" PRIu64 " ---\n", total_match_count);
        }
        else if (track_positions && matches && total_match_count == 0)
        {
            // If lines were not printed because no matches found
            printf("Found 0 matches in '%s'\n", filename);
        }

        // Print detailed summary if not count_only
        if (!count_only)
        {
            printf("Search completed in %.4f seconds (%.2f MB/s)\n", search_time, mb_per_sec);
            printf("Search details:\n");
            printf("  - File size: %.2f MB (%zu bytes)\n", file_size / (1024.0 * 1024.0), file_size);
            printf("  - Pattern length: %zu characters\n", pattern_len);
            printf("  - Pattern type: %s\n", use_regex ? "Regular expression" : "Literal text");
            printf("  - Execution: %s (%d thread%s)\n", execution_mode, actual_thread_count, actual_thread_count > 1 ? "s" : "");
            printf("  - Algorithm used: %s\n", algo_name);
#ifdef __AVX2__
            printf("  - SIMD Available: AVX2 (Using Fallback)\n");
#elif defined(__SSE4_2__)
            printf("  - SIMD Available: SSE4.2 (Using Fallback)\n");
#elif defined(__ARM_NEON)
            printf("  - SIMD Available: NEON (Using Fallback)\n");
#else
            printf("  - SIMD Available: No (Using scalar algorithms)\n");
#endif
            printf("  - %s search\n", case_sensitive ? "Case-sensitive" : "Case-insensitive");
        }
    }

cleanup: // Central cleanup point
    // --- Cleanup mmap and regex ---
    if (file_data != MAP_FAILED)
    {
        munmap(file_data, file_size); /* Ignore munmap error? */
    }
    if (regex_compiled)
    {
        regfree(&compiled_regex);
    }
    if (matches)
    {
        match_result_free(matches);
    }
    if (fd != -1)
        close(fd); // Should already be closed, but just in case

    return result_code;
}

/* --- Main Entry Point --- */
#ifndef TESTING // Exclude main when compiling for tests
int main(int argc, char *argv[])
{
    char *pattern_arg = NULL;
    char *file_or_string_arg = NULL;
    bool case_sensitive = true;
    bool count_only = false;
    bool string_mode = false;
    bool use_regex = false;
    int thread_count = 0; // 0 for default/auto
    int opt;
    // Basic color detection (can be overridden by --color option later)
    // TODO: Add proper --color=WHEN option parsing using getopt_long
    color_output_enabled = isatty(STDOUT_FILENO);

    // Basic check for minimum arguments
    if (argc < 3)
    {
        print_usage(argv[0]);
        return 1;
    }

    // Parse options using getopt
    while ((opt = getopt(argc, argv, "icrvt:shv")) != -1)
    {
        switch (opt)
        {
        case 'i':
            case_sensitive = false;
            break;
        case 'c':
            count_only = true;
            color_output_enabled = false;
            break; // Disable color if only counting
        case 'r':
            use_regex = true;
            break;
        case 't':
        {
            char *endptr;
            errno = 0;
            long val = strtol(optarg, &endptr, 10);
            if (errno != 0 || optarg == endptr || *endptr != '\0' || val <= 0 || val > INT_MAX)
            {
                fprintf(stderr, "Warning: Invalid thread count '%s', using default.\n", optarg);
                thread_count = 0;
            }
            else
            {
                thread_count = (int)val;
            }
            break;
        }
        case 's':
            string_mode = true;
            break;
        case 'v':
            printf("krep v%s\n", VERSION);
            return 0;
        case 'h':
            print_usage(argv[0]);
            return 0;
        case '?':
            print_usage(argv[0]);
            return 1;
        default:
            fprintf(stderr, "Internal error parsing options.\n");
            abort();
        }
    }

    // Check for required positional arguments after options
    if (optind >= argc)
    { /* ... error missing PATTERN ... */
        return 1;
    }
    pattern_arg = argv[optind++];
    if (optind >= argc)
    { /* ... error missing FILE/STRING ... */
        return 1;
    }
    file_or_string_arg = argv[optind];
    if (optind + 1 < argc)
    { /* ... error extra arguments ... */
        return 1;
    }

    // Validate pattern length
    size_t pattern_len = strlen(pattern_arg);

    // Execute search based on mode
    int result = 1;
    if (string_mode)
    {
        result = search_string(pattern_arg, pattern_len, file_or_string_arg, case_sensitive, use_regex, count_only);
    }
    else
    {
        result = search_file(file_or_string_arg, pattern_arg, pattern_len, case_sensitive, count_only, thread_count, use_regex);
    }

    return result;
}
#endif // TESTING

/* --- TESTING COMPATIBILITY WRAPPERS --- */
#ifdef TESTING
// Wrappers now need to pass the extra tracking parameters

uint64_t boyer_moore_search_compat(const char *text, size_t text_len, const char *pattern, size_t pattern_len, bool case_sensitive, size_t report_limit_offset)
{
    // Pass false/NULL for tracking as tests likely only check counts
    return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, false, NULL);
}

uint64_t kmp_search_compat(const char *text, size_t text_len, const char *pattern, size_t pattern_len, bool case_sensitive, size_t report_limit_offset)
{
    return kmp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, false, NULL);
}

uint64_t rabin_karp_search_compat(const char *text, size_t text_len, const char *pattern, size_t pattern_len, bool case_sensitive, size_t report_limit_offset)
{
    return rabin_karp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, false, NULL);
}

// ...existing code for other compatibility wrappers...

#endif // TESTING
