/* krep - A high-performance string search utility
 *
 * Author: Davide Santangelo
 * Version: 0.1.0
 * Year: 2025
 *
 * Features:
 * - Multiple optimized search algorithms (Boyer-Moore-Horspool, KMP, Rabin-Karp, SIMD, AVX2)
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
#include <assert.h>
#include <stdatomic.h>

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
#define CHUNK_SIZE (16 * 1024 * 1024) // 16MB base chunk size
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
    uint64_t local_count;   // Local match counter for this thread
} search_job_t;

/* Cached pattern preprocessing data */
typedef struct {
    char pattern[MAX_PATTERN_LENGTH];
    size_t pattern_len;
    bool case_sensitive;
    int bad_char_table[256];
} cached_pattern_t;

/* Global cached pattern (for repeated searches) */
static cached_pattern_t cached_pattern = { .pattern_len = 0 };

/* Forward declarations */
void print_usage(const char *program_name);
double get_time(void);
void prepare_bad_char_table(const char *pattern, size_t pattern_len,
                           int *bad_char_table, bool case_sensitive);
uint64_t boyer_moore_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive);
uint64_t kmp_search(const char *text, size_t text_len,
                    const char *pattern, size_t pattern_len,
                    bool case_sensitive);
uint64_t rabin_karp_search(const char *text, size_t text_len,
                          const char *pattern, size_t pattern_len,
                          bool case_sensitive);
#ifdef __SSE4_2__
uint64_t simd_search(const char *text, size_t text_len,
                    const char *pattern, size_t pattern_len,
                    bool case_sensitive);
#endif
#ifdef __AVX2__
uint64_t avx2_search(const char *text, size_t text_len,
                    const char *pattern, size_t pattern_len,
                    bool case_sensitive);
#endif
void* search_thread(void *arg);
int search_file(const char *filename, const char *pattern, bool case_sensitive,
               bool count_only, int thread_count);
int search_string(const char *pattern, const char *text, bool case_sensitive);

/**
 * Get current time with high precision for performance measurement
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
 * @param pattern The search pattern
 * @param pattern_len Length of the pattern
 * @param bad_char_table Output table (256 elements)
 * @param case_sensitive Whether the search is case-sensitive
 */
void prepare_bad_char_table(const char *pattern, size_t pattern_len,
                           int *bad_char_table, bool case_sensitive) {
    for (int i = 0; i < 256; i++) {
        bad_char_table[i] = pattern_len;
    }
    for (size_t i = 0; i < pattern_len - 1; i++) {
        unsigned char c = (unsigned char)pattern[i];
        if (!case_sensitive) {
            c = tolower(c);
            bad_char_table[toupper(c)] = pattern_len - 1 - i;
        }
        bad_char_table[c] = pattern_len - 1 - i;
    }
}

/**
 * Boyer-Moore-Horspool search algorithm with prefetching
 */
uint64_t boyer_moore_search(const char *text, size_t text_len,
                           const char *pattern, size_t pattern_len,
                           bool case_sensitive) {
    uint64_t match_count = 0;
    int bad_char_table[256];

    if (pattern_len == 0 || text_len < pattern_len) return 0;

    // Use cached bad character table if available
    if (cached_pattern.pattern_len == pattern_len &&
        cached_pattern.case_sensitive == case_sensitive &&
        strncmp(cached_pattern.pattern, pattern, pattern_len) == 0) {
        memcpy(bad_char_table, cached_pattern.bad_char_table, sizeof(bad_char_table));
    } else {
        prepare_bad_char_table(pattern, pattern_len, bad_char_table, case_sensitive);
        // Cache the result
        strncpy(cached_pattern.pattern, pattern, pattern_len);
        cached_pattern.pattern_len = pattern_len;
        cached_pattern.case_sensitive = case_sensitive;
        memcpy(cached_pattern.bad_char_table, bad_char_table, sizeof(bad_char_table));
    }

    size_t i = pattern_len - 1;
    while (i < text_len) {
        size_t j = pattern_len - 1;
        bool match = true;
        while (j != (size_t)-1 && match) {
            char text_char = text[i - (pattern_len - 1 - j)];
            char pattern_char = pattern[j];
            if (!case_sensitive) {
                text_char = tolower(text_char);
                pattern_char = tolower(pattern_char);
            }
            match = (text_char == pattern_char);
            j--;
        }
        if (match) {
            match_count++;
            i++;
        } else {
            unsigned char bad_char = text[i];
            if (!case_sensitive) bad_char = tolower(bad_char);
            i += bad_char_table[bad_char];
        }
        // Manual prefetching for next iteration
        if (i + pattern_len < text_len) {
            __builtin_prefetch(&text[i + pattern_len], 0, 1);
        }
    }
    return match_count;
}

/**
 * Knuth-Morris-Pratt (KMP) search for short patterns
 */
uint64_t kmp_search(const char *text, size_t text_len,
                    const char *pattern, size_t pattern_len,
                    bool case_sensitive) {
    uint64_t match_count = 0;
    if (pattern_len == 0 || text_len < pattern_len) return 0;

    int *prefix_table = malloc(pattern_len * sizeof(int));
    if (!prefix_table) {
        fprintf(stderr, "Error allocating prefix table\n");
        return 0;
    }

    // Compute prefix table
    prefix_table[0] = 0;
    size_t j = 0;
    for (size_t i = 1; i < pattern_len; i++) {
        while (j > 0 && (case_sensitive ? pattern[i] != pattern[j] :
                         tolower(pattern[i]) != tolower(pattern[j]))) {
            j = prefix_table[j - 1];
        }
        if (case_sensitive ? pattern[i] == pattern[j] :
            tolower(pattern[i]) == tolower(pattern[j])) {
            j++;
        }
        prefix_table[i] = j;
    }

    // Search
    j = 0;
    for (size_t i = 0; i < text_len; i++) {
        while (j > 0 && (case_sensitive ? text[i] != pattern[j] :
                         tolower(text[i]) != tolower(pattern[j]))) {
            j = prefix_table[j - 1];
        }
        if (case_sensitive ? text[i] == pattern[j] :
            tolower(text[i]) == tolower(pattern[j])) {
            j++;
        }
        if (j == pattern_len) {
            match_count++;
            j = prefix_table[j - 1];
        }
    }
    free(prefix_table);
    return match_count;
}

/**
 * Rabin-Karp search for multiple pattern scenarios
 */
uint64_t rabin_karp_search(const char *text, size_t text_len,
                          const char *pattern, size_t pattern_len,
                          bool case_sensitive) {
    uint64_t match_count = 0;
    if (pattern_len == 0 || text_len < pattern_len) return 0;

    const int base = 256;
    const int prime = 101;
    uint64_t pattern_hash = 0, text_hash = 0, h = 1;

    for (size_t i = 0; i < pattern_len - 1; i++) {
        h = (h * base) % prime;
    }

    for (size_t i = 0; i < pattern_len; i++) {
        char pc = case_sensitive ? pattern[i] : tolower(pattern[i]);
        char tc = case_sensitive ? text[i] : tolower(text[i]);
        pattern_hash = (base * pattern_hash + pc) % prime;
        text_hash = (base * text_hash + tc) % prime;
    }

    for (size_t i = 0; i <= text_len - pattern_len; i++) {
        if (pattern_hash == text_hash) {
            bool match = true;
            for (size_t j = 0; j < pattern_len; j++) {
                char tc = case_sensitive ? text[i + j] : tolower(text[i + j]);
                char pc = case_sensitive ? pattern[j] : tolower(pattern[j]);
                if (tc != pc) {
                    match = false;
                    break;
                }
            }
            if (match) match_count++;
        }
        if (i < text_len - pattern_len) {
            char out = case_sensitive ? text[i] : tolower(text[i]);
            char in = case_sensitive ? text[i + pattern_len] : tolower(text[i + pattern_len]);
            text_hash = (base * (text_hash - out * h) + in) % prime;
            if (text_hash < 0) text_hash += prime;
        }
    }
    return match_count;
}

#ifdef __SSE4_2__
/**
 * SIMD-accelerated search with SSE4.2
 */
uint64_t simd_search(const char *text, size_t text_len,
                    const char *pattern, size_t pattern_len,
                    bool case_sensitive) {
    uint64_t match_count = 0;
    if (pattern_len <= 2 || pattern_len > 16 || text_len < pattern_len) {
        return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive);
    }

    if (!case_sensitive) {
        char lower_pattern[16], upper_pattern[16];
        for (size_t i = 0; i < pattern_len; i++) {
            lower_pattern[i] = tolower(pattern[i]);
            upper_pattern[i] = toupper(pattern[i]);
        }
        __m128i lp = _mm_loadu_si128((__m128i*)lower_pattern);
        __m128i up = _mm_loadu_si128((__m128i*)upper_pattern);

        for (size_t i = 0; i <= text_len - pattern_len; i++) {
            __m128i t = _mm_loadu_si128((__m128i*)(text + i));
            __m128i lt = _mm_or_si128(_mm_and_si128(t, _mm_set1_epi8(0xDF)), _mm_set1_epi8(0x20));
            int lm = _mm_cmpestri(lp, pattern_len, lt, pattern_len, _SIDD_CMP_EQUAL_ORDERED);
            int um = _mm_cmpestri(up, pattern_len, lt, pattern_len, _SIDD_CMP_EQUAL_ORDERED);
            if (lm == 0 || um == 0) match_count++;
        }
    } else {
        __m128i p = _mm_loadu_si128((__m128i*)pattern);
        for (size_t i = 0; i <= text_len - pattern_len; i++) {
            __m128i t = _mm_loadu_si128((__m128i*)(text + i));
            if (_mm_cmpestri(p, pattern_len, t, pattern_len, _SIDD_CMP_EQUAL_ORDERED) == 0) {
                match_count++;
            }
        }
    }
    return match_count;
}
#endif

#ifdef __AVX2__
/**
 * AVX2-accelerated search with 256-bit registers
 */
uint64_t avx2_search(const char *text, size_t text_len,
                    const char *pattern, size_t pattern_len,
                    bool case_sensitive) {
    uint64_t match_count = 0;
    if (pattern_len <= 2 || pattern_len > 32 || text_len < pattern_len) {
        return boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive);
    }

    if (!case_sensitive) {
        char lower_pattern[32], upper_pattern[32];
        memset(lower_pattern, 0, 32);
        memset(upper_pattern, 0, 32);
        for (size_t i = 0; i < pattern_len; i++) {
            lower_pattern[i] = tolower(pattern[i]);
            upper_pattern[i] = toupper(pattern[i]);
        }
        __m256i lp = _mm256_loadu_si256((__m256i*)lower_pattern);
        __m256i up = _mm256_loadu_si256((__m256i*)upper_pattern);

        for (size_t i = 0; i <= text_len - pattern_len; i += 32) {
            __m256i t = _mm256_loadu_si256((__m256i*)(text + i));
            __m256i lt = _mm256_or_si256(_mm256_and_si256(t, _mm256_set1_epi8(0xDF)), _mm256_set1_epi8(0x20));
            __m256i lc = _mm256_cmpeq_epi8(lp, lt);
            __m256i uc = _mm256_cmpeq_epi8(up, lt);
            uint32_t mask = _mm256_movemask_epi8(_mm256_or_si256(lc, uc));
            while (mask) {
                match_count += (mask & 1);
                mask >>= 1;
            }
        }
    } else {
        __m256i p = _mm256_loadu_si256((__m256i*)pattern);
        for (size_t i = 0; i <= text_len - pattern_len; i += 32) {
            __m256i t = _mm256_loadu_si256((__m256i*)(text + i));
            uint32_t mask = _mm256_movemask_epi8(_mm256_cmpeq_epi8(p, t));
            while (mask) {
                match_count += (mask & 1);
                mask >>= 1;
            }
        }
    }
    return match_count;
}
#endif

/**
 * Thread worker function for parallel search
 */
void* search_thread(void *arg) {
    search_job_t *job = (search_job_t *)arg;
    job->local_count = 0;

    size_t effective_end = job->end_pos;
    if (effective_end > job->start_pos + job->pattern_len) {
        effective_end -= (job->pattern_len - 1);
    } else {
        return NULL;
    }

    // Dynamic algorithm selection
    if (job->pattern_len < 3) {
        job->local_count = kmp_search(job->file_data + job->start_pos,
                                     effective_end - job->start_pos,
                                     job->pattern, job->pattern_len,
                                     job->case_sensitive);
    } else if (job->pattern_len > 16) {
        job->local_count = rabin_karp_search(job->file_data + job->start_pos,
                                            effective_end - job->start_pos,
                                            job->pattern, job->pattern_len,
                                            job->case_sensitive);
    } else {
#ifdef __AVX2__
        job->local_count = avx2_search(job->file_data + job->start_pos,
                                      effective_end - job->start_pos,
                                      job->pattern, job->pattern_len,
                                      job->case_sensitive);
#elif defined(__SSE4_2__)
        job->local_count = simd_search(job->file_data + job->start_pos,
                                      effective_end - job->start_pos,
                                      job->pattern, job->pattern_len,
                                      job->case_sensitive);
#else
        job->local_count = boyer_moore_search(job->file_data + job->start_pos,
                                             effective_end - job->start_pos,
                                             job->pattern, job->pattern_len,
                                             job->case_sensitive);
#endif
    }
    return NULL;
}

/**
 * Search within a string
 */
int search_string(const char *pattern, const char *text, bool case_sensitive) {
    double start_time = get_time();
    size_t text_len = strlen(text);
    size_t pattern_len = strlen(pattern);
    uint64_t match_count = 0;

    if (text_len == 0) {
        printf("String is empty\n");
        return 0;
    }

    if (pattern_len < 3) {
        match_count = kmp_search(text, text_len, pattern, pattern_len, case_sensitive);
    } else if (pattern_len > 16) {
        match_count = rabin_karp_search(text, text_len, pattern, pattern_len, case_sensitive);
    } else {
#ifdef __AVX2__
        match_count = avx2_search(text, text_len, pattern, pattern_len, case_sensitive);
#elif defined(__SSE4_2__)
        match_count = simd_search(text, text_len, pattern, pattern_len, case_sensitive);
#else
        match_count = boyer_moore_search(text, text_len, pattern, pattern_len, case_sensitive);
#endif
    }

    double end_time = get_time();
    double search_time = end_time - start_time;

    printf("Found %" PRIu64 " matches\n", match_count);
    printf("Search completed in %.4f seconds\n", search_time);
    printf("Search details:\n");
    printf("  - String length: %zu characters\n", text_len);
    printf("  - Pattern length: %zu characters\n", pattern_len);
#ifdef __AVX2__
    printf("  - Using AVX2 acceleration\n");
#elif defined(__SSE4_2__)
    printf("  - Using SSE4.2 acceleration\n");
#else
    printf("  - Using Boyer-Moore-Horspool algorithm\n");
#endif
    printf("  - %s search\n", case_sensitive ? "Case-sensitive" : "Case-insensitive");
    return 0;
}

/**
 * Search within a file with adaptive threading
 */
int search_file(const char *filename, const char *pattern, bool case_sensitive,
                bool count_only, int thread_count) {
    double start_time = get_time();
    size_t pattern_len = strlen(pattern);
    uint64_t match_count = 0;

    int fd = open(filename, O_RDONLY);
    if (fd == -1) {
        fprintf(stderr, "Error opening file '%s': %s\n", filename, strerror(errno));
        return 1;
    }

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

    // Set mmap flags conditionally to handle systems without MAP_POPULATE
    int flags = MAP_PRIVATE;
#ifdef MAP_POPULATE
    flags |= MAP_POPULATE;
#endif
    char *file_data = mmap(NULL, file_size, PROT_READ, flags, fd, 0);
    if (file_data == MAP_FAILED) {
        fprintf(stderr, "Error memory-mapping file: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    madvise(file_data, file_size, MADV_SEQUENTIAL);

    // Adaptive threading threshold
    int cpu_cores = sysconf(_SC_NPROCESSORS_ONLN);
    thread_count = (thread_count > cpu_cores) ? cpu_cores : thread_count;
    size_t dynamic_threshold = MIN_FILE_SIZE_FOR_THREADS * cpu_cores;

    if (file_size < dynamic_threshold || thread_count <= 1) {
        if (pattern_len < 3) {
            match_count = kmp_search(file_data, file_size, pattern, pattern_len, case_sensitive);
        } else if (pattern_len > 16) {
            match_count = rabin_karp_search(file_data, file_size, pattern, pattern_len, case_sensitive);
        } else {
#ifdef __AVX2__
            match_count = avx2_search(file_data, file_size, pattern, pattern_len, case_sensitive);
#elif defined(__SSE4_2__)
            match_count = simd_search(file_data, file_size, pattern, pattern_len, case_sensitive);
#else
            match_count = boyer_moore_search(file_data, file_size, pattern, pattern_len, case_sensitive);
#endif
        }
    } else {
        pthread_t *threads = malloc(thread_count * sizeof(pthread_t));
        search_job_t *jobs = malloc(thread_count * sizeof(search_job_t));
        if (!threads || !jobs) {
            fprintf(stderr, "Error allocating thread memory\n");
            munmap(file_data, file_size);
            close(fd);
            free(threads);
            free(jobs);
            return 1;
        }

        // Dynamic chunk sizing
        size_t chunk_size = file_size / thread_count;
        chunk_size = (chunk_size < CHUNK_SIZE) ? CHUNK_SIZE : chunk_size;

        for (int i = 0; i < thread_count; i++) {
            jobs[i].file_data = file_data;
            jobs[i].start_pos = i * chunk_size;
            jobs[i].end_pos = (i == thread_count - 1) ? file_size : (i + 1) * chunk_size;
            if (jobs[i].end_pos + pattern_len - 1 <= file_size) {
                jobs[i].end_pos += pattern_len - 1;
            }
            jobs[i].pattern = pattern;
            jobs[i].pattern_len = pattern_len;
            jobs[i].case_sensitive = case_sensitive;
            jobs[i].thread_id = i;
            jobs[i].local_count = 0;

            if (pthread_create(&threads[i], NULL, search_thread, &jobs[i]) != 0) {
                fprintf(stderr, "Error creating thread %d\n", i);
            }
        }

        for (int i = 0; i < thread_count; i++) {
            pthread_join(threads[i], NULL);
            match_count += jobs[i].local_count;
        }
        free(threads);
        free(jobs);
    }

    double end_time = get_time();
    double search_time = end_time - start_time;
    double mb_per_sec = file_size / (1024.0 * 1024.0) / search_time;

    // Output controlled by count_only parameter
    printf("Found %" PRIu64 " matches\n", match_count);
    if (!count_only) {
        printf("Search completed in %.4f seconds (%.2f MB/s)\n", search_time, mb_per_sec);
        printf("Search details:\n");
        printf("  - File size: %.2f MB\n", file_size / (1024.0 * 1024.0));
        printf("  - Pattern length: %zu characters\n", pattern_len);
#ifdef __AVX2__
        printf("  - Using AVX2 acceleration\n");
#elif defined(__SSE4_2__)
        printf("  - Using SSE4.2 acceleration\n");
#else
        printf("  - Using Boyer-Moore-Horspool algorithm\n");
#endif
        printf("  - %s search\n", case_sensitive ? "Case-sensitive" : "Case-insensitive");
    }

    munmap(file_data, file_size);
    close(fd);
    return 0;
}

/**
 * Print usage information
 */
void print_usage(const char *program_name) {
    printf("krep v%s - A high-performance string search utility\n\n", VERSION);
    printf("Usage: %s [OPTIONS] PATTERN [FILE]\n\n", program_name);
    printf("OPTIONS:\n");
    printf("  -i            Case-insensitive search\n");
    printf("  -c            Count matches only\n");
    printf("  -t NUM        Use NUM threads (default: %d)\n", DEFAULT_THREAD_COUNT);
    printf("  -s            Search within a string\n");
    printf("  -v            Display version\n");
    printf("  -h            Display this help\n\n");
    printf("EXAMPLES:\n");
    printf("  %s \"search term\" file.txt\n", program_name);
    printf("  %s -i -t 8 \"ERROR\" logfile.log\n", program_name);
    printf("  %s -s \"Hello\" \"Hello world\"\n", program_name);
}

/**
 * Main entry point
 */
int main(int argc, char *argv[]) {
    char *pattern = NULL, *filename = NULL, *input_string = NULL;
    bool case_sensitive = true, count_only = false, string_mode = false;
    int thread_count = DEFAULT_THREAD_COUNT, opt;

    while ((opt = getopt(argc, argv, "icvt:sh")) != -1) {
        switch (opt) {
            case 'i': case_sensitive = false; break;
            case 'c': count_only = true; break;
            case 't':
                thread_count = atoi(optarg);
                if (thread_count <= 0) thread_count = 1;
                break;
            case 's': string_mode = true; break;
            case 'v': printf("krep v%s\n", VERSION); return 0;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Error: Missing pattern\n");
        print_usage(argv[0]);
        return 1;
    }

    pattern = argv[optind++];
    if (strlen(pattern) > MAX_PATTERN_LENGTH) {
        fprintf(stderr, "Error: Pattern too long\n");
        return 1;
    }

    if (string_mode) {
        if (optind >= argc) {
            fprintf(stderr, "Error: Missing text\n");
            print_usage(argv[0]);
            return 1;
        }
        input_string = argv[optind];
        return search_string(pattern, input_string, case_sensitive);
    } else {
        if (optind >= argc) {
            fprintf(stderr, "Error: Missing file\n");
            print_usage(argv[0]);
            return 1;
        }
        filename = argv[optind];
        return search_file(filename, pattern, case_sensitive, count_only, thread_count);
    }
}
