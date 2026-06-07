#ifndef NEVERC_INDEX_H
#define NEVERC_INDEX_H

/*
 * NeverC index — umbrella header for index submodules.
 */

#include "index/suffixarray.h"

#ifdef __neverc__
struct __neverc_std_suffixarray_t { char __tag; };

struct __neverc_std_index_t {
    struct __neverc_std_suffixarray_t suffixarray;
};
extern struct __neverc_std_index_t __neverc_mod_index;
extern struct __neverc_std_index_t index_mod;
#endif

#endif /* NEVERC_INDEX_H */
