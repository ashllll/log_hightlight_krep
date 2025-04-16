/**
 * Test suite for krep directory search functions
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>

/* Define TESTING before including headers if not done by Makefile */
#ifndef TESTING
#define TESTING
#endif

#include "../krep.h"

// Constants definitions
#define TEST_DIR_BASE "/tmp/krep_test_dir"
#define TEST_FILE_MAX_SIZE 8192

// Function declarations
static void create_test_directory_structure(void);
static void cleanup_test_directory_structure(void);
static void create_binary_file(const char *path);
static void create_text_file(const char *path, const char *content);
static void create_nested_directory(const char *base_path, int depth, int max_depth);

/**
 * Nested directory search test
 */
void test_recursive_directory_search(void)
{
    printf("\n=== Testing Recursive Directory Search ===\n");

    // Setup: Create test directory structure
    create_test_directory_structure();

    // Configure basic search parameters
    search_params_t params = {0};
    params.patterns = malloc(sizeof(char *));
    params.pattern_lens = malloc(sizeof(size_t));
    if (!params.patterns || !params.pattern_lens)
    {
        fprintf(stderr, "Failed to allocate memory for test parameters\n");
        cleanup_test_directory_structure();
        return;
    }

    // Pattern that exists only in some files
    const char *test_pattern = "FINDME";
    params.patterns[0] = (char *)test_pattern;
    params.pattern_lens[0] = strlen(test_pattern);
    params.num_patterns = 1;
    params.case_sensitive = true;
    params.use_regex = false;
    params.count_lines_mode = false;
    params.track_positions = true;
    params.max_count = SIZE_MAX;

    // Execute recursive search on the test directory
    printf("Testing recursive search for pattern '%s'...\n", test_pattern);

    // Reset global_match_found_flag
    extern atomic_bool global_match_found_flag;
    atomic_store(&global_match_found_flag, false);

    int errors = search_directory_recursive(TEST_DIR_BASE, &params, 1);

    // Verify results
    printf("Recursive search completed with %d errors\n", errors);
    bool matches_found = atomic_load(&global_match_found_flag);
    printf("Matches found: %s\n", matches_found ? "YES" : "NO");

    if (errors > 0)
    {
        printf("FAIL: Recursive search reported errors\n");
    }
    else if (!matches_found)
    {
        printf("FAIL: Recursive search didn't find expected matches\n");
    }
    else
    {
        printf("PASS: Recursive search found matches without errors\n");
    }

    // Cleanup
    free(params.patterns);
    free(params.pattern_lens);
    cleanup_test_directory_structure();
}

/**
 * Binary file handling test
 */
void test_binary_file_handling(void)
{
    printf("\n=== Testing Binary File Handling ===\n");

    // Create a test binary file
    const char *binary_file_path = "/tmp/krep_test_binary.bin";
    create_binary_file(binary_file_path);

    // Configure search parameters
    search_params_t params = {0};
    params.patterns = malloc(sizeof(char *));
    params.pattern_lens = malloc(sizeof(size_t));
    if (!params.patterns || !params.pattern_lens)
    {
        fprintf(stderr, "Failed to allocate memory for test parameters\n");
        unlink(binary_file_path);
        return;
    }

    // Pattern that might exist in the binary file (we'll search for 'AB' which will be present)
    const char *test_pattern = "AB";
    params.patterns[0] = (char *)test_pattern;
    params.pattern_lens[0] = strlen(test_pattern);
    params.num_patterns = 1;
    params.case_sensitive = true;
    params.use_regex = false;
    params.count_lines_mode = false;
    params.track_positions = true;
    params.max_count = SIZE_MAX;

    // Execute search on the binary file
    printf("Testing search on binary file...\n");
    int result = search_file(&params, binary_file_path, 1);

    // Verify results (should identify the file as binary or handle it appropriately)
    printf("Binary file search resulted in code: %d\n", result);
    printf("Note: We expect proper handling, either by skipping as binary or by searching safely.\n");

    // Cleanup
    free(params.patterns);
    free(params.pattern_lens);
    unlink(binary_file_path);
}

/**
 * Main function for test execution
 */
int main(void)
{
    printf("Starting Directory and Binary File Tests\n");

    // Verify permissions to create test directories and files
    if (access("/tmp", W_OK) != 0)
    {
        fprintf(stderr, "Cannot write to /tmp, skipping tests\n");
        return 1;
    }

    // Run tests
    test_recursive_directory_search();
    test_binary_file_handling();

    printf("\nAll directory and binary file tests completed\n");
    return 0;
}

/* ========================================================================= */
/* Test support functions                                                    */
/* ========================================================================= */

/**
 * Creates a test directory structure
 */
static void create_test_directory_structure(void)
{
    printf("Creating test directory structure at %s\n", TEST_DIR_BASE);

    // Remove any existing directories
    cleanup_test_directory_structure();

    // Create base directory
    mkdir(TEST_DIR_BASE, 0755);

    // Create regular test files
    create_text_file(TEST_DIR_BASE "/file1.txt", "This is a text file\nIt has the pattern FINDME here\nAnd more text");
    create_text_file(TEST_DIR_BASE "/file2.txt", "This file doesn't have the pattern\nJust normal text");
    create_text_file(TEST_DIR_BASE "/file3.log", "Log file with FINDME pattern\nMultiple times FINDME");

    // Create symbolic link to test that it's handled correctly
    symlink(TEST_DIR_BASE "/file1.txt", TEST_DIR_BASE "/link_to_file1.txt");

    // Create directories to skip (should_skip_directory)
    mkdir(TEST_DIR_BASE "/.git", 0755);
    create_text_file(TEST_DIR_BASE "/.git/file_in_git.txt", "This has FINDME but should be skipped");

    mkdir(TEST_DIR_BASE "/node_modules", 0755);
    create_text_file(TEST_DIR_BASE "/node_modules/file_in_modules.txt", "This has FINDME but should be skipped");

    // Create nested directories
    create_nested_directory(TEST_DIR_BASE, 0, 3);

    // Create files with extensions to skip
    create_binary_file(TEST_DIR_BASE "/binary.exe");
    create_text_file(TEST_DIR_BASE "/minified.min.js", "function minified(){console.log('FINDME')}");
    create_text_file(TEST_DIR_BASE "/image.jpg", "Not a real image but should be skipped FINDME");

    printf("Test directory structure created\n");
}

/**
 * Cleans up the test directory structure
 */
static void cleanup_test_directory_structure(void)
{
    char cmd[256];
    snprintf(cmd, sizeof(cmd), "rm -rf %s", TEST_DIR_BASE);
    system(cmd);
}

/**
 * Creates a test binary file
 */
static void create_binary_file(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f)
    {
        fprintf(stderr, "Failed to create binary file: %s\n", path);
        return;
    }

    // Create binary data with NULL bytes and recognizable patterns
    unsigned char data[TEST_FILE_MAX_SIZE];
    for (int i = 0; i < TEST_FILE_MAX_SIZE; i++)
    {
        if (i % 32 == 0)
        {
            data[i] = 0; // NULL byte every 32 bytes
        }
        else if (i % 128 < 2)
        {
            data[i] = 'A' + (i % 2); // Insert "AB" every 128 bytes
        }
        else
        {
            data[i] = (i % 95) + 32; // Printable ASCII characters
        }
    }

    fwrite(data, 1, TEST_FILE_MAX_SIZE, f);
    fclose(f);
}

/**
 * Creates a text file with specific content
 */
static void create_text_file(const char *path, const char *content)
{
    FILE *f = fopen(path, "w");
    if (!f)
    {
        fprintf(stderr, "Failed to create text file: %s\n", path);
        return;
    }

    fputs(content, f);
    fclose(f);
}

/**
 * Creates nested directories with test files
 */
static void create_nested_directory(const char *base_path, int depth, int max_depth)
{
    if (depth >= max_depth)
        return;

    // Create 2 subdirectories for each level
    for (int i = 1; i <= 2; i++)
    {
        char subdir_path[PATH_MAX];
        snprintf(subdir_path, sizeof(subdir_path), "%s/level%d_%d", base_path, depth, i);

        mkdir(subdir_path, 0755);

        // Create files in this directory
        char file_path[PATH_MAX];
        snprintf(file_path, sizeof(file_path), "%s/file_level%d_%d.txt", subdir_path, depth, i);

        // Put the pattern only in files at even levels
        if (depth % 2 == 0)
        {
            create_text_file(file_path, "This file has FINDME pattern in nested directory");
        }
        else
        {
            create_text_file(file_path, "This file doesn't have the pattern");
        }

        // Recursion
        create_nested_directory(subdir_path, depth + 1, max_depth);
    }
}
