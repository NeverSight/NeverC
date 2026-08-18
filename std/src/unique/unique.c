#include "neverc/std/unique.h"
#include "neverc/std/_platform.h"
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
 * Each interned value is allocated as a [size_t len][data...] block and the
 * handle points at the data, so a handle can recover its own length in O(1)
 * (header read) instead of scanning the whole table.
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
static uint64_t        g_epoch = 1;

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

/*
 * wyhash (final v3) over (data, len) — the same hash maps.c and hash/maphash
 * use, so the whole std library now shares one hash. It replaces a byte-at-a-time
 * FNV-1a: wyhash folds 8 bytes per step (and a 48-byte stride past 48 bytes), so
 * interning long byte slices is dramatically faster, and its stronger mixing
 * spreads keys better across the linear-probe table (fewer collisions). The hash
 * only picks a bucket and is re-verified by a full memcmp in entry_matches(), so
 * swapping the function cannot change which values are considered equal.
 *
 * Reads go through memcpy (no unaligned-access UB) and nci_wymix has a portable
 * 64x64 fallback when __int128 is unavailable, so this is identical on
 * Windows / Linux / macOS / Android / iOS. The table lives only in process
 * memory and is never serialized, so endianness affecting the hash is harmless.
 */
static inline uint64_t nci_read8(const uint8_t *p) {
    uint64_t v; memcpy(&v, p, 8); return v;
}
static inline uint64_t nci_read4(const uint8_t *p) {
    uint32_t v; memcpy(&v, p, 4); return (uint64_t)v;
}
static inline uint64_t nci_wymix(uint64_t a, uint64_t b) {
#ifdef __SIZEOF_INT128__
    __uint128_t r = (__uint128_t)a * b;
    return (uint64_t)r ^ (uint64_t)(r >> 64);
#else
    uint64_t ha = a >> 32, la = (uint32_t)a;
    uint64_t hb = b >> 32, lb = (uint32_t)b;
    uint64_t rh = ha * hb, rl = la * lb;
    uint64_t rm0 = ha * lb, rm1 = hb * la;
    uint64_t t = rl + (rm0 << 32), c = (t < rl);
    uint64_t lo = t + (rm1 << 32); c += (lo < t);
    return lo ^ (rh + (rm0 >> 32) + (rm1 >> 32) + c);
#endif
}

#define NCI_WY_S0 0xa0761d6478bd642fULL
#define NCI_WY_S1 0xe7037ed1a0b428dbULL
#define NCI_WY_S2 0x8ebc6af09c88c6e3ULL

static uint64_t intern_hash(const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint64_t seed = NCI_WY_S0;
    uint64_t a, b;

    if (len <= 16) {
        if (len >= 4) {
            a = (nci_read4(p) << 32) | nci_read4(p + ((len >> 3) << 2));
            b = (nci_read4(p + len - 4) << 32)
              | nci_read4(p + len - 4 - ((len >> 3) << 2));
        } else if (len > 0) {
            a = ((uint64_t)p[0] << 16) | ((uint64_t)p[len >> 1] << 8) | p[len - 1];
            b = 0;
        } else {
            a = 0; b = 0;
        }
    } else if (len <= 48) {
        size_t i = 0;
        for (; len - i >= 16; i += 16)
            seed = nci_wymix(nci_read8(p + i) ^ NCI_WY_S1, nci_read8(p + i + 8) ^ seed);
        a = nci_read8(p + len - 16);
        b = nci_read8(p + len - 8);
    } else {
        uint64_t s1 = seed, s2 = seed;
        size_t i = 0;
        for (; len - i >= 48; i += 48) {
            seed = nci_wymix(nci_read8(p + i)      ^ NCI_WY_S0, nci_read8(p + i + 8)  ^ seed);
            s1   = nci_wymix(nci_read8(p + i + 16) ^ NCI_WY_S1, nci_read8(p + i + 24) ^ s1);
            s2   = nci_wymix(nci_read8(p + i + 32) ^ NCI_WY_S2, nci_read8(p + i + 40) ^ s2);
        }
        seed ^= s1 ^ s2;
        for (; len - i >= 16; i += 16)
            seed = nci_wymix(nci_read8(p + i) ^ NCI_WY_S1, nci_read8(p + i + 8) ^ seed);
        a = nci_read8(p + len - 16);
        b = nci_read8(p + len - 8);
    }
    return nci_wymix(NCI_WY_S1 ^ len, nci_wymix(a ^ NCI_WY_S1, b ^ seed));
}

/*
 * Value storage: [size_t len][data bytes]. The returned/stored pointer is the
 * data, so the length is recoverable in O(1) from any handle via intern_len().
 * Reads/writes of the header use memcpy so unaligned size_t access is never an
 * issue (the block is malloc-aligned, but this keeps it portable regardless).
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
static size_t intern_len(const void *dataptr) {
    size_t len;
    memcpy(&len, (const unsigned char *)dataptr - sizeof(size_t), sizeof(size_t));
    return len;
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
    g_epoch++;
    if (g_epoch == 0) g_epoch = 1;
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

static neverc_unique_handle_t intern_ok(const void *ptr) {
    neverc_unique_handle_t h = {0};
    h.ptr = ptr;
    h.epoch = g_epoch;
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
            h = intern_ok(g_table[idx].data);
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
    g_table[idx].kind = kind;
    g_table[idx].hash = hash;
    g_table[idx].data = copy;
    g_table[idx].len = len;
    g_count++;

    h = intern_ok(copy);
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

static int intern_live(neverc_unique_handle_t h) {
    return h.ptr && h.epoch == g_epoch && g_table != NULL;
}

const char *neverc_unique_string_value(neverc_unique_handle_t h) {
    const char *result = NULL;
    LOCK();
    if (intern_live(h)) {
        size_t n = intern_len(h.ptr);
        if (n > 0) {
            const unsigned char *p = (const unsigned char *)h.ptr;
            if (p[n - 1] == '\0')
                result = (const char *)h.ptr;
        }
    }
    UNLOCK();
    return result;
}

int64_t neverc_unique_int64_value(neverc_unique_handle_t h) {
    int64_t v = 0;
    LOCK();
    if (intern_live(h) && intern_len(h.ptr) == sizeof(v))
        memcpy(&v, h.ptr, sizeof(v));
    UNLOCK();
    return v;
}

uint64_t neverc_unique_uint64_value(neverc_unique_handle_t h) {
    uint64_t v = 0;
    LOCK();
    if (intern_live(h) && intern_len(h.ptr) == sizeof(v))
        memcpy(&v, h.ptr, sizeof(v));
    UNLOCK();
    return v;
}

const void *neverc_unique_bytes_value(neverc_unique_handle_t h, size_t *len) {
    const void *result = NULL;
    size_t n = 0;
    LOCK();
    if (intern_live(h)) {
        n = intern_len(h.ptr);
        result = h.ptr;
    }
    UNLOCK();
    if (len) *len = n;
    return result;
}

int neverc_unique_handle_equal(neverc_unique_handle_t a, neverc_unique_handle_t b) {
    return a.ptr == b.ptr && a.epoch == b.epoch;
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
