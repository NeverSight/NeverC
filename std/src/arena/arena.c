#include "neverc/std/arena.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define ARENA_DEFAULT_CHUNK 65536

typedef struct arena_chunk {
    struct arena_chunk *next;
    size_t cap;
    size_t used;
    /* data follows (flexible array member) */
    char data[];
} arena_chunk_t;

struct neverc_arena {
    arena_chunk_t *head;
    size_t total_alloc;
    size_t num_chunks;
};

static arena_chunk_t *chunk_new(size_t min_size) {
    size_t cap = min_size > ARENA_DEFAULT_CHUNK ? min_size : ARENA_DEFAULT_CHUNK;
    if (cap > SIZE_MAX - sizeof(arena_chunk_t)) return NULL;
    arena_chunk_t *c = (arena_chunk_t *)malloc(sizeof(arena_chunk_t) + cap);
    if (!c) return NULL;
    c->next = NULL;
    c->cap = cap;
    c->used = 0;
    return c;
}

neverc_arena_t *neverc_arena_new(void) {
    neverc_arena_t *a = (neverc_arena_t *)calloc(1, sizeof(*a));
    if (!a) return NULL;
    a->head = chunk_new(ARENA_DEFAULT_CHUNK);
    if (!a->head) { free(a); return NULL; }
    a->num_chunks = 1;
    return a;
}

void neverc_arena_free(neverc_arena_t *a) {
    if (!a) return;
    arena_chunk_t *c = a->head;
    while (c) {
        arena_chunk_t *next = c->next;
        free(c);
        c = next;
    }
    free(a);
}

void neverc_arena_reset(neverc_arena_t *a) {
    if (!a) return;
    arena_chunk_t *keep = a->head;
    arena_chunk_t *c = keep ? keep->next : NULL;
    while (c) {
        arena_chunk_t *next = c->next;
        free(c);
        c = next;
    }
    if (keep) { keep->used = 0; keep->next = NULL; }
    a->total_alloc = 0;
    a->num_chunks = keep ? 1 : 0;
}

static int aligned_offset(const char *base, size_t used, size_t al,
                          size_t *result) {
    uintptr_t address = (uintptr_t)base;
    if (used > UINTPTR_MAX - address) return -1;
    uintptr_t current = address + (uintptr_t)used;
    size_t pad = (al - ((size_t)current & (al - 1))) & (al - 1);
    if (pad > SIZE_MAX - used) return -1;
    *result = used + pad;
    return 0;
}

void *neverc_arena_alloc_aligned(neverc_arena_t *a, size_t size, size_t al) {
    if (!a || size == 0) return NULL;
    if (al == 0) al = sizeof(void *);
    if ((al & (al - 1)) != 0 || al > UINTPTR_MAX || !a->head ||
        size > SIZE_MAX - a->total_alloc)
        return NULL;

    arena_chunk_t *c = a->head;
    size_t off;
    if (c->used <= c->cap &&
        aligned_offset(c->data, c->used, al, &off) == 0 &&
        off <= c->cap && size <= c->cap - off) {
        c->used = off + size;
        a->total_alloc += size;
        return c->data + off;
    }

    if (al - 1 > SIZE_MAX - size || a->num_chunks == SIZE_MAX) return NULL;
    size_t need = size + al - 1;
    arena_chunk_t *nc = chunk_new(need);
    if (!nc) return NULL;
    if (aligned_offset(nc->data, 0, al, &off) != 0 ||
        off > nc->cap || size > nc->cap - off) {
        free(nc);
        return NULL;
    }
    nc->next = a->head;
    a->head = nc;
    a->num_chunks++;

    nc->used = off + size;
    a->total_alloc += size;
    return nc->data + off;
}

void *neverc_arena_alloc(neverc_arena_t *a, size_t size) {
    return neverc_arena_alloc_aligned(a, size, sizeof(void *));
}

void *neverc_arena_calloc(neverc_arena_t *a, size_t count, size_t size) {
    if (size != 0 && count > SIZE_MAX / size) return NULL;
    size_t total = count * size;
    void *p = neverc_arena_alloc(a, total);
    if (p) memset(p, 0, total);
    return p;
}

char *neverc_arena_strdup(neverc_arena_t *a, const char *s) {
    if (!s) return NULL;
    size_t len = strlen(s);
    if (len == SIZE_MAX) return NULL;
    char *p = (char *)neverc_arena_alloc_aligned(a, len + 1, 1);
    if (p) memcpy(p, s, len + 1);
    return p;
}

char *neverc_arena_strndup(neverc_arena_t *a, const char *s, size_t n) {
    if (!s) return NULL;
    size_t len = 0;
    while (len < n && s[len]) len++;
    if (len == SIZE_MAX) return NULL;
    char *p = (char *)neverc_arena_alloc_aligned(a, len + 1, 1);
    if (p) { memcpy(p, s, len); p[len] = '\0'; }
    return p;
}

void *neverc_arena_memdup(neverc_arena_t *a, const void *src, size_t len) {
    if (!src || len == 0) return NULL;
    void *p = neverc_arena_alloc(a, len);
    if (p) memcpy(p, src, len);
    return p;
}

size_t neverc_arena_bytes_allocated(const neverc_arena_t *a) {
    return a ? a->total_alloc : 0;
}

size_t neverc_arena_num_chunks(const neverc_arena_t *a) {
    return a ? a->num_chunks : 0;
}
