#include "neverc/std/unique.h"
#include "neverc/std/_platform.h"
#include <stdlib.h>
#include <string.h>

/*
 * Intern table: open-addressing hash map with linear probing.
 * Each entry stores: kind, hash, data copy, data length.
 */

typedef enum { UK_EMPTY = 0, UK_STRING, UK_INT64, UK_UINT64, UK_BYTES } uk_kind_t;

typedef struct {
    uk_kind_t kind;
    uint64_t  hash;
    void     *data;
    size_t    len;
} intern_entry_t;

#define INTERN_INIT_CAP 256

static intern_entry_t *g_table = NULL;
static size_t          g_cap   = 0;
static size_t          g_count = 0;

#if defined(NEVERC_PLATFORM_WINDOWS)
#include <windows.h>
static CRITICAL_SECTION g_lock;
static int g_lock_init = 0;
#define LOCK()   do { if (!g_lock_init) { InitializeCriticalSection(&g_lock); g_lock_init = 1; } EnterCriticalSection(&g_lock); } while(0)
#define UNLOCK() LeaveCriticalSection(&g_lock)
#else
#include <pthread.h>
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
#define LOCK()   pthread_mutex_lock(&g_lock)
#define UNLOCK() pthread_mutex_unlock(&g_lock)
#endif

static uint64_t fnv64(const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ULL;
    }
    return h;
}

void neverc_unique_init(void) {
    LOCK();
    if (!g_table) {
        g_cap = INTERN_INIT_CAP;
        g_table = (intern_entry_t *)calloc(g_cap, sizeof(intern_entry_t));
        g_count = 0;
    }
    UNLOCK();
}

void neverc_unique_destroy(void) {
    LOCK();
    if (g_table) {
        for (size_t i = 0; i < g_cap; i++)
            if (g_table[i].kind != UK_EMPTY) free(g_table[i].data);
        free(g_table);
        g_table = NULL;
        g_cap = 0;
        g_count = 0;
    }
    UNLOCK();
}

static void grow_table(void) {
    size_t new_cap = g_cap * 2;
    intern_entry_t *new_tab = (intern_entry_t *)calloc(new_cap, sizeof(intern_entry_t));
    for (size_t i = 0; i < g_cap; i++) {
        if (g_table[i].kind == UK_EMPTY) continue;
        size_t idx = (size_t)(g_table[i].hash % new_cap);
        while (new_tab[idx].kind != UK_EMPTY) idx = (idx + 1) % new_cap;
        new_tab[idx] = g_table[i];
    }
    free(g_table);
    g_table = new_tab;
    g_cap = new_cap;
}

static int entry_matches(const intern_entry_t *e, uk_kind_t kind, uint64_t hash,
                          const void *data, size_t len) {
    return e->kind == kind && e->hash == hash && e->len == len &&
           memcmp(e->data, data, len) == 0;
}

static neverc_unique_handle_t intern(uk_kind_t kind, const void *data, size_t len) {
    neverc_unique_handle_t h = {NULL};
    if (!data && kind != UK_BYTES) return h;

    uint64_t hash = fnv64(data, len);

    LOCK();
    if (!g_table) {
        g_cap = INTERN_INIT_CAP;
        g_table = (intern_entry_t *)calloc(g_cap, sizeof(intern_entry_t));
    }

    size_t idx = (size_t)(hash % g_cap);
    while (g_table[idx].kind != UK_EMPTY) {
        if (entry_matches(&g_table[idx], kind, hash, data, len)) {
            h.ptr = g_table[idx].data;
            UNLOCK();
            return h;
        }
        idx = (idx + 1) % g_cap;
    }

    if ((g_count + 1) * 4 > g_cap * 3) {
        grow_table();
        idx = (size_t)(hash % g_cap);
        while (g_table[idx].kind != UK_EMPTY) idx = (idx + 1) % g_cap;
    }

    void *copy = malloc(len);
    memcpy(copy, data, len);
    g_table[idx].kind = kind;
    g_table[idx].hash = hash;
    g_table[idx].data = copy;
    g_table[idx].len = len;
    g_count++;

    h.ptr = copy;
    UNLOCK();
    return h;
}

neverc_unique_handle_t neverc_unique_make_string(const char *s) {
    if (!s) { neverc_unique_handle_t h = {NULL}; return h; }
    return intern(UK_STRING, s, strlen(s) + 1);
}

neverc_unique_handle_t neverc_unique_make_int64(int64_t v) {
    return intern(UK_INT64, &v, sizeof(v));
}

neverc_unique_handle_t neverc_unique_make_uint64(uint64_t v) {
    return intern(UK_UINT64, &v, sizeof(v));
}

neverc_unique_handle_t neverc_unique_make_bytes(const void *data, size_t len) {
    if (!data || len == 0) { neverc_unique_handle_t h = {NULL}; return h; }
    return intern(UK_BYTES, data, len);
}

const char *neverc_unique_string_value(neverc_unique_handle_t h) {
    return (const char *)h.ptr;
}

int64_t neverc_unique_int64_value(neverc_unique_handle_t h) {
    if (!h.ptr) return 0;
    int64_t v; memcpy(&v, h.ptr, sizeof(v)); return v;
}

uint64_t neverc_unique_uint64_value(neverc_unique_handle_t h) {
    if (!h.ptr) return 0;
    uint64_t v; memcpy(&v, h.ptr, sizeof(v)); return v;
}

const void *neverc_unique_bytes_value(neverc_unique_handle_t h, size_t *len) {
    if (!h.ptr) { if (len) *len = 0; return NULL; }
    if (len) {
        LOCK();
        for (size_t i = 0; i < g_cap; i++) {
            if (g_table[i].data == h.ptr) { *len = g_table[i].len; break; }
        }
        UNLOCK();
    }
    return h.ptr;
}

int neverc_unique_handle_equal(neverc_unique_handle_t a, neverc_unique_handle_t b) {
    return a.ptr == b.ptr;
}

int neverc_unique_handle_valid(neverc_unique_handle_t h) {
    return h.ptr != NULL;
}

size_t neverc_unique_count(void) {
    LOCK();
    size_t c = g_count;
    UNLOCK();
    return c;
}
