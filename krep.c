/* krep - A high-performance string search utility
 *
 * Author: Davide Santangelo
 * Version: 0.3.6
 * Year: 2025
 *
 * Features:
 * - Multiple search algorithms (Boyer-Moore-Horspool, KMP, Rabin-Karp)
 * - Basic SIMD (SSE4.2) acceleration implemented (Currently Disabled - Fallback)
 * - Placeholders for AVX2 acceleration
 * - Memory-mapped file I/O for maximum throughput (Optimized flags)
 * - Multi-threaded parallel search for large files (count mode only)
 * - Case-sensitive and case-insensitive matching
 * - Direct string search in addition to file search
 * - Regular expression search support (POSIX, compiled once, fixed loop)
 * - Matching line printing with filename prefix and highlighting (single-thread only)
 * - Reports unique matching lines AND total occurrences when printing lines,
 * reports total occurrences when using -c.
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
#include <inttypes.h> // For PRIu64 macro
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

#ifdef __ARM_NEON
#include <arm_neon.h>
#endif

// Include the header file where match_position_t and match_result_t are defined
#include "krep.h"

/* Constants */
#define MAX_PATTERN_LENGTH 1024
#define DEFAULT_THREAD_COUNT 4
#define MIN_FILE_SIZE_FOR_THREADS (1 * 1024 * 1024) // 1MB minimum for threading
#define VERSION "0.3.6"                             // Keep version consistent

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
match_result_t *match_result_init(uint64_t initial_capacity)
{
    match_result_t *result = malloc(sizeof(match_result_t));
    if (!result)
    {
        perror("malloc failed for match_result_t");
        return NULL;
    }

    // Ensure initial capacity is reasonable
    if (initial_capacity == 0)
        initial_capacity = 16; // Default initial capacity

    result->positions = malloc(initial_capacity * sizeof(match_position_t));
    if (!result->positions)
    {
        perror("malloc failed for match positions array");
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

    // Check if capacity needs to be increased
    if (result->count >= result->capacity)
    {
        // Double the capacity, check for potential overflow
        if (result->capacity > SIZE_MAX / (2 * sizeof(match_position_t)))
        {
            fprintf(stderr, "Error: Match result capacity overflow potential during reallocation.\n");
            return false; // Prevent overflow
        }
        uint64_t new_capacity = (result->capacity == 0) ? 16 : result->capacity * 2; // Start at 16 if capacity was 0

        match_position_t *new_positions = realloc(result->positions,
                                                  new_capacity * sizeof(match_position_t));
        if (!new_positions)
        {
            perror("Error reallocating match positions");
            // Keep existing data, but cannot add new match
            return false; // Reallocation failed
        }

        result->positions = new_positions;
        result->capacity = new_capacity;
    }

    // Add the new match position
    result->positions[result->count].start_offset = start_offset;
    result->positions[result->count].end_offset = end_offset;
    result->count++;
    return true;
}

void match_result_free(match_result_t *result)
{
    if (!result)
        return;

    // Free the positions array first
    if (result->positions)
        free(result->positions);

    // Then free the struct itself
    free(result);
}

/* --- Line finding and printing functions --- */
static size_t find_line_start(const char *text, size_t pos)
{
    // Scan backwards from pos until newline or start of text
    while (pos > 0 && text[pos - 1] != '\n')
    {
        pos--;
    }
    return pos; // Returns start index of the line (or 0)
}

static size_t find_line_end(const char *text, size_t text_len, size_t pos)
{
    // Ensure pos starts within bounds
    if (pos >= text_len)
        return text_len; // Return end of text if pos is already out of bounds

    // Scan forwards from pos until newline or end of text
    while (pos < text_len && text[pos] != '\n')
    {
        pos++;
    }
    // pos now points to the newline or end of text
    return pos;
}

// MODIFIED: Returns the number of unique lines printed
size_t print_matching_lines(const char *filename, const char *text, size_t text_len, const match_result_t *result)
{
    if (!result || !text || result->count == 0)
        return 0; // Return 0 lines printed

    bool is_string_search = (filename == NULL);
    size_t *printed_lines = NULL;
    size_t printed_lines_count = 0; // Count of unique lines printed
    size_t printed_lines_capacity = 0;
    bool allocation_failed = false;

    // Allocate tracking array only for file search to avoid duplicate lines
    if (!is_string_search)
    {
        printed_lines_capacity = 64; // Initial capacity
        printed_lines = malloc(printed_lines_capacity * sizeof(size_t));
        if (!printed_lines)
        {
            fprintf(stderr, "Warning: Failed to allocate memory for line tracking. Duplicate lines might be printed.\n");
            allocation_failed = true; // Proceed without tracking if allocation fails
        }
    }

    for (uint64_t i = 0; i < result->count; i++)
    {
        size_t match_start = result->positions[i].start_offset;
        size_t match_end = result->positions[i].end_offset;

        if (match_start >= text_len || match_end > text_len || match_start > match_end)
        {
            fprintf(stderr, "Warning: Skipping invalid match position (%zu, %zu) in print.\n", match_start, match_end);
            continue;
        }

        size_t line_start = find_line_start(text, match_start);
        size_t line_end = find_line_end(text, text_len, match_start);

        // --- Check if line was already printed (only for file mode) ---
        if (!is_string_search && printed_lines && !allocation_failed)
        {
            bool already_printed = false;
            for (size_t j = 0; j < printed_lines_count; j++)
            {
                if (line_start == printed_lines[j])
                {
                    already_printed = true;
                    break;
                }
            }
            if (already_printed)
                continue; // Skip printing this line again

            // Add line_start to tracking array if not full
            if (printed_lines_count >= printed_lines_capacity)
            {
                // Attempt to resize
                if (printed_lines_capacity > SIZE_MAX / 2)
                {
                    fprintf(stderr, "Warning: Cannot expand line tracking array further due to potential overflow.\n");
                    allocation_failed = true; // Stop trying to track
                }
                else
                {
                    size_t new_capacity = printed_lines_capacity * 2;
                    size_t *new_printed_lines = realloc(printed_lines, new_capacity * sizeof(size_t));
                    if (!new_printed_lines)
                    {
                        fprintf(stderr, "Warning: Failed to reallocate line tracking array. Duplicate lines might be printed.\n");
                        allocation_failed = true; // Stop trying to track
                    }
                    else
                    {
                        printed_lines = new_printed_lines;
                        printed_lines_capacity = new_capacity;
                    }
                }
            }
            // Add if space available and no previous allocation failure
            if (!allocation_failed && printed_lines_count < printed_lines_capacity)
            {
                printed_lines[printed_lines_count] = line_start;
                // Increment count AFTER potential printing below
            }
            else if (!allocation_failed)
            {
                fprintf(stderr, "Warning: Line tracking array full, duplicate lines might be printed.\n");
                allocation_failed = true; // Stop trying to track
            }
        }

        // --- Format and Print the Line ---
        char *formatted_line = NULL;
        size_t formatted_length = 0;
        FILE *memfile = open_memstream(&formatted_line, &formatted_length);
        if (!memfile)
        {
            perror("Error: Failed to create memory stream for line formatting");
            continue; // Skip this match
        }

        // 1. Print filename prefix (if applicable)
        if (filename)
        {
            if (color_output_enabled)
            {
                fprintf(memfile, "%s%s%s%s", KREP_COLOR_FILENAME, filename, KREP_COLOR_SEPARATOR, ":");
            }
            else
            {
                fprintf(memfile, "%s:", filename);
            }
        }

        // 2. Construct the line content with highlighting
        if (is_string_search) // String search: highlight only the first match found for this line
        {
            if (color_output_enabled)
            {
                fprintf(memfile, "%s[%zu]%s ", KREP_COLOR_FILENAME, match_start, KREP_COLOR_RESET);
            }
            else
            {
                fprintf(memfile, "[%zu] ", match_start);
            }
            // Print part before match
            if (match_start > line_start)
            {
                fprintf(memfile, "%.*s", (int)(match_start - line_start), text + line_start);
            }
            // Print highlighted match
            if (color_output_enabled)
                fprintf(memfile, KREP_COLOR_MATCH);
            fprintf(memfile, "%.*s", (int)(match_end - match_start), text + match_start);
            if (color_output_enabled)
                fprintf(memfile, KREP_COLOR_RESET);
            // Print part after match
            if (line_end > match_end)
            {
                fprintf(memfile, "%.*s", (int)(line_end - match_end), text + match_end);
            }
        }
        else // File search: highlight all matches on the line
        {
            size_t current_pos_on_line = line_start;
            // Inner loop to find all matches pertaining to the *current* line
            for (uint64_t k = 0; k < result->count; k++)
            {
                size_t k_match_start = result->positions[k].start_offset;
                size_t k_match_end = result->positions[k].end_offset;

                // Check if this match (k) falls within the current line boundaries
                if (k_match_start >= line_start && k_match_start < line_end)
                {
                    // Ensure we process matches in order on the line
                    if (k_match_start >= current_pos_on_line)
                    {
                        // Print text before this match
                        if (k_match_start > current_pos_on_line)
                        {
                            fprintf(memfile, "%.*s", (int)(k_match_start - current_pos_on_line), text + current_pos_on_line);
                        }

                        // Print the highlighted match
                        if (color_output_enabled)
                            fprintf(memfile, KREP_COLOR_MATCH); // START COLOR
                        // Clamp match end to line end if it spans lines
                        size_t highlight_end = (k_match_end > line_end) ? line_end : k_match_end;
                        size_t highlight_len = (highlight_end > k_match_start) ? highlight_end - k_match_start : 0;
                        fprintf(memfile, "%.*s", (int)highlight_len, text + k_match_start);
                        if (color_output_enabled)
                            fprintf(memfile, KREP_COLOR_RESET); // END COLOR

                        current_pos_on_line = highlight_end; // Update position
                    }
                }
            } // End inner loop (k) for highlighting all matches on the line
            if (current_pos_on_line < line_end)
            {
                fprintf(memfile, "%.*s", (int)(line_end - current_pos_on_line), text + current_pos_on_line);
            }
        }

        if (fclose(memfile) != 0)
        {
            perror("Error closing memory stream");
            free(formatted_line);
            continue;
        }

        if (formatted_line)
        {
            puts(formatted_line); // Print the complete line
            free(formatted_line);
            // Increment unique line count *after* successfully printing
            if (!is_string_search && !allocation_failed)
            {
                printed_lines_count++;
            }
            else if (is_string_search)
            {
                printed_lines_count++; // Count every line in string search mode
            }
        }
        else
        {
            fprintf(stderr, "Internal Warning: Formatted line buffer is NULL after fclose.\n");
        }
    } // End of loop through matches (i)

    if (printed_lines)
        free(printed_lines);

    return printed_lines_count; // Return the count of unique lines printed
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
#ifdef __SSE4_2__
uint64_t simd_sse42_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset,
                           bool track_positions, match_result_t *result);
#endif
#ifdef __AVX2__
uint64_t simd_avx2_search(const char *text, size_t text_len,
                          const char *pattern, size_t pattern_len,
                          bool case_sensitive, size_t report_limit_offset,
                          bool track_positions, match_result_t *result);
#endif
#ifdef __ARM_NEON
uint64_t neon_search(const char *text, size_t text_len,
                     const char *pattern, size_t pattern_len,
                     bool case_sensitive, size_t report_limit_offset,
                     bool track_positions, match_result_t *result);
#endif

/* --- Utility Functions --- */
double get_time(void)
{
    struct timespec ts;
    // Use CLOCK_MONOTONIC for measuring intervals, as it's not affected by system time changes
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        perror("Failed to get monotonic time");
        // Fallback or return error indicator? Returning 0.0 might be misleading.
        // Maybe return -1.0 and check? For simplicity, keep returning 0.0 on error.
        return 0.0;
    }
    // Combine seconds and nanoseconds into a double
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void print_usage(const char *program_name)
{
    // Add --color option description
    printf("krep v%s - A high-performance string search utility\n\n", VERSION);
    printf("Usage: %s [OPTIONS] PATTERN FILE\n", program_name);
    printf("   or: %s [OPTIONS] -s PATTERN STRING_TO_SEARCH\n\n", program_name);
    printf("OPTIONS:\n");
    printf("  -i             Perform case-insensitive matching.\n");
    printf("  -c             Only print a count of matches found (total occurrences).\n"); // Clarified -c count
    printf("  -r             Treat PATTERN as a regular expression (POSIX Extended).\n");
    printf("  -t NUM         Use NUM threads for file search (count mode only, default: auto).\n"); // Clarified threading limitation
    printf("  -s             Search in STRING_TO_SEARCH instead of a FILE.\n");
    printf("  --color[=WHEN] Control color output ('always', 'never', 'auto'). Default: 'auto'.\n"); // TODO: Implement fully with getopt_long
    printf("  -v             Display version information and exit.\n");
    printf("  -h             Display this help message and exit.\n\n");
    printf("EXAMPLES:\n");
    printf("  %s \"search term\" input.log\n", program_name);
    printf("  %s -i -c -t 8 ERROR large_log.txt\n", program_name);        // Example with -c and -t
    printf("  %s -r \"^[Ee]rror: .*failed\" system.log\n", program_name); // Example regex
    printf("  %s -s \"pattern\" \"Search for pattern in this string.\"\n", program_name);
    // printf("  echo \"Data stream with word\" | %s -c word -\n", program_name); // Example with stdin (needs file arg '-') - CURRENTLY UNSUPPORTED
}

/* --- Boyer-Moore-Horspool Algorithm --- */
void prepare_bad_char_table(const char *pattern, size_t pattern_len,
                            int *bad_char_table, bool case_sensitive)
{
    // Initialize all shifts to pattern length (default shift)
    for (int i = 0; i < 256; i++)
    {
        // Using pattern_len as the default shift is correct for Horspool
        bad_char_table[i] = (int)pattern_len;
    }

    // Calculate shifts based on characters present in the pattern
    // The shift is the distance from the end of the pattern.
    // The last character of the pattern gets a default shift (pattern_len)
    // unless it appears earlier in the pattern.
    for (size_t i = 0; i < pattern_len - 1; i++) // Iterate up to the second-to-last character
    {
        unsigned char c = (unsigned char)pattern[i];
        // Shift is distance from end: pattern_len - 1 - i
        int shift = (int)(pattern_len - 1 - i);

        if (!case_sensitive)
        {
            // Apply shift for both lower and upper case if insensitive
            unsigned char lower_c = lower_table[c];
            bad_char_table[lower_c] = shift;
            // Only set uppercase if different from lowercase
            unsigned char upper_c = toupper(lower_c); // toupper expects int, returns int
            if (upper_c != lower_c)
            {
                // Cast upper_c back to unsigned char for array index
                bad_char_table[(unsigned char)upper_c] = shift;
            }
        }
        else
        {
            // Case-sensitive: only set for the exact character
            bad_char_table[c] = shift;
        }
    }
    // Note: The character pattern[pattern_len - 1] retains the default shift
    // unless it also appears earlier in the pattern.
}

uint64_t boyer_moore_search(const char *text, size_t text_len,
                            const char *pattern, size_t pattern_len,
                            bool case_sensitive, size_t report_limit_offset,
                            bool track_positions, match_result_t *result)
{
    uint64_t match_count = 0;
    int bad_char_table[256]; // Bad character shift table

    // Basic checks for validity
    if (pattern_len == 0 || text_len < pattern_len || report_limit_offset == 0)
    {
        return 0; // Cannot find empty pattern or pattern longer than text
    }

    // Precompute the bad character table
    prepare_bad_char_table(pattern, pattern_len, bad_char_table, case_sensitive);

    size_t i = 0; // Current alignment position in text (start index of window)
    // Loop while the potential match window fits within the text length
    while (i <= text_len - pattern_len)
    {
        // Start comparison from the end of the pattern
        size_t j = pattern_len - 1;     // Index for pattern (from end)
        size_t k = i + pattern_len - 1; // Index for text (corresponding end of window)

        // Compare characters backward from the end of the pattern
        while (true) // Loop until mismatch or full match
        {
            char text_char = text[k];
            char pattern_char = pattern[j];

            // Apply case-insensitivity if needed
            if (!case_sensitive)
            {
                text_char = lower_table[(unsigned char)text_char];
                pattern_char = lower_table[(unsigned char)pattern_char];
            }

            // If characters do not match
            if (text_char != pattern_char)
            {
                // Mismatch occurred. Calculate shift based on the character in the text
                // that caused the mismatch (text[k]). However, Horspool variant uses
                // the character in the text aligned with the *last* character of the pattern (text[i + pattern_len - 1]).
                unsigned char bad_char_from_text_window_end = (unsigned char)text[i + pattern_len - 1];
                int shift = case_sensitive ? bad_char_table[bad_char_from_text_window_end]
                                           : bad_char_table[lower_table[bad_char_from_text_window_end]];

                // Apply the calculated shift. Ensure we shift by at least 1.
                i += (shift > 0 ? shift : 1);
                break; // Break inner comparison loop and realign window
            }

            // Characters match. Check if we have compared the entire pattern.
            if (j == 0)
            {
                // Full match found (from j=pattern_len-1 down to 0)!
                // Check if the match starts within the reportable range for this chunk
                if (i < report_limit_offset)
                {
                    match_count++;
                    // Track position if requested
                    if (track_positions && result)
                    {
                        if (!match_result_add(result, i, i + pattern_len))
                        {
                            // Failed to add match, print warning but continue searching
                            fprintf(stderr, "Warning: Failed to add match position (BMH).\n");
                        }
                    }
                }
                else
                {
                    // Match found, but starts outside the report limit for this thread/chunk.
                    // Stop searching *within this chunk* if report_limit_offset signifies chunk boundary.
                    // However, the outer loop condition (i <= text_len - pattern_len) handles overall bounds.
                    // If report_limit_offset is strictly less than text_len, we might need to break here
                    // depending on the exact multithreading contract. Assuming single-threaded or
                    // report_limit_offset handles the boundary correctly for counting.
                }

                // Shift the pattern forward to find next potential match.
                // A simple shift by 1 is correct for finding overlapping matches.
                // More advanced BM versions might calculate a better shift here too.
                i++;
                break; // Break inner comparison loop and realign window
            }

            // Continue comparing characters backward
            j--;
            k--;
        } // End inner comparison loop
    } // End outer alignment loop
    return match_count;
}

/* --- Knuth-Morris-Pratt (KMP) Algorithm --- */
static void compute_lps_array(const char *pattern, size_t pattern_len, int *lps, bool case_sensitive)
{
    // lps[i] stores the length of the longest proper prefix of pattern[0...i]
    // which is also a suffix of pattern[0...i].
    size_t length = 0; // Length of the previous longest prefix suffix
    lps[0] = 0;        // lps[0] is always 0
    size_t i = 1;      // Start computing from the second character

    // Loop calculates lps[i] for i = 1 to pattern_len-1
    while (i < pattern_len)
    {
        char char_i = pattern[i];
        char char_len = pattern[length]; // Character at the current prefix end

        // Apply case-insensitivity if needed
        if (!case_sensitive)
        {
            char_i = lower_table[(unsigned char)char_i];
            char_len = lower_table[(unsigned char)char_len];
        }

        // If characters match, extend the current LPS length
        if (char_i == char_len)
        {
            length++;
            lps[i] = length;
            i++;
        }
        else // Characters do not match
        {
            // If length is not 0, we need to backtrack using the LPS array
            // This is the core idea of KMP: reuse previous computations.
            if (length != 0)
            {
                // Move to the next shorter possible prefix/suffix
                length = lps[length - 1];
                // Note: We do not increment 'i' here, we re-evaluate pattern[i]
                // against the character at the new 'length'.
            }
            else // If length is 0, no prefix/suffix match possible ending here
            {
                lps[i] = 0; // LPS length is 0
                i++;        // Move to the next character in the pattern
            }
        }
    }
}

uint64_t kmp_search(const char *text, size_t text_len,
                    const char *pattern, size_t pattern_len,
                    bool case_sensitive, size_t report_limit_offset,
                    bool track_positions, match_result_t *result)
{
    uint64_t match_count = 0;

    // Basic checks
    if (pattern_len == 0 || text_len < pattern_len || report_limit_offset == 0)
    {
        return 0;
    }

    // Optimization for single character pattern (avoids LPS array overhead)
    if (pattern_len == 1)
    {
        char p_char = pattern[0];
        char p_lower = lower_table[(unsigned char)p_char]; // Pre-calculate lower version

        for (size_t i = 0; i < text_len; i++)
        {
            // Check report limit before processing match
            if (i >= report_limit_offset)
                break;

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
                    if (!match_result_add(result, i, i + 1)) // Match length is 1
                    {
                        fprintf(stderr, "Warning: Failed to add match position (KMP-1).\n");
                    }
                }
            }
        }
        return match_count;
    }

    // Allocate memory for LPS array
    int *lps = malloc(pattern_len * sizeof(int));
    if (!lps)
    {
        perror("malloc failed for KMP LPS array");
        return 0; // Return 0 count on memory allocation failure
    }

    // Preprocess the pattern to compute LPS array
    compute_lps_array(pattern, pattern_len, lps, case_sensitive);

    size_t i = 0; // Index for text[]
    size_t j = 0; // Index for pattern[] (also represents length of current match)
    while (i < text_len)
    {
        // Check report limit before processing potential match start
        // This check is slightly complex because a match is only confirmed when j == pattern_len.
        // We count matches only if they *start* before report_limit_offset.
        // The check `match_start_index < report_limit_offset` below handles this.

        char text_char = text[i];
        char pattern_char = pattern[j];

        // Apply case-insensitivity if needed
        if (!case_sensitive)
        {
            text_char = lower_table[(unsigned char)text_char];
            pattern_char = lower_table[(unsigned char)pattern_char];
        }

        // If characters match, advance both pointers
        if (pattern_char == text_char)
        {
            i++;
            j++;
        }

        // If pattern pointer reaches the end, a full match is found
        if (j == pattern_len)
        {
            size_t match_start_index = i - j; // Calculate start index of the found match
            // Check if the match *starts* within the reportable range
            if (match_start_index < report_limit_offset)
            {
                match_count++;
                if (track_positions && result)
                {
                    // Add match [start_index, end_index)
                    if (!match_result_add(result, match_start_index, i))
                    {
                        fprintf(stderr, "Warning: Failed to add match position (KMP).\n");
                    }
                }
            }
            else
            {
                // Match starts outside report limit, potentially break if this signifies end of chunk
                // For single thread, continue searching. For multi-thread, this check matters more.
            }

            // Use LPS value to shift the pattern efficiently for the next potential match
            j = lps[j - 1];
        }
        // Mismatch occurred after j matches
        else if (i < text_len && pattern_char != text_char)
        {
            // If j is not 0, use LPS array to find the next shorter prefix that is also a suffix
            if (j != 0)
            {
                j = lps[j - 1]; // Backtrack pattern pointer 'j'
            }
            else // Mismatch occurred at the very first character of the pattern (j=0)
            {
                i = i + 1; // Simply advance the text pointer 'i'
            }
        }
    } // End while loop

    free(lps); // Free the allocated LPS array
    return match_count;
}

/* --- Rabin-Karp Algorithm --- */
uint64_t rabin_karp_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset,
                           bool track_positions, match_result_t *result)
{
    uint64_t match_count = 0;

    // Basic checks
    if (pattern_len == 0 || text_len < pattern_len || report_limit_offset == 0)
    {
        return 0;
    }

    // Use KMP for very short patterns where RK hash overhead isn't beneficial
    if (pattern_len <= 4) // Threshold can be tuned
    {
        // Pass through the tracking parameters
        return kmp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, track_positions, result);
    }

    // Constants for hashing
    const uint64_t prime = 1000000007ULL; // A large prime number
    const uint64_t base = 256ULL;         // Number of characters in the input alphabet (assuming ASCII/UTF-8 subset)
    uint64_t pattern_hash = 0;            // Hash value for pattern
    uint64_t text_hash = 0;               // Hash value for current text window
    uint64_t h = 1;                       // base^(pattern_len-1) % prime, used for rolling hash calculation

    // Calculate h = base^(pattern_len-1) % prime efficiently
    for (size_t i = 0; i < pattern_len - 1; i++)
    {
        h = (h * base) % prime;
    }

    // Calculate the hash value of the pattern and the first window of text
    for (size_t i = 0; i < pattern_len; i++)
    {
        char pc = pattern[i];
        char tc = text[i];
        // Apply case-insensitivity if needed during hash calculation
        if (!case_sensitive)
        {
            pc = lower_table[(unsigned char)pc];
            tc = lower_table[(unsigned char)tc];
        }
        pattern_hash = (base * pattern_hash + pc) % prime;
        text_hash = (base * text_hash + tc) % prime;
    }

    // Slide the pattern over the text one character at a time
    size_t limit = text_len - pattern_len;
    for (size_t i = 0; i <= limit; i++)
    {
        // Check if the current window's start 'i' is within the report limit
        // We only proceed if the potential match could be counted.
        if (i >= report_limit_offset)
        {
            // If we are past the report limit for this chunk, stop searching in this chunk.
            break;
        }

        // Check if the hash values of the current window of text and pattern match.
        if (pattern_hash == text_hash)
        {
            // Hashes match, verify character by character (handle collisions)
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
                    match = false; // Mismatch found
                    break;
                }
            }

            // If characters match after hash match (genuine match)
            if (match)
            {
                // We already checked `i < report_limit_offset` above
                match_count++;
                if (track_positions && result)
                {
                    if (!match_result_add(result, i, i + pattern_len))
                    {
                        fprintf(stderr, "Warning: Failed to add match position (RK).\n");
                    }
                }
            }
        } // End hash check block

        // Calculate hash value for the next window of text using rolling hash
        if (i < limit) // Check if there is a next window
        {
            char leading_char = text[i];                // Character leaving the window
            char trailing_char = text[i + pattern_len]; // Character entering the window

            // Apply case-insensitivity to characters used in hash update
            if (!case_sensitive)
            {
                leading_char = lower_table[(unsigned char)leading_char];
                trailing_char = lower_table[(unsigned char)trailing_char];
            }

            // Calculate rolling hash:
            // text_hash = (base * (text_hash - leading_char * h) + trailing_char) % prime;
            // Add prime before subtracting to handle potential negative result in modulo arithmetic
            text_hash = (base * (text_hash + prime - (h * leading_char) % prime)) % prime;
            text_hash = (text_hash + trailing_char) % prime;
        }
    } // End loop sliding window
    return match_count;
}

/* --- SIMD Search Implementations --- */
#ifdef __SSE4_2__
uint64_t simd_sse42_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset,
                           bool track_positions, match_result_t *result)
{
    // fprintf(stderr, "Info: Using SSE4.2 search (currently fallback to Boyer-Moore).\n");
    // TODO: Implement actual SSE4.2 search (e.g., using _mm_cmpistri)
    //       If implemented, it MUST correctly handle report_limit_offset and
    //       call match_result_add if track_positions is true.
    // Fallback to Boyer-Moore which supports position tracking
    return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, track_positions, result);
}
#endif // __SSE4_2__

#ifdef __AVX2__
uint64_t simd_avx2_search(const char *text, size_t text_len,
                          const char *pattern, size_t pattern_len,
                          bool case_sensitive, size_t report_limit_offset,
                          bool track_positions, match_result_t *result)
{
    // fprintf(stderr, "Info: Using AVX2 search (currently fallback).\n");
    // TODO: Implement actual AVX2 search (e.g., using _mm256_cmpeq_epi8 etc.)
    //       Must handle report_limit_offset and position tracking correctly.
#ifdef __SSE4_2__
    // Fallback strategy: Use SSE4.2 if pattern fits, otherwise BMH
    if (pattern_len <= SIMD_MAX_LEN_SSE42)
    {
        // Fallback to SSE4.2 (which currently falls back to BMH)
        return simd_sse42_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, track_positions, result);
    }
    else
    {
        // Fallback to BMH for longer patterns
        return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, track_positions, result);
    }
#else
    // Fallback directly to BMH if SSE4.2 is not available
    return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, track_positions, result);
#endif
}
#endif // __AVX2__

#ifdef __ARM_NEON
uint64_t neon_search(const char *text, size_t text_len,
                     const char *pattern, size_t pattern_len,
                     bool case_sensitive, size_t report_limit_offset,
                     bool track_positions, match_result_t *result)
{
    // fprintf(stderr, "Info: Using NEON search (currently fallback to Boyer-Moore).\n");
    // TODO: Implement actual NEON search.
    //       Must handle report_limit_offset and position tracking correctly.
    // Fallback to BMH
    return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, track_positions, result);
}
#endif // __ARM_NEON

/* --- Regex Search --- */
uint64_t regex_search(const char *text, size_t text_len,
                      const regex_t *compiled_regex,
                      size_t report_limit_offset,
                      bool track_positions,
                      match_result_t *result)
{
    uint64_t match_count = 0;
    regmatch_t match; // Structure to hold match start/end offsets (rm_so, rm_eo)

    // Basic validation
    if (!compiled_regex || text_len == 0 || report_limit_offset == 0)
        return 0;

    const char *search_start = text; // Pointer to the current search position in text
    size_t current_offset = 0;       // Offset from original text start to search_start

    // Loop while matches are found within the text buffer
    int exec_flags = 0;               // Flags for regexec
    while (current_offset < text_len) // Ensure we don't start search past the end
    {
        // Execute the regex search starting from search_start
        // regexec returns 0 on match, REG_NOMATCH otherwise
        int ret = regexec(compiled_regex, search_start, 1, &match, exec_flags);

        if (ret == REG_NOMATCH)
        {
            break; // No more matches found in the remaining text
        }
        else if (ret != 0)
        {
            // Handle other regex execution errors
            char errbuf[256];
            regerror(ret, compiled_regex, errbuf, sizeof(errbuf));
            fprintf(stderr, "Error during regex execution: %s\n", errbuf);
            break; // Stop searching on error
        }

        // Match found! Calculate absolute offsets in the original text buffer
        // match.rm_so and match.rm_eo are offsets relative to search_start
        size_t match_start_abs = current_offset + match.rm_so;
        size_t match_end_abs = current_offset + match.rm_eo;

        // Check if the match *starts* before the reporting limit for this chunk/call
        if (match_start_abs < report_limit_offset)
        {
            match_count++;
            if (track_positions && result)
            {
                // Add the absolute match offsets
                if (!match_result_add(result, match_start_abs, match_end_abs))
                {
                    fprintf(stderr, "Warning: Failed to add regex match position, results may be incomplete.\n");
                    // Optionally break or handle memory exhaustion
                }
            }
        }
        else
        {
            // Match starts at or after the limit for this chunk/call.
            // In multi-threading, this means we stop processing for this chunk.
            // In single-threading, this check might be redundant if report_limit_offset is text_len.
            break;
        }

        // --- Advance Logic for Next Search ---
        // Calculate how much to advance search_start for the next iteration.
        // Default: advance past the end of the current match.
        regoff_t advance_offset_in_chunk = match.rm_eo;

        // Crucial fix for zero-length matches (e.g., `^`, `$`, `\b`):
        // If the match was zero-length (rm_so == rm_eo), we MUST advance
        // by at least one character *relative to the start of the current search (search_start)*
        // to avoid an infinite loop on the same position.
        if (match.rm_so == match.rm_eo)
        {
            // Advance by 1 character from the start of the current search window
            advance_offset_in_chunk = (match.rm_so == 0) ? 1 : match.rm_so + 1;
            // Safety check: ensure we always advance at least 1 char if match was zero-length
            if (advance_offset_in_chunk <= match.rm_so)
                advance_offset_in_chunk = match.rm_so + 1;

            // Another safety check: if rm_so was already at the end of the buffer,
            // advancing by 1 might go out of bounds. Check against remaining length.
            size_t remaining_len_in_search_start = text_len - current_offset;
            if ((size_t)advance_offset_in_chunk > remaining_len_in_search_start)
            {
                // Cannot advance, we are at the end.
                break;
            }
        }
        // Also handle potential issues where rm_eo <= rm_so (shouldn't happen with valid regex/match)
        else if (advance_offset_in_chunk <= match.rm_so)
        {
            // Force advancement by at least one character past the start of the match
            advance_offset_in_chunk = match.rm_so + 1;
        }

        // Ensure advancement doesn't try to go past the end of the original text buffer
        size_t remaining_len_total = text_len - current_offset;
        if ((size_t)advance_offset_in_chunk > remaining_len_total)
        {
            // Cannot advance further within the original text length
            break;
        }

        // Advance the search pointer and the absolute offset tracker
        search_start += advance_offset_in_chunk;
        current_offset += advance_offset_in_chunk;

        // Set REG_NOTBOL for subsequent searches within the same text buffer,
        // unless the regex engine/flags handle this automatically. POSIX requires it.
        // This prevents '^' from matching incorrectly after the first line.
        exec_flags = REG_NOTBOL;
        // REG_NOTEOL might also be needed depending on '$' behavior desired.

    } // End while loop for finding matches

    return match_count;
}

/* --- Multi-threading Logic --- */
void *search_thread(void *arg)
{
    search_job_t *job = (search_job_t *)arg;
    job->local_count = 0; // Initialize local counter for this thread

    // --- Determine Buffer and Reporting Limits ---

    // Start position for reading is the thread's assigned start
    size_t buffer_abs_start = job->start_pos;

    // Overlap needed for non-regex searches to catch matches spanning chunk boundaries.
    // Regex handles its own context, so no overlap needed there.
    size_t overlap = (job->use_regex || job->pattern_len == 0) ? 0 : job->pattern_len - 1;

    // Calculate the end position for *reading*, including overlap if needed.
    size_t buffer_abs_end;
    if (job->thread_id == job->total_threads - 1)
    {
        // Last thread reads until the very end of the file.
        buffer_abs_end = job->file_size;
    }
    else
    {
        // Other threads read up to their designated end + overlap.
        buffer_abs_end = job->end_pos + overlap;
    }

    // Clamp buffer end to the actual file size to prevent reading past the end.
    if (buffer_abs_end > job->file_size)
    {
        buffer_abs_end = job->file_size;
    }

    // Calculate pointer and length for the buffer this thread actually processes.
    const char *buffer_ptr = job->file_data + buffer_abs_start;
    // Ensure length calculation doesn't underflow if end < start (shouldn't happen).
    size_t buffer_len = (buffer_abs_end > buffer_abs_start) ? buffer_abs_end - buffer_abs_start : 0;

    // Determine the limit for *counting* matches. Matches should only be counted
    // by this thread if they *start* within the thread's primary assigned chunk
    // (i.e., start offset < job->end_pos). This offset is relative to buffer_ptr.
    size_t report_limit_offset = (job->end_pos > buffer_abs_start) ? job->end_pos - buffer_abs_start : 0;
    // Clamp report limit to the actual buffer length being processed.
    if (report_limit_offset > buffer_len)
    {
        report_limit_offset = buffer_len;
    }

    // --- Basic Checks Before Searching ---
    if (buffer_len == 0 || report_limit_offset == 0)
    {
        // No data in this chunk or no space to report matches within the primary zone.
        return NULL;
    }
    // If buffer is shorter than pattern (non-regex), no match is possible.
    if (!job->use_regex && buffer_len < job->pattern_len)
    {
        return NULL;
    }

    // --- Select and Execute Search Algorithm ---
    // NOTE: Position tracking is DISABLED in multi-threaded mode for simplicity.
    //       The 'result' parameter is passed as NULL to search functions.
    bool track_positions = false;
    match_result_t *result = NULL;

    if (job->use_regex)
    {
        // Use regex search
        job->local_count = regex_search(buffer_ptr, buffer_len,
                                        job->regex,
                                        report_limit_offset, // Only count matches starting before this offset within buffer
                                        track_positions,
                                        result);
    }
    else // Non-regex search
    {
        // --- Dynamic algorithm selection based on pattern length and SIMD availability ---
        const size_t KMP_THRESH = 3; // Threshold for using KMP (short patterns)
        const size_t RK_THRESH = 32; // Threshold for considering Rabin-Karp (long patterns)

        if (job->pattern_len < KMP_THRESH)
        {
            job->local_count = kmp_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset, track_positions, result);
        }
#ifdef __AVX2__
        else if (job->pattern_len <= SIMD_MAX_LEN_AVX2)
        {
            // Note: SIMD fallbacks currently call BMH. If AVX2 is implemented, use it here.
            job->local_count = simd_avx2_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset, track_positions, result);
        }
#elif defined(__SSE4_2__)
        else if (job->pattern_len <= SIMD_MAX_LEN_SSE42)
        {
            // Note: SIMD fallbacks currently call BMH. If SSE4.2 is implemented, use it here.
            job->local_count = simd_sse42_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset, track_positions, result);
        }
#elif defined(__ARM_NEON)
        // Placeholder for potential NEON implementation
        // else if (job->pattern_len <= NEON_MAX_LEN) { // Define NEON_MAX_LEN if needed
        //     job->local_count = neon_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset, track_positions, result);
        // }
        else if (job->pattern_len > RK_THRESH) // Use RK only if no SIMD and pattern is long
        {
            job->local_count = rabin_karp_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset, track_positions, result);
        }
        else // Default to BMH if no better option applies
        {
            job->local_count = boyer_moore_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset, track_positions, result);
        }
#else  // No SIMD defined path
        else if (job->pattern_len > RK_THRESH) // Use RK if long pattern
        {
            job->local_count = rabin_karp_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset, track_positions, result);
        }
        else // Default to BMH
        {
            job->local_count = boyer_moore_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset, track_positions, result);
        }
#endif // End SIMD checks
    } // End non-regex block

    return NULL; // Thread function returns void*
}

/* --- Public API Implementations --- */

/**
 * Search within a string (single-threaded).
 * Exposed potentially as part of a library API.
 */
int search_string(const char *pattern, size_t pattern_len, const char *text, bool case_sensitive, bool use_regex, bool count_only)
{
    double start_time = get_time();
    size_t text_len = strlen(text);  // Assume text is null-terminated
    uint64_t total_match_count = 0;  // Total occurrences
    size_t unique_lines_printed = 0; // Count of unique lines printed
    regex_t compiled_regex;
    bool regex_compiled = false;
    const char *algo_name = "Unknown";
    match_result_t *matches = NULL;
    // Position tracking is enabled if not count_only (always single-threaded here)
    bool track_positions = !count_only;

    // --- Input Validation ---
    if (!pattern)
    {
        fprintf(stderr, "Error (search_string): Pattern cannot be NULL.\n");
        return 1; // Return error code
    }
    if (pattern_len == 0 && !use_regex)
    {
        fprintf(stderr, "Error (search_string): Pattern cannot be empty for literal search.\n");
        return 1;
    }
    if (!text)
    {
        fprintf(stderr, "Error (search_string): Text to search cannot be NULL.\n");
        return 1;
    }
    // Handle empty text or pattern longer than text early
    if (text_len == 0 || (!use_regex && pattern_len > text_len))
    {
        if (count_only)
            printf("0\n");
        else
        {
            // Print minimal summary even for no matches
            printf("\nFound 0 matching lines\n"); // Report lines here
            // Optionally print timing and details
            double end_time = get_time();
            printf("Search completed in %.4f seconds\n", end_time - start_time);
            printf("Search details:\n");
            printf("  - String length: %zu characters\n", text_len);
            printf("  - Pattern length: %zu characters\n", pattern_len);
            printf("  - Algorithm used: N/A\n");
            printf("  - %s search\n", case_sensitive ? "Case-sensitive" : "Case-insensitive");
            printf("  - %s pattern\n", use_regex ? "Regular expression" : "Literal");
        }
        return 0; // Return success code (0 matches found)
    }
    if (pattern_len > MAX_PATTERN_LENGTH && !use_regex)
    {
        fprintf(stderr, "Error (search_string): Pattern length exceeds maximum allowed (%d).\n", MAX_PATTERN_LENGTH);
        return 1;
    }

    // Allocate results struct if tracking positions
    if (track_positions)
    {
        matches = match_result_init(16); // Start with small capacity
        if (!matches)
        {
            fprintf(stderr, "Error (search_string): Failed to allocate memory for match results.\n");
            return 1; // Return error code
        }
    }

    // --- Compile Regex (if needed) ---
    if (use_regex)
    {
        int regex_flags = REG_EXTENDED;
        if (!case_sensitive)
            regex_flags |= REG_ICASE;

        int ret = regcomp(&compiled_regex, pattern, regex_flags);
        if (ret != 0)
        {
            char errbuf[256];
            regerror(ret, &compiled_regex, errbuf, sizeof(errbuf));
            fprintf(stderr, "Error (search_string) compiling regex: %s\n", errbuf);
            match_result_free(matches); // Free if allocated
            return 1;                   // Return error code
        }
        regex_compiled = true;
        algo_name = "Regex (POSIX)";
        // Perform regex search
        total_match_count = regex_search(text, text_len, &compiled_regex, text_len, track_positions, matches);
    }
    else
    { // Not using regex
        // --- Select and Execute Non-Regex Algorithm ---
        const size_t KMP_THRESH = 3;
        const size_t RK_THRESH = 32;
        size_t report_limit = text_len; // Search the whole string

        if (pattern_len < KMP_THRESH)
        {
            total_match_count = kmp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
            algo_name = "KMP";
        }
#ifdef __AVX2__
        else if (pattern_len <= SIMD_MAX_LEN_AVX2)
        {
            total_match_count = simd_avx2_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
            algo_name = "AVX2 (Fallback)"; // Update if implemented
        }
#elif defined(__SSE4_2__)
        else if (pattern_len <= SIMD_MAX_LEN_SSE42)
        {
            total_match_count = simd_sse42_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
            algo_name = "SSE4.2 (Fallback)"; // Update if implemented
        }
#elif defined(__ARM_NEON)
        // else if (pattern_len <= NEON_MAX_LEN) { total_match_count = neon_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches); algo_name = "NEON (Fallback)"; }
        else if (pattern_len > RK_THRESH)
        { // Use RK only if no SIMD and long pattern
            total_match_count = rabin_karp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
            algo_name = "Rabin-Karp";
        }
        else
        { // Default BMH
            total_match_count = boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
            algo_name = "Boyer-Moore-Horspool";
        }
#else // No SIMD defined path
        else if (pattern_len > RK_THRESH)
        { // Use RK if long pattern
            total_match_count = rabin_karp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
            algo_name = "Rabin-Karp";
        }
        else
        { // Default BMH
            total_match_count = boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
            algo_name = "Boyer-Moore-Horspool";
        }
#endif
    }

    // --- Report Results ---
    double end_time = get_time();
    double search_time = end_time - start_time;

    if (count_only)
    {
        // Report total occurrences when -c is used
        printf("%" PRIu64 "\n", total_match_count);
    }
    else
    {
        // Print matching lines (using filename=NULL for string search context)
        if (track_positions && matches && matches->count > 0)
        {
            // Get the count of unique lines printed
            unique_lines_printed = print_matching_lines(NULL, text, text_len, matches);
        }

        // Print summary details
        // Add separator if lines were printed
        if (track_positions && unique_lines_printed > 0)
        {
            // MODIFIED: Print both lines and matches
            printf("\nLines: %zu \n Matches: %" PRIu64 "\n", unique_lines_printed, total_match_count);
        }
        else
        {
            // Otherwise, print simple count line (using unique lines if available, else total matches)
            printf("Found %" PRIu64 " matching lines/matches\n", track_positions ? unique_lines_printed : total_match_count);
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
        match_result_free(matches); // Free matches struct if allocated

    return 0; // Return success code
}

/**
 * Search within a file using memory mapping and adaptive threading.
 * Exposed potentially as part of a library API.
 */
int search_file(const char *filename, const char *pattern, size_t pattern_len, bool case_sensitive,
                bool count_only, int thread_count, bool use_regex)
{
    double start_time = get_time();
    uint64_t total_match_count = 0;  // Total occurrences found
    size_t unique_lines_printed = 0; // Count of unique lines printed (only used in single-thread print mode)
    int fd = -1;                     // File descriptor
    char *file_data = MAP_FAILED;    // Pointer to memory-mapped file data
    size_t file_size = 0;
    regex_t compiled_regex;
    bool regex_compiled = false;
    int result_code = 0;            // Return code for this function (0=success)
    match_result_t *matches = NULL; // Used only for single-threaded line printing
    const char *algo_name = "Unknown";
    bool track_positions; // Determined later based on count_only and threading

    // --- Input Validation ---
    if (!filename)
    {
        fprintf(stderr, "Error (search_file): Filename cannot be NULL.\n");
        return 1;
    }
    if (!pattern)
    {
        fprintf(stderr, "Error (search_file): Pattern cannot be NULL.\n");
        return 1;
    }
    if (pattern_len == 0 && !use_regex)
    {
        fprintf(stderr, "Error (search_file): Pattern cannot be empty for literal search.\n");
        return 1;
    }
    if (pattern_len > MAX_PATTERN_LENGTH && !use_regex)
    {
        fprintf(stderr, "Error (search_file): Pattern length exceeds maximum allowed (%d).\n", MAX_PATTERN_LENGTH);
        return 1;
    }
    // Handle stdin case - currently unsupported placeholder
    if (strcmp(filename, "-") == 0)
    {
        fprintf(stderr, "Error: Reading from stdin ('-') is not currently supported.\n");
        // TODO: Implement reading stdin into a buffer, then use search_string or adapt file logic.
        return 1;
    }

    // --- File Opening and Stat ---
    fd = open(filename, O_RDONLY | O_CLOEXEC);
    if (fd == -1)
    {
        perror("Error opening file");
        fprintf(stderr, "Filename: %s\n", filename);
        return 1; // Return error
    }
    struct stat file_stat;
    if (fstat(fd, &file_stat) == -1)
    {
        perror("Error getting file stats");
        close(fd); // Close file descriptor before returning
        return 1;  // Return error
    }
    file_size = file_stat.st_size;

    // Handle empty file or pattern longer than file (non-regex) early exit
    if (file_size == 0 || (!use_regex && pattern_len > file_size))
    {
        close(fd);
        if (count_only)
            printf("0\n");
        else
            printf("\nFound 0 matches in '%s'\n", filename); // Provide feedback
        return 0;                                            // Return success (0 matches found)
    }

    // --- Compile Regex (if needed, before memory mapping) ---
    if (use_regex)
    {
        int regex_flags = REG_EXTENDED;
        if (!case_sensitive)
            regex_flags |= REG_ICASE;
        int ret = regcomp(&compiled_regex, pattern, regex_flags);
        if (ret != 0)
        {
            char errbuf[256];
            regerror(ret, &compiled_regex, errbuf, sizeof(errbuf));
            fprintf(stderr, "Error (search_file) compiling regex: %s\n", errbuf);
            close(fd);
            return 1; // Return error
        }
        regex_compiled = true;
    }

    // --- Memory Mapping ---
    // Ensure file_size > 0 before mapping
    file_data = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_data == MAP_FAILED)
    {
        perror("Error memory mapping file");
        close(fd); // Close fd on mmap failure
        if (regex_compiled)
            regfree(&compiled_regex); // Free regex if compiled
        return 1;                     // Return error
    }
    // Advise kernel about access pattern (optional optimization)
    if (madvise(file_data, file_size, MADV_SEQUENTIAL | MADV_WILLNEED) != 0)
    {
        // MADV_WILLNEED might help prefetch, MADV_SEQUENTIAL indicates linear access.
        // Failure is non-critical.
        perror("Warning: madvise failed (non-critical)");
    }
    // Close the file descriptor as it's no longer needed after successful mmap
    close(fd);
    fd = -1; // Mark fd as closed

    // --- Determine Thread Count ---
    int actual_thread_count = DEFAULT_THREAD_COUNT; // Start with default
    int cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);  // Get available cores
    if (cpu_cores > 0)
    {
        actual_thread_count = cpu_cores; // Use core count as a better default
    }
    if (thread_count > 0)
    { // User override
        actual_thread_count = thread_count;
    }
    // Limit threads to a reasonable maximum (e.g., 2x cores or 16)
    int max_reasonable_threads = (cpu_cores > 0) ? (cpu_cores * 2 < 16 ? cpu_cores * 2 : 16) : 16;
    if (actual_thread_count > max_reasonable_threads)
    {
        actual_thread_count = max_reasonable_threads;
    }
    if (actual_thread_count < 1)
        actual_thread_count = 1; // Ensure at least one thread

    // --- Decide Threading Strategy ---
    // Determine if multi-threading should be used based on file size, thread count, and line printing.
    size_t dynamic_threshold = MIN_FILE_SIZE_FOR_THREADS * actual_thread_count;
    // Initially assume track_positions is true if !count_only
    track_positions = !count_only;
    // Use multi-threading only if file is large enough, more than 1 thread, AND count_only is true.
    bool use_multithreading = (file_size >= dynamic_threshold) && (actual_thread_count > 1) && count_only;

    // Force single-threaded mode if line printing is requested, issue warning.
    if (!count_only && actual_thread_count > 1)
    {
        fprintf(stderr, "Warning: Line printing forces single-threaded mode (-t ignored).\n");
        use_multithreading = false;
        actual_thread_count = 1; // Override thread count
    }
    // Final determination of track_positions: True only if !count_only AND !use_multithreading.
    track_positions = !count_only && !use_multithreading;

    const char *execution_mode = use_multithreading ? "Multi-threaded" : "Single-threaded";

    // Allocate results struct ONLY if tracking positions (single-threaded, !count_only)
    if (track_positions)
    {
        matches = match_result_init(100); // Initial capacity
        if (!matches)
        {
            fprintf(stderr, "Error (search_file): Failed to allocate memory for match results.\n");
            result_code = 1;
            goto cleanup; // Use goto for centralized cleanup
        }
    }

    // --- Perform Search (Single or Multi-threaded) ---
    if (!use_multithreading)
    {
        // --- Single-threaded Search ---
        size_t report_limit = file_size; // Search the entire file

        if (use_regex)
        {
            total_match_count = regex_search(file_data, file_size, &compiled_regex, report_limit, track_positions, matches);
            algo_name = "Regex (POSIX)";
        }
        else
        {
            // Select appropriate non-regex algorithm
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
            else if (pattern_len > RK_THRESH)
            { // Use RK only if no SIMD and long pattern
                total_match_count = rabin_karp_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
                algo_name = "Rabin-Karp";
            }
            else
            { // Default BMH
                total_match_count = boyer_moore_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
                algo_name = "Boyer-Moore-Horspool";
            }
#else // No SIMD
            else if (pattern_len > RK_THRESH)
            { // Use RK if long pattern
                total_match_count = rabin_karp_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
                algo_name = "Rabin-Karp";
            }
            else
            { // Default BMH
                total_match_count = boyer_moore_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit, track_positions, matches);
                algo_name = "Boyer-Moore-Horspool";
            }
#endif
        }
    }
    else
    { // Use Multi-threading (track_positions is guaranteed false here, count_only is true)
        // --- Multi-threaded Search ---
        pthread_t *threads = malloc(actual_thread_count * sizeof(pthread_t));
        search_job_t *jobs = malloc(actual_thread_count * sizeof(search_job_t));
        // Use calloc for thread_valid to initialize to false (0)
        bool *thread_valid = calloc(actual_thread_count, sizeof(bool));
        bool memory_error = (!threads || !jobs || !thread_valid);

        if (memory_error)
        {
            perror("Error allocating memory for threads/jobs/flags");
            result_code = 1;
            free(threads);
            free(jobs);
            free(thread_valid); // free checks for NULL
            goto cleanup;
        }

        // Divide work among threads
        size_t chunk_size_per_thread = file_size / actual_thread_count; // Use different name from global CHUNK_SIZE
        if (chunk_size_per_thread == 0 && file_size > 0)
            chunk_size_per_thread = 1; // Min chunk size 1
        size_t current_pos = 0;
        int created_threads = 0; // Track successfully created threads

        for (int i = 0; i < actual_thread_count; i++)
        {
            jobs[i].file_data = file_data;
            jobs[i].file_size = file_size;
            jobs[i].start_pos = current_pos;
            jobs[i].end_pos = (i == actual_thread_count - 1) ? file_size : current_pos + chunk_size_per_thread; // Last thread takes remainder

            // Clamp end_pos and handle edge cases
            if (jobs[i].end_pos > file_size)
                jobs[i].end_pos = file_size;
            if (jobs[i].start_pos >= file_size)
                jobs[i].end_pos = jobs[i].start_pos; // No work if start is already at end

            jobs[i].pattern = pattern;
            jobs[i].pattern_len = pattern_len;
            jobs[i].case_sensitive = case_sensitive;
            jobs[i].use_regex = use_regex;
            jobs[i].regex = use_regex ? &compiled_regex : NULL;
            jobs[i].thread_id = i;
            jobs[i].total_threads = actual_thread_count;
            jobs[i].local_count = 0; // Initialize local count

            // Only create thread if there's work to do (start < end)
            if (jobs[i].start_pos < jobs[i].end_pos)
            {
                if (pthread_create(&threads[i], NULL, search_thread, &jobs[i]) != 0)
                {
                    perror("Error creating search thread");
                    // Attempt to join already created threads before failing
                    for (int j = 0; j < created_threads; j++)
                    {
                        if (thread_valid[j])
                        {
                            pthread_join(threads[j], NULL);
                            total_match_count += jobs[j].local_count;
                        }
                    }
                    result_code = 1;
                    free(threads);
                    free(jobs);
                    free(thread_valid);
                    goto cleanup;
                }
                thread_valid[i] = true; // Mark thread as successfully created
                created_threads++;
            }
            else
            {
                thread_valid[i] = false; // No work for this thread index
            }
            // Update current_pos for the next thread's start_pos
            current_pos = jobs[i].end_pos;
        } // End thread creation loop

        // Wait for threads to complete and aggregate results
        for (int i = 0; i < actual_thread_count; i++)
        {
            if (thread_valid[i])
            { // Only join valid threads
                pthread_join(threads[i], NULL);
                total_match_count += jobs[i].local_count; // Add this thread's count
            }
        }

        // Free thread-related memory
        free(threads);
        free(jobs);
        free(thread_valid);
        algo_name = "Multiple (Dynamic)"; // Indicate multi-threaded execution
    } // End multi-threaded block

    // --- Final Reporting ---
    if (result_code == 0) // Only report if no errors occurred during setup/threading
    {
        // Print matching lines IF tracking was enabled (single-threaded, !count_only) AND matches were found
        if (track_positions && matches && matches->count > 0)
        {
            unique_lines_printed = print_matching_lines(filename, file_data, file_size, matches);
        }

        double end_time = get_time();
        double search_time = end_time - start_time;
        // Avoid division by zero for MB/s calculation
        double mb_per_sec = (search_time > 1e-9 && file_size > 0) ? (file_size / (1024.0 * 1024.0)) / search_time : 0.0;

        // --- Output based on flags ---
        if (count_only)
        {
            // Only print the final count (total occurrences)
            printf("%" PRIu64 "\n", total_match_count);
        }
        else
        {
            // Not count_only: Print summary info
            // Add separator if lines were printed
            if (track_positions && unique_lines_printed > 0)
            {
                // MODIFIED: Print both lines and matches
                printf("\nLines: %zu\nMatches: %" PRIu64 "\n", unique_lines_printed, total_match_count);
            }
            else if (track_positions && matches && matches->count == 0)
            {
                // If tracking positions but found 0 matches (so 0 lines printed)
                printf("\nFound 0 matching lines in '%s'\n", filename);
            }
            else
            {
                // If not tracking positions (i.e., count_only was true), report total matches found.
                // Or if tracking failed allocation.
                // Or if string mode and no lines printed (unique_lines_printed is 0)
                printf("Found %" PRIu64 " matches in '%s'\n", total_match_count, filename);
            }

            // Print detailed summary
            printf("Search completed in %.4f seconds (%.2f MB/s)\n", search_time, mb_per_sec);
            printf("Search details:\n");
            printf("  - File size: %.2f MB (%zu bytes)\n", file_size / (1024.0 * 1024.0), file_size);
            printf("  - Pattern length: %zu characters\n", pattern_len);
            printf("  - Pattern type: %s\n", use_regex ? "Regular expression" : "Literal text");
            printf("  - Execution: %s (%d thread%s)\n", execution_mode, actual_thread_count, actual_thread_count > 1 ? "s" : "");
            printf("  - Algorithm used: %s\n", algo_name);
#ifdef __AVX2__
            printf("  - SIMD Available: AVX2%s\n", (strstr(algo_name, "AVX2") ? " (Used Fallback)" : ""));
#elif defined(__SSE4_2__)
            printf("  - SIMD Available: SSE4.2%s\n", (strstr(algo_name, "SSE4.2") ? " (Used Fallback)" : ""));
#elif defined(__ARM_NEON)
            printf("  - SIMD Available: NEON%s\n", (strstr(algo_name, "NEON") ? " (Used Fallback)" : ""));
#else
            printf("  - SIMD Available: No (Using scalar algorithms)\n");
#endif
            printf("  - %s search\n", case_sensitive ? "Case-sensitive" : "Case-insensitive");
        }
    }

cleanup: // Central cleanup point
    // --- Cleanup mmap, regex, results, and fd ---
    if (file_data != MAP_FAILED)
    {
        munmap(file_data, file_size); // Unmap the memory region
    }
    if (regex_compiled)
    {
        regfree(&compiled_regex); // Free compiled regex
    }
    if (matches)
    { // Free results struct if allocated
        match_result_free(matches);
    }
    if (fd != -1)
    { // Close fd if it wasn't closed after mmap (e.g., if mmap failed)
        close(fd);
    }

    return result_code; // Return 0 on success, non-zero on error
}

/* --- Main Entry Point --- */
// (main function remains the same as previous version)
#if !defined(TESTING) && !defined(TEST)
__attribute__((weak)) // Make main weak for testing overrides
int
main(int argc, char *argv[])
{
    char *pattern_arg = NULL;
    char *file_or_string_arg = NULL;
    bool case_sensitive = true; // Default to case-sensitive
    bool count_only = false;    // Default to printing lines/matches
    bool string_mode = false;   // Default to file mode
    bool use_regex = false;     // Default to literal search
    int thread_count = 0;       // 0 signifies default/auto thread count
    int opt;

    // Basic color detection (can be overridden by --color option later)
    // TODO: Add proper --color=WHEN option parsing using getopt_long
    color_output_enabled = isatty(STDOUT_FILENO);

    // Check for minimum arguments (program name + pattern + source)
    if (argc < 3)
    {
        print_usage(argv[0]);
        return 1; // Exit with error code
    }

    // Parse command-line options using getopt
    while ((opt = getopt(argc, argv, "icrvt:shv")) != -1)
    {
        switch (opt)
        {
        case 'i': // Case-insensitive
            case_sensitive = false;
            break;
        case 'c': // Count only
            count_only = true;
            color_output_enabled = false; // Disable color if only counting
            break;
        case 'r': // Regex mode
            use_regex = true;
            break;
        case 't': // Thread count
        {
            char *endptr;
            errno = 0; // Reset errno before strtol
            long val = strtol(optarg, &endptr, 10);
            // Validate strtol result
            if (errno != 0 || optarg == endptr || *endptr != '\0' || val <= 0 || val > INT_MAX)
            {
                fprintf(stderr, "Warning: Invalid thread count '%s', using default/auto.\n", optarg);
                thread_count = 0; // Use 0 for default/auto
            }
            else
            {
                thread_count = (int)val;
            }
            break;
        }
        case 's': // String mode
            string_mode = true;
            break;
        case 'v': // Version
            printf("krep v%s\n", VERSION);
            return 0; // Exit successfully
        case 'h':     // Help
            print_usage(argv[0]);
            return 0; // Exit successfully
        case '?':     // Unknown option or missing argument
            print_usage(argv[0]);
            return 1; // Exit with error code
        default:      // Should not happen
            fprintf(stderr, "Internal error parsing options.\n");
            abort();
        }
    }

    // --- Validate Positional Arguments ---
    // optind is the index of the first non-option argument
    if (optind >= argc)
    {
        fprintf(stderr, "Error: Missing required PATTERN argument.\n");
        print_usage(argv[0]);
        return 1;
    }
    pattern_arg = argv[optind++]; // Get pattern, increment index

    if (optind >= argc)
    {
        fprintf(stderr, "Error: Missing required %s argument.\n", string_mode ? "STRING_TO_SEARCH" : "FILE");
        print_usage(argv[0]);
        return 1;
    }
    file_or_string_arg = argv[optind++]; // Get file/string, increment index

    // Check for any extra arguments
    if (optind < argc)
    {
        fprintf(stderr, "Error: Unexpected extra arguments starting with '%s'.\n", argv[optind]);
        print_usage(argv[0]);
        return 1;
    }

    // --- Validate Pattern Length (early check) ---
    size_t pattern_len = strlen(pattern_arg);
    if (pattern_len == 0 && !use_regex)
    {
        fprintf(stderr, "Error: Pattern cannot be empty for literal search.\n");
        return 1;
    }
    if (pattern_len > MAX_PATTERN_LENGTH && !use_regex)
    {
        fprintf(stderr, "Error: Pattern length (%zu) exceeds maximum allowed (%d) for literal search.\n", pattern_len, MAX_PATTERN_LENGTH);
        return 1;
    }

    // --- Execute Search ---
    int result = 1; // Default to error status
    if (string_mode)
    {
        // Call search_string API function
        result = search_string(pattern_arg, pattern_len, file_or_string_arg, case_sensitive, use_regex, count_only);
    }
    else
    {
        // Call search_file API function
        result = search_file(file_or_string_arg, pattern_arg, pattern_len, case_sensitive, count_only, thread_count, use_regex);
    }

    return result; // Return exit code from search function (0 for success)
}
#endif // !defined(TESTING) && !defined(TEST)

/* --- TESTING COMPATIBILITY WRAPPERS --- */
#ifdef TESTING
// These wrappers adapt the main search functions for testing frameworks
// that might expect a simpler signature (without position tracking parameters).
// They call the main functions with position tracking disabled.

uint64_t boyer_moore_search_compat(const char *text, size_t text_len, const char *pattern, size_t pattern_len, bool case_sensitive, size_t report_limit_offset)
{
    // Call the actual function with track_positions=false and result=NULL
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

uint64_t regex_search_compat(const char *text, size_t text_len, const regex_t *compiled_regex, size_t report_limit_offset)
{
    return regex_search(text, text_len, compiled_regex, report_limit_offset, false, NULL);
}

// Add compatibility wrappers for SIMD functions if they are tested directly
#ifdef __SSE4_2__
uint64_t simd_sse42_search_compat(const char *text, size_t text_len, const char *pattern, size_t pattern_len, bool case_sensitive, size_t report_limit_offset)
{
    return simd_sse42_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, false, NULL);
}
#endif
#ifdef __AVX2__
uint64_t simd_avx2_search_compat(const char *text, size_t text_len, const char *pattern, size_t pattern_len, bool case_sensitive, size_t report_limit_offset)
{
    return simd_avx2_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, false, NULL);
}
#endif
#ifdef __ARM_NEON__
uint64_t neon_search_compat(const char *text, size_t text_len, const char *pattern, size_t pattern_len, bool case_sensitive, size_t report_limit_offset)
{
    return neon_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset, false, NULL);
}
#endif

#endif // TESTING
