/* krep - A high-performance string search utility
 *
 * Author: Davide Santangelo
 * Version: 0.4.1
 * Year: 2025
 *
 * Features:
 * - Multiple search algorithms (Boyer-Moore-Horspool, KMP) - All find non-overlapping matches.
 * - Basic SIMD (SSE4.2) acceleration implemented (Currently Disabled - Fallback)
 * - Placeholders for AVX2 acceleration
 * - Memory-mapped file I/O for maximum throughput (Optimized flags)
 * - Multi-threaded parallel search (DEPRECATED, now single-threaded for modes needing positions/line state)
 * - Case-sensitive and case-insensitive matching (-i)
 * - Direct string search (-s) in addition to file search
 * - Regular expression search support (POSIX ERE, enabled with -E)
 * - Pattern specification via -e PATTERN
 * - Recursive directory search (-r) with skipping of binary files and common non-code dirs/exts.
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
#include <pthread.h>  // Still included, but threading logic disabled
#include <inttypes.h> // For PRIu64 macro
#include <errno.h>
#include <limits.h>    // For SIZE_MAX, PATH_MAX
#include <regex.h>     // For POSIX regex support
#include <dirent.h>    // For directory operations
#include <sys/types.h> // For mode_t, DIR*, struct dirent
#include <getopt.h>    // For command-line parsing

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
#endif

// Constants
#define MAX_PATTERN_LENGTH 1024
#define DEFAULT_THREAD_COUNT 4
#define MIN_FILE_SIZE_FOR_THREADS (1 * 1024 * 1024) // Currently unused
#define VERSION "0.4.1"
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#define BINARY_CHECK_BUFFER_SIZE 1024 // Bytes to check for binary content

// ANSI Color Codes
#define KREP_COLOR_RESET "\033[0m"
#define KREP_COLOR_FILENAME "\033[1;34m"  // Blue
#define KREP_COLOR_SEPARATOR "\033[1;30m" // Dark Gray
#define KREP_COLOR_MATCH "\033[1;31m"     // Red

// Global state
static bool color_output_enabled = false;
static bool show_summary = false;
static bool only_matching = false;

// Global lookup table for fast lowercasing
static unsigned char lower_table[256];

static void __attribute__((constructor)) init_lower_table(void)
{
    for (int i = 0; i < 256; i++)
    {
        lower_table[i] = tolower(i);
    }
}

// Type definitions
typedef struct
{
    size_t start_offset;
    size_t end_offset;
} match_position_t;

typedef struct
{
    match_position_t *positions;
    uint64_t count;
    uint64_t capacity;
} match_result_t;

// Match result management functions
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

// Line finding and printing functions
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
            {
                if (color_output_enabled)
                    fprintf(memfile, "%s%s%s%s", KREP_COLOR_FILENAME, filename, KREP_COLOR_SEPARATOR, ":");
                else
                    fprintf(memfile, "%s:", filename);
            }
            if (color_output_enabled)
                fprintf(memfile, KREP_COLOR_MATCH);
            fwrite(text + match_start, 1, match_end - match_start, memfile);
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
        {
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

            char *formatted_line = NULL;
            size_t formatted_length = 0;
            FILE *memfile = open_memstream(&formatted_line, &formatted_length);
            if (!memfile)
            {
                perror("Error: Failed to create memory stream for line formatting");
                continue;
            }

            if (filename)
            {
                if (color_output_enabled)
                    fprintf(memfile, "%s%s%s%s", KREP_COLOR_FILENAME, filename, KREP_COLOR_SEPARATOR, ":");
                else
                    fprintf(memfile, "%s:", filename);
            }
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

// Forward declarations
void print_usage(const char *program_name);
double get_time(void);
void prepare_bad_char_table(const char *pattern, size_t pattern_len, int *bad_char_table, bool case_sensitive);
static bool is_binary_file(const char *filename);
static bool should_skip_extension(const char *filename);

// Search function declarations
uint64_t boyer_moore_search(const char *text_start, size_t text_len, const char *search_pattern, size_t pattern_len,
                            bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                            uint64_t *line_match_count, size_t *last_counted_line_start,
                            bool track_positions, match_result_t *result);
uint64_t kmp_search(const char *text_start, size_t text_len, const char *search_pattern, size_t pattern_len,
                    bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                    uint64_t *line_match_count, size_t *last_counted_line_start,
                    bool track_positions, match_result_t *result);
uint64_t regex_search(const char *text_start, size_t text_len, const regex_t *compiled_regex,
                      size_t report_limit_offset, bool count_lines_mode,
                      uint64_t *line_match_count, size_t *last_counted_line_start,
                      bool track_positions, match_result_t *result);

// Utility Functions
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
    printf("Usage: %s [OPTIONS] PATTERN [FILE | DIRECTORY]\n", program_name);
    printf("   or: %s [OPTIONS] -e PATTERN [-e PATTERN...] [FILE | DIRECTORY]\n", program_name);
    printf("   or: %s [OPTIONS] -s PATTERN STRING_TO_SEARCH\n\n", program_name);
    printf("OPTIONS:\n");
    printf("  -i               Perform case-insensitive matching.\n");
    printf("  -c               Count matching lines. Print only a count of lines containing matches.\n");
    printf("  -o               Only matching. Print only the matched (non-empty) parts of lines,\n");
    printf("                   each match on a separate output line (like grep -o).\n");
    printf("  -d               Display detailed search summary (ignored with -c or -o).\n");
    printf("  -e PATTERN       Specify pattern. Useful for patterns starting with '-' or multiple patterns.\n");
    printf("  -E               Interpret PATTERN as a POSIX Extended Regular Expression (ERE).\n");
    printf("  -r               Recursively search directories. Skips binary files and common non-code\n");
    printf("                   directories (., .., .git, node_modules, etc.) and extensions.\n");
    printf("  -t NUM           Use NUM threads (currently ignored, single-threaded for accuracy).\n");
    printf("  -s               Search in STRING_TO_SEARCH instead of a FILE or DIRECTORY.\n");
    printf("  --color[=WHEN]   Control color output ('always', 'never', 'auto'). Default: 'auto'.\n");
    printf("  -v               Display version information and exit.\n");
    printf("  -h               Display this help message and exit.\n\n");
    printf("EXAMPLES:\n");
    printf("  %s \"search term\" input.log\n", program_name);
    printf("  %s -i -c ERROR large_log.txt\n", program_name);
    printf("  %s -o '[0-9]+' data.log | sort | uniq -c\n", program_name);
    printf("  %s -E \"^[Ee]rror: .*failed\" system.log\n", program_name);
    printf("  %s -e '-pattern-' file.txt\n", program_name);
    printf("  %s -s \"pattern\" \"Search for pattern in this string.\"\n", program_name);
    printf("  %s -r \"MyClass\" /path/to/project\n", program_name);
    printf("  %s -ir 'TODO' .\n", program_name);
}

// Boyer-Moore-Horspool Algorithm
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

uint64_t boyer_moore_search(const char *text_start, size_t text_len, const char *search_pattern, size_t pattern_len,
                            bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                            uint64_t *line_match_count, size_t *last_counted_line_start,
                            bool track_positions, match_result_t *result)
{
    uint64_t total_match_count = 0;
    int bad_char_table[256];
    if (pattern_len == 0 || text_len < pattern_len || report_limit_offset == 0)
        return 0;
    prepare_bad_char_table(search_pattern, pattern_len, bad_char_table, case_sensitive);

    size_t i = 0;
    while (i <= text_len - pattern_len)
    {
        size_t j = pattern_len - 1;
        size_t k = i + pattern_len - 1;
        while (true)
        {
            char tc = text_start[k];
            char pc = search_pattern[j];
            if (!case_sensitive)
            {
                tc = lower_table[(unsigned char)tc];
            }
            if (tc != pc)
            {
                unsigned char bad_char = (unsigned char)text_start[i + pattern_len - 1];
                if (!case_sensitive)
                {
                    bad_char = lower_table[bad_char];
                }
                int shift = bad_char_table[bad_char];
                i += (shift > 0 ? shift : 1);
                break;
            }
            if (j == 0)
            {
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
                i += pattern_len; // Non-overlapping matches
                break;
            }
            j--;
            k--;
        }
    }
    return total_match_count;
}

// Knuth-Morris-Pratt (KMP) Algorithm
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

uint64_t kmp_search(const char *text_start, size_t text_len, const char *search_pattern, size_t pattern_len,
                    bool case_sensitive, size_t report_limit_offset, bool count_lines_mode,
                    uint64_t *line_match_count, size_t *last_counted_line_start,
                    bool track_positions, match_result_t *result)
{
    uint64_t total_match_count = 0;
    if (pattern_len == 0 || text_len < pattern_len || report_limit_offset == 0)
        return 0;

    if (pattern_len == 1)
    {
        char p_char = search_pattern[0];
        if (!case_sensitive)
            p_char = lower_table[(unsigned char)p_char];

        for (size_t i = 0; i < text_len; i++)
        {
            if (i >= report_limit_offset)
                break;
            char text_char = text_start[i];
            bool match_found = case_sensitive ? (text_char == p_char) : (lower_table[(unsigned char)text_char] == p_char);
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
    compute_lps_array(search_pattern, pattern_len, lps, case_sensitive);

    size_t i = 0;
    size_t j = 0;
    while (i < text_len)
    {
        char tc = text_start[i];
        char pc = search_pattern[j];
        if (!case_sensitive)
        {
            tc = lower_table[(unsigned char)tc];
            pc = lower_table[(unsigned char)pc]; // Also convert pattern character for case-insensitive matching
        }
        if (pc == tc)
        {
            i++;
            j++;
        }
        if (j == pattern_len)
        {
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
            i = match_start_index + pattern_len; // Non-overlapping matches
            j = 0;
        }
        else if (i < text_len && pc != tc)
        {
            if (j != 0)
                j = lps[j - 1];
            else
                i++;
        }
    }
    free(lps);
    return total_match_count;
}

// Regex Search
uint64_t regex_search(const char *text_start, size_t text_len, const regex_t *compiled_regex,
                      size_t report_limit_offset, bool count_lines_mode,
                      uint64_t *line_match_count, size_t *last_counted_line_start,
                      bool track_positions, match_result_t *result)
{
    uint64_t total_match_count = 0;
    regmatch_t match;
    if (!compiled_regex)
        return 0;

    const char *search_ptr = text_start;
    size_t current_offset = 0;
    int exec_flags = 0;

    if (text_len == 0)
    {
        if (report_limit_offset > 0)
        {
            int ret = regexec(compiled_regex, text_start, 1, &match, exec_flags);
            if (ret == 0 && match.rm_so == 0 && match.rm_eo == 0)
            {
                total_match_count = 1;
                if (count_lines_mode)
                {
                    if (0 != *last_counted_line_start)
                    {
                        (*line_match_count)++;
                        *last_counted_line_start = 0;
                    }
                }
                else if (track_positions && result)
                {
                    if (!match_result_add(result, 0, 0))
                        fprintf(stderr, "Warning: Failed to add regex match position (empty string).\n");
                }
            }
            else if (ret != REG_NOMATCH)
            {
                char errbuf[256];
                regerror(ret, compiled_regex, errbuf, sizeof(errbuf));
                fprintf(stderr, "Error during regex execution on empty string: %s\n", errbuf);
            }
        }
        return total_match_count;
    }

    while (current_offset < text_len && current_offset < report_limit_offset)
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

        if (match_end_abs > text_len)
            match_end_abs = text_len;
        if (match_end_abs > report_limit_offset)
            match_end_abs = report_limit_offset;
        if (match_start_abs > match_end_abs)
            match_start_abs = match_end_abs;

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
            advance++;
        }
        if (advance <= 0)
        {
            advance = 1;
        }

        search_ptr += advance;
        current_offset += advance;
        exec_flags = REG_NOTBOL;

        if (current_offset >= text_len)
        {
            break;
        }
    }
    return total_match_count;
}

// Public API Implementations
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
        return 2;
    }
    if (pattern_len == 0 && !use_regex)
    {
        fprintf(stderr, "Error: Empty pattern for literal search.\n");
        return 2;
    }
    if (text_len == 0 && !use_regex)
    {
        return 1;
    }
    if (text_len > 0 && (!use_regex && pattern_len > text_len))
    {
        return 1;
    }
    if (pattern_len > MAX_PATTERN_LENGTH && !use_regex)
    {
        fprintf(stderr, "Error: Pattern too long (max %d).\n", MAX_PATTERN_LENGTH);
        return 2;
    }

    if (track_positions)
    {
        matches = match_result_init(16);
        if (!matches)
        {
            fprintf(stderr, "Error: Failed to allocate memory for match results.\n");
            return 2;
        }
    }

    char search_pattern[MAX_PATTERN_LENGTH];
    if (!use_regex)
    {
        if (!case_sensitive)
        {
            for (size_t i = 0; i < pattern_len; i++)
            {
                search_pattern[i] = lower_table[(unsigned char)pattern[i]];
            }
        }
        else
        {
            memcpy(search_pattern, pattern, pattern_len);
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
            return 2;
        }
        regex_compiled = true;
        algo_name = "Regex (POSIX)";
        total_match_count = regex_search(text, text_len, &compiled_regex, text_len, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
    }
    else
    {
        const size_t KMP_THRESH = 2;
        size_t report_limit = text_len;

        if (pattern_len < KMP_THRESH)
        {
            total_match_count = kmp_search(text, text_len, search_pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "KMP";
        }
        else
        {
            total_match_count = boyer_moore_search(text, text_len, search_pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "Boyer-Moore-Horspool";
        }
    }

    double end_time = get_time();
    double search_time = end_time - start_time;
    bool match_found_flag = (total_match_count > 0);

    if (count_lines_mode)
    {
        printf("%" PRIu64 "\n", line_match_count);
        if (line_match_count > 0)
            match_found_flag = true;
    }
    else if (count_only && only_matching)
    {
        printf("%" PRIu64 "\n", total_match_count);
    }
    else if (only_matching)
    {
        if (matches && matches->count > 0)
        {
            items_printed_count = print_matching_items(NULL, text, text_len, matches);
            if (items_printed_count > 0)
                match_found_flag = true;
        }
    }
    else
    {
        if (matches && matches->count > 0)
        {
            items_printed_count = print_matching_items(NULL, text, text_len, matches);
            if (items_printed_count > 0)
                match_found_flag = true;
        }

        if (show_summary)
        {
            fprintf(stderr, "\n--- Summary for String Search ---\n");
            if (match_found_flag)
                fprintf(stderr, "Lines Printed/Matches Found: Yes (Count: %zu / %" PRIu64 ")\n", items_printed_count, total_match_count);
            else
                fprintf(stderr, "Found 0 matching lines/matches\n");
            fprintf(stderr, "Search completed in %.4f seconds\n", search_time);
            fprintf(stderr, "Search details:\n");
            fprintf(stderr, "  - String length: %zu characters\n", text_len);
            fprintf(stderr, "  - Pattern length: %zu characters\n", pattern_len);
            fprintf(stderr, "  - Algorithm used: %s\n", algo_name);
            fprintf(stderr, "  - %s search\n", case_sensitive ? "Case-sensitive" : "Case-insensitive");
            fprintf(stderr, "  - %s pattern\n", use_regex ? "Regular expression" : "Literal");
            fprintf(stderr, "-------------------------------\n");
        }
    }

    if (regex_compiled)
        regfree(&compiled_regex);
    match_result_free(matches);

    if (result_code != 0)
        return 2;
    return match_found_flag ? 0 : 1;
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
    bool match_found_flag = false;
    match_result_t *matches = NULL;
    const char *algo_name = "N/A";

    bool count_lines_mode = count_only && !only_matching;
    bool track_positions = !count_lines_mode && !(count_only && only_matching);

    if (!filename || !pattern)
    {
        fprintf(stderr, "krep: Error: Filename and pattern cannot be NULL.\n");
        return 2;
    }
    if (pattern_len == 0 && !use_regex)
    {
        fprintf(stderr, "krep: Error: Pattern empty for literal search.\n");
        return 2;
    }
    if (pattern_len > MAX_PATTERN_LENGTH && !use_regex)
    {
        fprintf(stderr, "krep: Error: Pattern too long (max %d).\n", MAX_PATTERN_LENGTH);
        return 2;
    }
    if (strcmp(filename, "-") == 0)
    {
        fprintf(stderr, "krep: Error: Stdin not supported.\n");
        return 2;
    }

    fd = open(filename, O_RDONLY | O_CLOEXEC);
    if (fd == -1)
    {
        return 2;
    }
    struct stat file_stat;
    if (fstat(fd, &file_stat) == -1)
    {
        close(fd);
        return 2;
    }
    file_size = file_stat.st_size;

    if (file_size == 0 && !use_regex)
    {
        close(fd);
        if (count_only)
            printf("%s:0\n", filename);
        return 1;
    }
    if (file_size > 0 && (!use_regex && pattern_len > file_size))
    {
        close(fd);
        if (count_only)
            printf("%s:0\n", filename);
        return 1;
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
            fprintf(stderr, "krep: Error compiling regex for %s: %s\n", filename, ebuf);
            close(fd);
            return 2;
        }
        regex_compiled = true;
    }

    if (file_size > 0)
    {
        file_data = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (file_data == MAP_FAILED)
        {
            close(fd);
            if (regex_compiled)
                regfree(&compiled_regex);
            return 2;
        }
        madvise(file_data, file_size, MADV_SEQUENTIAL | MADV_WILLNEED);
    }
    else
    {
        file_data = "";
    }
    close(fd);
    fd = -1;

    int actual_thread_count = 1;
    const char *execution_mode = "Single-threaded";
    if (thread_count > 1)
    {
        fprintf(stderr, "krep: Warning: Multi-threading disabled for accuracy, using single thread (-t ignored for %s).\n", filename);
    }

    if (track_positions)
    {
        matches = match_result_init(100);
        if (!matches)
        {
            fprintf(stderr, "krep: Error: Failed to allocate memory for match results for %s.\n", filename);
            result_code = 2;
            goto cleanup;
        }
    }

    char search_pattern[MAX_PATTERN_LENGTH];
    if (!use_regex)
    {
        if (!case_sensitive)
        {
            for (size_t i = 0; i < pattern_len; i++)
            {
                search_pattern[i] = lower_table[(unsigned char)pattern[i]];
            }
        }
        else
        {
            memcpy(search_pattern, pattern, pattern_len);
        }
    }

    size_t report_limit = file_size;
    if (use_regex)
    {
        total_match_count = regex_search(file_data, file_size, &compiled_regex, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
        algo_name = "Regex (POSIX)";
    }
    else
    {
        const size_t KMP_THRESH = 2;
        if (pattern_len < KMP_THRESH)
        {
            total_match_count = kmp_search(file_data, file_size, search_pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "KMP";
        }
        else
        {
            total_match_count = boyer_moore_search(file_data, file_size, search_pattern, pattern_len, case_sensitive, report_limit, count_lines_mode, &line_match_count, &last_counted_line, track_positions, matches);
            algo_name = "Boyer-Moore-Horspool";
        }
    }

    match_found_flag = (total_match_count > 0);

    if (result_code == 0)
    {
        double end_time = get_time();
        double search_time = end_time - start_time;
        double mb_per_sec = (search_time > 1e-9 && file_size > 0) ? (file_size / (1024.0 * 1024.0)) / search_time : 0.0;

        if (count_lines_mode)
        {
            printf("%s:%" PRIu64 "\n", filename, line_match_count);
            if (line_match_count > 0)
                match_found_flag = true;
        }
        else if (count_only && only_matching)
        {
            printf("%s:%" PRIu64 "\n", filename, total_match_count);
        }
        else if (only_matching)
        {
            if (matches && matches->count > 0)
            {
                items_printed_count = print_matching_items(filename, file_data, file_size, matches);
                if (items_printed_count > 0)
                    match_found_flag = true;
            }
        }
        else
        {
            if (matches && matches->count > 0)
            {
                items_printed_count = print_matching_items(filename, file_data, file_size, matches);
                if (items_printed_count > 0)
                    match_found_flag = true;
            }

            if (show_summary)
            {
                fprintf(stderr, "\n--- Summary for %s ---\n", filename);
                if (match_found_flag)
                    fprintf(stderr, "Lines Printed/Matches Found: Yes (Count: %zu / %" PRIu64 ")\n", items_printed_count, total_match_count);
                else
                    fprintf(stderr, "Found 0 matching lines/matches\n");
                fprintf(stderr, "Search completed in %.4f seconds (%.2f MB/s)\n", search_time, mb_per_sec);
                fprintf(stderr, "Search details:\n");
                fprintf(stderr, "  - File size: %.2f MB (%zu bytes)\n", file_size / (1024.0 * 1024.0), file_size);
                fprintf(stderr, "  - Pattern length: %zu characters\n", pattern_len);
                fprintf(stderr, "  - Pattern type: %s\n", use_regex ? "Regular expression" : "Literal text");
                fprintf(stderr, "  - Execution: %s (%d thread%s)\n", execution_mode, actual_thread_count, actual_thread_count > 1 ? "s" : "");
                fprintf(stderr, "  - Algorithm used: %s\n", algo_name);
                fprintf(stderr, "  - %s search\n", case_sensitive ? "Case-sensitive" : "Case-insensitive");
                fprintf(stderr, "-------------------------\n");
            }
        }
    }

cleanup:
    if (file_data != MAP_FAILED && file_size > 0)
        munmap(file_data, file_size);
    if (regex_compiled)
        regfree(&compiled_regex);
    match_result_free(matches);
    if (fd != -1)
        close(fd);

    if (result_code != 0)
        return result_code;
    return match_found_flag ? 0 : 1;
}

// Recursive Directory Search
static const char *skip_directories[] = {
    ".", "..", ".git", "node_modules", ".svn", ".hg", "build", "dist",
    "__pycache__", ".pytest_cache", ".mypy_cache", ".venv", ".env", "venv", "env",
    "target", "bin", "obj"};
static const size_t num_skip_directories = sizeof(skip_directories) / sizeof(skip_directories[0]);

static const char *skip_extensions[] = {
    ".o", ".so", ".a", ".dll", ".exe", ".lib", ".dylib", ".class", ".pyc", ".pyo", ".obj", ".elf", ".wasm",
    ".zip", ".tar", ".gz", ".bz2", ".xz", ".rar", ".7z", ".jar", ".war", ".ear", ".iso", ".img", ".pkg", ".deb", ".rpm",
    ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".tiff", ".webp", ".svg", ".ico", ".psd", ".ai",
    ".mp3", ".wav", ".ogg", ".flac", ".aac", ".mp4", ".avi", ".mkv", ".mov", ".wmv", ".flv",
    ".pdf", ".doc", ".docx", ".xls", ".xlsx", ".ppt", ".pptx", ".odt", ".ods", ".odp",
    ".dat", ".bin", ".bak", ".log", ".min.js", ".min.css",
    ".swp", ".swo", ".db", ".sqlite", ".mdb", ".ttf", ".otf", ".woff", ".woff2", ".DS_Store"};
static const size_t num_skip_extensions = sizeof(skip_extensions) / sizeof(skip_extensions[0]);

static bool should_skip_extension(const char *filename)
{
    const char *dot = strrchr(filename, '.');
    if (!dot || dot == filename)
    {
        return false;
    }
    for (size_t i = 0; i < num_skip_extensions; ++i)
    {
        if (strcasecmp(dot, skip_extensions[i]) == 0)
        {
            return true;
        }
    }
    return false;
}

static bool is_binary_file(const char *filename)
{
    FILE *f = fopen(filename, "rb");
    if (!f)
    {
        return false;
    }
    char buffer[BINARY_CHECK_BUFFER_SIZE];
    size_t bytes_read = fread(buffer, 1, sizeof(buffer), f);
    fclose(f);

    if (bytes_read == 0)
    {
        return false;
    }

    for (size_t i = 0; i < bytes_read; ++i)
    {
        if (buffer[i] == '\0')
        {
            return true;
        }
    }
    return false;
}

int search_directory_recursive(const char *base_dir, const char *pattern, size_t pattern_len,
                               bool case_sensitive, bool count_only, int thread_count, bool use_regex,
                               bool *global_match_found)
{
    DIR *dir = opendir(base_dir);
    if (!dir)
    {
        fprintf(stderr, "krep: %s: %s\n", base_dir, strerror(errno));
        return 1;
    }

    struct dirent *entry;
    int total_errors = 0;
    char path_buffer[PATH_MAX];

    while ((entry = readdir(dir)) != NULL)
    {
        bool skip = false;
        for (size_t i = 0; i < num_skip_directories; ++i)
        {
            if (strcmp(entry->d_name, skip_directories[i]) == 0)
            {
                skip = true;
                break;
            }
        }
        if (skip)
        {
            continue;
        }

        int path_len = snprintf(path_buffer, sizeof(path_buffer), "%s/%s", base_dir, entry->d_name);
        if (path_len < 0 || (size_t)path_len >= sizeof(path_buffer))
        {
            fprintf(stderr, "krep: Error constructing path for %s/%s (path too long or encoding error)\n", base_dir, entry->d_name);
            total_errors++;
            continue;
        }

        struct stat entry_stat;
        if (lstat(path_buffer, &entry_stat) == -1)
        {
            fprintf(stderr, "krep: %s: %s\n", path_buffer, strerror(errno));
            total_errors++;
            continue;
        }

        if (S_ISDIR(entry_stat.st_mode))
        {
            total_errors += search_directory_recursive(path_buffer, pattern, pattern_len,
                                                       case_sensitive, count_only, thread_count, use_regex,
                                                       global_match_found);
        }
        else if (S_ISREG(entry_stat.st_mode))
        {
            if (should_skip_extension(entry->d_name) || is_binary_file(path_buffer))
            {
                continue;
            }

            int file_result = search_file(path_buffer, pattern, pattern_len, case_sensitive,
                                          count_only, thread_count, use_regex);
            if (file_result == 0)
            {
                *global_match_found = true;
            }
            else if (file_result > 1)
            {
                total_errors++;
            }
        }
        else if (S_ISLNK(entry_stat.st_mode))
        {
            if (stat(path_buffer, &entry_stat) == -1)
            {
                continue;
            }
            if (S_ISREG(entry_stat.st_mode))
            {
                if (should_skip_extension(entry->d_name) || is_binary_file(path_buffer))
                {
                    continue;
                }
                int file_result = search_file(path_buffer, pattern, pattern_len, case_sensitive,
                                              count_only, thread_count, use_regex);
                if (file_result == 0)
                {
                    *global_match_found = true;
                }
                else if (file_result > 1)
                {
                    total_errors++;
                }
            }
        }
    }

    closedir(dir);
    return total_errors;
}

// Main Entry Point
#if !defined(TESTING) && !defined(TEST)
__attribute__((weak)) int main(int argc, char *argv[])
{
    char *pattern_arg = NULL;
    char *target_arg = NULL;
    bool case_sensitive = true;
    bool count_only = false;
    bool string_mode = false;
    bool use_regex = false;
    bool recursive_mode = false;
    int thread_count = 0;
    int opt;
    bool pattern_provided_via_e = false;

    color_output_enabled = isatty(STDOUT_FILENO);
    show_summary = false;
    only_matching = false;

    while ((opt = getopt(argc, argv, "icodErt:shve:")) != -1)
    {
        switch (opt)
        {
        case 'i':
            case_sensitive = false;
            break;
        case 'c':
            count_only = true;
            color_output_enabled = false;
            show_summary = false;
            break;
        case 'o':
            only_matching = true;
            show_summary = false;
            break;
        case 'd':
            show_summary = true;
            break;
        case 'E':
            use_regex = true;
            break;
        case 'r':
            recursive_mode = true;
            break;
        case 't':
        {
            char *e;
            errno = 0;
            long v = strtol(optarg, &e, 10);
            if (errno || optarg == e || *e || v <= 0 || v > INT_MAX)
            {
                fprintf(stderr, "Warning: Invalid thread count '%s', using 1.\n", optarg);
                thread_count = 1;
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
        case 'e':
            if (pattern_arg != NULL)
            {
                fprintf(stderr, "Warning: Multiple -e flags not yet supported, using last one.\n");
            }
            pattern_arg = optarg;
            pattern_provided_via_e = true;
            break;
        case '?':
        default:
            print_usage(argv[0]);
            return 2;
        }
    }

    if (!pattern_provided_via_e)
    {
        if (optind >= argc)
        {
            fprintf(stderr, "Error: Missing PATTERN.\n");
            print_usage(argv[0]);
            return 2;
        }
        pattern_arg = argv[optind++];
    }

    if (pattern_arg == NULL)
    {
        fprintf(stderr, "Error: Pattern not specified.\n");
        print_usage(argv[0]);
        return 2;
    }

    if (optind >= argc)
    {
        fprintf(stderr, "Error: Missing %s.\n", string_mode ? "STRING" : (recursive_mode ? "DIRECTORY" : "FILE or DIRECTORY"));
        print_usage(argv[0]);
        return 2;
    }
    target_arg = argv[optind++];

    if (optind < argc)
    {
        fprintf(stderr, "Error: Extra arguments provided.\n");
        print_usage(argv[0]);
        return 2;
    }

    if (string_mode && recursive_mode)
    {
        fprintf(stderr, "Error: Options -s and -r cannot be used together.\n");
        print_usage(argv[0]);
        return 2;
    }
    if (count_only || only_matching)
    {
        show_summary = false;
    }

    size_t pattern_len = strlen(pattern_arg);
    if (pattern_len == 0 && !use_regex)
    {
        fprintf(stderr, "Error: Pattern cannot be empty for literal search.\n");
        return 2;
    }
    if (pattern_len > MAX_PATTERN_LENGTH && !use_regex)
    {
        fprintf(stderr, "Error: Pattern too long (%zu > %d).\n", pattern_len, MAX_PATTERN_LENGTH);
        return 2;
    }

    int exit_code = 1;
    if (string_mode)
    {
        exit_code = search_string(pattern_arg, pattern_len, target_arg, case_sensitive, use_regex, count_only);
    }
    else if (recursive_mode)
    {
        struct stat target_stat;
        if (stat(target_arg, &target_stat) == -1)
        {
            fprintf(stderr, "krep: %s: %s\n", target_arg, strerror(errno));
            return 2;
        }
        if (!S_ISDIR(target_stat.st_mode))
        {
            fprintf(stderr, "krep: %s: Is not a directory (required for -r)\n", target_arg);
            return 2;
        }

        bool global_match_found = false;
        int errors = search_directory_recursive(target_arg, pattern_arg, pattern_len, case_sensitive, count_only, thread_count, use_regex, &global_match_found);

        if (errors > 0)
        {
            exit_code = 2;
        }
        else
        {
            exit_code = global_match_found ? 0 : 1;
        }
    }
    else
    {
        struct stat target_stat;
        if (stat(target_arg, &target_stat) == 0 && S_ISDIR(target_stat.st_mode))
        {
            fprintf(stderr, "krep: %s: Is a directory\n", target_arg);
            return 2;
        }
        exit_code = search_file(target_arg, pattern_arg, pattern_len, case_sensitive, count_only, thread_count, use_regex);
    }

    return exit_code;
}
#endif // !defined(TESTING) && !defined(TEST)