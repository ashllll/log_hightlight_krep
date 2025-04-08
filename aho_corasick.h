/* aho_corasick.h - Header for memory-optimized implementation of Aho-Corasick algorithm
 *
 * This file declares the interface for the Aho-Corasick string matching algorithm
 * used in krep for efficient multiple pattern searching.
 */

#ifndef KREP_AHO_CORASICK_H
#define KREP_AHO_CORASICK_H

#include <stdint.h>  // For uint64_t
#include <stddef.h>  // For size_t
#include <stdbool.h> // For bool
#include "krep.h"    // For search_params_t and match_result_t

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief Search for multiple patterns in text using the Aho-Corasick algorithm
     *
     * This function builds an Aho-Corasick automaton from the patterns provided in params
     * and then searches the text for all occurrences of these patterns.
     *
     * @param params Search parameters including patterns and options
     * @param text_start Pointer to the start of the text to search
     * @param text_len Length of the text to search
     * @param result Optional pointer to match_result_t structure to store match positions
     * @return Number of matches found
     */
    uint64_t aho_corasick_search(const search_params_t *params,
                                 const char *text_start,
                                 size_t text_len,
                                 match_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* KREP_AHO_CORASICK_H */
