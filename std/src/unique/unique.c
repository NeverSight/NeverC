#include "neverc/std/unique.h"
#include "neverc/std/_platform.h"
#include "../hash/_wyhash_final3.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/*
 * Intern table: open-addressing hash map with linear probing.
 * Each entry stores: kind, hash, data copy, data length.
 *
 * The capacity is always a power of two, so the probe wraps with a bit-mask
 * (`& (cap - 1)`) instead of a `% cap` division on every step — the same trick
 * the SwissTable in maps.c relies on.
 *
 * Each interned value is allocated as a [size_t len][data...] block. Public
 * handles contain a monotonically assigned opaque token rather than the data
 * address; accessors locate that token before reading intern storage. This both
 * rejects forged pointers and prevents a stale handle from becoming valid if
 * malloc reuses an address after destroy.
 */

typedef enum { UK_EMPTY = 0, UK_STRING, UK_INT64, UK_UINT64, UK_BYTES } uk_kind_t;

typedef struct {
    uk_kind_t kind;
    uint64_t  hash;
    void     *data;
    size_t    len;
    uintptr_t handle_token;
} intern_entry_t;

#define INTERN_INIT_CAP 256

static intern_entry_t *g_table = NULL;
static size_t          g_cap   = 0;
static size_t          g_count = 0;
static uintptr_t       g_next_handle_token = 1;
static int             g_handle_tokens_exhausted = 0;

#if defined(NEVERC_PLATFORM_WINDOWS)
#include <windows.h>
static SRWLOCK g_lock = SRWLOCK_INIT;
#define LOCK()   AcquireSRWLockExclusive(&g_lock)
#define UNLOCK() ReleaseSRWLockExclusive(&g_lock)
#else
#include <pthread.h>
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static void unique_lock(void) {
    while (pthread_mutex_lock(&g_lock) != 0) {
    }
}
#define LOCK()   unique_lock()
#define UNLOCK() pthread_mutex_unlock(&g_lock)
#endif

/* Hash only selects a probe bucket; entry_matches() re-verifies full equality. */
static uint64_t intern_hash(const void *data, size_t len) {
    return nci_wyhash_final3(data, len, 0);
}

/* Tokens are never dereferenced and never reused. Exhaustion permanently
 * fails closed instead of wrapping a stale public value back to life. */
static uintptr_t next_handle_token_locked(void) {
    uintptr_t token;
    if (g_handle_tokens_exhausted) return 0;
    token = g_next_handle_token;
    if (g_next_handle_token == UINTPTR_MAX)
        g_handle_tokens_exhausted = 1;
    else
        g_next_handle_token++;
    return token;
}

/*
 * Value storage: [size_t len][data bytes]. The returned/stored pointer is the
 * data. The prefix keeps empty values backed by a distinct non-NULL allocation
 * and lets intern_free recover the allocation base.
 */
static void *intern_alloc(const void *data, size_t len) {
    if (len > SIZE_MAX - sizeof(size_t)) return NULL;
    unsigned char *base = (unsigned char *)malloc(sizeof(size_t) + len);
    if (!base) return NULL;
    memcpy(base, &len, sizeof(size_t));
    if (len > 0) memcpy(base + sizeof(size_t), data, len);
    return base + sizeof(size_t);
}
static void intern_free(void *dataptr) {
    if (dataptr) free((unsigned char *)dataptr - sizeof(size_t));
}
void neverc_unique_init(void) {
    LOCK();
    if (!g_table) {
        g_table = (intern_entry_t *)calloc(INTERN_INIT_CAP, sizeof(intern_entry_t));
        g_cap = g_table ? INTERN_INIT_CAP : 0;   /* keep state consistent on OOM */
        g_count = 0;
    }
    UNLOCK();
}

void neverc_unique_destroy(void) {
    LOCK();
    if (g_table) {
        for (size_t i = 0; i < g_cap; i++)
            if (g_table[i].kind != UK_EMPTY) intern_free(g_table[i].data);
        free(g_table);
        g_table = NULL;
        g_cap = 0;
        g_count = 0;
    }
    UNLOCK();
}

/* Returns 1 on success, 0 on allocation failure (table left unchanged). */
static int grow_table(void) {
    if (g_cap == 0 || g_cap > SIZE_MAX / 2) return 0;
    size_t new_cap = g_cap * 2;                  /* stays a power of two */
    if (new_cap > SIZE_MAX / sizeof(intern_entry_t)) return 0;
    intern_entry_t *new_tab = (intern_entry_t *)calloc(new_cap, sizeof(intern_entry_t));
    if (!new_tab) return 0;
    size_t mask = new_cap - 1;
    for (size_t i = 0; i < g_cap; i++) {
        if (g_table[i].kind == UK_EMPTY) continue;
        size_t idx = (size_t)(g_table[i].hash & mask);
        while (new_tab[idx].kind != UK_EMPTY) idx = (idx + 1) & mask;
        new_tab[idx] = g_table[i];
    }
    free(g_table);
    g_table = new_tab;
    g_cap = new_cap;
    return 1;
}

static int entry_matches(const intern_entry_t *e, uk_kind_t kind, uint64_t hash,
                          const void *data, size_t len) {
    return e->kind == kind && e->hash == hash && e->len == len &&
           (len == 0 || memcmp(e->data, data, len) == 0);
}

static neverc_unique_handle_t intern_ok(uintptr_t token) {
    neverc_unique_handle_t h = {0};
    h.ptr = (const void *)token;
    return h;
}

static neverc_unique_handle_t intern(uk_kind_t kind, const void *data, size_t len) {
    neverc_unique_handle_t h = {0};
    if (!data && (kind != UK_BYTES || len != 0)) return h;

    uint64_t hash = intern_hash(data, len);

    LOCK();
    if (!g_table) {
        g_table = (intern_entry_t *)calloc(INTERN_INIT_CAP, sizeof(intern_entry_t));
        if (!g_table) { UNLOCK(); return h; }     /* OOM: invalid handle */
        g_cap = INTERN_INIT_CAP;
    }

    size_t mask = g_cap - 1;
    size_t idx = (size_t)(hash & mask);
    while (g_table[idx].kind != UK_EMPTY) {
        if (entry_matches(&g_table[idx], kind, hash, data, len)) {
            h = intern_ok(g_table[idx].handle_token);
            UNLOCK();
            return h;
        }
        idx = (idx + 1) & mask;
    }

    /* Grow at 75% load. */
    if (g_count == SIZE_MAX) {
        UNLOCK();
        return h;
    }
    if (g_count >= g_cap - g_cap / 4) {
        if (grow_table()) {
            mask = g_cap - 1;
            idx = (size_t)(hash & mask);
            while (g_table[idx].kind != UK_EMPTY) idx = (idx + 1) & mask;
        } else if (g_count >= g_cap - 1) {
            /* Grow failed under memory pressure and the table is otherwise
             * full: consuming the last EMPTY slot would make every future
             * probe loop (lookup and insert) non-terminating, so refuse the
             * insert instead. Below 100% the empty slot found above is still
             * valid, so only this exact corner is rejected. */
            UNLOCK();
            return h;   /* OOM: invalid handle */
        }
    }

    void *copy = intern_alloc(data, len);
    if (!copy) { UNLOCK(); return h; }            /* OOM: invalid handle */
    uintptr_t handle_token = next_handle_token_locked();
    if (handle_token == 0) {
        intern_free(copy);
        UNLOCK();
        return h;
    }
    g_table[idx].kind = kind;
    g_table[idx].hash = hash;
    g_table[idx].data = copy;
    g_table[idx].len = len;
    g_table[idx].handle_token = handle_token;
    g_count++;

    h = intern_ok(handle_token);
    UNLOCK();
    return h;
}

neverc_unique_handle_t neverc_unique_make_string(const char *s) {
    if (!s) { neverc_unique_handle_t h = {0}; return h; }
    size_t len = strlen(s);
    if (len == SIZE_MAX) { neverc_unique_handle_t h = {0}; return h; }
    return intern(UK_STRING, s, len + 1);
}

neverc_unique_handle_t neverc_unique_make_int64(int64_t v) {
    return intern(UK_INT64, &v, sizeof(v));
}

neverc_unique_handle_t neverc_unique_make_uint64(uint64_t v) {
    return intern(UK_UINT64, &v, sizeof(v));
}

neverc_unique_handle_t neverc_unique_make_bytes(const void *data, size_t len) {
    if (!data && len != 0) { neverc_unique_handle_t h = {0}; return h; }
    return intern(UK_BYTES, data, len);
}

/* Handles are public value structs, so callers can construct an arbitrary
 * token. Validate membership using integer equality only before reading the
 * associated entry or value. */
static const intern_entry_t *intern_entry_for_handle(
        neverc_unique_handle_t h) {
    uintptr_t token = (uintptr_t)h.ptr;
    if (token == 0 || !g_table)
        return NULL;
    for (size_t i = 0; i < g_cap; i++) {
        if (g_table[i].kind != UK_EMPTY &&
            g_table[i].handle_token == token)
            return &g_table[i];
    }
    return NULL;
}

static int intern_live(neverc_unique_handle_t h) {
    return intern_entry_for_handle(h) != NULL;
}

const char *neverc_unique_string_value(neverc_unique_handle_t h) {
    const char *result = NULL;
    LOCK();
    const intern_entry_t *entry = intern_entry_for_handle(h);
    if (entry) {
        size_t n = entry->len;
        if (n > 0) {
            const unsigned char *p =
                (const unsigned char *)entry->data;
            if (p[n - 1] == '\0')
                result = (const char *)entry->data;
        }
    }
    UNLOCK();
    return result;
}

int64_t neverc_unique_int64_value(neverc_unique_handle_t h) {
    int64_t v = 0;
    LOCK();
    const intern_entry_t *entry = intern_entry_for_handle(h);
    if (entry && entry->len == sizeof(v))
        memcpy(&v, entry->data, sizeof(v));
    UNLOCK();
    return v;
}

uint64_t neverc_unique_uint64_value(neverc_unique_handle_t h) {
    uint64_t v = 0;
    LOCK();
    const intern_entry_t *entry = intern_entry_for_handle(h);
    if (entry && entry->len == sizeof(v))
        memcpy(&v, entry->data, sizeof(v));
    UNLOCK();
    return v;
}

const void *neverc_unique_bytes_value(neverc_unique_handle_t h, size_t *len) {
    const void *result = NULL;
    size_t n = 0;
    LOCK();
    const intern_entry_t *entry = intern_entry_for_handle(h);
    if (entry) {
        n = entry->len;
        result = entry->data;
    }
    UNLOCK();
    if (len) *len = n;
    return result;
}

int neverc_unique_handle_equal(neverc_unique_handle_t a, neverc_unique_handle_t b) {
    return a.ptr == b.ptr;
}

int neverc_unique_handle_valid(neverc_unique_handle_t h) {
    LOCK();
    int ok = intern_live(h);
    UNLOCK();
    return ok;
}

size_t neverc_unique_count(void) {
    LOCK();
    size_t c = g_count;
    UNLOCK();
    return c;
}
