/*
 * NeverC maps — string-keyed hash map.
 * C adaptation of Go maps package.
 * Open-addressing hash table with Robin Hood hashing.
 */

#include "neverc/std/maps.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAP_INIT_CAP 16
#define MAP_LOAD_FACTOR 0.75

typedef struct {
    char *key;
    void *value;
    int   occupied;
} map_entry_t;

struct neverc_map {
    map_entry_t *entries;
    size_t       cap;
    size_t       len;
};

static uint64_t hash_string(const char *s) {
    uint64_t h = 14695981039346656037ULL;
    while (*s) {
        h ^= (uint64_t)(unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

neverc_map_t *neverc_maps_new(void) {
    neverc_map_t *m = (neverc_map_t *)calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->cap = MAP_INIT_CAP;
    m->entries = (map_entry_t *)calloc(m->cap, sizeof(map_entry_t));
    return m;
}

void neverc_maps_free(neverc_map_t *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; i++)
        if (m->entries[i].occupied) free(m->entries[i].key);
    free(m->entries);
    free(m);
}

void neverc_maps_clear(neverc_map_t *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->entries[i].occupied) { free(m->entries[i].key); m->entries[i].key = NULL; }
        m->entries[i].occupied = 0;
    }
    m->len = 0;
}

static int map_grow(neverc_map_t *m) {
    size_t new_cap = m->cap * 2;
    map_entry_t *new_entries = (map_entry_t *)calloc(new_cap, sizeof(map_entry_t));
    if (!new_entries) return -1;

    for (size_t i = 0; i < m->cap; i++) {
        if (!m->entries[i].occupied) continue;
        uint64_t h = hash_string(m->entries[i].key);
        size_t idx = (size_t)(h & (new_cap - 1));
        while (new_entries[idx].occupied)
            idx = (idx + 1) & (new_cap - 1);
        new_entries[idx] = m->entries[i];
    }
    free(m->entries);
    m->entries = new_entries;
    m->cap = new_cap;
    return 0;
}

int neverc_maps_set(neverc_map_t *m, const char *key, void *value) {
    if (!m || !key) return -1;
    if ((double)(m->len + 1) > (double)m->cap * MAP_LOAD_FACTOR) {
        if (map_grow(m) < 0) return -1;
    }
    uint64_t h = hash_string(key);
    size_t idx = (size_t)(h & (m->cap - 1));
    while (m->entries[idx].occupied) {
        if (strcmp(m->entries[idx].key, key) == 0) {
            m->entries[idx].value = value;
            return 0;
        }
        idx = (idx + 1) & (m->cap - 1);
    }
    m->entries[idx].key = strdup(key);
    if (!m->entries[idx].key) return -1;
    m->entries[idx].value = value;
    m->entries[idx].occupied = 1;
    m->len++;
    return 0;
}

void *neverc_maps_get(const neverc_map_t *m, const char *key) {
    if (!m || !key) return NULL;
    uint64_t h = hash_string(key);
    size_t idx = (size_t)(h & (m->cap - 1));
    while (m->entries[idx].occupied) {
        if (strcmp(m->entries[idx].key, key) == 0) return m->entries[idx].value;
        idx = (idx + 1) & (m->cap - 1);
    }
    return NULL;
}

int neverc_maps_has(const neverc_map_t *m, const char *key) {
    if (!m || !key) return 0;
    uint64_t h = hash_string(key);
    size_t idx = (size_t)(h & (m->cap - 1));
    while (m->entries[idx].occupied) {
        if (strcmp(m->entries[idx].key, key) == 0) return 1;
        idx = (idx + 1) & (m->cap - 1);
    }
    return 0;
}

int neverc_maps_delete(neverc_map_t *m, const char *key) {
    if (!m || !key) return -1;
    uint64_t h = hash_string(key);
    size_t idx = (size_t)(h & (m->cap - 1));
    while (m->entries[idx].occupied) {
        if (strcmp(m->entries[idx].key, key) == 0) {
            free(m->entries[idx].key);
            m->entries[idx].key = NULL;
            m->entries[idx].value = NULL;
            m->entries[idx].occupied = 0;
            m->len--;
            /* Backward-shift deletion: move subsequent entries back to fill
               the gap.  This avoids calling neverc_maps_set (which could
               trigger map_grow and invalidate the entries pointer). */
            size_t vacant = idx;
            size_t next = (idx + 1) & (m->cap - 1);
            while (m->entries[next].occupied) {
                uint64_t nh = hash_string(m->entries[next].key);
                size_t natural = (size_t)(nh & (m->cap - 1));
                /* If this entry's natural slot is at or before the vacant
                   slot (accounting for wrap-around), shift it back. */
                int should_move;
                if (vacant <= next)
                    should_move = (natural <= vacant || natural > next);
                else
                    should_move = (natural <= vacant && natural > next);
                if (should_move) {
                    m->entries[vacant] = m->entries[next];
                    m->entries[next].key = NULL;
                    m->entries[next].value = NULL;
                    m->entries[next].occupied = 0;
                    vacant = next;
                }
                next = (next + 1) & (m->cap - 1);
            }
            return 0;
        }
        idx = (idx + 1) & (m->cap - 1);
    }
    return -1;
}

size_t neverc_maps_len(const neverc_map_t *m) {
    return m ? m->len : 0;
}

char **neverc_maps_keys(const neverc_map_t *m, size_t *count) {
    if (!m || m->len == 0) { if (count) *count = 0; return NULL; }
    char **keys = (char **)malloc(m->len * sizeof(char *));
    if (!keys) { if (count) *count = 0; return NULL; }
    size_t k = 0;
    for (size_t i = 0; i < m->cap; i++)
        if (m->entries[i].occupied) keys[k++] = m->entries[i].key;
    if (count) *count = k;
    return keys;
}

void **neverc_maps_values(const neverc_map_t *m, size_t *count) {
    if (!m || m->len == 0) { if (count) *count = 0; return NULL; }
    void **vals = (void **)malloc(m->len * sizeof(void *));
    if (!vals) { if (count) *count = 0; return NULL; }
    size_t k = 0;
    for (size_t i = 0; i < m->cap; i++)
        if (m->entries[i].occupied) vals[k++] = m->entries[i].value;
    if (count) *count = k;
    return vals;
}

void neverc_maps_foreach(const neverc_map_t *m, neverc_maps_iter_func_t fn, void *user_data) {
    if (!m || !fn) return;
    for (size_t i = 0; i < m->cap; i++)
        if (m->entries[i].occupied) fn(m->entries[i].key, m->entries[i].value, user_data);
}

void neverc_maps_delete_func(neverc_map_t *m, neverc_maps_filter_func_t fn) {
    if (!m || !fn) return;
    size_t del_cap = m->len < 16 ? 16 : m->len;
    char **to_delete = (char **)malloc(del_cap * sizeof(char *));
    size_t del_count = 0;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->entries[i].occupied && fn(m->entries[i].key, m->entries[i].value)) {
            if (del_count >= del_cap) {
                del_cap *= 2;
                to_delete = (char **)realloc(to_delete, del_cap * sizeof(char *));
            }
            to_delete[del_count++] = strdup(m->entries[i].key);
        }
    }
    for (size_t i = 0; i < del_count; i++) {
        neverc_maps_delete(m, to_delete[i]);
        free(to_delete[i]);
    }
    free(to_delete);
}

neverc_map_t *neverc_maps_clone(const neverc_map_t *m) {
    if (!m) return NULL;
    neverc_map_t *c = neverc_map_new();
    for (size_t i = 0; i < m->cap; i++)
        if (m->entries[i].occupied)
            neverc_maps_set(c, m->entries[i].key, m->entries[i].value);
    return c;
}

int neverc_maps_equal(const neverc_map_t *a, const neverc_map_t *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->cap; i++) {
        if (!a->entries[i].occupied) continue;
        if (!neverc_maps_has(b, a->entries[i].key)) return 0;
        if (neverc_maps_get(b, a->entries[i].key) != a->entries[i].value) return 0;
    }
    return 1;
}

void neverc_maps_copy(neverc_map_t *dst, const neverc_map_t *src) {
    if (!dst || !src) return;
    for (size_t i = 0; i < src->cap; i++)
        if (src->entries[i].occupied)
            neverc_maps_set(dst, src->entries[i].key, src->entries[i].value);
}
