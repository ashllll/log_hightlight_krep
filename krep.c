/* krep - A high-performance string search utility
 *
 * Author: Davide Santangelo
 * Version: 0.2.3
 * Year: 2025
 *
 * Features:
 * - Multiple search algorithms (Boyer-Moore-Horspool, KMP, Rabin-Karp)
 * - Basic SIMD (SSE4.2) acceleration implemented
 * - Placeholders for AVX2 acceleration
 * - Memory-mapped file I/O for maximum throughput
 * - Multi-threaded parallel search for large files
 * - Case-sensitive and case-insensitive matching
 * - Direct string search in addition to file search
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
#define VERSION "0.2.3"

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
    const char *pattern;   // Search pattern
    size_t pattern_len;    // Length of search pattern
    bool case_sensitive;   // Whether search is case-sensitive
    int thread_id;         // Thread identifier
    int total_threads;     // Total number of threads
    uint64_t local_count;  // Local match counter for this thread
} search_job_t;

/* --- Forward declarations --- */
void print_usage(const char *program_name);
double get_time(void);
void prepare_bad_char_table(const char *pattern, size_t pattern_len,
                            int *bad_char_table, bool case_sensitive);
void *search_thread(void *arg);

/* --- Utility Functions --- */

/**
 * Get current time with high precision for performance measurement
 */
double get_time(void)
{
    struct timespec ts;
    // Use CLOCK_MONOTONIC for consistent timing
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
        perror("Failed to get current time");
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/**
 * Print usage information
 */
void print_usage(const char *program_name)
{
    printf("krep v%s - A high-performance string search utility\n\n", VERSION);
    printf("Usage: %s [OPTIONS] PATTERN FILE\n", program_name);
    printf("   or: %s [OPTIONS] -s PATTERN STRING_TO_SEARCH\n\n", program_name);
    printf("OPTIONS:\n");
    printf("  -i            Perform case-insensitive matching.\n");
    printf("  -c            Only print a count of matches found.\n");
    printf("  -t NUM        Use NUM threads for file search (default: %d, auto-adjusts).\n", DEFAULT_THREAD_COUNT);
    printf("  -s            Search in STRING_TO_SEARCH instead of a FILE.\n");
    printf("  -v            Display version information and exit.\n");
    printf("  -h            Display this help message and exit.\n\n");
    printf("EXAMPLES:\n");
    printf("  %s \"search term\" input.log\n", program_name);
    printf("  %s -i -t 8 ERROR large_log.txt\n", program_name);
    printf("  %s -s \"pattern\" \"Search for pattern in this string.\"\n", program_name);
    printf("  echo \"Data stream\" | %s -c word -\n", program_name); // Example with stdin (needs file arg '-')
}

/* --- Boyer-Moore-Horspool Algorithm --- */

/**
 * Prepare the bad character table for Boyer-Moore-Horspool algorithm
 */
void prepare_bad_char_table(const char *pattern, size_t pattern_len,
                            int *bad_char_table, bool case_sensitive)
{
    // Initialize all shifts to pattern length (default shift)
    for (int i = 0; i < 256; i++)
    {
        bad_char_table[i] = (int)pattern_len;
    }
    // Calculate specific shifts based on characters in the pattern (excluding the last char)
    for (size_t i = 0; i < pattern_len - 1; i++)
    {
        unsigned char c = (unsigned char)pattern[i];
        int shift = (int)(pattern_len - 1 - i);
        if (!case_sensitive)
        {
            // For case-insensitive, set shift for both lower and upper case using the lookup table
            unsigned char lower_c = lower_table[c];
            bad_char_table[lower_c] = shift;
            // Ensure we also set the uppercase variant if different
            unsigned char upper_c = toupper(lower_c); // Use toupper on lower_c for locale safety
            if (upper_c != lower_c)
            {
                bad_char_table[upper_c] = shift;
            }
        }
        else
        {
            bad_char_table[c] = shift;
        }
    }
}

/**
 * Boyer-Moore-Horspool search algorithm implementation.
 * Counts matches starting *before* report_limit_offset.
 */
uint64_t boyer_moore_search(const char *text, size_t text_len,
                            const char *pattern, size_t pattern_len,
                            bool case_sensitive, size_t report_limit_offset)
{
    uint64_t match_count = 0;
    // Stack allocation is fine for 256 ints
    int bad_char_table[256];

    // Basic validation
    if (pattern_len == 0 || text_len < pattern_len || report_limit_offset == 0)
        return 0;

    // Prepare the bad character table for this specific search instance
    prepare_bad_char_table(pattern, pattern_len, bad_char_table, case_sensitive);

    size_t i = 0; // Current window start position in text
    // Optimization: Pre-calculate loop limit
    size_t limit = text_len - pattern_len;

    while (i <= limit)
    {
        size_t j = pattern_len - 1;     // Index for pattern (from end)
        size_t k = i + pattern_len - 1; // Index for text (corresponding to j)

        // Compare pattern from right to left
        while (true)
        {
            char text_char = text[k];
            char pattern_char = pattern[j];

            if (!case_sensitive)
            {
                // Use fast lookup table for lowercasing
                text_char = lower_table[(unsigned char)text_char];
                pattern_char = lower_table[(unsigned char)pattern_char];
            }

            if (text_char != pattern_char)
            {
                // Mismatch: Calculate shift based on the character in the text
                // aligning with the *last* character of the pattern in the current window.
                unsigned char bad_char_from_text = (unsigned char)text[i + pattern_len - 1];
                int shift;
                if (!case_sensitive)
                {
                    shift = bad_char_table[lower_table[bad_char_from_text]];
                }
                else
                {
                    shift = bad_char_table[bad_char_from_text];
                }

                // Ensure minimum shift of 1 to prevent infinite loops with certain patterns
                i += (shift > 0 ? shift : 1);
                break; // Move to next window
            }

            // Characters match
            if (j == 0)
            {
                // Full match found!
                if (i < report_limit_offset)
                { // Only count if match starts within the allowed limit
                    match_count++;
                }
                // Shift by 1 to find overlapping matches potentially starting at i+1
                i++;
                break; // Move to next window
            }

            // Continue comparing characters to the left
            j--;
            k--;
        }
        // Optional Prefetching: Its effectiveness varies significantly. Profile needed.
        // Consider prefetching further ahead if beneficial.
        // if (i + pattern_len + 64 < text_len) { // Prefetch one cache line ahead?
        //     __builtin_prefetch(&text[i + pattern_len + 64], 0, 0); // Read, low locality hint
        // }
    }
    return match_count;
}

/* --- Knuth-Morris-Pratt (KMP) Algorithm --- */

// Helper function to compute KMP LPS array
static void compute_lps_array(const char *pattern, size_t pattern_len, int *lps, bool case_sensitive)
{
    size_t length = 0; // Length of the previous longest prefix suffix
    lps[0] = 0;        // lps[0] is always 0
    size_t i = 1;

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
                // This is tricky. Consider the example. AAACAAAA and i = 7.
                // The idea is similar to search step below.
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

/**
 * Knuth-Morris-Pratt (KMP) search algorithm implementation.
 * Counts matches starting *before* report_limit_offset.
 */
uint64_t kmp_search(const char *text, size_t text_len,
                    const char *pattern, size_t pattern_len,
                    bool case_sensitive, size_t report_limit_offset)
{
    uint64_t match_count = 0;
    if (pattern_len == 0 || text_len < pattern_len || report_limit_offset == 0)
        return 0;

    // --- Special case for single character patterns (avoids LPS overhead) ---
    if (pattern_len == 1)
    {
        char p_char = pattern[0];
        char p_lower = lower_table[(unsigned char)p_char];

        for (size_t i = 0; i < text_len && i < report_limit_offset; i++)
        {
            char text_char = text[i];
            if (case_sensitive)
            {
                if (text_char == p_char)
                    match_count++;
            }
            else
            {
                if (lower_table[(unsigned char)text_char] == p_lower)
                    match_count++;
            }
        }
        return match_count;
    }

    // --- KMP proper ---
    // Allocate space for LPS array (Longest Proper Prefix which is also Suffix)
    int *lps = malloc(pattern_len * sizeof(int));
    if (!lps)
    {
        perror("Error allocating KMP LPS table");
        return 0; // Indicate error
    }

    // Preprocess the pattern to fill lps[]
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
            { // Count only if within limit
                match_count++;
            }
            // Shift pattern based on LPS array to find next (potentially overlapping) match
            j = lps[j - 1];
        }
        // Mismatch after j matches
        else if (i < text_len && pattern_char != text_char)
        {
            // Do not match lps[0..lps[j-1]] characters, they will match anyway
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

    free(lps); // Free allocated memory
    return match_count;
}

/* --- Rabin-Karp Algorithm --- */

/**
 * Rabin-Karp search algorithm implementation.
 * Counts matches starting *before* report_limit_offset.
 */
uint64_t rabin_karp_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset)
{
    uint64_t match_count = 0;
    if (pattern_len == 0 || text_len < pattern_len || report_limit_offset == 0)
        return 0;

    // --- Use KMP for very short patterns where hashing overhead might dominate ---
    // Tuning: Benchmark to find the optimal threshold. 4 is a reasonable guess.
    if (pattern_len <= 4)
    {
        return kmp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
    }

    // --- Rabin-Karp proper ---
    // Using 64-bit hashes reduces collision probability compared to 32-bit
    const uint64_t prime = 1000000007ULL; // A large 64-bit prime
    const uint64_t base = 256ULL;         // Base for the hash calculation
    uint64_t pattern_hash = 0;            // Hash value for pattern
    uint64_t text_hash = 0;               // Hash value for current text window
    uint64_t h = 1;                       // base^(pattern_len-1) % prime

    // Calculate h = base^(pattern_len-1) % prime efficiently
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
        if (pattern_hash == text_hash)
        {
            /* Check for characters one by one only if hashes match (collision check) */
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
                { // Count only if within limit
                    match_count++;
                }
                // Continue to i+1 to find overlapping matches
            }
        }

        // Calculate hash value for next window: Remove leading digit, add trailing digit
        if (i < limit)
        {
            char leading_char = text[i];
            char trailing_char = text[i + pattern_len];
            if (!case_sensitive)
            {
                leading_char = lower_table[(unsigned char)leading_char];
                trailing_char = lower_table[(unsigned char)trailing_char];
            }

            // Calculate rolling hash using modular arithmetic properties
            // text_hash = (base * (text_hash - text[i]*h) + text[i+pattern_len]) % prime;
            // Add prime before subtraction to handle potential negative result in modulo
            text_hash = (base * (text_hash + prime - (h * leading_char) % prime)) % prime;
            text_hash = (text_hash + trailing_char) % prime;
        }
    }

    return match_count;
}

/* --- SIMD Search Implementations --- */

#ifdef __SSE4_2__
/**
 * SIMD-accelerated search with SSE4.2 using _mm_cmpestri.
 * Counts matches starting *before* report_limit_offset.
 * Best for patterns of length 1 to 16 bytes. Case-sensitive only currently.
 */
uint64_t simd_sse42_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive, size_t report_limit_offset)
{
    uint64_t match_count = 0;
    // Check if SSE4.2 is usable for this case
    if (pattern_len == 0 || pattern_len > SIMD_MAX_LEN_SSE42 || text_len < pattern_len || report_limit_offset == 0)
    {
        // Fallback for unsupported pattern lengths or trivial cases
        return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
    }

    // SSE4.2 PCMPESTRI does not directly support case-insensitivity efficiently.
    // Fallback to scalar for case-insensitive searches.
    if (!case_sensitive)
    {
        // fprintf(stderr, "Warning: SSE4.2 case-insensitive search falling back to Boyer-Moore.\n");
        return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
    }

    // Load the pattern into an XMM register. Unaligned load is safe here.
    __m128i pattern_vec = _mm_loadu_si128((const __m128i *)pattern);

    // Define the comparison mode for _mm_cmpestri:
    // Find equal ordered substrings, return index of first match byte.
    const int mode = _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ORDERED | _SIDD_POSITIVE_POLARITY | _SIDD_LEAST_SIGNIFICANT;

    size_t current_pos = 0;
    size_t limit = text_len - pattern_len; // Max starting position for a full match

    while (current_pos <= limit)
    {
        // Determine how many bytes we can safely load for the text vector
        // We need at least 16 bytes for _mm_cmpestri's text operand length `lb`.
        size_t remaining_text = text_len - current_pos;
        if (remaining_text < 16)
        {
            // Not enough text left for a full 16-byte SIMD compare.
            // Handle the tail end using scalar search.
            if (current_pos < report_limit_offset)
            {
                size_t scalar_limit = report_limit_offset - current_pos;
                if (scalar_limit > remaining_text)
                    scalar_limit = remaining_text; // Clamp limit
                match_count += boyer_moore_search(text + current_pos, remaining_text,
                                                  pattern, pattern_len,
                                                  case_sensitive, scalar_limit);
            }
            break; // Done with the SIMD part
        }

        // Load 16 bytes of text (unaligned load)
        __m128i text_vec = _mm_loadu_si128((const __m128i *)(text + current_pos));

        // Perform the comparison: find pattern (len pattern_len) in text_vec (len 16)
        int index = _mm_cmpestri(pattern_vec, pattern_len, text_vec, 16, mode);

        // _mm_cmpestri returns the index within text_vec (0-15) if found, or 16 if not found.
        if (index < 16)
        {
            // Potential match found starting at index 'index' within the current 16-byte chunk.
            size_t match_start_pos = current_pos + index;

            // Check if match start is within the reporting limit
            if (match_start_pos < report_limit_offset)
            {
                match_count++;
            }
            current_pos += (index + pattern_len);
        }
        else
        {
            // No match found in this 16-byte block.
            // Advance the search window using the optimized skip.
            current_pos += (pattern_len <= 15) ? (16 - pattern_len + 1) : 1;
        }
    }

    return match_count;
}
#endif // __SSE4_2__

#ifdef __AVX2__
/**
 * AVX2-accelerated search (Placeholder - Requires Implementation).
 * Counts matches starting *before* report_limit_offset.
 */
uint64_t simd_avx2_search(const char *text, size_t text_len,
                          const char *pattern, size_t pattern_len,
                          bool case_sensitive, size_t report_limit_offset)
{
    // TODO: Implement correct AVX2 search using _mm256 intrinsics.
    // Techniques might involve:
    // - Loading 32-byte text chunks (_mm256_loadu_si256).
    // - Broadcasting the first pattern byte (_mm256_set1_epi8).
    // - Comparing first bytes (_mm256_cmpeq_epi8).
    // - Creating a mask (_mm256_movemask_epi8).
    // - If first bytes match, perform full comparison for candidates using scalar or other SIMD.
    // - Handling case-insensitivity efficiently (e.g., _mm256_shuffle_epi8 for lowercase conversion).
    // - Handling matches across 32-byte boundaries.

    // Fallback to SSE4.2 if available and suitable, otherwise Boyer-Moore
    // fprintf(stderr, "Warning: AVX2 search not implemented, falling back.\n");
#ifdef __SSE4_2__
    // Use SSE4.2 if pattern length allows and it's implemented
    if (pattern_len <= SIMD_MAX_LEN_SSE42)
    { // Use SSE4.2 constant
        return simd_sse42_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
    }
    else
    {
        return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
    }
#else
    // Fallback to Boyer-Moore if SSE4.2 is not available
    return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
#endif
}
#endif // __AVX2__

#ifdef __ARM_NEON // Placeholder for potential NEON implementation
/**
 * SIMD-accelerated search using ARM NEON intrinsics. (Placeholder)
 */
uint64_t neon_search(const char *text, size_t text_len,
                     const char *pattern, size_t pattern_len,
                     bool case_sensitive, size_t report_limit_offset)
{
    // TODO: Implement NEON search using intrinsics like vld1q_u8, vceqq_u8, etc.
    // fprintf(stderr, "Warning: NEON search not implemented, falling back to Boyer-Moore.\n");
    return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
}
#endif // __ARM_NEON

/* --- Multi-threading Logic --- */

/**
 * Thread worker function for parallel search.
 */
void *search_thread(void *arg)
{
    search_job_t *job = (search_job_t *)arg;
    job->local_count = 0; // Initialize local count for this thread

    // Determine the actual buffer this thread needs to read.
    // It reads from its start position potentially into the next thread's chunk
    // by 'pattern_len - 1' bytes to catch matches spanning the boundary.
    size_t buffer_abs_start = job->start_pos;
    size_t buffer_abs_end = (job->thread_id == job->total_threads - 1)
                                ? job->end_pos                         // Last thread stops precisely at the file end
                                : job->end_pos + job->pattern_len - 1; // Others read overlap

    // Clamp buffer end to the actual file size to prevent reading beyond bounds
    if (buffer_abs_end > job->file_size)
    {
        buffer_abs_end = job->file_size;
    }

    // Calculate the pointer and length for the buffer this thread will process
    const char *buffer_ptr = job->file_data + buffer_abs_start;
    size_t buffer_len = (buffer_abs_end > buffer_abs_start) ? buffer_abs_end - buffer_abs_start : 0;

    // Determine the limit for *counting* matches. Matches must START strictly
    // *before* the original end position (job->end_pos) of this thread's primary chunk.
    // This limit is relative to the start of the buffer_ptr this thread is processing.
    size_t report_limit_offset = (job->end_pos > buffer_abs_start) ? job->end_pos - buffer_abs_start : 0;

    // Basic checks: If buffer is too short for pattern or no counting region, exit.
    if (buffer_len < job->pattern_len || report_limit_offset == 0)
    {
        return NULL;
    }

    // --- Dynamic algorithm selection ---
    // Profile and benchmark to tune these thresholds based on actual performance
    // after SIMD implementations are complete.
    const size_t KMP_THRESH = 3; // Use KMP for very short patterns
    const size_t RK_THRESH = 32; // Use RK for very long patterns (adjust based on benchmarks)

    if (job->pattern_len < KMP_THRESH)
    {
        job->local_count = kmp_search(buffer_ptr, buffer_len,
                                      job->pattern, job->pattern_len,
                                      job->case_sensitive, report_limit_offset);
    }
#ifdef __AVX2__ // Prefer AVX2 if available and pattern length suitable
    else if (job->pattern_len <= SIMD_MAX_LEN_AVX2)
    { // Use AVX2 constant
        job->local_count = simd_avx2_search(buffer_ptr, buffer_len,
                                            job->pattern, job->pattern_len,
                                            job->case_sensitive, report_limit_offset);
    }
#endif
#ifdef __SSE4_2__ // Use SSE4.2 if AVX2 not used and pattern length suitable
    else if (job->pattern_len <= SIMD_MAX_LEN_SSE42)
    { // Use SSE4.2 constant
        job->local_count = simd_sse42_search(buffer_ptr, buffer_len,
                                             job->pattern, job->pattern_len,
                                             job->case_sensitive, report_limit_offset);
    }
#endif
#ifdef __ARM_NEON // Use NEON if available (needs length check based on impl)
    // else if (job->pattern_len <= NEON_MAX_LEN) { // Add appropriate check
    //      job->local_count = neon_search(buffer_ptr, buffer_len,
    //                                     job->pattern, job->pattern_len,
    //                                     job->case_sensitive, report_limit_offset);
    // }
#endif
    else if (job->pattern_len > RK_THRESH)
    { // Use Rabin-Karp for very long patterns
        job->local_count = rabin_karp_search(buffer_ptr, buffer_len,
                                             job->pattern, job->pattern_len,
                                             job->case_sensitive, report_limit_offset);
    }
    else
    { // Fallback to Boyer-Moore for intermediate lengths or no SIMD
        job->local_count = boyer_moore_search(buffer_ptr, buffer_len,
                                              job->pattern, job->pattern_len,
                                              job->case_sensitive, report_limit_offset);
    }

    return NULL;
}

/* --- Public API Implementations --- */

/**
 * Search within a string (single-threaded).
 */
int search_string(const char *pattern, size_t pattern_len, const char *text, bool case_sensitive)
{
    double start_time = get_time();
    size_t text_len = strlen(text); // Calculate length once
    uint64_t match_count = 0;

    if (text_len == 0 && pattern_len > 0)
    {
        printf("String is empty, pattern is not. Found 0 matches\n");
        return 0;
    }
    if (pattern_len > text_len)
    {
        printf("Pattern is longer than string. Found 0 matches\n");
        return 0;
    }
    if (pattern_len == 0)
    {
        fprintf(stderr, "Error: Empty pattern provided.\n");
        return 1; // Or handle as finding matches everywhere? Define behavior.
    }

    // Select algorithm (same logic as thread, but report_limit is text_len)
    size_t report_limit_offset = text_len; // Count all matches within the string

    const char *algo_name = "Unknown";
    const size_t KMP_THRESH = 3;
    const size_t RK_THRESH = 32;

    if (pattern_len < KMP_THRESH)
    {
        match_count = kmp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
        algo_name = "KMP";
    }
#ifdef __AVX2__
    else if (pattern_len <= SIMD_MAX_LEN_AVX2)
    {
        match_count = simd_avx2_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
        algo_name = "AVX2"; // Placeholder name
    }
#endif
#ifdef __SSE4_2__
    else if (pattern_len <= SIMD_MAX_LEN_SSE42)
    {
        match_count = simd_sse42_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
        algo_name = "SSE4.2";
    }
#endif
#ifdef __ARM_NEON
    // else if (pattern_len <= NEON_MAX_LEN) {
    //     match_count = neon_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
    //     algo_name = "NEON"; // Placeholder name
    // }
#endif
    else if (pattern_len > RK_THRESH)
    {
        match_count = rabin_karp_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
        algo_name = "Rabin-Karp";
    }
    else
    {
        match_count = boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive, report_limit_offset);
        algo_name = "Boyer-Moore-Horspool";
    }

    double end_time = get_time();
    double search_time = end_time - start_time;

    printf("Found %" PRIu64 " matches\n", match_count);
    printf("Search completed in %.4f seconds\n", search_time);
    printf("Search details:\n");
    printf("  - String length: %zu characters\n", text_len);
    printf("  - Pattern length: %zu characters\n", pattern_len);
    printf("  - Algorithm used: %s\n", algo_name);
    printf("  - %s search\n", case_sensitive ? "Case-sensitive" : "Case-insensitive");
    return 0; // Success
}

/**
 * Search within a file using memory mapping and adaptive threading.
 */
int search_file(const char *filename, const char *pattern, size_t pattern_len, bool case_sensitive,
                bool count_only, int thread_count)
{
    double start_time = get_time();
    uint64_t total_match_count = 0;

    // --- Input Validation ---
    if (pattern_len == 0)
    {
        fprintf(stderr, "Error: Empty pattern\n");
        return 1;
    }
    if (pattern_len > MAX_PATTERN_LENGTH)
    {
        fprintf(stderr, "Error: Pattern exceeds maximum length (%d)\n", MAX_PATTERN_LENGTH);
        return 1;
    }

    // --- File Opening and Stat ---
    int fd = open(filename, O_RDONLY | O_CLOEXEC); // Use O_CLOEXEC for safety
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

    size_t file_size = file_stat.st_size;
    if (file_size == 0)
    {
        printf("File '%s' is empty. Found 0 matches.\n", filename);
        close(fd);
        return 0;
    }
    if (pattern_len > file_size)
    {
        printf("Pattern is longer than file '%s'. Found 0 matches.\n", filename);
        close(fd);
        return 0;
    }

    // --- Memory Mapping ---
    // Set mmap flags - try MAP_POPULATE if available for potential pre-faulting
    // Profile to see if MAP_POPULATE helps or hurts on target system/workload.
    int mmap_flags = MAP_PRIVATE;
#ifdef MAP_POPULATE
    mmap_flags |= MAP_POPULATE;
#endif
    // Use MAP_FAILED standard check
    char *file_data = mmap(NULL, file_size, PROT_READ, mmap_flags, fd, 0);
    if (file_data == MAP_FAILED)
    {
        fprintf(stderr, "Error memory-mapping file '%s' (size %zu): %s\n", filename, file_size, strerror(errno));
        close(fd);
        return 1;
    }
    // Close file descriptor immediately after successful mmap, it's no longer needed
    close(fd);

    // Advise kernel of sequential access pattern - potentially improves read-ahead
    if (madvise(file_data, file_size, MADV_SEQUENTIAL) != 0)
    {
        // Non-fatal error, just report it
        perror("Warning: madvise(MADV_SEQUENTIAL) failed");
    }

    // --- Determine Thread Count ---
    int cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cpu_cores <= 0)
        cpu_cores = 1; // Fallback

    int actual_thread_count = (thread_count <= 0) ? DEFAULT_THREAD_COUNT : thread_count;
    // Cap threads loosely based on cores to avoid excessive context switching
    int max_reasonable_threads = cpu_cores * 2;
    if (actual_thread_count > max_reasonable_threads)
    {
        // fprintf(stderr, "Warning: Thread count (%d) high relative to CPU cores (%d). Capping at %d.\n", actual_thread_count, cpu_cores, max_reasonable_threads);
        actual_thread_count = max_reasonable_threads;
    }
    // Don't use more threads than potential non-overlapping matches (heuristic)
    if (pattern_len > 0)
    {
        size_t max_useful_threads = file_size / pattern_len;
        if (max_useful_threads == 0)
            max_useful_threads = 1; // At least 1
        if ((size_t)actual_thread_count > max_useful_threads)
        {
            actual_thread_count = (int)max_useful_threads;
        }
    }
    if (actual_thread_count < 1)
        actual_thread_count = 1; // Ensure at least one thread

    // --- Decide Threading Strategy ---
    // Use multiple threads only if file is large enough and more than 1 thread is requested/useful
    // Adjust threshold based on profiling - larger files benefit more from threading overhead.
    size_t dynamic_threshold = MIN_FILE_SIZE_FOR_THREADS * actual_thread_count;
    bool use_multithreading = (file_size >= dynamic_threshold) && (actual_thread_count > 1);
    const char *execution_mode = use_multithreading ? "Multi-threaded" : "Single-threaded";

    if (!use_multithreading)
    {
        // --- Single-threaded Search ---
        actual_thread_count = 1;         // Ensure reported thread count is 1
        size_t report_limit = file_size; // Count all matches

        // Select algorithm (same logic as thread function)
        const size_t KMP_THRESH = 3;
        const size_t RK_THRESH = 32;

        if (pattern_len < KMP_THRESH)
        {
            total_match_count = kmp_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit);
        }
#ifdef __AVX2__
        else if (pattern_len <= SIMD_MAX_LEN_AVX2)
        {
            total_match_count = simd_avx2_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit);
        }
#endif
#ifdef __SSE4_2__
        else if (pattern_len <= SIMD_MAX_LEN_SSE42)
        {
            total_match_count = simd_sse42_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit);
        }
#endif
#ifdef __ARM_NEON
        // else if (pattern_len <= NEON_MAX_LEN) {
        //     total_match_count = neon_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit);
        // }
#endif
        else if (pattern_len > RK_THRESH)
        {
            total_match_count = rabin_karp_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit);
        }
        else
        {
            total_match_count = boyer_moore_search(file_data, file_size, pattern, pattern_len, case_sensitive, report_limit);
        }
    }
    else
    {
        // --- Multi-threaded Search ---
        pthread_t *threads = malloc(actual_thread_count * sizeof(pthread_t));
        search_job_t *jobs = malloc(actual_thread_count * sizeof(search_job_t));
        bool memory_error = false;

        if (!threads || !jobs)
        {
            perror("Error allocating memory for threads/jobs");
            memory_error = true;
        }
        else
        {
            size_t chunk_size = file_size / actual_thread_count;
            size_t current_pos = 0;
            // Keep track of which thread indices were actually created
            // Use calloc for zero initialization
            bool *thread_valid = calloc(actual_thread_count, sizeof(bool));
            if (!thread_valid)
            {
                perror("Error allocating memory for thread validity flags");
                memory_error = true;
            }
            else
            {
                for (int i = 0; i < actual_thread_count; i++)
                {
                    jobs[i].file_data = file_data;
                    jobs[i].file_size = file_size;
                    jobs[i].start_pos = current_pos;
                    // Assign end position, ensuring last thread gets the exact remainder
                    jobs[i].end_pos = (i == actual_thread_count - 1) ? file_size : current_pos + chunk_size;

                    // Prevent overflow and ensure end_pos doesn't exceed file_size
                    if (jobs[i].end_pos < current_pos || jobs[i].end_pos > file_size)
                    {
                        jobs[i].end_pos = file_size;
                    }
                    current_pos = jobs[i].end_pos; // Start of next chunk

                    jobs[i].pattern = pattern;
                    jobs[i].pattern_len = pattern_len;
                    jobs[i].case_sensitive = case_sensitive;
                    jobs[i].thread_id = i;
                    jobs[i].total_threads = actual_thread_count;
                    jobs[i].local_count = 0;

                    // Only create thread if there's a valid chunk to process
                    // (start_pos < end_pos ensures non-empty chunk)
                    if (jobs[i].start_pos < jobs[i].end_pos)
                    {
                        if (pthread_create(&threads[i], NULL, search_thread, &jobs[i]) != 0)
                        {
                            perror("Error creating search thread");
                            // Attempt cleanup of already created threads before failing
                            for (int j = 0; j < i; j++)
                            {
                                if (thread_valid[j])
                                { // Only cancel/join valid threads
                                    // pthread_cancel is generally discouraged; let threads finish if possible
                                    // pthread_cancel(threads[j]);
                                    pthread_join(threads[j], NULL); // Wait for them
                                }
                            }
                            memory_error = true; // Signal cleanup needed
                            break;               // Stop creating more threads
                        }
                        thread_valid[i] = true; // Mark thread as successfully created
                    }
                    else
                    {
                        thread_valid[i] = false; // No thread created for this index
                    }
                }

                // Wait for all successfully created threads to complete and aggregate results
                if (!memory_error)
                {
                    for (int i = 0; i < actual_thread_count; i++)
                    {
                        if (thread_valid[i])
                        { // Only join threads that were created
                            pthread_join(threads[i], NULL);
                            total_match_count += jobs[i].local_count;
                        }
                    }
                }
                free(thread_valid);
            } // end else (!thread_valid)
        } // end else (!threads || !jobs)

        // Cleanup thread/job memory
        free(threads); // free(NULL) is safe
        free(jobs);

        if (memory_error)
        {
            munmap(file_data, file_size);
            return 1; // Indicate failure
        }
    }

    // --- Cleanup and Reporting ---
    if (munmap(file_data, file_size) != 0)
    {
        perror("Warning: munmap failed");
        // Continue to report results if possible
    }

    double end_time = get_time();
    double search_time = end_time - start_time;
    // Avoid division by zero for extremely fast searches or zero-size files (though handled earlier)
    double mb_per_sec = (search_time > 1e-9 && file_size > 0) ? (file_size / (1024.0 * 1024.0)) / search_time : 0.0;

    printf("Found %" PRIu64 " matches in '%s'\n", total_match_count, filename);
    if (!count_only)
    {
        printf("Search completed in %.4f seconds (%.2f MB/s)\n", search_time, mb_per_sec);
        printf("Search details:\n");
        printf("  - File size: %.2f MB (%zu bytes)\n", file_size / (1024.0 * 1024.0), file_size);
        printf("  - Pattern length: %zu characters\n", pattern_len);
        printf("  - Execution: %s (%d thread%s)\n", execution_mode, actual_thread_count, actual_thread_count > 1 ? "s" : "");
#ifdef __AVX2__
        printf("  - AVX2 Available: Yes\n");
#elif defined(__SSE4_2__)
        printf("  - SSE4.2 Available: Yes\n");
#elif defined(__ARM_NEON)
        printf("  - NEON Available: Yes\n");
#else
        printf("  - SIMD Available: No (Using scalar algorithms)\n");
#endif
        printf("  - %s search\n", case_sensitive ? "Case-sensitive" : "Case-insensitive");
    }

    return 0; // Success
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
    int thread_count = 0; // Use 0 to signify default/auto
    int opt;

    // Note: init_lower_table() is called automatically via constructor attribute

    // Parse command-line options
    while ((opt = getopt(argc, argv, "icvt:sh")) != -1)
    {
        switch (opt)
        {
        case 'i':
            case_sensitive = false;
            break;
        case 'c':
            count_only = true;
            break;
        case 't':
        {                 // Start new scope for variable declaration
            char *endptr; // Declare endptr at the beginning of the block
            long val = strtol(optarg, &endptr, 10);
            if (optarg == endptr || *endptr != '\0' || val <= 0 || val > INT_MAX)
            {
                fprintf(stderr, "Warning: Invalid thread count '%s', using default.\n", optarg);
                thread_count = 0; // Signal default/auto
            }
            else
            {
                thread_count = (int)val;
            }
        } // End scope
        break;
        case 's':
            string_mode = true;
            break;
        case 'v':
            printf("krep v%s\n", VERSION);
            return 0;
        case 'h':
            print_usage(argv[0]);
            return 0;
        case '?': // Error case from getopt
            print_usage(argv[0]);
            return 1;
        default: // Should not happen
            fprintf(stderr, "Internal error parsing options.\n");
            return 1;
        }
    }

    // Check for required positional arguments
    if (optind >= argc)
    {
        fprintf(stderr, "Error: Missing PATTERN.\n");
        print_usage(argv[0]);
        return 1;
    }
    pattern_arg = argv[optind++];

    if (optind >= argc)
    {
        fprintf(stderr, "Error: Missing %s.\n", string_mode ? "STRING_TO_SEARCH" : "FILE");
        print_usage(argv[0]);
        return 1;
    }
    file_or_string_arg = argv[optind];

    // Validate pattern length
    size_t pattern_len = strlen(pattern_arg);
    if (pattern_len == 0)
    {
        fprintf(stderr, "Error: Pattern cannot be empty.\n");
        return 1;
    }
    if (pattern_len > MAX_PATTERN_LENGTH)
    {
        fprintf(stderr, "Error: Pattern length (%zu) exceeds maximum of %d.\n", pattern_len, MAX_PATTERN_LENGTH);
        return 1;
    }

    // Execute search based on mode
    int result = 1; // Default to error
    if (string_mode)
    {
        result = search_string(pattern_arg, pattern_len, file_or_string_arg, case_sensitive);
    }
    else
    {
        // Handle stdin if filename is "-"
        const char *filename_to_search = file_or_string_arg;
        if (strcmp(filename_to_search, "-") == 0)
        {
            // TODO: Implement reading from stdin. Requires buffered reading,
            // cannot use mmap. Handle matches spanning buffer boundaries.
            fprintf(stderr, "Error: Searching from stdin ('-') is not yet implemented.\n");
            result = 1; // Indicate error until implemented
        }
        else
        {
            result = search_file(filename_to_search, pattern_arg, pattern_len, case_sensitive, count_only, thread_count);
        }
    }

    return result; // 0 on success, non-zero on error
}
#endif // TESTING
