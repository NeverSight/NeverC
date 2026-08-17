/*
 * NeverC maps — string-keyed hash map.
 * C adaptation of Go maps package.
 *
 * SwissTable open addressing (Abseil / Rust hashbrown / Go 1.24 swissmap):
 *   - A dense array of 1-byte control words sits alongside the slots. Each
 *     control byte is EMPTY (0xFF), DELETED (0x80), or FULL (the low 7 bits of
 *     the hash, "H2"). Probing scans a whole GROUP of control bytes at once —
 *     SSE2 on x86, an 8-byte SWAR word everywhere else — so one cheap vector
 *     compare rejects ~all non-matching slots before a single key is touched.
 *   - The hash splits into H1 (= hash >> 7, the group position) and H2 (the
 *     control byte). A lookup compares 16/8 H2 bytes in parallel, then verifies
 *     only the rare candidates against the stored 64-bit hash + key bytes.
 *   - Quadratic (triangular-number) group probing visits every group exactly
 *     once on a power-of-two table, so the scan always terminates at an EMPTY.
 *   - Deletion writes a DELETED tombstone (or EMPTY when the slot provably sat
 *     on no probe chain), and a tombstone-aware rehash reclaims them.
 *
 * This replaces the previous Robin-Hood open-addressing table: SwissTable wins
 * on lookups (the hot path) and especially on misses, because the control-byte
 * filter resolves a group in a few instructions with far better cache behavior
 * than walking 32-byte entries and recomputing probe distances.
 */

#include "neverc/std/maps.h"
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define MAP_INIT_CAP 16
/* SwissTable grows at 7/8 load (cap - cap/8), leaving >= 1/8 empty so a probe
 * always finds a terminating EMPTY and lookups stay short. */

typedef struct {
    char    *key;
    void    *value;
    uint64_t hash;
    size_t   key_len;
} map_entry_t;

typedef struct {
    char *key;
    void *value;
} map_callback_entry_t;

struct neverc_map {
    uint8_t     *ctrl;       /* cap + NCI_GROUP control bytes (head mirrored at tail) */
    map_entry_t *slots;      /* cap slots, parallel to ctrl */
    size_t       cap;        /* power of two, >= NCI_GROUP */
    size_t       len;        /* number of FULL slots */
    size_t       tombstones; /* number of DELETED slots */
};

/* ------------------------------------------------------------------ *
 * wyhash (final v3) — identical to hash/maphash so map and maphash agree.
 * ------------------------------------------------------------------ */

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

static uint64_t hash_string(const char *key) {
    const uint8_t *p = (const uint8_t *)key;

    /* Ultra-short keys (0-3 bytes): avoid strlen entirely by checking
       null bytes inline. Matches FNV-1a's "scan until null" pattern. */
    if (!p[0]) return NCI_WY_S0;
    if (!p[1]) return p[0] * NCI_WY_S1;
    if (!p[2]) return (((uint64_t)p[0] << 8) | p[1]) * NCI_WY_S1 ^ NCI_WY_S0;
    if (!p[3]) return (((uint64_t)p[0] << 16) | ((uint64_t)p[1] << 8) | p[2]) * NCI_WY_S1 ^ NCI_WY_S0;

    size_t len = 4 + strlen(key + 4);
    uint64_t seed = NCI_WY_S0;
    uint64_t a, b;

    if (len <= 16) {
        a = (nci_read4(p) << 32) | nci_read4(p + ((len >> 3) << 2));
        b = (nci_read4(p + len - 4) << 32) | nci_read4(p + len - 4 - ((len >> 3) << 2));
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

/* ------------------------------------------------------------------ *
 * Control-byte group scan.
 *
 * Control encoding (hashbrown convention):
 *   EMPTY   = 0xFF  (high bit set)
 *   DELETED = 0x80  (high bit set)
 *   FULL    = 0x00..0x7F  (high bit clear; the low 7 hash bits, "H2")
 *
 * On x86 a GROUP is 16 control bytes scanned with SSE2; everywhere else it is
 * an 8-byte SWAR word using the classic "find zero byte" bit tricks. Both
 * return a bitmask where the set bits identify matching slots; NCI_OFFSET maps
 * the lowest set bit to a slot offset, and NCI_CLEAR_LOWEST advances the mask.
 * ------------------------------------------------------------------ */

#define NCI_EMPTY   0xFF
#define NCI_DELETED 0x80
#define NCI_IS_FULL(c) (((c) & 0x80) == 0)

/* x86 gets a 16-wide SSE2 group; every other target (and any toolchain without
 * the intrinsic header) falls back to the portable 8-wide SWAR group, which is
 * exactly the 8-slot group Go 1.24's swissmap uses. */
#if defined(__SSE2__) && (!defined(__has_include) || __has_include(<emmintrin.h>))
#include <emmintrin.h>

#define NCI_GROUP 16
typedef __m128i  nci_group_t;
typedef uint32_t nci_bitmask_t;

static inline nci_group_t nci_load(const uint8_t *p) {
    return _mm_loadu_si128((const __m128i *)(const void *)p);
}
static inline nci_bitmask_t nci_match(nci_group_t g, uint8_t h2) {
    return (uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(g, _mm_set1_epi8((char)h2)));
}
static inline nci_bitmask_t nci_match_empty(nci_group_t g) {
    return (uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(g, _mm_set1_epi8((char)NCI_EMPTY)));
}
static inline nci_bitmask_t nci_match_empty_or_deleted(nci_group_t g) {
    /* movemask extracts the high bit of each byte (set for EMPTY and DELETED). */
    return (uint16_t)_mm_movemask_epi8(g);
}
#define NCI_OFFSET(m)   ((size_t)__builtin_ctz(m))
#define NCI_LEADING(m)  ((size_t)(__builtin_clz((unsigned)(m)) - 16))

#else  /* portable 8-byte SWAR */

#define NCI_GROUP 8
typedef uint64_t nci_group_t;
typedef uint64_t nci_bitmask_t;

#define NCI_SWAR_LSB 0x0101010101010101ULL
#define NCI_SWAR_MSB 0x8080808080808080ULL

static inline nci_group_t nci_load(const uint8_t *p) {
    nci_group_t g;
    memcpy(&g, p, sizeof(g));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    g = __builtin_bswap64(g);
#endif
    return g;
}
/* Set the high bit of every byte equal to h2 (classic hasless zero detection
 * on g ^ broadcast(h2); FULL bytes are 0x00..0x7F and h2 is too, while EMPTY/
 * DELETED have their high bit set so they can never spuriously match). */
static inline nci_bitmask_t nci_match(nci_group_t g, uint8_t h2) {
    nci_group_t x = g ^ (NCI_SWAR_LSB * (nci_group_t)h2);
    return (x - NCI_SWAR_LSB) & ~x & NCI_SWAR_MSB;
}
/* EMPTY is 0xFF (bit7 and bit6 set); DELETED is 0x80 (only bit7). */
static inline nci_bitmask_t nci_match_empty(nci_group_t g) {
    return g & (g << 1) & NCI_SWAR_MSB;
}
static inline nci_bitmask_t nci_match_empty_or_deleted(nci_group_t g) {
    return g & NCI_SWAR_MSB;
}
#define NCI_OFFSET(m)   ((size_t)(__builtin_ctzll(m) >> 3))
#define NCI_LEADING(m)  ((size_t)(__builtin_clzll(m) >> 3))

#endif

/* Both encodings keep exactly one set bit per matching slot, so clearing the
 * lowest set bit advances to the next match. */
#define NCI_CLEAR_LOWEST(m) ((m) & ((m) - 1))

/* Mirror a control byte into the cloned tail so a group load near the end of
 * the table reads valid bytes without wrapping. */
static inline void nci_set_ctrl(uint8_t *ctrl, size_t cap, size_t idx, uint8_t v) {
    ctrl[idx] = v;
    size_t mirror = ((idx - NCI_GROUP) & (cap - 1)) + NCI_GROUP;
    ctrl[mirror] = v;
}

static inline size_t map_max_load(size_t cap) { return cap - cap / 8; }

/* ------------------------------------------------------------------ *
 * Table allocation / rehash
 * ------------------------------------------------------------------ */

static int map_alloc_tables(size_t cap, uint8_t **pctrl, map_entry_t **pslots) {
    if (cap > SIZE_MAX - NCI_GROUP ||
        cap > SIZE_MAX / sizeof(map_entry_t)) return 0;
    uint8_t *ctrl = (uint8_t *)malloc(cap + NCI_GROUP);
    if (!ctrl) return 0;
    memset(ctrl, NCI_EMPTY, cap + NCI_GROUP);
    map_entry_t *slots = (map_entry_t *)calloc(cap, sizeof(map_entry_t));
    if (!slots) { free(ctrl); return 0; }
    *pctrl = ctrl;
    *pslots = slots;
    return 1;
}

/* Place an entry known to be absent into a table with no tombstones (used by
 * resize / clone): the first EMPTY in the probe is the first non-full. */
static void map_emplace(uint8_t *ctrl, map_entry_t *slots, size_t cap, map_entry_t e) {
    size_t mask = cap - 1;
    size_t pos = (size_t)(e.hash >> 7) & mask;
    size_t stride = 0;
    uint8_t h2 = (uint8_t)(e.hash & 0x7F);
    for (;;) {
        nci_group_t g = nci_load(ctrl + pos);
        nci_bitmask_t em = nci_match_empty(g);
        if (em) {
            size_t idx = (pos + NCI_OFFSET(em)) & mask;
            nci_set_ctrl(ctrl, cap, idx, h2);
            slots[idx] = e;
            return;
        }
        stride += NCI_GROUP;
        pos = (pos + stride) & mask;
    }
}

/* Smallest power-of-two capacity (>= MAP_INIT_CAP) that holds len at <=7/8. */
static size_t map_ideal_cap(size_t len) {
    size_t cap = MAP_INIT_CAP;
    while (len > map_max_load(cap)) {
        if (cap > (SIZE_MAX >> 1))
            return cap;
        cap <<= 1;
    }
    return cap;
}

/* Rebuild the table at new_cap, dropping every tombstone. Used to grow, to
 * reclaim tombstones in place, and to shrink after bulk deletes. */
static int map_resize(neverc_map_t *m, size_t new_cap) {
    uint8_t *nctrl;
    map_entry_t *nslots;
    if (!map_alloc_tables(new_cap, &nctrl, &nslots)) return -1;

    for (size_t i = 0; i < m->cap; i++)
        if (NCI_IS_FULL(m->ctrl[i]))
            map_emplace(nctrl, nslots, new_cap, m->slots[i]);

    free(m->ctrl);
    free(m->slots);
    m->ctrl = nctrl;
    m->slots = nslots;
    m->cap = new_cap;
    m->tombstones = 0;
    return 0;
}

/* Return the slot holding key, or SIZE_MAX. */
static size_t map_find(const neverc_map_t *m, const char *key,
                       uint64_t h, uint8_t h2, size_t klen) {
    size_t mask = m->cap - 1;
    size_t pos = (size_t)(h >> 7) & mask;
    size_t stride = 0;
    for (;;) {
        nci_group_t g = nci_load(m->ctrl + pos);
        nci_bitmask_t mm = nci_match(g, h2);
        while (mm) {
            size_t idx = (pos + NCI_OFFSET(mm)) & mask;
            const map_entry_t *e = &m->slots[idx];
            if (e->hash == h && e->key_len == klen &&
                memcmp(e->key, key, klen) == 0)
                return idx;
            mm = NCI_CLEAR_LOWEST(mm);
        }
        if (nci_match_empty(g)) return (size_t)-1;
        stride += NCI_GROUP;
        pos = (pos + stride) & mask;
    }
}

/* First EMPTY-or-DELETED slot in the probe (a fresh table after rehash has no
 * tombstones, so this is the first EMPTY). */
static size_t map_find_insert(const neverc_map_t *m, uint64_t h) {
    size_t mask = m->cap - 1;
    size_t pos = (size_t)(h >> 7) & mask;
    size_t stride = 0;
    for (;;) {
        nci_group_t g = nci_load(m->ctrl + pos);
        nci_bitmask_t e = nci_match_empty_or_deleted(g);
        if (e) return (pos + NCI_OFFSET(e)) & mask;
        stride += NCI_GROUP;
        pos = (pos + stride) & mask;
    }
}

static void map_free_callback_snapshot(map_callback_entry_t *entries,
                                       size_t count) {
    if (!entries) return;
    for (size_t i = 0; i < count; i++) free(entries[i].key);
    free(entries);
}

static map_callback_entry_t *map_callback_snapshot(const neverc_map_t *m,
                                                   size_t *count) {
    *count = 0;
    if (m->len == 0 || m->len > SIZE_MAX / sizeof(map_callback_entry_t))
        return NULL;
    map_callback_entry_t *entries =
        (map_callback_entry_t *)calloc(m->len, sizeof(*entries));
    if (!entries) return NULL;

    size_t k = 0;
    for (size_t i = 0; i < m->cap; i++) {
        if (!NCI_IS_FULL(m->ctrl[i])) continue;
        size_t len = m->slots[i].key_len;
        if (len == SIZE_MAX) {
            map_free_callback_snapshot(entries, k);
            return NULL;
        }
        entries[k].key = (char *)malloc(len + 1);
        if (!entries[k].key) {
            map_free_callback_snapshot(entries, k);
            return NULL;
        }
        memcpy(entries[k].key, m->slots[i].key, len + 1);
        entries[k].value = m->slots[i].value;
        k++;
    }
    *count = k;
    return entries;
}

/* ------------------------------------------------------------------ *
 * Public API
 * ------------------------------------------------------------------ */

neverc_map_t *neverc_maps_new(void) {
    neverc_map_t *m = (neverc_map_t *)calloc(1, sizeof(*m));
    if (!m) return NULL;
    m->cap = MAP_INIT_CAP;
    if (!map_alloc_tables(m->cap, &m->ctrl, &m->slots)) { free(m); return NULL; }
    return m;
}

void neverc_maps_free(neverc_map_t *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; i++)
        if (NCI_IS_FULL(m->ctrl[i])) free(m->slots[i].key);
    free(m->ctrl);
    free(m->slots);
    free(m);
}

void neverc_maps_clear(neverc_map_t *m) {
    if (!m) return;
    for (size_t i = 0; i < m->cap; i++)
        if (NCI_IS_FULL(m->ctrl[i])) { free(m->slots[i].key); m->slots[i].key = NULL; }
    memset(m->ctrl, NCI_EMPTY, m->cap + NCI_GROUP);
    m->len = 0;
    m->tombstones = 0;
}

int neverc_maps_set(neverc_map_t *m, const char *key, void *value) {
    if (!m || !key) return -1;
    size_t klen = strlen(key);
    if (klen == SIZE_MAX) return -1;
    uint64_t h = hash_string(key);
    uint8_t h2 = (uint8_t)(h & 0x7F);

    /* One probe both checks for an existing key (overwrite) and remembers the
     * first reusable slot so a new key lands at the earliest tombstone/empty. */
    size_t mask = m->cap - 1;
    size_t pos = (size_t)(h >> 7) & mask;
    size_t stride = 0;
    size_t first_eod = (size_t)-1;
    for (;;) {
        nci_group_t g = nci_load(m->ctrl + pos);
        nci_bitmask_t mm = nci_match(g, h2);
        while (mm) {
            size_t idx = (pos + NCI_OFFSET(mm)) & mask;
            map_entry_t *e = &m->slots[idx];
            if (e->hash == h && e->key_len == klen &&
                memcmp(e->key, key, klen) == 0) {
                e->value = value;
                return 0;
            }
            mm = NCI_CLEAR_LOWEST(mm);
        }
        if (first_eod == (size_t)-1) {
            nci_bitmask_t eod = nci_match_empty_or_deleted(g);
            if (eod) first_eod = (pos + NCI_OFFSET(eod)) & mask;
        }
        if (nci_match_empty(g)) break;   /* key absent */
        stride += NCI_GROUP;
        pos = (pos + stride) & mask;
    }

    char *dup = (char *)malloc(klen + 1);
    if (!dup) return -1;
    memcpy(dup, key, klen + 1);

    size_t target;
    int was_empty;
    if (m->len + m->tombstones >= map_max_load(m->cap)) {
        /* Occupancy includes tombstones. If live keys still fit, drop them
         * in place (or shrink) instead of growing — otherwise a handful of
         * DELETED slots at high live-load forces a 2x table (and can OOM
         * when a same-cap rehash would have succeeded). */
        size_t new_cap;
        if (m->len < map_max_load(m->cap)) {
            new_cap = map_ideal_cap(m->len);
        } else if (m->cap > (SIZE_MAX >> 1)) {
            free(dup);
            return -1;
        } else {
            new_cap = m->cap * 2;
        }
        if (map_resize(m, new_cap) < 0) { free(dup); return -1; }
        target = map_find_insert(m, h);   /* fresh table: first EMPTY */
        was_empty = 1;
    } else {
        target = first_eod;
        was_empty = 0;
    }
    if (target == (size_t)-1) {
        free(dup);
        return -1;
    }
    if (!was_empty)
        was_empty = (m->ctrl[target] == NCI_EMPTY);

    nci_set_ctrl(m->ctrl, m->cap, target, h2);
    m->slots[target].key = dup;
    m->slots[target].value = value;
    m->slots[target].hash = h;
    m->slots[target].key_len = klen;
    m->len++;
    if (!was_empty && m->tombstones > 0) m->tombstones--;
    return 0;
}

void *neverc_maps_get(const neverc_map_t *m, const char *key) {
    if (!m || !key) return NULL;
    size_t klen = strlen(key);
    uint64_t h = hash_string(key);
    size_t idx = map_find(m, key, h, (uint8_t)(h & 0x7F), klen);
    return idx == (size_t)-1 ? NULL : m->slots[idx].value;
}

int neverc_maps_has(const neverc_map_t *m, const char *key) {
    if (!m || !key) return 0;
    size_t klen = strlen(key);
    uint64_t h = hash_string(key);
    return map_find(m, key, h, (uint8_t)(h & 0x7F), klen) != (size_t)-1;
}

int neverc_maps_delete(neverc_map_t *m, const char *key) {
    if (!m || !key) return -1;
    size_t klen = strlen(key);
    uint64_t h = hash_string(key);
    size_t idx = map_find(m, key, h, (uint8_t)(h & 0x7F), klen);
    if (idx == (size_t)-1) return -1;

    free(m->slots[idx].key);
    m->slots[idx].key = NULL;
    m->slots[idx].value = NULL;

    /* If the slot provably sits on no probe chain (an EMPTY lies within one
     * group on both sides), reset it to EMPTY so no tombstone accumulates;
     * otherwise leave a DELETED tombstone. (Abseil's erase heuristic.) */
    size_t mask = m->cap - 1;
    size_t before = (idx - NCI_GROUP) & mask;
    nci_bitmask_t empty_after  = nci_match_empty(nci_load(m->ctrl + idx));
    nci_bitmask_t empty_before = nci_match_empty(nci_load(m->ctrl + before));
    int never_full = empty_before && empty_after &&
        (NCI_OFFSET(empty_after) + NCI_LEADING(empty_before) < NCI_GROUP);

    if (never_full) {
        nci_set_ctrl(m->ctrl, m->cap, idx, NCI_EMPTY);
    } else {
        nci_set_ctrl(m->ctrl, m->cap, idx, NCI_DELETED);
        m->tombstones++;
    }
    m->len--;

    /* Once tombstones outnumber live entries, rebuild at the right size: this
     * reclaims the dead slots (so probes stay short) and returns memory after
     * bulk deletes — something Abseil/Go-style tables never do. The trigger is
     * geometric, so amortized delete cost stays O(1). */
    if (m->tombstones > m->len)
        (void)map_resize(m, map_ideal_cap(m->len));
    return 0;
}

size_t neverc_maps_len(const neverc_map_t *m) {
    return m ? m->len : 0;
}

char **neverc_maps_keys(const neverc_map_t *m, size_t *count) {
    if (!m || m->len == 0 || m->len > SIZE_MAX / sizeof(char *)) {
        if (count) *count = 0;
        return NULL;
    }
    char **keys = (char **)malloc(m->len * sizeof(char *));
    if (!keys) { if (count) *count = 0; return NULL; }
    size_t k = 0;
    for (size_t i = 0; i < m->cap; i++)
        if (NCI_IS_FULL(m->ctrl[i])) keys[k++] = m->slots[i].key;
    if (count) *count = k;
    return keys;
}

void **neverc_maps_values(const neverc_map_t *m, size_t *count) {
    if (!m || m->len == 0 || m->len > SIZE_MAX / sizeof(void *)) {
        if (count) *count = 0;
        return NULL;
    }
    void **vals = (void **)malloc(m->len * sizeof(void *));
    if (!vals) { if (count) *count = 0; return NULL; }
    size_t k = 0;
    for (size_t i = 0; i < m->cap; i++)
        if (NCI_IS_FULL(m->ctrl[i])) vals[k++] = m->slots[i].value;
    if (count) *count = k;
    return vals;
}

void neverc_maps_foreach(const neverc_map_t *m, neverc_maps_iter_func_t fn, void *user_data) {
    if (!m || !fn || m->len == 0) return;
    size_t count;
    map_callback_entry_t *entries = map_callback_snapshot(m, &count);
    if (!entries) return;
    for (size_t i = 0; i < count; i++)
        fn(entries[i].key, entries[i].value, user_data);
    map_free_callback_snapshot(entries, count);
}

void neverc_maps_delete_func(neverc_map_t *m, neverc_maps_filter_func_t fn) {
    if (!m || !fn || m->len == 0) return;
    size_t count;
    map_callback_entry_t *entries = map_callback_snapshot(m, &count);
    if (!entries) return;
    for (size_t i = 0; i < count; i++)
        if (fn(entries[i].key, entries[i].value))
            neverc_maps_delete(m, entries[i].key);
    map_free_callback_snapshot(entries, count);
}

neverc_map_t *neverc_maps_clone(const neverc_map_t *m) {
    if (!m) return NULL;
    neverc_map_t *c = (neverc_map_t *)calloc(1, sizeof(*c));
    if (!c) return NULL;
    c->cap = m->cap;
    if (!map_alloc_tables(c->cap, &c->ctrl, &c->slots)) { free(c); return NULL; }
    for (size_t i = 0; i < m->cap; i++) {
        if (!NCI_IS_FULL(m->ctrl[i])) continue;
        size_t klen = m->slots[i].key_len;
        if (klen == SIZE_MAX) { neverc_maps_free(c); return NULL; }
        char *dup = (char *)malloc(klen + 1);
        if (!dup) { neverc_maps_free(c); return NULL; }
        memcpy(dup, m->slots[i].key, klen + 1);
        map_entry_t e = { dup, m->slots[i].value, m->slots[i].hash, klen };
        map_emplace(c->ctrl, c->slots, c->cap, e);
        c->len++;
    }
    return c;
}

int neverc_maps_equal(const neverc_map_t *a, const neverc_map_t *b) {
    if (!a && !b) return 1;
    if (!a || !b) return 0;
    if (a->len != b->len) return 0;
    for (size_t i = 0; i < a->cap; i++) {
        if (!NCI_IS_FULL(a->ctrl[i])) continue;
        const map_entry_t *e = &a->slots[i];
        size_t idx = map_find(b, e->key, e->hash, (uint8_t)(e->hash & 0x7F), e->key_len);
        if (idx == (size_t)-1) return 0;
        if (b->slots[idx].value != e->value) return 0;
    }
    return 1;
}

int neverc_maps_copy(neverc_map_t *dst, const neverc_map_t *src) {
    if (!dst || !src) return -1;
    if (dst == src) return 0;
    for (size_t i = 0; i < src->cap; i++) {
        if (!NCI_IS_FULL(src->ctrl[i])) continue;
        if (neverc_maps_set(dst, src->slots[i].key, src->slots[i].value) != 0)
            return -1;
    }
    return 0;
}
