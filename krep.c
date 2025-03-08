/* krep - A high-performance string search utility
 *
 * Author: Davide Santangelo
 * Version: 0.1.0
 * Year: 2025
 *
 * Features:
 * - Multiple optimized search algorithms (Boyer-Moore-Horspool, SIMD)
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
#include <inttypes.h> // For PRIu64 macro
#include <errno.h>    // For errno
#include <assert.h>   // For assertions

#ifdef __SSE4_2__
#include <immintrin.h>
#endif

#ifdef __AVX2__
#include <immintrin.h>
#endif

/* Constants */
#define MAX_PATTERN_LENGTH 1024
#define MAX_LINE_LENGTH 4096
#define DEFAULT_THREAD_COUNT 4
#define MIN_FILE_SIZE_FOR_THREADS (1 * 1024 * 1024) // 1MB minimum for threading
#define CHUNK_SIZE (16 * 1024 * 1024) // 16MB chunks for threading
#define VERSION "0.1.0"

/* Type definitions */
typedef struct {
    const char *file_data;  // Memory-mapped file content
    size_t start_pos;       // Starting position for this thread
    size_t end_pos;         // Ending position for this thread
    const char *pattern;    // Search pattern
    size_t pattern_len;     // Length of search pattern
    bool case_sensitive;    // Whether search is case-sensitive
    int thread_id;          // Thread identifier
    uint64_t *match_count;  // Pointer to shared match counter
    pthread_mutex_t *count_mutex; // Mutex for thread-safe counter updates
} search_job_t;

/* Forward declarations */
void print_usage(const char *program_name);
double get_time(void);
void prepare_bad_char_table(const char *pattern, size_t pattern_len,
                           int *bad_char_table, bool case_sensitive);
uint64_t boyer_moore_search(const char *text, size_t text_len,
                          const char *pattern, size_t pattern_len,
                          bool case_sensitive);
#ifdef __SSE4_2__
uint64_t simd_search(const char *text, size_t text_len,
                   const char *pattern, size_t pattern_len,
                   bool case_sensitive);
#endif
void* search_thread(void *arg);
int search_file(const char *filename, const char *pattern, bool case_sensitive,
               bool count_only, int thread_count);
int search_string(const char *text, const char *pattern, bool case_sensitive);

/**
 * Get current time with high precision for performance measurement
 *
 * @return Current time in seconds as a double
 */
double get_time(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        perror("Failed to get current time");
        return 0.0;
    }
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
}

/**
 * Prepare the bad character table for Boyer-Moore-Horspool algorithm
 *
 * @param pattern The search pattern
 * @param pattern_len Length of the pattern
 * @param bad_char_table Output table (must be pre-allocated with 256 elements)
 * @param case_sensitive Whether the search is case-sensitive
 */
void prepare_bad_char_table(const char *pattern, size_t pattern_len,
                           int *bad_char_table, bool case_sensitive) {
    // Initialize all entries to the pattern length
    for (int i = 0; i < 256; i++) {
        bad_char_table[i] = pattern_len;
    }

    // For each character in the pattern (except the last one),
    // record the distance from the rightmost occurrence to the end
    for (size_t i = 0; i < pattern_len - 1; i++) {
        unsigned char c = pattern[i];
        if (!case_sensitive) {
            c = tolower(c);
            // Also handle uppercase variant for case-insensitive search
            bad_char_table[toupper(c)] = pattern_len - 1 - i;
        }
        bad_char_table[c] = pattern_len - 1 - i;
    }
}

/**
 * Boyer-Moore-Horspool search algorithm implementation
 *
 * @param text The text to search in
 * @param text_len Length of the text
 * @param pattern The pattern to search for
 * @param pattern_len Length of the pattern
 * @param case_sensitive Whether the search is case-sensitive
 * @return Number of matches found
 */
uint64_t boyer_moore_search(const char *text, size_t text_len,
                          const char *pattern, size_t pattern_len,
                          bool case_sensitive) {
    int bad_char_table[256];
    uint64_t match_count = 0;

    if (pattern_len == 0 || text_len == 0 || pattern_len > text_len) {
        return 0;
    }

    // Preprocess the pattern
    prepare_bad_char_table(pattern, pattern_len, bad_char_table, case_sensitive);

    // Search algorithm
    size_t i = pattern_len - 1;
    while (i < text_len) {
        size_t j = pattern_len - 1;
        size_t k = i;
        bool match = true;

        // Check for pattern match at current position
        while (j != (size_t)-1 && match) {
            char text_char = text[k];
            char pattern_char = pattern[j];

            if (!case_sensitive) {
                text_char = tolower(text_char);
                pattern_char = tolower(pattern_char);
            }

            if (text_char != pattern_char) {
                match = false;
            }
            k--;
            j--;
        }

        if (match) {
            match_count++;
            // Move to the position right after this match
            i++;
        } else {
            // Use the bad character rule to skip ahead
            unsigned char bad_char = text[i];
            if (!case_sensitive) {
                bad_char = tolower(bad_char);
            }
            i += bad_char_table[bad_char];
        }
    }

    return match_count;
}

#ifdef __SSE4_2__
/**
 * SIMD accelerated string search for compatible hardware
 *
 * @param text The text to search in
 * @param text_len Length of the text
 * @param pattern The pattern to search for
 * @param pattern_len Length of the pattern
 * @param case_sensitive Whether the search is case-sensitive
 * @return Number of matches found
 */
uint64_t simd_search(const char *text, size_t text_len,
                   const char *pattern, size_t pattern_len,
                   bool case_sensitive) {
    uint64_t match_count = 0;

    // For very short patterns or patterns that are too long for SIMD registers,
    // fall back to Boyer-Moore
    if (pattern_len <= 2 || pattern_len > 16) {
        return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive);
    }

    // Case insensitive search not optimized for SIMD - fallback
    if (!case_sensitive) {
        return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive);
    }

    // Prepare pattern for SIMD comparison
    __m128i pattern_xmm = _mm_loadu_si128((__m128i*)pattern);

    // Process text in chunks, using SIMD instructions for comparison
    for (size_t i = 0; i <= text_len - pattern_len; i++) {
        __m128i text_xmm = _mm_loadu_si128((__m128i*)(text + i));

        // Compare strings with SIMD - returns position of first match (0 if match at start)
        int mask = _mm_cmpestri(pattern_xmm, pattern_len,
                               text_xmm, pattern_len,
                               _SIDD_CMP_EQUAL_ORDERED);

        if (mask == 0) {
            match_count++;
        }
    }

    return match_count;
}

#ifdef __AVX2__
/**
 * AVX2 accelerated string search for newer hardware
 *
 * Note: This is a placeholder function that could be implemented to use
 * AVX2 instructions for even faster searching on compatible hardware
 */
uint64_t avx2_search(const char *text, size_t text_len,
                    const char *pattern, size_t pattern_len,
                    bool case_sensitive) {
    // Use AVX2 instructions for larger register width (256-bit)
    // This would require a proper implementation
    return simd_search(text, text_len, pattern, pattern_len, case_sensitive);
}
#endif
#endif

/**
 * Thread worker function for parallel search
 *
 * @param arg Pointer to search_job_t structure with job parameters
 * @return NULL (result is stored through job parameters)
 */
void* search_thread(void *arg) {
    search_job_t *job = (search_job_t *)arg;
    uint64_t local_count = 0;

    // Calculate effective search range to avoid buffer overruns
    size_t effective_end = job->end_pos;
    if (effective_end > job->start_pos + job->pattern_len) {
        effective_end -= (job->pattern_len - 1);
    } else {
        // Not enough text to match pattern, nothing to search
        return NULL;
    }

    // Choose the appropriate search algorithm based on available hardware
#if defined(__AVX2__)
    local_count = avx2_search(job->file_data + job->start_pos,
                             effective_end - job->start_pos,
                             job->pattern, job->pattern_len,
                             job->case_sensitive);
#elif defined(__SSE4_2__)
    local_count = simd_search(job->file_data + job->start_pos,
                            effective_end - job->start_pos,
                            job->pattern, job->pattern_len,
                            job->case_sensitive);
#else
    local_count = boyer_moore_search(job->file_data + job->start_pos,
                                   effective_end - job->start_pos,
                                   job->pattern, job->pattern_len,
                                   job->case_sensitive);
#endif

    // Update the global counter safely
    if (local_count > 0) {
        pthread_mutex_lock(job->count_mutex);
        *job->match_count += local_count;
        pthread_mutex_unlock(job->count_mutex);
    }

    return NULL;
}

/**
 * Search function for direct string input
 *
 * @param text The text to search in
 * @param pattern The pattern to search for
 * @param case_sensitive Whether the search is case-sensitive
 * @return 0 on success, error code on failure
 */
int search_string(const char *text, const char *pattern, bool case_sensitive) {
    double start_time = get_time();
    size_t text_len = strlen(text);
    size_t pattern_len = strlen(pattern);
    uint64_t match_count = 0;

    if (text_len == 0) {
        printf("String is empty\n");
        return 0;
    }

    // Choose appropriate search algorithm
#if defined(__AVX2__)
    match_count = avx2_search(text, text_len, pattern, pattern_len, case_sensitive);
#elif defined(__SSE4_2__)
    match_count = simd_search(text, text_len, pattern, pattern_len, case_sensitive);
#else
    match_count = boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive);
#endif

    // Calculate and print performance metrics
    double end_time = get_time();
    double search_time = end_time - start_time;

    // Print results and details
    printf("Found %" PRIu64 " matches\n", match_count);
    printf("Search completed in %.4f seconds\n", search_time);
    printf("Search details:\n");
    printf("  - String length: %zu characters\n", text_len);
    printf("  - Pattern length: %zu characters\n", pattern_len);
#if defined(__AVX2__)
    printf("  - Using AVX2 acceleration\n");
#elif defined(__SSE4_2__)
    printf("  - Using SSE4.2 acceleration\n");
#else
    printf("  - Using standard Boyer-Moore-Horspool algorithm\n");
#endif
    printf("  - %s search\n", case_sensitive ? "Case-sensitive" : "Case-insensitive");

    return 0;
}

/**
 * Main search function that processes a file
 *
 * @param filename Path to the file to search
 * @param pattern The pattern to search for
 * @param case_sensitive Whether the search is case-sensitive
 * @param count_only Whether to only count matches (not display lines)
 * @param thread_count Number of threads to use
 * @return 0 on success, error code on failure
 */
int search_file(const char *filename, const char *pattern, bool case_sensitive,
               bool count_only, int thread_count) {
    double start_time = get_time();
    size_t pattern_len = strlen(pattern);
    uint64_t match_count = 0;

    // Open the file
    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Error opening file '%s': %s\n", filename, strerror(errno));
        return 1;
    }

    // Get file size
    struct stat file_stat;
    if (fstat(fd, &file_stat) == -1) {
        fprintf(stderr, "Error getting file size: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    size_t file_size = file_stat.st_size;
    if (file_size == 0) {
        printf("File is empty\n");
        close(fd);
        return 0;
    }

    // Memory map the file for maximum I/O performance
    char *file_data = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (file_data == MAP_FAILED) {
        fprintf(stderr, "Error memory-mapping file: %s\n", strerror(errno));
        close(fd);
        return 1;
    }

    // Advise the kernel about our access pattern
    madvise(file_data, file_size, MADV_SEQUENTIAL);

    pthread_mutex_t count_mutex = PTHREAD_MUTEX_INITIALIZER;

    // For small files or if only one thread is requested, don't use threading
    if (file_size < MIN_FILE_SIZE_FOR_THREADS || thread_count <= 1) {
        // Choose appropriate search algorithm
#if defined(__AVX2__)
        match_count = avx2_search(file_data, file_size, pattern, pattern_len, case_sensitive);
#elif defined(__SSE4_2__)
        match_count = simd_search(file_data, file_size, pattern, pattern_len, case_sensitive);
#else
        match_count = boyer_moore_search(file_data, file_size, pattern, pattern_len, case_sensitive);
#endif
    } else {
        // Multi-threaded search for larger files
        pthread_t *threads = malloc(thread_count * sizeof(pthread_t));
        search_job_t *jobs = malloc(thread_count * sizeof(search_job_t));

        if (!threads || !jobs) {
            fprintf(stderr, "Error allocating memory for threads\n");
            munmap(file_data, file_size);
            close(fd);
            free(threads); // Safe if NULL
            free(jobs);    // Safe if NULL
            pthread_mutex_destroy(&count_mutex);
            return 1;
        }

        // Distribute workload across threads
        size_t chunk_per_thread = file_size / thread_count;

        // Create and launch threads
        for (int i = 0; i < thread_count; i++) {
            jobs[i].file_data = file_data;
            jobs[i].start_pos = i * chunk_per_thread;

            // Last thread gets the remainder
            if (i == thread_count - 1) {
                jobs[i].end_pos = file_size;
            } else {
                jobs[i].end_pos = (i + 1) * chunk_per_thread;

                // Extend search boundary by pattern length to catch matches that cross chunks
                if (jobs[i].end_pos + pattern_len - 1 <= file_size) {
                    jobs[i].end_pos += pattern_len - 1;
                } else {
                    jobs[i].end_pos = file_size;
                }
            }

            jobs[i].pattern = pattern;
            jobs[i].pattern_len = pattern_len;
            jobs[i].case_sensitive = case_sensitive;
            jobs[i].thread_id = i;
            jobs[i].match_count = &match_count;
            jobs[i].count_mutex = &count_mutex;

            int result = pthread_create(&threads[i], NULL, search_thread, &jobs[i]);
            if (result != 0) {
                fprintf(stderr, "Error creating thread %d: %s\n", i, strerror(result));
                // Continue with fewer threads
            }
        }

        // Wait for all threads to complete
        for (int i = 0; i < thread_count; i++) {
            pthread_join(threads[i], NULL);
        }

        free(threads);
        free(jobs);
    }

    // Calculate and print performance metrics
    double end_time = get_time();
    double search_time = end_time - start_time;
    double mb_per_sec = file_size / (1024.0 * 1024.0) / search_time;

    // Print results
    printf("Found %" PRIu64 " matches\n", match_count);
    printf("Search completed in %.4f seconds (%.2f MB/s)\n", search_time, mb_per_sec);

    // Print search details
    printf("Search details:\n");
    printf("  - File size: %.2f MB\n", file_size / (1024.0 * 1024.0));
    printf("  - Pattern length: %zu characters\n", pattern_len);
#if defined(__AVX2__)
    printf("  - Using AVX2 acceleration\n");
#elif defined(__SSE4_2__)
    printf("  - Using SSE4.2 acceleration\n");
#else
    printf("  - Using standard Boyer-Moore-Horspool algorithm\n");
#endif
    printf("  - %s search\n", case_sensitive ? "Case-sensitive" : "Case-insensitive");

    // Clean up
    munmap(file_data, file_size);
    close(fd);
    pthread_mutex_destroy(&count_mutex);

    return 0;
}

/**
 * Print usage information
 *
 * @param program_name The name of the program executable
 */
void print_usage(const char *program_name) {
    printf("krep v%s - A high-performance string search utility\n\n", VERSION);
    printf("Usage: %s [OPTIONS] PATTERN [FILE]\n\n", program_name);
    printf("OPTIONS:\n");
    printf("  -i            Case-insensitive search\n");
    printf("  -c            Count matches only (don't print matching lines)\n");
    printf("  -t NUM        Use NUM threads (default: %d)\n", DEFAULT_THREAD_COUNT);
    printf("  -s STRING     Search within STRING instead of a file\n");
    printf("  -v            Display version information\n");
    printf("  -h            Display this help message\n\n");
    printf("EXAMPLES:\n");
    printf("  %s \"search term\" file.txt         Search for \"search term\" in file.txt\n", program_name);
    printf("  %s -i -t 8 \"ERROR\" logfile.log    Case-insensitive search with 8 threads\n", program_name);
    printf("  %s -s \"Hello world\" \"Hello\"       Search for \"Hello\" in the string \"Hello world\"\n\n", program_name);
}

/**
 * Main entry point for the program
 */
int main(int argc, char *argv[]) {
    char *pattern = NULL;
    char *filename = NULL;
    char *input_string = NULL;
    bool case_sensitive = true;
    bool count_only = false;
    int thread_count = DEFAULT_THREAD_COUNT;
    int opt;

    // Parse command line arguments
    while ((opt = getopt(argc, argv, "icvt:s:h")) != -1) {
        switch (opt) {
            case 'i':
                case_sensitive = false;
                break;
            case 'c':
                count_only = true;
                break;
            case 't':
                thread_count = atoi(optarg);
                if (thread_count <= 0) {
                    fprintf(stderr, "Warning: Invalid thread count, using 1 thread\n");
                    thread_count = 1;
                } else if (thread_count > 64) {
                    fprintf(stderr, "Warning: Very high thread count specified (%d), this may impact performance\n", thread_count);
                }
                break;
            case 's':
                input_string = optarg;
                break;
            case 'v':
                printf("krep v%s\n", VERSION);
                return 0;
            case 'h':
                print_usage(argv[0]);
                return 0;
            default:
                print_usage(argv[0]);
                return 1;
        }
    }

    // We need at least a pattern
    if (optind >= argc) {
        fprintf(stderr, "Error: Missing required pattern argument\n");
        print_usage(argv[0]);
        return 1;
    }

    pattern = argv[optind];
    
    // Check if we're working with a string or file
    if (input_string) {
        // String search mode
        return search_string(input_string, pattern, case_sensitive);
    } else {
        // File search mode - check if we have a filename
        if (optind + 1 >= argc) {
            fprintf(stderr, "Error: Missing required file argument\n");
            print_usage(argv[0]);
            return 1;
        }
        
        filename = argv[optind + 1];
    }

    size_t pattern_len = strlen(pattern);
    if (pattern_len == 0) {
        fprintf(stderr, "Error: Pattern cannot be empty\n");
        return 1;
    }

    if (pattern_len > MAX_PATTERN_LENGTH) {
        fprintf(stderr, "Error: Pattern exceeds maximum length (%d characters)\n", MAX_PATTERN_LENGTH);
        return 1;
    }

    // Perform the search based on the mode
    if (input_string) {
        return search_string(input_string, pattern, case_sensitive);
    } else {
        return search_file(filename, pattern, case_sensitive, count_only, thread_count);
    }
}
