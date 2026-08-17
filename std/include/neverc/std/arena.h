#ifndef NEVERC_ARENA_H
#define NEVERC_ARENA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * NeverC arena — bump-allocator-based memory arena.
 * C adaptation of Go's arena package.
 *
 * An arena allocates objects from large memory chunks.  When the arena
 * is freed every allocation made from it is released at once.  This is
 * far more efficient than individual malloc/free calls for short-lived
 * allocation bursts.
 *
 * Arenas are NOT thread-safe; callers must synchronise externally.
 */

typedef struct neverc_arena neverc_arena_t;

neverc_arena_t *neverc_arena_new(void);
void            neverc_arena_free(neverc_arena_t *a);
/* Reset releases every allocation from this arena. Pointers obtained
 * before reset must not be used. */
void            neverc_arena_reset(neverc_arena_t *a);

void *neverc_arena_alloc(neverc_arena_t *a, size_t size);
/* align==0 selects pointer alignment; otherwise align must be a power of two.
 * All allocation functions return NULL without changing arena state when a
 * size calculation overflows or the backing allocation fails. */
void *neverc_arena_alloc_aligned(neverc_arena_t *a, size_t size, size_t align);
void *neverc_arena_calloc(neverc_arena_t *a, size_t count, size_t size);
char *neverc_arena_strdup(neverc_arena_t *a, const char *s);
char *neverc_arena_strndup(neverc_arena_t *a, const char *s, size_t n);
void *neverc_arena_memdup(neverc_arena_t *a, const void *src, size_t len);

size_t neverc_arena_bytes_allocated(const neverc_arena_t *a);
size_t neverc_arena_num_chunks(const neverc_arena_t *a);

/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
struct __neverc_std_arena_t { char __tag; };
extern struct __neverc_std_arena_t __neverc_mod_arena;
extern struct __neverc_std_arena_t arena;
#endif

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_ARENA_H */
