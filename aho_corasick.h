/**
 * Aho-Corasick algorithm for multiple pattern search.
 * This header declares the Aho-Corasick trie structure and functions.
 */

#ifndef AHO_CORASICK_H
#define AHO_CORASICK_H

#include "krep.h" // Include main header for search_params_t and other types

/**
 * Aho-Corasick trie structure (opaque type)
 */
typedef struct ac_trie ac_trie_t;

/**
 * Build an Aho-Corasick trie from the patterns in search_params
 *
 * @param params Search parameters containing patterns
 * @return A new Aho-Corasick trie, or NULL on failure
 */
ac_trie_t *ac_trie_build(const search_params_t *params);

/**
 * Free an Aho-Corasick trie
 *
 * @param trie The trie to free
 */
void ac_trie_free(ac_trie_t *trie);

/**
 * Search for all patterns in the text using the Aho-Corasick algorithm
 *
 * @param params Search parameters including patterns and options
 * @param text_start Pointer to the start of the text
 * @param text_len Length of the text
 * @param result Match result structure to store positions (if track_positions is true)
 * @return The number of matches found (or lines matching if count_lines_mode is true)
 */
uint64_t aho_corasick_search(const search_params_t *params,
                             const char *text_start,
                             size_t text_len,
                             match_result_t *result);

#endif /* AHO_CORASICK_H */
