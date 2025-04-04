/* krep - A high-performance string search utility
 *
 * Author: Davide Santangelo
 * Version: 0.3.7
 * Year: 2025
 *
 * Features:
 * - Multiple search algorithms (Boyer-Moore-Horspool, KMP, Rabin-Karp)
 * - Basic SIMD (SSE4.2) acceleration implemented (Currently Disabled - Fallback)
 * - Placeholders for AVX2 acceleration
 * - Memory-mapped file I/O for maximum throughput (Optimized flags)
 * - Multi-threaded parallel search (DEPRECATED, now single-threaded for modes needing positions/line state)
 * - Case-sensitive and case-insensitive matching
 * - Direct string search in addition to file search
 * - Regular expression search support (POSIX, compiled once, fixed loop)
 * - Matching line printing with filename prefix and highlighting (single-thread only)
 * - Reports unique matching lines when printing lines (default mode).
 * - Reports count of matching lines when using -c (Optimized).
 * - Prints only matched parts (like grep -o) when using -o.
 * - Optional detailed summary (-d).
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
#include <nmmintrin.h> // SSE4.2 intrinsics
const size_t SIMD_MAX_LEN_SSE42 = 16;
#endif

#ifdef __AVX2__
#include <immintrin.h> // AVX2 intrinsics
const size_t SIMD_MAX_LEN_AVX2 = 32;
#endif

#ifdef __ARM_NEON
#include <arm_neon.h>
// const size_t SIMD_MAX_LEN_NEON = 16; // Define if needed
#endif

// Include the header file where match_position_t and match_result_t are defined
#include "krep.h"

/* Constants */
#define MAX_PATTERN_LENGTH 1024
#define DEFAULT_THREAD_COUNT 4
#define MIN_FILE_SIZE_FOR_THREADS (1 * 1024 * 1024) // Currently unused
#define VERSION "0.3.7"

// ANSI Color Codes for Highlighting
#define KREP_COLOR_MATCH "\033[1;31m" // Bold Red
#define KREP_COLOR_RESET "\033[0m"
#define KREP_COLOR_FILENAME "\033[35m"  // Magenta for filename
#define KREP_COLOR_SEPARATOR "\033[36m" // Cyan for separator (:)

// --- Global state ---
static bool color_output_enabled = false;
static bool show_summary = false;
static bool only_matching = false;

// --- Global lookup table for fast lowercasing ---
static unsigned char lower_table[256];

static void __attribute__((constructor)) init_lower_table(void)
{
    for (int i = 0; i < 256; i++)
    {
        lower_table[i] = tolower(i);
    }
}

/* --- Type definitions --- */
// search_job_t is currently unused
typedef struct
{
    const char *file_data;
    size_t file_size;
    size_t start_pos;
    size_t end_pos;
    const char *pattern;
    size_t pattern_len;
    bool case_sensitive;
    bool use_regex;
    const regex_t *regex;
    int thread_id;
    int total_threads;
    uint64_t local_count;
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
    if (initial_capacity == 0)
        initial_capacity = 16;
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
    if (result->count >= result->capacity)
    {
        if (result->capacity > SIZE_MAX / (2 * sizeof(match_position_t)))
        {
            fprintf(stderr, "Error: Match result capacity overflow potential during reallocation.\n");
            return false;
        }
        uint64_t new_capacity = (result->capacity == 0) ? 16 : result->capacity * 2;
        match_position_t *new_positions = realloc(result->positions, new_capacity * sizeof(match_position_t));
        if (!new_positions)
        {
            perror("Error reallocating match positions");
            return false;
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
static size_t find_line_start(const char *text, size_t pos)
{
    while (pos > 0 && text[pos - 1] != '\n')
        pos--;
    return pos;
}

static size_t find_line_end(const char *text, size_t text_len, size_t pos)
{
    if (pos >= text_len)
        return text_len;
    while (pos < text_len && text[pos] != '\n')
        pos++;
    return pos;
}

size_t print_matching_items(const char *filename, const char *text, size_t text_len, const match_result_t *result)
{
    if (!result || !text || result->count == 0)
        return 0;
    bool is_string_search = (filename == NULL);
    size_t items_printed_count = 0;

    if (only_matching)
    { // Print only matches (-o mode)
        for (uint64_t i = 0; i < result->count; i++)
        {
            size_t match_start = result->positions[i].start_offset;
            size_t match_end = result->positions[i].end_offset;
            if (match_start >= text_len || match_end > text_len || match_start >= match_end)
                continue;

            char *formatted_output = NULL;
            size_t formatted_length = 0;
            FILE *memfile = open_memstream(&formatted_output, &formatted_length);
            if (!memfile)
            {
                perror("Error: Failed to create memory stream for -o formatting");
                continue;
            }

            if (filename)
            { // Optional filename prefix
                if (color_output_enabled)
                    fprintf(memfile, "%s%s%s%s", KREP_COLOR_FILENAME, filename, KREP_COLOR_SEPARATOR, ":");
                else
                    fprintf(memfile, "%s:", filename);
            }
            if (color_output_enabled)
                fprintf(memfile, KREP_COLOR_MATCH);
            fwrite(text + match_start, 1, match_end - match_start, memfile); // Use fwrite for safety
            if (color_output_enabled)
                fprintf(memfile, KREP_COLOR_RESET);

            if (fclose(memfile) != 0)
            {
                perror("Error closing memory stream for -o");
                free(formatted_output);
                continue;
            }
            if (formatted_output)
            {
                puts(formatted_output);
                free(formatted_output);
                items_printed_count++;
            }
            else
            {
                fprintf(stderr, "Internal Warning: Formatted output buffer is NULL after fclose (-o mode).\n");
            }
        }
    }
    else
    { // Print unique matching lines (default mode)
        size_t *printed_lines_starts = NULL;
        size_t printed_lines_capacity = 0;
        bool allocation_failed = false;
        if (!is_string_search)
        { // Allocate tracking array for files
            printed_lines_capacity = 64;
            printed_lines_starts = malloc(printed_lines_capacity * sizeof(size_t));
            if (!printed_lines_starts)
            {
                fprintf(stderr, "Warning: Failed to allocate memory for line tracking...\n");
                allocation_failed = true;
            }
        }

        for (uint64_t i = 0; i < result->count; i++)
        {
            size_t match_start = result->positions[i].start_offset;
            size_t match_end = result->positions[i].end_offset;
            if (match_start >= text_len || match_end > text_len || match_start > match_end)
                continue;

            size_t line_start = find_line_start(text, match_start);
            size_t line_end = find_line_end(text, text_len, match_start);

            if (!is_string_search && printed_lines_starts && !allocation_failed)
            { // Check duplicates
                bool already_printed = false;
                for (size_t j = 0; j < items_printed_count; j++)
                {
                    if (line_start == printed_lines_starts[j])
                    {
                        already_printed = true;
                        break;
                    }
                }
                if (already_printed)
                    continue;
                // Resize tracking array if needed
                if (items_printed_count >= printed_lines_capacity)
                {
                    if (printed_lines_capacity > SIZE_MAX / 2)
                    {
                        fprintf(stderr, "Warning: Cannot expand line tracking array further.\n");
                        allocation_failed = true;
                    }
                    else
                    {
                        size_t new_cap = (printed_lines_capacity == 0) ? 16 : printed_lines_capacity * 2;
                        size_t *new_ptr = realloc(printed_lines_starts, new_cap * sizeof(size_t));
                        if (!new_ptr)
                        {
                            fprintf(stderr, "Warning: Failed to reallocate line tracking array.\n");
                            allocation_failed = true;
                        }
                        else
                        {
                            printed_lines_starts = new_ptr;
                            printed_lines_capacity = new_cap;
                        }
                    }
                }
                if (!allocation_failed && items_printed_count < printed_lines_capacity)
                    printed_lines_starts[items_printed_count] = line_start;
                else if (!allocation_failed)
                {
                    fprintf(stderr, "Warning: Line tracking array full...\n");
                    allocation_failed = true;
                }
            }

            // Format and Print the Line
            char *formatted_line = NULL;
            size_t formatted_length = 0;
            FILE *memfile = open_memstream(&formatted_line, &formatted_length);
            if (!memfile)
            {
                perror("Error: Failed to create memory stream for line formatting");
                continue;
            }

            if (filename)
            { // Filename prefix
                if (color_output_enabled)
                    fprintf(memfile, "%s%s%s%s", KREP_COLOR_FILENAME, filename, KREP_COLOR_SEPARATOR, ":");
                else
                    fprintf(memfile, "%s:", filename);
            }
            // Construct line with highlighting (complex logic for multiple matches per line)
            size_t current_pos = line_start;
            for (uint64_t k = 0; k < result->count; k++)
            {
                size_t k_start = result->positions[k].start_offset;
                size_t k_end = result->positions[k].end_offset;
                if (k_start >= line_start && k_start < line_end && k_start >= current_pos)
                {
                    if (k_start > current_pos)
                        fwrite(text + current_pos, 1, k_start - current_pos, memfile);
                    if (color_output_enabled)
                        fprintf(memfile, KREP_COLOR_MATCH);
                    size_t h_end = (k_end > line_end) ? line_end : k_end;
                    size_t h_len = (h_end > k_start) ? h_end - k_start : 0;
                    if (h_len > 0)
                        fwrite(text + k_start, 1, h_len, memfile);
                    if (color_output_enabled)
                        fprintf(memfile, KREP_COLOR_RESET);
                    current_pos = h_end;
                }
            }
            if (current_pos < line_end)
                fwrite(text + current_pos, 1, line_end - current_pos, memfile);

            if (fclose(memfile) != 0)
            {
                perror("Error closing memory stream");
                free(formatted_line);
                continue;
            }
            if (formatted_line)
            {
                puts(formatted_line);
                free(formatted_line);
                items_printed_count++;
            }
            else
            {
                fprintf(stderr, "Internal Warning: Formatted line buffer is NULL after fclose.\n");
            }
        }
        if (printed_lines_starts)
            free(printed_lines_starts);
    }
    return items_printed_count;
}

// count_unique_matching_lines function removed.

/* --- Forward declarations --- */
void print_usage(const char *program_name);
double get_time(void);
void prepare_bad_char_table(const char *pattern, size_t pattern_len, int *bad_char_table, bool case_sensitive);
// void *search_thread(void *arg); // Threading disabled

// Modified search function signatures
uint64_t boyer_moore_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                            bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                            uint64_t *line_match_count, size_t *last_counted_line_start,
                            bool track_positions, match_result_t *result);
uint64_t kmp_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                    bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                    uint64_t *line_match_count, size_t *last_counted_line_start,
                    bool track_positions, match_result_t *result);
uint64_t rabin_karp_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                           uint64_t *line_match_count, size_t *last_counted_line_start,
                           bool track_positions, match_result_t *result);
uint64_t regex_search(const char *text_start, size_t text_len, const regex_t *compiled_regex,
                      size_t report_limit_offset, bool count_lines_mode,
                      uint64_t *line_match_count, size_t *last_counted_line_start,
                      bool track_positions, match_result_t *result);
#ifdef __SSE4_2__
uint64_t simd_sse42_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                           uint64_t *line_match_count, size_t *last_counted_line_start,
                           bool track_positions, match_result_t *result);
#endif
#ifdef __AVX2__
uint64_t simd_avx2_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                          bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                          uint64_t *line_match_count, size_t *last_counted_line_start,
                          bool track_positions, match_result_t *result);
#endif
#ifdef __ARM_NEON
uint64_t neon_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                     bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                     uint64_t *line_match_count, size_t *last_counted_line_start,
                     bool track_positions, match_result_t *result);
#endif

/* --- Utility Functions --- */
double get_time(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        perror("Failed to get monotonic time");
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

void print_usage(const char *program_name)
{
    printf("krep v%s - A high-performance string search utility\n\n", VERSION);
    printf("Usage: %s [OPTIONS] PATTERN FILE\n", program_name);
    printf("   or: %s [OPTIONS] -s PATTERN STRING_TO_SEARCH\n\n", program_name);
    printf("OPTIONS:\n");
    printf("  -i               Perform case-insensitive matching.\n");
    printf("  -c               Count matching lines. Print only a count of lines containing matches.\n");
    printf("  -o               Only matching. Print only the matched (non-empty) parts of lines,\n");
    printf("                   each match on a separate output line (like grep -o).\n");
    printf("  -d               Display detailed search summary (ignored with -c or -o).\n");
    printf("  -r               Treat PATTERN as a regular expression (POSIX Extended).\n");
    printf("  -t NUM           Use NUM threads (currently ignored unless counting total matches).\n");
    printf("  -s               Search in STRING_TO_SEARCH instead of a FILE.\n");
    printf("  --color[=WHEN] Control color output ('always', 'never', 'auto'). Default: 'auto'.\n");
    printf("  -v               Display version information and exit.\n");
    printf("  -h               Display this help message and exit.\n\n");
    printf("EXAMPLES:\n");
    printf("  %s \"search term\" input.log\n", program_name);
    printf("  %s -i -c ERROR large_log.txt\n", program_name);
    printf("  %s -o '[0-9]+' data.log | sort | uniq -c\n", program_name);
    printf("  %s -r \"^[Ee]rror: .*failed\" system.log\n", program_name);
    printf("  %s -s \"pattern\" \"Search for pattern in this string.\"\n", program_name);
}

/* --- Boyer-Moore-Horspool Algorithm --- */
void prepare_bad_char_table(const char *pattern, size_t pattern_len, int *bad_char_table, bool case_sensitive)
{
    for (int i = 0; i < 256; i++)
        bad_char_table[i] = (int)pattern_len;
    for (size_t i = 0; i < pattern_len - 1; i++)
    {
        unsigned char c = (unsigned char)pattern[i];
        int shift = (int)(pattern_len - 1 - i);
        if (!case_sensitive)
        {
            unsigned char lc = lower_table[c];
            bad_char_table[lc] = shift;
            unsigned char uc = toupper(lc);
            if (uc != lc)
                bad_char_table[uc] = shift;
        }
        else
        {
            bad_char_table[c] = shift;
        }
    }
}

uint64_t boyer_moore_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                            bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                            uint64_t *line_match_count, size_t *last_counted_line_start,
                            bool track_positions, match_result_t *result)
{
    uint64_t total_match_count = 0;
    int bad_char_table[256];
    if (pattern_len == 0 || text_len < pattern_len || report_limit_offset == 0)
        return 0;
    prepare_bad_char_table(pattern, pattern_len, bad_char_table, case_sensitive);

    size_t i = 0;
    while (i <= text_len - pattern_len)
    {
        size_t j = pattern_len - 1;
        size_t k = i + pattern_len - 1;
        while (true)
        {
            char tc = text_start[k];
            char pc = pattern[j];
            if (!case_sensitive)
            {
                tc = lower_table[(unsigned char)tc];
                pc = lower_table[(unsigned char)pc];
            }
            if (tc != pc)
            {
                unsigned char bad_char = (unsigned char)text_start[i + pattern_len - 1];
                int shift = case_sensitive ? bad_char_table[bad_char] : bad_char_table[lower_table[bad_char]];
                i += (shift > 0 ? shift : 1);
                break;
            }
            if (j == 0)
            { // Match found
                if (i < report_limit_offset)
                {
                    total_match_count++;
                    if (count_lines_mode)
                    {
                        size_t line_start = find_line_start(text_start, i);
                        if (line_start != *last_counted_line_start)
                        {
                            (*line_match_count)++;
                            *last_counted_line_start = line_start;
                        }
                    }
                    else if (track_positions && result)
                    {
                        if (!match_result_add(result, i, i + pattern_len))
                            fprintf(stderr, "Warning: Failed to add match position (BMH).\n");
                    }
                }
                i += pattern_len;
                break; // Skip non-overlapping
            }
            j--;
            k--;
        }
    }
    return total_match_count;
}

/* --- Knuth-Morris-Pratt (KMP) Algorithm --- */
static void compute_lps_array(const char *pattern, size_t pattern_len, int *lps, bool case_sensitive)
{
    size_t length = 0;
    lps[0] = 0;
    size_t i = 1;
    while (i < pattern_len)
    {
        char ci = pattern[i];
        char cl = pattern[length];
        if (!case_sensitive)
        {
            ci = lower_table[(unsigned char)ci];
            cl = lower_table[(unsigned char)cl];
        }
        if (ci == cl)
        {
            length++;
            lps[i] = length;
            i++;
        }
        else
        {
            if (length != 0)
                length = lps[length - 1];
            else
            {
                lps[i] = 0;
                i++;
            }
        }
    }
}

uint64_t kmp_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                    bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                    uint64_t *line_match_count, size_t *last_counted_line_start,
                    bool track_positions, match_result_t *result)
{
    uint64_t total_match_count = 0;
    if (pattern_len == 0 || text_len < pattern_len || report_limit_offset == 0)
        return 0;

    // Optimization for single character pattern
    if (pattern_len == 1)
    {
        char p_char = pattern[0];
        char p_lower = lower_table[(unsigned char)p_char];
        for (size_t i = 0; i < text_len; i++)
        {
            if (i >= report_limit_offset)
                break;
            char text_char = text_start[i];
            bool match_found = case_sensitive ? (text_char == p_char) : (lower_table[(unsigned char)text_char] == p_lower);
            if (match_found)
            {
                total_match_count++;
                if (count_lines_mode)
                {
                    size_t line_start = find_line_start(text_start, i);
                    if (line_start != *last_counted_line_start)
                    {
                        (*line_match_count)++;
                        *last_counted_line_start = line_start;
                    }
                }
                else if (track_positions && result)
                {
                    if (!match_result_add(result, i, i + 1))
                        fprintf(stderr, "Warning: Failed to add match position (KMP-1).\n");
                }
            }
        }
        return total_match_count;
    }

    int *lps = malloc(pattern_len * sizeof(int));
    if (!lps)
    {
        perror("malloc failed for KMP LPS array");
        return 0;
    }
    compute_lps_array(pattern, pattern_len, lps, case_sensitive);

    size_t i = 0;
    size_t j = 0; // i: text index, j: pattern index
    while (i < text_len)
    {
        char tc = text_start[i];
        char pc = pattern[j];
        if (!case_sensitive)
        {
            tc = lower_table[(unsigned char)tc];
            pc = lower_table[(unsigned char)pc];
        }

        if (pc == tc)
        {
            i++;
            j++;
        }

        if (j == pattern_len)
        { // Match found
            size_t match_start_index = i - j;
            if (match_start_index < report_limit_offset)
            {
                total_match_count++;
                if (count_lines_mode)
                {
                    size_t line_start = find_line_start(text_start, match_start_index);
                    if (line_start != *last_counted_line_start)
                    {
                        (*line_match_count)++;
                        *last_counted_line_start = line_start;
                    }
                }
                else if (track_positions && result)
                {
                    if (!match_result_add(result, match_start_index, i))
                        fprintf(stderr, "Warning: Failed to add match position (KMP).\n");
                }
            }
            i = match_start_index + pattern_len;
            j = 0; // Skip non-overlapping
        }
        else if (i < text_len && pc != tc)
        { // Mismatch
            if (j != 0)
                j = lps[j - 1];
            else
                i++;
        }
    }
    free(lps);
    return total_match_count;
}

/* --- Rabin-Karp Algorithm --- */
uint64_t rabin_karp_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                           uint64_t *line_match_count, size_t *last_counted_line_start,
                           bool track_positions, match_result_t *result)
{
    uint64_t total_match_count = 0;
    if (pattern_len == 0 || text_len < pattern_len || report_limit_offset == 0)
        return 0;
    if (pattern_len <= 4)
    { // Fallback for short patterns
        return kmp_search(text_start, text_len, pattern, pattern_len, case_sensitive, report_limit_offset,
                          count_lines_mode, line_match_count, last_counted_line_start, track_positions, result);
    }

    const uint64_t prime = 1000000007ULL;
    const uint64_t base = 256ULL;
    uint64_t ph = 0, th = 0, h = 1;
    for (size_t i = 0; i < pattern_len - 1; i++)
        h = (h * base) % prime;
    for (size_t i = 0; i < pattern_len; i++)
    {
        char pc = pattern[i];
        char tc = text_start[i];
        if (!case_sensitive)
        {
            pc = lower_table[(unsigned char)pc];
            tc = lower_table[(unsigned char)tc];
        }
        ph = (base * ph + pc) % prime;
        th = (base * th + tc) % prime;
    }

    size_t limit = text_len - pattern_len;
    for (size_t i = 0; i <= limit; /* i incremented below */)
    {
        if (i >= report_limit_offset)
            break;
        if (ph == th)
        { // Hash match, verify
            bool match = true;
            for (size_t j = 0; j < pattern_len; j++)
            {
                char pc = pattern[j];
                char tc = text_start[i + j];
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
            { // Genuine match
                total_match_count++;
                if (count_lines_mode)
                {
                    size_t line_start = find_line_start(text_start, i);
                    if (line_start != *last_counted_line_start)
                    {
                        (*line_match_count)++;
                        *last_counted_line_start = line_start;
                    }
                }
                else if (track_positions && result)
                {
                    if (!match_result_add(result, i, i + pattern_len))
                        fprintf(stderr, "Warning: Failed to add match position (RK).\n");
                }
                // Skip ahead (non-overlapping) and recalculate hash
                size_t next_start = i + pattern_len;
                if (next_start <= limit)
                {
                    th = 0;
                    for (size_t k = 0; k < pattern_len; ++k)
                    {
                        char tc = text_start[next_start + k];
                        if (!case_sensitive)
                            tc = lower_table[(unsigned char)tc];
                        th = (base * th + tc) % prime;
                    }
                    i = next_start;
                }
                else
                {
                    i = next_start;
                }
                continue;
            }
        }
        // Calculate rolling hash for next window
        if (i < limit)
        {
            char lead = text_start[i];
            char trail = text_start[i + pattern_len];
            if (!case_sensitive)
            {
                lead = lower_table[(unsigned char)lead];
                trail = lower_table[(unsigned char)trail];
            }
            th = (base * (th + prime - (h * lead) % prime)) % prime;
            th = (th + trail) % prime;
        }
        i++;
    }
    return total_match_count;
}

/* --- SIMD Search Implementations (Fallbacks) --- */
#ifdef __SSE4_2__
uint64_t simd_sse42_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                           uint64_t *line_match_count, size_t *last_counted_line_start,
                           bool track_positions, match_result_t *result)
{
    return boyer_moore_search(text_start, text_len, pattern, pattern_len, case_sensitive, report_limit_offset,
                              count_lines_mode, line_match_count, last_counted_line_start, track_positions, result);
}
#endif
#ifdef __AVX2__
uint64_t simd_avx2_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                          bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                          uint64_t *line_match_count, size_t *last_counted_line_start,
                          bool track_positions, match_result_t *result)
{
#ifdef __SSE4_2__
    if (pattern_len <= SIMD_MAX_LEN_SSE42)
    {
        return simd_sse42_search(text_start, text_len, pattern, pattern_len, case_sensitive, report_limit_offset,
                                 count_lines_mode, line_match_count, last_counted_line_start, track_positions, result);
    }
    else
    {
        return boyer_moore_search(text_start, text_len, pattern, pattern_len, case_sensitive, report_limit_offset,
                                  count_lines_mode, line_match_count, last_counted_line_start, track_positions, result);
    }
#else
    return boyer_moore_search(text_start, text_len, pattern, pattern_len, case_sensitive, report_limit_offset,
                              count_lines_mode, line_match_count, last_counted_line_start, track_positions, result);
#endif
}
#endif
#ifdef __ARM_NEON
uint64_t neon_search(const char *text_start, size_t text_len, const char *pattern, size_t pattern_len,
                     bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                     uint64_t *line_match_count, size_t *last_counted_line_start,
                     bool track_positions, match_result_t *result)
{
    return boyer_moore_search(text_start, text_len, pattern, pattern_len, case_sensitive, report_limit_offset,
                              count_lines_mode, line_match_count, last_counted_line_start, track_positions, result);
}
#endif

/* --- Regex Search --- */
uint64_t regex_search(const char *text_start, size_t text_len, const regex_t *compiled_regex,
                      size_t report_limit_offset, bool count_lines_mode,
                      uint64_t *line_match_count, size_t *last_counted_line_start,
                      bool track_positions, match_result_t *result)
{
    uint64_t total_match_count = 0;
    regmatch_t match;
    if (!compiled_regex || report_limit_offset == 0)
        return 0; // Allow text_len == 0

    const char *search_ptr = text_start;
    size_t current_offset = 0;
    int exec_flags = 0;

    // --- FIXED: Handle empty string case ---
    if (text_len == 0)
    {
        int ret = regexec(compiled_regex, text_start, 1, &match, exec_flags);
        if (ret == 0 && match.rm_so == 0 && match.rm_eo == 0)
        { // Check for zero-length match at start
            if (0 < report_limit_offset)
            { // Check report limit (offset 0)
                total_match_count = 1;
                if (count_lines_mode)
                {
                    // For empty string, line start is 0
                    if (0 != *last_counted_line_start)
                    {
                        (*line_match_count)++;
                        *last_counted_line_start = 0;
                    }
                }
                else if (track_positions && result)
                {
                    if (!match_result_add(result, 0, 0))
                        fprintf(stderr, "Warning: Failed to add regex match position.\n");
                }
            }
        }
        else if (ret != REG_NOMATCH)
        { // Handle potential errors
            char errbuf[256];
            regerror(ret, compiled_regex, errbuf, sizeof(errbuf));
            fprintf(stderr, "Error during regex execution on empty string: %s\n", errbuf);
        }
        return total_match_count; // Return count for empty string
    }
    // --- End empty string fix ---

    while (current_offset < text_len)
    {
        int ret = regexec(compiled_regex, search_ptr, 1, &match, exec_flags);
        if (ret == REG_NOMATCH)
            break;
        if (ret != 0)
        {
            char errbuf[256];
            regerror(ret, compiled_regex, errbuf, sizeof(errbuf));
            fprintf(stderr, "Error during regex execution: %s\n", errbuf);
            break;
        }

        size_t match_start_abs = current_offset + match.rm_so;
        size_t match_end_abs = current_offset + match.rm_eo;

        if (match_start_abs < report_limit_offset)
        {
            total_match_count++;
            if (count_lines_mode)
            {
                size_t line_start = find_line_start(text_start, match_start_abs);
                if (line_start != *last_counted_line_start)
                {
                    (*line_match_count)++;
                    *last_counted_line_start = line_start;
                }
            }
            else if (track_positions && result)
            {
                if (!match_result_add(result, match_start_abs, match_end_abs))
                    fprintf(stderr, "Warning: Failed to add regex match position.\n");
            }
        }
        else
        {
            break;
        }

        regoff_t advance = match.rm_eo;
        if (match.rm_so == match.rm_eo)
        {
            advance = (match.rm_so == 0) ? 1 : match.rm_so + 1;
            if (advance <= match.rm_so)
                advance = match.rm_so + 1;
            size_t rem = text_len - current_offset;
            if ((size_t)advance > rem)
                break;
        }
        else if (advance <= match.rm_so)
            advance = match.rm_so + 1;
        size_t rem_tot = text_len - current_offset;
        if ((size_t)advance > rem_tot)
            break;

        search_ptr += advance;
        current_offset += advance;
        exec_flags = REG_NOTBOL;
    }
    return total_match_count;
}

/* --- Public API Implementations --- */
int search_string(const char *pattern, size_t pattern_len, const char *text,
                  bool case_sensitive, bool use_regex, bool count_only)
{
    double start_time = get_time();
    size_t text_len = strlen(text);
    uint64_t total_match_count = 0;
    uint64_t line_match_count = 0;
    size_t items_printed_count = 0;
    size_t last_counted_line = SIZE_MAX;
    regex_t compiled_regex;
    bool regex_compiled = false;
    const char *algo_name = "N/A";
    match_result_t *matches = NULL;
    int result_code = 0;

    bool count_lines_mode = count_only && !only_matching;
    bool track_positions = !count_lines_mode && !(count_only && only_matching);

    if (!pattern || !text)
    {
        fprintf(stderr, "Error: NULL pattern or text in search_string.\n");
        return 1;
    }
    if (pattern_len == 0 && !use_regex)
    {
        fprintf(stderr, "Error: Empty pattern for literal search.\n");
        return 1;
    }
    // Allow empty text for regex ^$ match
    // if (text_len == 0 || (!use_regex && pattern_len > text_len)) { if (count_only) printf("0\n"); return 0; }
    if (text_len > 0 && (!use_regex && pattern_len > text_len))
    {
        if (count_only)
            printf("0\n");
        return 0;
    } // Check only if text not empty
    if (pattern_len > MAX_PATTERN_LENGTH && !use_regex)
    {
        fprintf(stderr, "Error: Pattern too long.\n");
        return 1;
    }

    if (track_positions)
    {
        matches = match_result_init(16);
        if (!matches)
        {
            fprintf(stderr, "Error: Failed to allocate memory for match results.\n");
            return 1;
        }
    }

    if (use_regex)
    {
        int rflags = REG_EXTENDED | REG_NEWLINE;
        if (!case_sensitive)
            rflags |= REG_ICASE;
        int ret = regcomp(&compiled_regex, pattern, rflags);
        if (ret != 0)
        {
            char ebuf[256];
            regerror(ret, &compiled_regex, ebuf, sizeof(ebuf));
            fprintf(stderr, "Error compiling regex: %s\n", ebuf);
            match_result_free(matches);
            return 1;
        }
        regex_compiled = true;
        algo_name = "Regex (POSIX)";
        total_match_count = regex_search(text, text_len, &compiled_regex, text_len, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
    }
    else
    {
        const size_t KMP_THRESH = 3, RK_THRESH = 32;
        size_t report_limit = text_len;
        if (pattern_len < KMP_THRESH)
        {
            total_match_count = kmp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "KMP";
        }
#ifdef __AVX2__
        else if (pattern_len <= SIMD_MAX_LEN_AVX2)
        {
            total_match_count = simd_avx2_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "AVX2 (Fallback)";
        }
#elif defined(__SSE4_2__)
        else if (pattern_len <= SIMD_MAX_LEN_SSE42)
        {
            total_match_count = simd_sse42_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "SSE4.2 (Fallback)";
        }
#elif defined(__ARM_NEON)
        else if (pattern_len > RK_THRESH)
        {
            total_match_count = rabin_karp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "Rabin-Karp";
        }
        else
        {
            total_match_count = boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "Boyer-Moore-Horspool";
        }
#else // No SIMD
        else if (pattern_len > RK_THRESH)
        {
            total_match_count = rabin_karp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "Rabin-Karp";
        }
        else
        {
            total_match_count = boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "Boyer-Moore-Horspool";
        }
#endif
    }

    // Report Results
    double end_time = get_time();
    double search_time = end_time - start_time;
    if (count_lines_mode)
    {
        printf("%" PRIu64 "\n", line_match_count);
    }
    else if (count_only && only_matching)
    {
        printf("%" PRIu64 "\n", total_match_count);
    }
    else if (only_matching)
    {
        if (matches && matches->count > 0)
            items_printed_count = print_matching_items(NULL, text, text_len, matches);
    }
    else
    {
        if (matches && matches->count > 0)
            items_printed_count = print_matching_items(NULL, text, text_len, matches);
        if (show_summary)
        {
            if (items_printed_count > 0)
                printf("\nLines: %zu\nMatches: %" PRIu64 "\n", items_printed_count, total_match_count);
            else
                printf("\nFound 0 matching lines (%" PRIu64 " total matches)\n", total_match_count);
            printf("Search completed in %.4f seconds\n", search_time);
            printf("Search details:\n");
            printf("  - String length: %zu characters\n", text_len);
            printf("  - Pattern length: %zu characters\n", pattern_len);
            printf("  - Algorithm used: %s\n", algo_name);
            printf("  - %s search\n", case_sensitive ? "Case-sensitive" : "Case-insensitive");
            printf("  - %s pattern\n", use_regex ? "Regular expression" : "Literal");
        }
    }

    if (regex_compiled)
        regfree(&compiled_regex);
    match_result_free(matches);
    return result_code;
}

int search_file(const char *filename, const char *pattern, size_t pattern_len,
                bool case_sensitive, bool count_only, int thread_count, bool use_regex)
{
    double start_time = get_time();
    uint64_t total_match_count = 0;
    uint64_t line_match_count = 0;
    size_t items_printed_count = 0;
    size_t last_counted_line = SIZE_MAX;
    int fd = -1;
    char *file_data = MAP_FAILED;
    size_t file_size = 0;
    regex_t compiled_regex;
    bool regex_compiled = false;
    int result_code = 0;
    match_result_t *matches = NULL;
    const char *algo_name = "N/A";

    bool count_lines_mode = count_only && !only_matching;
    bool track_positions = !count_lines_mode && !(count_only && only_matching);

    if (!filename || !pattern)
    {
        fprintf(stderr, "Error: Filename and pattern cannot be NULL.\n");
        return 1;
    }
    if (pattern_len == 0 && !use_regex)
    {
        fprintf(stderr, "Error: Pattern empty.\n");
        return 1;
    }
    if (pattern_len > MAX_PATTERN_LENGTH && !use_regex)
    {
        fprintf(stderr, "Error: Pattern too long.\n");
        return 1;
    }
    if (strcmp(filename, "-") == 0)
    {
        fprintf(stderr, "Error: Stdin not supported.\n");
        return 1;
    }

    fd = open(filename, O_RDONLY | O_CLOEXEC);
    if (fd == -1)
    {
        perror("Error opening file");
        fprintf(stderr, "Filename: %s\n", filename);
        return 1;
    }
    struct stat file_stat;
    if (fstat(fd, &file_stat) == -1)
    {
        perror("Error getting file stats");
        close(fd);
        return 1;
    }
    file_size = file_stat.st_size;
    // Allow empty file only if regex might match it (like ^$)
    if (file_size == 0 && !use_regex)
    {
        close(fd);
        if (count_only)
            printf("0\n");
        return 0;
    }
    if (file_size > 0 && (!use_regex && pattern_len > file_size))
    {
        close(fd);
        if (count_only)
            printf("0\n");
        return 0;
    }

    if (use_regex)
    {
        int rflags = REG_EXTENDED | REG_NEWLINE;
        if (!case_sensitive)
            rflags |= REG_ICASE;
        int ret = regcomp(&compiled_regex, pattern, rflags);
        if (ret != 0)
        {
            char ebuf[256];
            regerror(ret, &compiled_regex, ebuf, sizeof(ebuf));
            fprintf(stderr, "Error compiling regex: %s\n", ebuf);
            close(fd);
            return 1;
        }
        regex_compiled = true;
    }
    // Only map if file size > 0
    if (file_size > 0)
    {
        file_data = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (file_data == MAP_FAILED)
        {
            perror("Error mmap");
            close(fd);
            if (regex_compiled)
                regfree(&compiled_regex);
            return 1;
        }
        madvise(file_data, file_size, MADV_SEQUENTIAL | MADV_WILLNEED);
    }
    else
    {
        file_data = ""; // Use empty string literal if file size is 0
    }
    close(fd);
    fd = -1;

    int actual_thread_count = 1;
    const char *execution_mode = "Single-threaded";
    if (thread_count > 1)
    {
        fprintf(stderr, "Warning: Multi-threading is currently disabled for accuracy, using single thread (-t ignored).\n");
    }

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

    // --- Perform Search (Single-threaded path) ---
    size_t report_limit = file_size;
    if (use_regex)
    {
        total_match_count = regex_search(file_data, file_size, &compiled_regex, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
        algo_name = "Regex (POSIX)";
    }
    else
    {
        const size_t KMP_THRESH = 3, RK_THRESH = 32;
        if (pattern_len < KMP_THRESH)
        {
            total_match_count = kmp_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "KMP";
        }
#ifdef __AVX2__
        else if (pattern_len <= SIMD_MAX_LEN_AVX2)
        {
            total_match_count = simd_avx2_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "AVX2 (Fallback)";
        }
#elif defined(__SSE4_2__)
        else if (pattern_len <= SIMD_MAX_LEN_SSE42)
        {
            total_match_count = simd_sse42_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "SSE4.2 (Fallback)";
        }
#elif defined(__ARM_NEON)
        else if (pattern_len > RK_THRESH)
        {
            total_match_count = rabin_karp_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "Rabin-Karp";
        }
        else
        {
            total_match_count = boyer_moore_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "Boyer-Moore-Horspool";
        }
#else // No SIMD
        else if (pattern_len > RK_THRESH)
        {
            total_match_count = rabin_karp_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "Rabin-Karp";
        }
        else
        {
            total_match_count = boyer_moore_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "Boyer-Moore-Horspool";
        }
#endif
    }

    // --- Final Reporting ---
    if (result_code == 0)
    {
        double end_time = get_time();
        double search_time = end_time - start_time;
        double mb_per_sec = (search_time > 1e-9 && file_size > 0) ? (file_size / (1024.0 * 1024.0)) / search_time : 0.0;

        if (count_lines_mode)
        {
            printf("%" PRIu64 "\n", line_match_count);
        }
        else if (count_only && only_matching)
        {
            printf("%" PRIu64 "\n", total_match_count);
        }
        else if (only_matching)
        {
            if (matches && matches->count > 0)
                items_printed_count = print_matching_items(filename, file_data, file_size, matches);
        }
        else
        {
            if (matches && matches->count > 0)
                items_printed_count = print_matching_items(filename, file_data, file_size, matches);
            if (show_summary)
            {
                if (items_printed_count > 0)
                    printf("\nLines: %zu\nMatches: %" PRIu64 "\n", items_printed_count, total_match_count);
                else
                    printf("\nFound 0 matching lines (%" PRIu64 " total matches) in '%s'\n", total_match_count, filename);
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
    }

cleanup:
    if (file_data != MAP_FAILED && file_size > 0)
        munmap(file_data, file_size); // Only unmap if mapped
    if (regex_compiled)
        regfree(&compiled_regex);
    match_result_free(matches);
    if (fd != -1)
        close(fd);
    return result_code;
}

/* --- Main Entry Point --- */
#if !defined(TESTING) && !defined(TEST)
__attribute__((weak)) int main(int argc, char *argv[])
{
    char *pattern_arg = NULL, *file_or_string_arg = NULL;
    bool case_sensitive = true, count_only = false, string_mode = false, use_regex = false;
    int thread_count = 0, opt;

    color_output_enabled = isatty(STDOUT_FILENO);
    if (argc < 3)
    {
        print_usage(argv[0]);
        return 1;
    }
    show_summary = false;
    only_matching = false;

    while ((opt = getopt(argc, argv, "icodrvt:sh")) != -1)
    {
        switch (opt)
        {
        case 'i':
            case_sensitive = false;
            break;
        case 'c':
            count_only = true;
            color_output_enabled = false;
            break;
        case 'o':
            only_matching = true;
            break;
        case 'd':
            show_summary = true;
            break;
        case 'r':
            use_regex = true;
            break;
        case 't':
        {
            char *e;
            errno = 0;
            long v = strtol(optarg, &e, 10);
            if (errno || optarg == e || *e || v <= 0 || v > INT_MAX)
            {
                fprintf(stderr, "Warning: Invalid thread count '%s'.\n", optarg);
                thread_count = 0;
            }
            else
            {
                thread_count = (int)v;
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
        default:
            print_usage(argv[0]);
            return 1;
        }
    }

    if (optind >= argc)
    {
        fprintf(stderr, "Error: Missing PATTERN.\n");
        print_usage(argv[0]);
        return 1;
    }
    pattern_arg = argv[optind++];
    if (optind >= argc)
    {
        fprintf(stderr, "Error: Missing %s.\n", string_mode ? "STRING" : "FILE");
        print_usage(argv[0]);
        return 1;
    }
    file_or_string_arg = argv[optind++];
    if (optind < argc)
    {
        fprintf(stderr, "Error: Extra arguments.\n");
        print_usage(argv[0]);
        return 1;
    }

    size_t pattern_len = strlen(pattern_arg);
    if (pattern_len == 0 && !use_regex)
    {
        fprintf(stderr, "Error: Pattern empty.\n");
        return 1;
    }
    if (pattern_len > MAX_PATTERN_LENGTH && !use_regex)
    {
        fprintf(stderr, "Error: Pattern too long (%zu > %d).\n", pattern_len, MAX_PATTERN_LENGTH);
        return 1;
    }

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
#endif // !defined(TESTING) && !defined(TEST)

/* --- TESTING COMPATIBILITY WRAPPERS --- */
#ifdef TESTING
// Updated wrappers to call new function signatures with default args for new params
uint64_t boyer_moore_search_compat(const char *t, size_t tl, const char *p, size_t pl, bool cs, size_t rl)
{
    return boyer_moore_search(t, tl, p, pl, cs, rl, false, NULL, NULL, false, NULL);
}
uint64_t kmp_search_compat(const char *t, size_t tl, const char *p, size_t pl, bool cs, size_t rl)
{
    return kmp_search(t, tl, p, pl, cs, rl, false, NULL, NULL, false, NULL);
}
uint64_t rabin_karp_search_compat(const char *t, size_t tl, const char *p, size_t pl, bool cs, size_t rl)
{
    size_t dummy_last_line = SIZE_MAX;
    uint64_t dummy_line_count = 0; // Dummy state needed for potential KMP fallback
    return rabin_karp_search(t, tl, p, pl, cs, rl, false, &dummy_line_count, &dummy_last_line, false, NULL);
}
uint64_t regex_search_compat(const char *t, size_t tl, const regex_t *r, size_t rl)
{
    return regex_search(t, tl, r, rl, false, NULL, NULL, false, NULL);
}
#ifdef __SSE4_2__
uint64_t simd_sse42_search_compat(const char *t, size_t tl, const char *p, size_t pl, bool cs, size_t rl)
{
    return simd_sse42_search(t, tl, p, pl, cs, rl, false, NULL, NULL, false, NULL);
}
#endif
#ifdef __AVX2__
uint64_t simd_avx2_search_compat(const char *t, size_t tl, const char *p, size_t pl, bool cs, size_t rl)
{
    return simd_avx2_search(t, tl, p, pl, cs, rl, false, NULL, NULL, false, NULL);
}
#endif
#ifdef __ARM_NEON__
uint64_t neon_search_compat(const char *t, size_t tl, const char *p, size_t pl, bool cs, size_t rl)
{
    return neon_search(t, tl, p, pl, cs, rl, false, NULL, NULL, false, NULL);
}
#endif
#endif // TESTING
