/*
 * NeverC maps — string-keyed hash map.
 * C adaptation of Go maps package.
 * Robin Hood open-addressing with stored hashes and backward-shift deletion.
 */

#include "neverc/std/maps.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAP_INIT_CAP 16
#define MAP_LOAD_NUM 3
#define MAP_LOAD_DEN 4

typedef struct {
    char    *key;
    void    *value;
    uint64_t hash;
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

#define HASH_EMPTY 0

static uint64_t fix_hash(uint64_t h) {
    return h ? h : 1;
}

static size_t probe_dist(size_t slot, uint64_t hash, size_t cap) {
    size_t natural = (size_t)(hash & (cap - 1));
    return (slot + cap - natural) & (cap - 1);
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
        if (m->entries[i].hash != HASH_EMPTY) free(m->entries[i].key);
    free(m->entries);
    free(m);
}

void neverc_maps_clear(neverc_map_t *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->entries[i].hash != HASH_EMPTY) {
            free(m->entries[i].key);
            m->entries[i].key = NULL;
        }
        m->entries[i].hash = HASH_EMPTY;
    }
    m->len = 0;
}

static void map_insert_entry(map_entry_t *entries, size_t cap,
                              char *key, void *value, uint64_t hash) {
    size_t idx = (size_t)(hash & (cap - 1));
    map_entry_t incoming = { key, value, hash };

    for (;;) {
        if (entries[idx].hash == HASH_EMPTY) {
            entries[idx] = incoming;
            return;
        }
        size_t existing_dist = probe_dist(idx, entries[idx].hash, cap);
        size_t incoming_dist = probe_dist(idx, incoming.hash, cap);
        if (incoming_dist > existing_dist) {
            map_entry_t tmp = entries[idx];
            entries[idx] = incoming;
            incoming = tmp;
        }
        idx = (idx + 1) & (cap - 1);
    }
}

static int map_grow(neverc_map_t *m) {
    size_t new_cap = m->cap * 2;
    map_entry_t *new_entries = (map_entry_t *)calloc(new_cap, sizeof(map_entry_t));
    if (!new_entries) return -1;

    for (size_t i = 0; i < m->cap; i++) {
        if (m->entries[i].hash == HASH_EMPTY) continue;
        map_insert_entry(new_entries, new_cap,
                         m->entries[i].key, m->entries[i].value, m->entries[i].hash);
    }
    free(m->entries);
    m->entries = new_entries;
    m->cap = new_cap;
    return 0;
}

int neverc_maps_set(neverc_map_t *m, const char *key, void *value) {
    if (!m || !key) return -1;
    if (m->len * MAP_LOAD_DEN >= m->cap * MAP_LOAD_NUM) {
        if (map_grow(m) < 0) return -1;
    }
    uint64_t h = fix_hash(hash_string(key));
    size_t idx = (size_t)(h & (m->cap - 1));

    for (;;) {
        if (m->entries[idx].hash == HASH_EMPTY) break;
        if (m->entries[idx].hash == h && strcmp(m->entries[idx].key, key) == 0) {
            m->entries[idx].value = value;
            return 0;
        }
        size_t existing_dist = probe_dist(idx, m->entries[idx].hash, m->cap);
        size_t incoming_dist = probe_dist(idx, h, m->cap);
        if (incoming_dist > existing_dist) break;
        idx = (idx + 1) & (m->cap - 1);
    }

    char *dup = strdup(key);
    if (!dup) return -1;

    if (m->entries[idx].hash == HASH_EMPTY) {
        m->entries[idx].key = dup;
        m->entries[idx].value = value;
        m->entries[idx].hash = h;
    } else {
        map_entry_t displaced = m->entries[idx];
        m->entries[idx].key = dup;
        m->entries[idx].value = value;
        m->entries[idx].hash = h;
        map_insert_entry(m->entries, m->cap,
                         displaced.key, displaced.value, displaced.hash);
    }
    m->len++;
    return 0;
}

void *neverc_maps_get(const neverc_map_t *m, const char *key) {
    if (!m || !key) return NULL;
    uint64_t h = fix_hash(hash_string(key));
    size_t idx = (size_t)(h & (m->cap - 1));
    for (size_t dist = 0; ; dist++, idx = (idx + 1) & (m->cap - 1)) {
        if (m->entries[idx].hash == HASH_EMPTY) return NULL;
        if (probe_dist(idx, m->entries[idx].hash, m->cap) < dist) return NULL;
        if (m->entries[idx].hash == h && strcmp(m->entries[idx].key, key) == 0)
            return m->entries[idx].value;
    }
}

int neverc_maps_has(const neverc_map_t *m, const char *key) {
    if (!m || !key) return 0;
    uint64_t h = fix_hash(hash_string(key));
    size_t idx = (size_t)(h & (m->cap - 1));
    for (size_t dist = 0; ; dist++, idx = (idx + 1) & (m->cap - 1)) {
        if (m->entries[idx].hash == HASH_EMPTY) return 0;
        if (probe_dist(idx, m->entries[idx].hash, m->cap) < dist) return 0;
        if (m->entries[idx].hash == h && strcmp(m->entries[idx].key, key) == 0)
            return 1;
    }
}

int neverc_maps_delete(neverc_map_t *m, const char *key) {
    if (!m || !key) return -1;
    uint64_t h = fix_hash(hash_string(key));
    size_t idx = (size_t)(h & (m->cap - 1));
    for (size_t dist = 0; ; dist++, idx = (idx + 1) & (m->cap - 1)) {
        if (m->entries[idx].hash == HASH_EMPTY) return -1;
        if (probe_dist(idx, m->entries[idx].hash, m->cap) < dist) return -1;
        if (m->entries[idx].hash == h && strcmp(m->entries[idx].key, key) == 0)
            break;
    }
    free(m->entries[idx].key);
    m->entries[idx].hash = HASH_EMPTY;
    m->entries[idx].key = NULL;
    m->entries[idx].value = NULL;
    m->len--;

    size_t vacant = idx;
    size_t next = (idx + 1) & (m->cap - 1);
    while (m->entries[next].hash != HASH_EMPTY) {
        if (probe_dist(next, m->entries[next].hash, m->cap) == 0) break;
        m->entries[vacant] = m->entries[next];
        m->entries[next].hash = HASH_EMPTY;
        m->entries[next].key = NULL;
        m->entries[next].value = NULL;
        vacant = next;
        next = (next + 1) & (m->cap - 1);
    }
    return 0;
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
        if (m->entries[i].hash != HASH_EMPTY) keys[k++] = m->entries[i].key;
    if (count) *count = k;
    return keys;
}

void **neverc_maps_values(const neverc_map_t *m, size_t *count) {
    if (!m || m->len == 0) { if (count) *count = 0; return NULL; }
    void **vals = (void **)malloc(m->len * sizeof(void *));
    if (!vals) { if (count) *count = 0; return NULL; }
    size_t k = 0;
    for (size_t i = 0; i < m->cap; i++)
        if (m->entries[i].hash != HASH_EMPTY) vals[k++] = m->entries[i].value;
    if (count) *count = k;
    return vals;
}

void neverc_maps_foreach(const neverc_map_t *m, neverc_maps_iter_func_t fn, void *user_data) {
    if (!m || !fn) return;
    for (size_t i = 0; i < m->cap; i++)
        if (m->entries[i].hash != HASH_EMPTY) fn(m->entries[i].key, m->entries[i].value, user_data);
}

void neverc_maps_delete_func(neverc_map_t *m, neverc_maps_filter_func_t fn) {
    if (!m || !fn || m->len == 0) return;
    size_t cap = m->len < 16 ? 16 : m->len;
    const char **keys = (const char **)malloc(cap * sizeof(const char *));
    if (!keys) return;
    size_t count = 0;
    for (size_t i = 0; i < m->cap; i++) {
        if (m->entries[i].hash != HASH_EMPTY &&
            fn(m->entries[i].key, m->entries[i].value)) {
            if (count >= cap) {
                cap *= 2;
                keys = (const char **)realloc(keys, cap * sizeof(const char *));
                if (!keys) return;
            }
            keys[count++] = m->entries[i].key;
        }
    }
    for (size_t i = 0; i < count; i++)
        neverc_maps_delete(m, keys[i]);
    free(keys);
}

neverc_map_t *neverc_maps_clone(const neverc_map_t *m) {
    if (!m) return NULL;
    neverc_map_t *c = neverc_maps_new();
    if (!c) return NULL;
    for (size_t i = 0; i < m->cap; i++)
        if (m->entries[i].hash != HASH_EMPTY)
            neverc_maps_set(c, m->entries[i].key, m->entries[i].value);
    return c;
}

int neverc_maps_equal(const neverc_map_t *a, const neverc_map_t *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->cap; i++) {
        if (a->entries[i].hash == HASH_EMPTY) continue;
        uint64_t h = a->entries[i].hash;
        const char *key = a->entries[i].key;
        size_t idx = (size_t)(h & (b->cap - 1));
        int found = 0;
        for (size_t dist = 0; ; dist++, idx = (idx + 1) & (b->cap - 1)) {
            if (b->entries[idx].hash == HASH_EMPTY) break;
            if (probe_dist(idx, b->entries[idx].hash, b->cap) < dist) break;
            if (b->entries[idx].hash == h && strcmp(b->entries[idx].key, key) == 0) {
                if (b->entries[idx].value != a->entries[i].value) return 0;
                found = 1;
                break;
            }
        }
        if (!found) return 0;
    }
    return 1;
}

void neverc_maps_copy(neverc_map_t *dst, const neverc_map_t *src) {
    if (!dst || !src) return;
    for (size_t i = 0; i < src->cap; i++)
        if (src->entries[i].hash != HASH_EMPTY)
            neverc_maps_set(dst, src->entries[i].key, src->entries[i].value);
}
