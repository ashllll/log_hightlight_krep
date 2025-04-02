/* krep - A high-performance string search utility
 *
 * Author: Davide Santangelo
 * Version: 0.3.2
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
#define CHUNK_SIZE (16 * 1024 * 1024)               // 16MB base chunk size (adjust based on L3 cache?)
#define VERSION "0.3.2"                             // Updated version

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
        uint64_t new_capacity = result->capacity * 2;
        // Check for potential overflow before allocation
        if (new_capacity < result->capacity || new_capacity > SIZE_MAX / sizeof(match_position_t))
        {
            fprintf(stderr, "Error: Match result capacity overflow.\n");
            return false; // Avoid allocation if new capacity calculation overflows
        }
        match_position_t *new_positions = realloc(result->positions,
                                                  new_capacity * sizeof(match_position_t));
        if (!new_positions)
            return false; // Reallocation failed

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
// (find_line_start, find_line_end, print_matching_lines remain the same)
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

void print_matching_lines(const char *text, size_t text_len, const match_result_t *result)
{
    if (!result || !text || result->count == 0)
        return;

    for (uint64_t i = 0; i < result->count; i++)
    {
        // Ensure match position is valid before proceeding
        if (result->positions[i].start_offset >= text_len)
            continue;

        size_t match_pos = result->positions[i].start_offset;
        size_t line_start = find_line_start(text, match_pos);
        // Ensure line_end doesn't go past text_len, find_line_end handles this
        size_t line_end = find_line_end(text, text_len, match_pos);

        // Print the matching line safely
        printf("%.*s\n", (int)(line_end - line_start), text + line_start);
    }
}

/* --- Forward declarations --- */
void print_usage(const char *program_name);
double get_time(void);
void prepare_bad_char_table(const char *pattern, size_t pattern_len,
                            int *bad_char_table, bool case_sensitive);
void *search_thread(void *arg);
uint64_t regex_search(const char *text, size_t text_len,
                      const regex_t *compiled_regex, // Takes compiled regex
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
    printf("krep v%s - A high-performance string search utility\n\n", VERSION);
    printf("Usage: %s [OPTIONS] PATTERN FILE\n", program_name);
    printf("   or: %s [OPTIONS] -s PATTERN STRING_TO_SEARCH\n\n", program_name);
    printf("OPTIONS:\n");
    printf("  -i            Perform case-insensitive matching.\n");
    printf("  -c            Only print a count of matches found.\n");
    printf("  -r            Treat PATTERN as a regular expression.\n");
    printf("  -t NUM        Use NUM threads for file search (default: %d, auto-adjusts).\n", DEFAULT_THREAD_COUNT);
    printf("  -s            Search in STRING_TO_SEARCH instead of a FILE.\n");
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
// (prepare_bad_char_table, boyer_moore_search remain the same)
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

uint64_t boyer_moore_search(const char *text, size_t text_len,
                            const char *pattern, size_t pattern_len,
                            bool case_sensitive, size_t report_limit_offset)
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
    // The loop condition ensures we don't read past the end of text when checking the pattern
    size_t limit = text_len - pattern_len;

    while (i <= limit)
    {
        size_t j = pattern_len - 1;     // Index for pattern (from right to left)
        size_t k = i + pattern_len - 1; // Index for text corresponding to pattern[j]

        // Compare pattern from right to left
        while (true) // Loop until mismatch or full match
        {
            char text_char = text[k];
            char pattern_char = pattern[j];

            // Handle case-insensitivity using the lookup table
            if (!case_sensitive)
            {
                text_char = lower_table[(unsigned char)text_char];
                pattern_char = lower_table[(unsigned char)pattern_char];
            }

            // Mismatch found
            if (text_char != pattern_char)
            {
                // Calculate shift based on the mismatched character in the text
                unsigned char bad_char_from_text = (unsigned char)text[i + pattern_len - 1];
                int shift = case_sensitive ? bad_char_table[bad_char_from_text]
                                           : bad_char_table[lower_table[bad_char_from_text]];

                // Ensure minimum shift of 1 to avoid infinite loops
                i += (shift > 0 ? shift : 1);
                break; // Exit inner comparison loop, continue outer loop with new 'i'
            }

            // Characters match, check if we reached the beginning of the pattern
            if (j == 0)
            {
                // Full match found!
                if (i < report_limit_offset) // Only count if within reporting limit
                {
                    match_count++;
                    // Add position tracking here if needed
                }
                // Shift by 1 to find overlapping matches (or apply a better shift if known)
                // A simple shift by 1 is safe but potentially slow for repetitive patterns.
                // A more advanced BM might use a good-suffix rule here.
                i++;
                break; // Exit inner comparison loop, continue outer loop
            }

            // Continue comparing characters to the left
            j--;
            k--;
        }
    }
    return match_count;
}

/* --- Knuth-Morris-Pratt (KMP) Algorithm --- */
// (compute_lps_array, kmp_search remain the same)
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
            // This is tricky. Consider the example.
            // AAACAAAA and i = 7. The idea is similar
            // to search step of KMP
            if (length != 0)
            {
                length = lps[length - 1];
                // Also, note that we do not increment i here
            }
            else
            {
                lps[i] = 0;
                i++;
            }
        }
    }
}

uint64_t kmp_search(const char *text, size_t text_len,
                    const char *pattern, size_t pattern_len,
                    bool case_sensitive, size_t report_limit_offset)
{
    uint64_t match_count = 0;

    // Basic checks
    if (pattern_len == 0 || text_len < pattern_len || report_limit_offset == 0)
    {
        return 0;
    }

    // Optimization for single character pattern
    if (pattern_len == 1)
    {
        char p_char = pattern[0];
        char p_lower = lower_table[(unsigned char)p_char]; // Pre-calculate lower case pattern char

        for (size_t i = 0; i < text_len && i < report_limit_offset; i++)
        {
            char text_char = text[i];
            if (case_sensitive)
            {
                if (text_char == p_char)
                {
                    match_count++;
                    // Add position tracking if needed: match_result_add(result, i, i + 1);
                }
            }
            else
            {
                if (lower_table[(unsigned char)text_char] == p_lower)
                {
                    match_count++;
                    // Add position tracking if needed: match_result_add(result, i, i + 1);
                }
            }
        }
        return match_count;
    }

    // Preprocess the pattern (calculate lps[] array)
    int *lps = malloc(pattern_len * sizeof(int));
    if (!lps)
    {
        perror("malloc failed for LPS array");
        return 0; // Indicate error or handle differently
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
            // Match found at index i - j
            size_t match_start_index = i - j;
            if (match_start_index < report_limit_offset)
            { // Check against limit
                match_count++;
                // Add position tracking if needed: match_result_add(result, match_start_index, i);
            }
            // Move j based on LPS array to continue search for next match
            j = lps[j - 1];
        }
        // Mismatch after j matches
        else if (i < text_len && pattern_char != text_char)
        {
            // Do not match lps[0..lps[j-1]] characters,
            // they will match anyway
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

    free(lps); // Free memory allocated for LPS array
    return match_count;
}

/* --- Rabin-Karp Algorithm --- */
// (rabin_karp_search remains the same)
uint64_t rabin_karp_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset)
{
    uint64_t match_count = 0;

    // Basic checks
    if (pattern_len == 0 || text_len < pattern_len || report_limit_offset == 0)
    {
        return 0;
    }

    // Use KMP for very short patterns where RK overhead isn't worth it
    // Adjust threshold based on benchmarking if necessary
    if (pattern_len <= 4)
    {
        return kmp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
    }

    // Prime number for modulo operation (choose a large prime)
    const uint64_t prime = 1000000007ULL; // Example prime
    // Number of possible characters (e.g., 256 for ASCII)
    const uint64_t base = 256ULL;

    uint64_t pattern_hash = 0; // hash value for pattern
    uint64_t text_hash = 0;    // hash value for current text window
    uint64_t h = 1;            // base^(pattern_len-1) % prime

    // Calculate h = base^(pattern_len-1) % prime
    for (size_t i = 0; i < pattern_len - 1; i++)
    {
        h = (h * base) % prime;
    }

    // Calculate the hash value of pattern and first window of text
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

    // Slide the pattern over text one by one
    size_t limit = text_len - pattern_len;
    for (size_t i = 0; i <= limit; i++)
    {
        // Check the hash values of current window of text and pattern.
        // If the hash values match then only check for characters one by one
        if (pattern_hash == text_hash)
        {
            /* Check for characters one by one (handle hash collisions) */
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

            // If pattern[0...pattern_len-1] = text[i, i+1, ...i+pattern_len-1]
            if (match)
            {
                if (i < report_limit_offset)
                { // Check limit
                    match_count++;
                    // Add position tracking if needed: match_result_add(result, i, i + pattern_len);
                }
            }
        }

        // Calculate hash value for next window of text: Remove leading digit,
        // add trailing digit
        if (i < limit)
        {
            char leading_char = text[i];
            char trailing_char = text[i + pattern_len];

            if (!case_sensitive)
            {
                leading_char = lower_table[(unsigned char)leading_char];
                trailing_char = lower_table[(unsigned char)trailing_char];
            }

            // Calculate rolling hash: text_hash = (base * (text_hash - text[i]*h) + text[i+pattern_len]) % prime;
            // Need to handle potential negative result after subtraction before modulo
            text_hash = (base * (text_hash + prime - (h * leading_char) % prime)) % prime;
            text_hash = (text_hash + trailing_char) % prime;
        }
    }
    return match_count;
}

/* --- SIMD Search Implementations --- */
// (simd_sse42_search, simd_avx2_search, neon_search remain the same,
// including the fallbacks/placeholders)
#ifdef __SSE4_2__
uint64_t simd_sse42_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset)
{
    // fprintf(stderr, "Warning: SSE4.2 search temporarily disabled, falling back to Boyer-Moore.\n");
    // Placeholder: Actual SSE4.2 implementation using _mm_cmpestri etc. would go here.
    // It's complex and requires careful handling of modes, lengths, and case sensitivity.
    return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
}
#endif // __SSE4_2__

#ifdef __AVX2__
uint64_t simd_avx2_search(const char *text, size_t text_len,
                          const char *pattern, size_t pattern_len,
                          bool case_sensitive, size_t report_limit_offset)
{
    // fprintf(stderr, "Warning: AVX2 search not implemented, falling back.\n");
    // Placeholder: Actual AVX2 implementation using _mm256_ intrinsics would go here.
#ifdef __SSE4_2__
    // Fallback strategy: Use SSE4.2 if pattern fits, otherwise BMH
    if (pattern_len <= SIMD_MAX_LEN_SSE42)
    {
        // Note: This recursive call might not be ideal, directly call BMH if SSE4.2 is also disabled/placeholder
        return simd_sse42_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
    }
    else
    {
        return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
    }
#else
    // Fallback directly to BMH if SSE4.2 is not available
    return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
#endif
}
#endif // __AVX2__

#ifdef __ARM_NEON
uint64_t neon_search(const char *text, size_t text_len,
                     const char *pattern, size_t pattern_len,
                     bool case_sensitive, size_t report_limit_offset)
{
    // fprintf(stderr, "Warning: NEON search not implemented, falling back to Boyer-Moore.\n");
    // Placeholder: Actual NEON implementation would go here.
    return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
}
#endif // __ARM_NEON

/**
 * Regex-based search implementation using a pre-compiled POSIX regex.
 * Takes a pointer to a compiled regex_t object.
 * FIX: Corrected advancement logic to handle subsequent matches properly.
 */
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

/**
 * Thread worker function for parallel search.
 */
void *search_thread(void *arg)
{
    search_job_t *job = (search_job_t *)arg;
    job->local_count = 0; // Initialize local count

    // Determine the actual buffer this thread needs to read (includes overlap)
    // Overlap is crucial for finding matches that cross chunk boundaries.
    // For fixed patterns, overlap needs to be pattern_len - 1.
    // For regex, determining the maximum possible match length is complex,
    // so we currently don't add overlap for regex. This means regex matches
    // crossing boundaries might be missed in multi-threaded mode.
    // A more robust solution would involve more complex boundary handling or
    // potentially processing boundary regions separately.
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
    // Matches starting at or after end_pos (the original chunk boundary) should not be counted by this thread.
    size_t report_limit_offset = (job->end_pos > buffer_abs_start) ? job->end_pos - buffer_abs_start : 0;

    // Basic checks before searching
    if (buffer_len == 0 || report_limit_offset == 0)
    {
        return NULL; // Nothing to search or report in this chunk
    }
    // Check pattern length only if not using regex and buffer is smaller than pattern
    if (!job->use_regex && buffer_len < job->pattern_len)
    {
        return NULL; // Buffer too small to contain the pattern
    }

    // --- Select and Execute Search Algorithm ---
    if (job->use_regex)
    {
        // Use regex search with the pre-compiled regex object
        // Note: Position tracking is disabled in threaded mode for now.
        job->local_count = regex_search(buffer_ptr, buffer_len,
                                        job->regex, // Pass compiled regex
                                        report_limit_offset,
                                        false, // No position tracking in threads yet
                                        NULL);
    }
    else
    {
        // --- Dynamic algorithm selection for non-regex searches ---
        const size_t KMP_THRESH = 3;
        const size_t RK_THRESH = 32;

        // Choose algorithm based on pattern length and availability
        if (job->pattern_len < KMP_THRESH)
        {
            job->local_count = kmp_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset);
        }
#ifdef __AVX2__
        else if (job->pattern_len <= SIMD_MAX_LEN_AVX2)
        {
            job->local_count = simd_avx2_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset);
        }
#elif defined(__SSE4_2__) // Use elif to avoid using SSE if AVX2 is available and used
        else if (job->pattern_len <= SIMD_MAX_LEN_SSE42)
        {
            job->local_count = simd_sse42_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset);
        }
#elif defined(__ARM_NEON) // Placeholder for NEON
        // else if (job->pattern_len <= NEON_MAX_LEN) {
        //      job->local_count = neon_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset);
        // }
#endif
        // Use Rabin-Karp for potentially longer patterns where its overhead might pay off
        else if (job->pattern_len > RK_THRESH)
        {
            job->local_count = rabin_karp_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset);
        }
        // Default to Boyer-Moore-Horspool for intermediate lengths
        else
        {
            job->local_count = boyer_moore_search(buffer_ptr, buffer_len, job->pattern, job->pattern_len, job->case_sensitive, report_limit_offset);
        }
    }

    return NULL; // Thread finished
}

/* --- Public API Implementations --- */

/**
 * Search within a string (single-threaded).
 */
int search_string(const char *pattern, size_t pattern_len, const char *text, bool case_sensitive, bool use_regex, bool count_only)
{
    double start_time = get_time();
    size_t text_len = strlen(text); // Calculate length once
    uint64_t match_count = 0;
    regex_t compiled_regex; // For regex mode
    bool regex_compiled = false;
    const char *algo_name = "Unknown";
    match_result_t *matches = NULL; // For storing match positions if needed

    // --- Basic Input Validation ---
    if (!pattern || pattern_len == 0)
    {
        fprintf(stderr, "Error: Empty or NULL pattern provided.\n");
        return 1;
    }
    if (!text)
    {
        fprintf(stderr, "Error: NULL text provided.\n");
        return 1;
    }
    // If text is empty, no matches possible unless pattern is also empty (which we disallowed)
    if (text_len == 0)
    {
        if (!count_only)
            printf("String is empty. Found 0 matches.\n");
        else
            printf("0\n"); // Print count directly in count_only mode
        return 0;
    }
    // If pattern is longer than text (and not regex), no matches possible
    if (!use_regex && pattern_len > text_len)
    {
        if (!count_only)
            printf("Pattern is longer than string. Found 0 matches.\n");
        else
            printf("0\n");
        return 0;
    }

    // --- Compile Regex (if needed) ---
    if (use_regex)
    {
        int regex_flags = REG_EXTENDED | REG_NOSUB; // REG_NOSUB if only counting
        if (!case_sensitive)
        {
            regex_flags |= REG_ICASE;
        }
        // Only request subexpression info if we need to print lines/positions
        if (!count_only)
        {
            regex_flags &= ~REG_NOSUB; // Remove REG_NOSUB if we need match positions
        }

        int ret = regcomp(&compiled_regex, pattern, regex_flags);
        if (ret != 0)
        {
            char error_buffer[100];
            regerror(ret, &compiled_regex, error_buffer, sizeof(error_buffer));
            fprintf(stderr, "Regex compilation failed: %s\n", error_buffer);
            // No need to regfree if regcomp failed before fully initializing
            return 1;
        }
        regex_compiled = true; // Mark as successfully compiled
        algo_name = "Regex (POSIX)";

        // Perform search, track positions if needed
        if (!count_only)
        {
            matches = match_result_init(100); // Initial capacity
            if (!matches)
            {
                fprintf(stderr, "Error: Failed to allocate memory for match results.\n");
                regfree(&compiled_regex);
                return 1;
            }
        }
        // Use text_len as the report limit (count all matches in the string)
        match_count = regex_search(text, text_len, &compiled_regex, text_len, !count_only, matches);
    }
    else // Not using regex
    {
        // --- Select and Execute Non-Regex Algorithm ---
        const size_t KMP_THRESH = 3;
        const size_t RK_THRESH = 32;
        size_t report_limit = text_len; // Count all matches

        // Placeholder: Position tracking for non-regex needs modifications
        // to each search function or separate tracking logic.
        if (!count_only)
        {
            fprintf(stderr, "Warning: Line/position printing for non-regex string search not yet implemented.\n");
            // matches = match_result_init(100); // Allocate if implementing
        }

        if (pattern_len < KMP_THRESH)
        {
            match_count = kmp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit);
            algo_name = "KMP";
        }
#ifdef __AVX2__
        else if (pattern_len <= SIMD_MAX_LEN_AVX2)
        {
            match_count = simd_avx2_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit);
            algo_name = "AVX2 (Fallback)"; // Indicate if SIMD is placeholder
        }
#elif defined(__SSE4_2__)
        else if (pattern_len <= SIMD_MAX_LEN_SSE42)
        {
            match_count = simd_sse42_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit);
            algo_name = "SSE4.2 (Fallback)"; // Indicate if SIMD is placeholder
        }
#elif defined(__ARM_NEON)
        // else if (pattern_len <= NEON_MAX_LEN) { match_count = neon_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit); algo_name = "NEON (Fallback)"; }
#endif
        else if (pattern_len > RK_THRESH)
        {
            match_count = rabin_karp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit);
            algo_name = "Rabin-Karp";
        }
        else
        {
            match_count = boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit);
            algo_name = "Boyer-Moore-Horspool";
        }
    }

    // --- Report Results ---
    double end_time = get_time();
    double search_time = end_time - start_time;

    if (count_only)
    {
        printf("%" PRIu64 "\n", match_count); // Print only the count
    }
    else
    {
        // Print matching lines/positions if tracked
        if (matches)
        {
            print_matching_lines(text, text_len, matches);
        }
        // Print summary details
        // Only print count summary if lines were not printed or if count is zero
        if (!matches || match_count == 0)
        {
            printf("Found %" PRIu64 " matches\n", match_count);
        }
        else
        {
            printf("--- Total Matches: %" PRIu64 " ---\n", match_count);
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
    {
        regfree(&compiled_regex);
    }
    if (matches)
    {
        match_result_free(matches);
    }

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
    regex_t compiled_regex; // Compile once here if using regex
    bool regex_compiled = false;
    int result_code = 0;               // 0 for success, 1 for error
    match_result_t *matches = NULL;    // For storing match positions
    const char *algo_name = "Unknown"; // Declare here for use in final reporting

    // --- Input Validation ---
    if (!filename)
    {
        fprintf(stderr, "Error: NULL filename provided.\n");
        return 1;
    }
    if (!pattern || pattern_len == 0)
    {
        fprintf(stderr, "Error: Empty or NULL pattern\n");
        return 1;
    }
    // Max pattern length check only relevant for non-regex fixed matching
    if (!use_regex && pattern_len > MAX_PATTERN_LENGTH)
    {
        fprintf(stderr, "Error: Pattern exceeds maximum length (%d) for non-regex search\n", MAX_PATTERN_LENGTH);
        return 1;
    }

    // --- File Opening and Stat ---
    // Handle stdin separately if needed
    if (strcmp(filename, "-") == 0)
    {
        fprintf(stderr, "Error: Searching from stdin ('-') is not yet implemented.\n");
        return 1;
    }

    fd = open(filename, O_RDONLY | O_CLOEXEC);
    if (fd == -1)
    {
        fprintf(stderr, "Error opening file '%s': %s\n", filename, strerror(errno));
        return 1;
    }
    struct stat file_stat;
    if (fstat(fd, &file_stat) == -1)
    {
        fprintf(stderr, "Error getting file stats for '%s': %s\n", filename, strerror(errno));
        close(fd);
        return 1;
    }
    file_size = file_stat.st_size;

    // Handle empty file or pattern longer than file
    if (file_size == 0)
    {
        if (!count_only)
            printf("File '%s' is empty. Found 0 matches.\n", filename);
        else
            printf("0\n");
        close(fd);
        return 0;
    }
    if (!use_regex && pattern_len > file_size)
    {
        if (!count_only)
            printf("Pattern is longer than file '%s'. Found 0 matches.\n", filename);
        else
            printf("0\n");
        close(fd);
        return 0;
    }

    // --- Compile Regex (if needed) ---
    if (use_regex)
    {
        int regex_flags = REG_EXTENDED;
        if (!case_sensitive)
        {
            regex_flags |= REG_ICASE;
        }
        // Only compile with subexpression info if we need positions
        if (count_only)
        {
            regex_flags |= REG_NOSUB;
        }

        int ret = regcomp(&compiled_regex, pattern, regex_flags);
        if (ret != 0)
        {
            char error_buffer[100];
            regerror(ret, &compiled_regex, error_buffer, sizeof(error_buffer));
            fprintf(stderr, "Regex compilation failed for pattern in '%s': %s\n", filename, error_buffer);
            close(fd);
            // No regfree needed if regcomp failed early
            return 1;
        }
        regex_compiled = true;
    }

    // --- Memory Mapping ---
    // MAP_POPULATE removed based on performance analysis recommendation.
    // Relying on MADV_SEQUENTIAL for kernel read-ahead.
    int mmap_flags = MAP_PRIVATE; // Use MAP_PRIVATE for read-only safety
    file_data = mmap(NULL, file_size, PROT_READ, mmap_flags, fd, 0);
    if (file_data == MAP_FAILED)
    {
        fprintf(stderr, "Error memory-mapping file '%s' (size %zu): %s\n", filename, file_size, strerror(errno));
        close(fd);
        if (regex_compiled)
            regfree(&compiled_regex);
        return 1;
    }
    // Advise the kernel about access pattern *after* successful mapping
    if (madvise(file_data, file_size, MADV_SEQUENTIAL) != 0)
    {
        // This is advisory, so only print a warning, don't fail
        perror("Warning: madvise(MADV_SEQUENTIAL) failed");
    }
    // Close the file descriptor immediately after mmap, it's no longer needed
    close(fd);
    fd = -1; // Mark fd as closed

    // --- Determine Thread Count ---
    int cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_cores <= 0)
        cpu_cores = 1; // Fallback if sysconf fails
    // Start with user request or default
    int actual_thread_count = (thread_count <= 0) ? DEFAULT_THREAD_COUNT : thread_count;
    // Cap threads to something reasonable relative to cores
    int max_reasonable_threads = cpu_cores * 2; // Heuristic, adjust as needed
    if (actual_thread_count > max_reasonable_threads)
    {
        // Optional: Print warning if limiting threads
        // fprintf(stderr, "Warning: Requested thread count %d exceeds reasonable limit (%d), using %d threads.\n",
        //         thread_count, max_reasonable_threads, max_reasonable_threads);
        actual_thread_count = max_reasonable_threads;
    }
    // Further limit threads based on file size vs pattern length for non-regex
    // to avoid threads having too little work or less work than the pattern length.
    if (!use_regex && pattern_len > 0)
    {
        size_t max_useful_threads = file_size / pattern_len; // Each thread needs at least pattern_len bytes
        if (max_useful_threads == 0)
            max_useful_threads = 1; // Need at least one thread
        if ((size_t)actual_thread_count > max_useful_threads)
        {
            actual_thread_count = (int)max_useful_threads;
        }
    }
    // Ensure at least one thread
    if (actual_thread_count < 1)
        actual_thread_count = 1;

    // --- Decide Threading Strategy ---
    // Use multiple threads only if file is large enough and more than 1 thread is requested/sensible
    size_t dynamic_threshold = MIN_FILE_SIZE_FOR_THREADS * actual_thread_count;
    bool use_multithreading = (file_size >= dynamic_threshold) && (actual_thread_count > 1);
    const char *execution_mode = use_multithreading ? "Multi-threaded" : "Single-threaded";

    if (!use_multithreading)
    {
        // --- Single-threaded Search ---
        actual_thread_count = 1;         // Ensure count reflects reality
        size_t report_limit = file_size; // Report all matches in the file
        // algo_name declared earlier

        // Allocate match results if needed
        if (!count_only)
        {
            matches = match_result_init(100); // Initial capacity
            if (!matches)
            {
                fprintf(stderr, "Error: Failed to allocate memory for match results.\n");
                result_code = 1;
                goto cleanup; // Use goto for centralized cleanup
            }
        }

        if (use_regex)
        {
            total_match_count = regex_search(file_data, file_size, &compiled_regex, report_limit, !count_only, matches); // Use compiled regex
            algo_name = "Regex (POSIX)";
        }
        else
        {
            // Select non-regex algorithm (same logic as search_string/search_thread)
            const size_t KMP_THRESH = 3;
            const size_t RK_THRESH = 32;
            if (pattern_len < KMP_THRESH)
            {
                total_match_count = kmp_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit);
                algo_name = "KMP";
            }
#ifdef __AVX2__
            else if (pattern_len <= SIMD_MAX_LEN_AVX2)
            {
                total_match_count = simd_avx2_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit);
                algo_name = "AVX2 (Fallback)";
            }
#elif defined(__SSE4_2__)
            else if (pattern_len <= SIMD_MAX_LEN_SSE42)
            {
                total_match_count = simd_sse42_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit);
                algo_name = "SSE4.2 (Fallback)";
            }
#elif defined(__ARM_NEON)
            // else if (pattern_len <= NEON_MAX_LEN) { total_match_count = neon_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit); algo_name = "NEON (Fallback)"; }
#endif
            else if (pattern_len > RK_THRESH)
            {
                total_match_count = rabin_karp_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit);
                algo_name = "Rabin-Karp";
            }
            else
            {
                total_match_count = boyer_moore_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit);
                algo_name = "Boyer-Moore-Horspool";
            }
            // Position tracking needs to be added to the specific search functions if !count_only
            if (!count_only)
            {
                fprintf(stderr, "Warning: Line printing for non-regex file search not yet implemented.\n");
                // Call modified search function here: e.g., boyer_moore_search_track(...)
            }
        }
        // No need to print algo_name here, handled in final reporting
    }
    else // Use Multi-threading
    {
        // --- Multi-threaded Search ---
        pthread_t *threads = malloc(actual_thread_count * sizeof(pthread_t));
        search_job_t *jobs = malloc(actual_thread_count * sizeof(search_job_t));
        bool *thread_valid = calloc(actual_thread_count, sizeof(bool)); // Track created threads
        bool memory_error = (!threads || !jobs || !thread_valid);

        if (memory_error)
        {
            perror("Error allocating memory for threads/jobs/flags");
            result_code = 1;
            // Free any partially allocated memory before cleanup
            free(threads);
            free(jobs);
            free(thread_valid);
            goto cleanup;
        }

        // Divide work among threads
        size_t chunk_size = file_size / actual_thread_count;
        size_t current_pos = 0;
        int created_threads = 0; // Track successfully created threads

        for (int i = 0; i < actual_thread_count; i++)
        {
            jobs[i].file_data = file_data;
            jobs[i].file_size = file_size;
            jobs[i].start_pos = current_pos;
            // Last thread gets the remainder to avoid rounding issues
            jobs[i].end_pos = (i == actual_thread_count - 1) ? file_size : current_pos + chunk_size;

            // Ensure end_pos doesn't go backward or exceed file size (shouldn't happen with above logic)
            if (jobs[i].end_pos < current_pos)
                jobs[i].end_pos = current_pos;
            if (jobs[i].end_pos > file_size)
                jobs[i].end_pos = file_size;

            current_pos = jobs[i].end_pos; // Prepare start for next thread

            jobs[i].pattern = pattern;
            jobs[i].pattern_len = pattern_len;
            jobs[i].case_sensitive = case_sensitive;
            jobs[i].use_regex = use_regex;
            jobs[i].regex = use_regex ? &compiled_regex : NULL; // Pass pointer to compiled regex
            jobs[i].thread_id = i;
            jobs[i].total_threads = actual_thread_count;
            jobs[i].local_count = 0; // Initialize local count

            // Only create thread if there's a valid chunk to process
            if (jobs[i].start_pos < jobs[i].end_pos)
            {
                if (pthread_create(&threads[i], NULL, search_thread, &jobs[i]) != 0)
                {
                    perror("Error creating search thread");
                    // Attempt cleanup of already created threads before failing
                    for (int j = 0; j < created_threads; j++) // Join only successfully created threads
                    {
                        pthread_join(threads[j], NULL);           // Wait for them to finish
                        total_match_count += jobs[j].local_count; // Aggregate results from finished threads
                    }
                    result_code = 1; // Signal error
                    // Free allocated memory before cleanup
                    free(threads);
                    free(jobs);
                    free(thread_valid);
                    goto cleanup;
                }
                thread_valid[i] = true;
                created_threads++;
            }
            else
            {
                thread_valid[i] = false; // Mark as invalid if chunk size is zero
            }
        } // End thread creation loop

        // Wait for threads and aggregate results
        for (int i = 0; i < actual_thread_count; i++)
        {
            if (thread_valid[i]) // Only join threads that were successfully created
            {
                pthread_join(threads[i], NULL);
                total_match_count += jobs[i].local_count;
            }
        }

        // Position tracking/line printing in multi-threaded mode is complex
        // Requires collecting results centrally or re-scanning boundary regions.
        if (!count_only)
        {
            fprintf(stderr, "Warning: Line printing for multi-threaded searches not yet implemented.\n");
        }

        // Cleanup thread/job memory (moved after join)
        free(threads);
        free(jobs);
        free(thread_valid);

        // Algorithm name is "Unknown" for multi-threaded as different threads
        // might use different fallbacks or SIMD levels. A more complex
        // implementation could track this.
        algo_name = "Multiple (Dynamic)";

    } // End multi-threaded block

    // --- Final Reporting (only if no errors occurred before this point) ---
    if (result_code == 0)
    {
        // Print matching lines if not count_only and positions were tracked
        if (!count_only && matches)
        {
            print_matching_lines(file_data, file_size, matches);
        }

        double end_time = get_time();
        double search_time = end_time - start_time;
        double mb_per_sec = (search_time > 1e-9 && file_size > 0) ? (file_size / (1024.0 * 1024.0)) / search_time : 0.0;

        // Print count if in count_only mode OR if line printing wasn't done/possible
        if (count_only || !matches) // Print count if count_only or if matches weren't tracked/printed
        {
            // If count_only, just print the number. Otherwise, print context.
            if (count_only)
            {
                printf("%" PRIu64 "\n", total_match_count);
            }
            else
            {
                // Only print this summary line if lines weren't printed
                if (!matches || total_match_count == 0) // Also print if no matches were found
                {
                    printf("Found %" PRIu64 " matches in '%s'\n", total_match_count, filename);
                }
                else
                {
                    // If lines were printed, maybe just print the count summary at the end
                    printf("--- Total Matches: %" PRIu64 " ---\n", total_match_count);
                }
            }
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
            printf("  - Algorithm used: %s\n", algo_name); // Print determined algorithm name
#ifdef __AVX2__
            printf("  - SIMD Available: AVX2 (Using Fallback)\n"); // Update if implemented
#elif defined(__SSE4_2__)
            printf("  - SIMD Available: SSE4.2 (Using Fallback)\n"); // Update if implemented
#elif defined(__ARM_NEON)
            printf("  - SIMD Available: NEON (Using Fallback)\n"); // Update if implemented
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
        if (munmap(file_data, file_size) != 0)
        {
            perror("Warning: munmap failed");
            // Don't overwrite previous error code if munmap fails
            if (result_code == 0)
                result_code = 1;
        }
    }
    if (regex_compiled)
    {
        regfree(&compiled_regex);
    }
    if (matches)
    {
        match_result_free(matches);
    }

    // Close fd if it somehow remained open (should only happen on early error paths)
    if (fd != -1)
        close(fd);

    return result_code; // Return success (0) or error (1)
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

    // Basic check for minimum arguments
    if (argc < 3)
    {
        print_usage(argv[0]);
        return 1;
    }

    // Parse options using getopt
    // Note: Keep option string updated with all valid options
    while ((opt = getopt(argc, argv, "icrvt:shv")) != -1) // Added 'v' for version
    {
        switch (opt)
        {
        case 'i':
            case_sensitive = false;
            break;
        case 'c':
            count_only = true;
            break;
        case 'r':
            use_regex = true;
            break;
        case 't':
        {
            char *endptr;
            errno = 0; // Reset errno before call
            long val = strtol(optarg, &endptr, 10);
            // Check for errors: empty string, no digits, out of range, trailing chars
            if (errno != 0 || optarg == endptr || *endptr != '\0' || val <= 0 || val > INT_MAX)
            {
                fprintf(stderr, "Warning: Invalid thread count '%s', using default.\n", optarg);
                thread_count = 0; // Use default
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
        case 'v': // Version option
            printf("krep v%s\n", VERSION);
            return 0;
        case 'h': // Help option
            print_usage(argv[0]);
            return 0;
        case '?': // Unknown option or missing argument for options like -t
                  // getopt already prints an error message
            print_usage(argv[0]);
            return 1;
        default:
            // Should not happen with getopt
            fprintf(stderr, "Internal error parsing options.\n");
            abort(); // Use abort for unexpected internal errors
        }
    }

    // Check for required positional arguments after options
    if (optind >= argc)
    {
        fprintf(stderr, "Error: Missing PATTERN.\n");
        print_usage(argv[0]);
        return 1;
    }
    pattern_arg = argv[optind++]; // Get pattern

    if (optind >= argc)
    {
        fprintf(stderr, "Error: Missing %s.\n", string_mode ? "STRING_TO_SEARCH" : "FILE");
        print_usage(argv[0]);
        return 1;
    }
    file_or_string_arg = argv[optind]; // Get file or string

    // Check if there are extra arguments
    if (optind + 1 < argc)
    {
        fprintf(stderr, "Error: Unexpected extra arguments starting with '%s'.\n", argv[optind + 1]);
        print_usage(argv[0]);
        return 1;
    }

    // Validate pattern length (already checked for NULL/empty in functions)
    size_t pattern_len = strlen(pattern_arg);
    // No need for MAX_PATTERN_LENGTH check here, handled internally

    // Execute search based on mode
    int result = 1; // Default to error
    if (string_mode)
    {
        result = search_string(pattern_arg, pattern_len, file_or_string_arg, case_sensitive, use_regex, count_only);
    }
    else
    {
        // search_file handles the "-" case internally now
        result = search_file(file_or_string_arg, pattern_arg, pattern_len, case_sensitive, count_only, thread_count, use_regex);
    }

    // Exit with the result code from the search function
    return result;
}
#endif // TESTING

/* --- TESTING COMPATIBILITY WRAPPERS --- */
#ifdef TESTING
// (Compatibility wrappers remain the same, ensuring they call the correct
// underlying functions with the SIZE_MAX report limit)

uint64_t boyer_moore_search_compat(const char *text, size_t text_len, const char *pattern, size_t pattern_len, bool case_sensitive)
{
    return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, SIZE_MAX);
}

uint64_t kmp_search_compat(const char *text, size_t text_len, const char *pattern, size_t pattern_len, bool case_sensitive)
{
    return kmp_search(text, text_len, pattern, pattern_len, case_sensitive, SIZE_MAX);
}

uint64_t rabin_karp_search_compat(const char *text, size_t text_len, const char *pattern, size_t pattern_len, bool case_sensitive)
{
    return rabin_karp_search(text, text_len, pattern, pattern_len, case_sensitive, SIZE_MAX);
}
#ifdef __SSE4_2__
uint64_t simd_sse42_search_compat(const char *text, size_t text_len, const char *pattern, size_t pattern_len, bool case_sensitive)
{
    return simd_sse42_search(text, text_len, pattern, pattern_len, case_sensitive, SIZE_MAX);
}
#endif
#ifdef __AVX2__
uint64_t simd_avx2_search_compat(const char *text, size_t text_len, const char *pattern, size_t pattern_len, bool case_sensitive)
{
    return simd_avx2_search(text, text_len, pattern, pattern_len, case_sensitive, SIZE_MAX);
}
#endif
#ifdef __ARM_NEON
uint64_t neon_search_compat(const char *text, size_t text_len, const char *pattern, size_t pattern_len, bool case_sensitive)
{
    return neon_search(text, text_len, pattern, pattern_len, case_sensitive, SIZE_MAX);
}
#endif

// Define aliases for testing
#define boyer_moore_search boyer_moore_search_compat
#define kmp_search kmp_search_compat
#define rabin_karp_search rabin_karp_search_compat
#ifdef __SSE4_2__
#define simd_sse42_search simd_sse42_search_compat
#endif
#ifdef __AVX2__
#define simd_avx2_search simd_avx2_search_compat
#endif
#ifdef __ARM_NEON
#define neon_search neon_search_compat
#endif

#endif
