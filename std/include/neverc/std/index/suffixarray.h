#ifndef NEVERC_INDEX_SUFFIXARRAY_H
#define NEVERC_INDEX_SUFFIXARRAY_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const unsigned char *data;
    size_t               data_len;
    int32_t             *sa;
    size_t               sa_len;
    /* LCP-LR arrays for O(m + log n) pattern search (Manber & Myers). Indexed by
     * the midpoint of each binary-search interval; NULL if construction ran out
     * of memory, in which case queries fall back to a plain binary search. */
    int32_t             *llcp;
    int32_t             *rlcp;
} neverc_suffixarray_t;

int  neverc_suffixarray_new(neverc_suffixarray_t *idx, const unsigned char *data, size_t len);
void neverc_suffixarray_free(neverc_suffixarray_t *idx);

int  neverc_suffixarray_lookup(const neverc_suffixarray_t *idx,
                               const unsigned char *pattern, size_t pat_len,
                               int32_t *results, size_t max_results,
                               size_t *nresults);

size_t neverc_suffixarray_count(const neverc_suffixarray_t *idx,
                                const unsigned char *pattern, size_t pat_len);

int  neverc_suffixarray_at(const neverc_suffixarray_t *idx, size_t i);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/index.h>
#endif


#endif
