/* aho_corasick.c - Memory-optimized implementation of Aho-Corasick algorithm
 *
 * This implementation uses a more memory-efficient representation for trie nodes
 * to reduce the 2KB overhead per node (256 pointers x 8 bytes on 64-bit systems).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h> // Add this for tolower()
#include "krep.h"

// Traditional implementation uses a full 256-pointer array for every node:
//   struct ac_node { struct ac_node *children[256]; ... }
// Which consumes 2KB per node on 64-bit systems.
//
// The optimized implementation uses a sparse representation with dynamic allocation:

// Maximum number of edges we expect per node (tunable)
#define AC_DEFAULT_CAPACITY 8

typedef struct ac_node_sparse
{
    // Sparse representation: only store used character transitions
    unsigned char *chars;         // Characters with transitions
    struct ac_node_sparse **next; // Corresponding next nodes
    size_t edge_count;            // Number of edges from this node
    size_t capacity;              // Current allocation size

    // Pattern matching info
    int pattern_index;           // Index of matched pattern (-1 if not a match)
    struct ac_node_sparse *fail; // Failure link
} ac_node_sparse_t;

// Root of the trie
typedef struct
{
    ac_node_sparse_t *root;
    size_t pattern_count;
} ac_automaton_t;

// Create a new node
static ac_node_sparse_t *ac_create_node(void)
{
    ac_node_sparse_t *node = calloc(1, sizeof(ac_node_sparse_t));
    if (!node)
    {
        perror("Error allocating AC node");
        return NULL;
    }

    // Start with a small capacity that grows as needed
    node->capacity = AC_DEFAULT_CAPACITY;
    node->chars = malloc(node->capacity * sizeof(unsigned char));
    node->next = malloc(node->capacity * sizeof(ac_node_sparse_t *));

    if (!node->chars || !node->next)
    {
        perror("Error allocating AC node arrays");
        free(node->chars);
        free(node->next);
        free(node);
        return NULL;
    }

    node->edge_count = 0;
    node->pattern_index = -1;
    node->fail = NULL;

    return node;
}

// Find child node for a given character, or NULL if not found
static ac_node_sparse_t *ac_find_child(ac_node_sparse_t *node, unsigned char c)
{
    for (size_t i = 0; i < node->edge_count; i++)
    {
        if (node->chars[i] == c)
        {
            return node->next[i];
        }
    }
    return NULL;
}

// Add a child node for the given character
static ac_node_sparse_t *ac_add_child(ac_node_sparse_t *node, unsigned char c)
{
    // Check if we need to resize
    if (node->edge_count >= node->capacity)
    {
        size_t new_capacity = node->capacity * 2;
        unsigned char *new_chars = realloc(node->chars, new_capacity * sizeof(unsigned char));
        ac_node_sparse_t **new_next = realloc(node->next, new_capacity * sizeof(ac_node_sparse_t *));

        if (!new_chars || !new_next)
        {
            perror("Error expanding AC node");
            free(new_chars); // Safe to call with NULL
            free(new_next);  // Safe to call with NULL
            return NULL;
        }

        node->chars = new_chars;
        node->next = new_next;
        node->capacity = new_capacity;
    }

    // Create the new node
    ac_node_sparse_t *child = ac_create_node();
    if (!child)
    {
        return NULL;
    }

    // Add to the sparse arrays
    node->chars[node->edge_count] = c;
    node->next[node->edge_count] = child;
    node->edge_count++;

    return child;
}

// Initialize the automaton
static ac_automaton_t *ac_create(void)
{
    ac_automaton_t *ac = malloc(sizeof(ac_automaton_t));
    if (!ac)
    {
        perror("Error allocating AC automaton");
        return NULL;
    }

    ac->root = ac_create_node();
    if (!ac->root)
    {
        free(ac);
        return NULL;
    }

    ac->pattern_count = 0;
    return ac;
}

// Add a pattern to the trie
static bool ac_add_pattern(ac_automaton_t *ac, const char *pattern, size_t pattern_len, int pattern_idx, bool case_sensitive)
{
    if (!ac || !pattern)
        return false;

    ac_node_sparse_t *current = ac->root;

    for (size_t i = 0; i < pattern_len; i++)
    {
        unsigned char c = (unsigned char)pattern[i];
        if (!case_sensitive)
        {
            c = tolower(c);
        }

        ac_node_sparse_t *next = ac_find_child(current, c);
        if (!next)
        {
            next = ac_add_child(current, c);
            if (!next)
            {
                return false; // Failed to add child
            }
        }
        current = next;
    }

    // Mark the node as a match point
    current->pattern_index = pattern_idx;
    ac->pattern_count++;

    return true;
}

// Build the failure function using BFS
static bool ac_build_failure_links(ac_automaton_t *ac, bool case_sensitive KREP_UNUSED)
{
    if (!ac || !ac->root)
        return false;

    // Initialize a queue for BFS
    ac_node_sparse_t **queue = malloc(10000 * sizeof(ac_node_sparse_t *)); // Adjust size as needed
    if (!queue)
    {
        perror("Failed to allocate BFS queue");
        return false;
    }

    size_t front = 0, rear = 0;

    // For depth 1 nodes, set failure to root
    for (size_t i = 0; i < ac->root->edge_count; i++)
    {
        ac_node_sparse_t *child = ac->root->next[i];
        child->fail = ac->root;
        queue[rear++] = child; // Enqueue child
    }

    // BFS to set failure links for remaining nodes
    while (front < rear)
    {
        ac_node_sparse_t *current = queue[front++];

        for (size_t i = 0; i < current->edge_count; i++)
        {
            unsigned char c = current->chars[i];
            ac_node_sparse_t *child = current->next[i];

            queue[rear++] = child; // Enqueue child

            // Find failure link for child
            ac_node_sparse_t *fail = current->fail;
            while (fail != ac->root && !ac_find_child(fail, c))
            {
                fail = fail->fail;
            }

            ac_node_sparse_t *fail_child = ac_find_child(fail, c);
            child->fail = fail_child ? fail_child : ac->root;
        }
    }

    free(queue);
    return true;
}

// Free the automaton
static void ac_free(ac_automaton_t *ac)
{
    if (!ac)
        return;

    // Use BFS to free all nodes
    ac_node_sparse_t **queue = malloc(10000 * sizeof(ac_node_sparse_t *)); // Adjust size as needed
    if (!queue)
    {
        perror("Failed to allocate free queue");
        return; // Memory leak, but better than crashing
    }

    size_t front = 0, rear = 0;
    queue[rear++] = ac->root;

    while (front < rear)
    {
        ac_node_sparse_t *current = queue[front++];

        // Queue all children
        for (size_t i = 0; i < current->edge_count; i++)
        {
            if (current->next[i])
            {
                queue[rear++] = current->next[i];
            }
        }

        // Free this node
        free(current->chars);
        free(current->next);
        free(current);
    }

    free(queue);
    free(ac);
}

// Declare the find_line functions that are used in this file
// These are defined in krep.c but need to be accessible here
extern size_t find_line_start(const char *text, size_t max_len, size_t pos);
extern size_t find_line_end(const char *text, size_t text_len, size_t pos);

// Search for patterns in text
uint64_t aho_corasick_search(const search_params_t *params, const char *text_start, size_t text_len, match_result_t *result)
{
    if (!params || !text_start)
        return 0;

    uint64_t match_count = 0;
    bool case_sensitive = params->case_sensitive;
    bool count_lines_mode = params->count_lines_mode;
    bool track_positions = params->track_positions;

    // Build the automaton
    ac_automaton_t *ac = ac_create();
    if (!ac)
    {
        fprintf(stderr, "Failed to create Aho-Corasick automaton\n");
        return 0;
    }

    // Add all patterns to the trie
    for (size_t i = 0; i < params->num_patterns; i++)
    {
        if (!ac_add_pattern(ac, params->patterns[i], params->pattern_lens[i], i, case_sensitive))
        {
            fprintf(stderr, "Failed to add pattern %zu to Aho-Corasick automaton\n", i);
            ac_free(ac);
            return 0;
        }
    }

    // Build failure links
    if (!ac_build_failure_links(ac, case_sensitive))
    {
        fprintf(stderr, "Failed to build Aho-Corasick failure links\n");
        ac_free(ac);
        return 0;
    }

    // Variables for line counting
    size_t last_counted_line_start = SIZE_MAX;

    // Search the text
    ac_node_sparse_t *current = ac->root;
    for (size_t i = 0; i < text_len; i++)
    {
        unsigned char c = (unsigned char)text_start[i];
        if (!case_sensitive)
        {
            c = tolower(c);
        }

        // Follow failure links until we find a node with a transition on c
        while (current != ac->root && !ac_find_child(current, c))
        {
            current = current->fail;
        }

        // Find the next state
        ac_node_sparse_t *next = ac_find_child(current, c);
        current = next ? next : ac->root;

        // Check for matches
        ac_node_sparse_t *temp = current;
        while (temp != ac->root)
        {
            if (temp->pattern_index != -1)
            {
                // Found a match
                int pattern_idx = temp->pattern_index;
                size_t pattern_len = params->pattern_lens[pattern_idx];
                size_t match_end = i + 1; // End position is exclusive
                size_t match_start = match_end - pattern_len;

                if (count_lines_mode)
                {
                    // Count lines mode: Only count each line once
                    size_t line_start = find_line_start(text_start, text_len, match_start);
                    if (line_start != last_counted_line_start)
                    {
                        match_count++;
                        last_counted_line_start = line_start;

                        // OPTIMIZATION: Jump to end of line
                        size_t line_end = find_line_end(text_start, text_len, line_start);
                        if (line_end < text_len)
                        {
                            i = line_end;
                            current = ac->root; // Reset state
                        }
                        break; // Exit the inner while loop
                    }
                }
                else
                {
                    // Normal match counting
                    match_count++;

                    // Track positions if requested
                    if (track_positions && result)
                    {
                        if (!match_result_add(result, match_start, match_end))
                        {
                            fprintf(stderr, "Warning: Failed to add match position (Aho-Corasick)\n");
                        }
                    }
                }
            }
            temp = temp->fail;
        }
    }

    // Cleanup
    ac_free(ac);
    return match_count;
}
