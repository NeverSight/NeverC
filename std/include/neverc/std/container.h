#ifndef NEVERC_CONTAINER_H
#define NEVERC_CONTAINER_H

/*
 * NeverC container — umbrella header for container submodules.
 */

#include "container/heap.h"
#include "container/list.h"
#include "container/ring.h"
#include "container/vector.h"

#ifdef __neverc__
struct __neverc_std_heap_t { char __tag; };
struct __neverc_std_list_t { char __tag; };
struct __neverc_std_ring_t { char __tag; };
struct __neverc_std_vector_t { char __tag; };

struct __neverc_std_container_t {
    struct __neverc_std_heap_t heap;
    struct __neverc_std_list_t list;
    struct __neverc_std_ring_t ring;
    struct __neverc_std_vector_t vector;
};
extern struct __neverc_std_container_t __neverc_mod_container;
extern struct __neverc_std_container_t container;
#endif

#endif
