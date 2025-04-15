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
#include "aho_corasick.h"

// Aho-Corasick node structure
typedef struct ac_node
{
    struct ac_node *children[256]; // One child per possible character
    struct ac_node *failure;       // Failure link (used for jumping when character not found)
    int *output_indices;           // Indices of patterns ending at this node
    int num_outputs;               // Number of pattern indices
    int capacity;                  // Allocated capacity for outputs
} ac_node_t;

// Aho-Corasick trie structure
struct ac_trie
{
    ac_node_t *root;     // Root node of the trie
    size_t num_patterns; // Number of patterns in the trie
    bool case_sensitive; // Whether search is case-sensitive
};

// Create a new AC node
static ac_node_t *ac_node_create()
{
    ac_node_t *node = calloc(1, sizeof(ac_node_t));
    if (!node)
    {
        perror("Failed to allocate memory for Aho-Corasick node");
        return NULL;
    }
    // Initialize children array to NULL
    memset(node->children, 0, sizeof(node->children));

    // Initialize output_indices as NULL (allocated later if needed)
    node->output_indices = NULL;
    node->num_outputs = 0;
    node->capacity = 0;
    node->failure = NULL;

    return node;
}

// Add pattern index to node outputs
static bool ac_node_add_output(ac_node_t *node, int pattern_index)
{
    if (!node)
        return false;

    // Check if we need to allocate or resize
    if (node->num_outputs >= node->capacity)
    {
        int new_capacity = node->capacity == 0 ? 4 : node->capacity * 2;
        int *new_outputs = realloc(node->output_indices, new_capacity * sizeof(int));
        if (!new_outputs)
        {
            perror("Failed to resize Aho-Corasick node outputs");
            return false;
        }
        node->output_indices = new_outputs;
        node->capacity = new_capacity;
    }

    // Add the pattern index
    node->output_indices[node->num_outputs++] = pattern_index;
    return true;
}

// Free an AC node and all its children recursively
static void ac_node_free(ac_node_t *node)
{
    if (!node)
        return;

    // Recursively free children
    for (int i = 0; i < 256; i++)
    {
        if (node->children[i])
        {
            ac_node_free(node->children[i]);
        }
    }

    // Free outputs array if allocated
    if (node->output_indices)
    {
        free(node->output_indices);
    }

    // Free the node itself
    free(node);
}

// Build the Aho-Corasick Trie
ac_trie_t *ac_trie_build(const search_params_t *params)
{
    if (!params || params->num_patterns == 0)
    {
        return NULL;
    }

    // Allocate the trie structure
    ac_trie_t *trie = malloc(sizeof(ac_trie_t));
    if (!trie)
    {
        perror("Failed to allocate Aho-Corasick trie");
        return NULL;
    }

    // Create the root node
    trie->root = ac_node_create();
    if (!trie->root)
    {
        free(trie);
        return NULL;
    }

    trie->num_patterns = params->num_patterns;
    trie->case_sensitive = params->case_sensitive;

    // Build the trie - Insert all patterns
    for (size_t p = 0; p < params->num_patterns; p++)
    {
        const unsigned char *pattern = (const unsigned char *)params->patterns[p];
        size_t pattern_len = params->pattern_lens[p];

        if (pattern_len == 0)
            continue; // Skip empty patterns

        ac_node_t *current = trie->root;

        // Insert each character of the pattern
        for (size_t i = 0; i < pattern_len; i++)
        {
            unsigned char c = params->case_sensitive ? pattern[i] : tolower(pattern[i]);

            // Create child node if it doesn't exist
            if (!current->children[c])
            {
                current->children[c] = ac_node_create();
                if (!current->children[c])
                {
                    // Handle allocation failure
                    ac_trie_free(trie);
                    return NULL;
                }
            }

            // Advance to the child
            current = current->children[c];
        }

        // Mark this node with the pattern index (used later for output)
        ac_node_add_output(current, (int)p);
    }

    // Build failure links using BFS
    // Queue for BFS traversal
    ac_node_t **queue = malloc(trie->num_patterns * 256 * sizeof(ac_node_t *)); // Generous upper bound
    if (!queue)
    {
        ac_trie_free(trie);
        return NULL;
    }

    int queue_front = 0;
    int queue_rear = 0;

    // Set up the failure links for the first level (all point to root)
    for (int c = 0; c < 256; c++)
    {
        if (trie->root->children[c])
        {
            trie->root->children[c]->failure = trie->root;
            queue[queue_rear++] = trie->root->children[c]; // Add to queue
        }
    }

    // BFS to build failure links
    while (queue_front < queue_rear)
    {
        ac_node_t *current = queue[queue_front++]; // Dequeue

        // Process each child of current node
        for (int c = 0; c < 256; c++)
        {
            if (current->children[c])
            {
                ac_node_t *child = current->children[c];
                queue[queue_rear++] = child; // Add to queue

                // Find failure link for this child
                ac_node_t *failure = current->failure;

                // Keep following failure links until we find a node that has a child with the same character
                while (failure != trie->root && !failure->children[c])
                {
                    failure = failure->failure;
                }

                // Set the failure link for the child
                if (failure->children[c])
                {
                    child->failure = failure->children[c];

                    // Copy outputs from failure node to this node
                    for (int j = 0; j < child->failure->num_outputs; j++)
                    {
                        ac_node_add_output(child, child->failure->output_indices[j]);
                    }
                }
                else
                {
                    child->failure = trie->root;
                }
            }
        }
    }

    free(queue);
    return trie;
}

// Free the Aho-Corasick Trie
void ac_trie_free(ac_trie_t *trie)
{
    if (!trie)
        return;

    // Free the root node and all its children
    ac_node_free(trie->root);

    // Free the trie structure
    free(trie);
}

// Aho-Corasick search function
uint64_t aho_corasick_search(const search_params_t *params,
                             const char *text_start,
                             size_t text_len,
                             match_result_t *result)
{
    // Basic validation
    if (!params || params->num_patterns == 0 || text_len == 0)
    {
        return 0;
    }
    if (params->max_count == 0 && (params->count_lines_mode || params->track_positions))
    {
        return 0;
    }

    // Build the trie
    ac_trie_t *trie = ac_trie_build(params);
    if (!trie)
    {
        return 0; // Failed to build trie
    }

    uint64_t current_count = 0;
    bool count_lines_mode = params->count_lines_mode;
    bool track_positions = params->track_positions;
    size_t max_count = params->max_count;
    size_t last_counted_line_start = SIZE_MAX;

    // Search using the Aho-Corasick algorithm
    ac_node_t *current = trie->root;
    const unsigned char *text = (const unsigned char *)text_start;

    for (size_t i = 0; i < text_len; i++)
    {
        unsigned char c = trie->case_sensitive ? text[i] : tolower(text[i]);

        // Follow the trie nodes until we find a match for the current character
        while (current != trie->root && !current->children[c])
        {
            current = current->failure;
        }

        // Move to the next state
        if (current->children[c])
        {
            current = current->children[c];
        }

        // Check if the current node contains any pattern outputs
        if (current->num_outputs > 0)
        {
            // Process each pattern that ends at this position
            for (int j = 0; j < current->num_outputs; j++)
            {
                int pattern_idx = current->output_indices[j];
                size_t pattern_len = params->pattern_lens[pattern_idx];

                // Calculate the start position of the match
                size_t match_start = i - pattern_len + 1;
                size_t match_end = i + 1;

                // Whole word check
                if (params->whole_word && !is_whole_word_match((const char *)text_start, text_len, match_start, match_end))
                    continue;

                bool count_incremented_this_match = false;

                if (count_lines_mode)
                {
                    // Find the start of the line containing the match
                    size_t line_start = find_line_start(text_start, text_len, match_start);

                    // Only count each line once
                    if (line_start != last_counted_line_start)
                    {
                        current_count++;
                        last_counted_line_start = line_start;
                        count_incremented_this_match = true;
                    }
                }
                else
                {
                    // Count all matches
                    current_count++;
                    count_incremented_this_match = true;

                    // Add position to result if tracking
                    if (track_positions && result)
                    {
                        if (!match_result_add(result, match_start, match_end))
                        {
                            fprintf(stderr, "Warning: Failed to add match position in Aho-Corasick.\n");
                        }
                    }
                }

                // Check if we've reached the max count
                if (count_incremented_this_match && current_count >= max_count && max_count != SIZE_MAX)
                {
                    // Reached max_count, truncate the result if needed
                    if (track_positions && result && result->count > max_count)
                    {
                        result->count = max_count;
                    }
                    ac_trie_free(trie);
                    return current_count;
                }
            }
        }
    }

    ac_trie_free(trie);
    return current_count;
}
