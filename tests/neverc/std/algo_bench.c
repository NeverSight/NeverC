/*
 * Benchmark: new optimized algorithms vs old implementations.
 * Covers: hash (wyhash vs FNV-1a), substring search (BMH vs Rabin-Karp),
 *         binary search (typed vs generic).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* ============================================================
 * OLD implementations — noinline to prevent compiler hoisting
 * ============================================================ */

__attribute__((noinline))
static uint64_t old_hash_fnv1a(const char *s) {
    uint64_t h = 14695981039346656037ULL;
    while (*s) {
        h ^= (uint64_t)(unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

#define OLD_RK_PRIME 16777619U

__attribute__((noinline))
static size_t old_bytes_index_rk(const uint8_t *s, size_t slen,
                                  const uint8_t *sep, size_t seplen) {
    if (seplen == 0) return 0;
    if (seplen > slen) return (size_t)-1;
    if (seplen == 1) {
        const uint8_t *p = (const uint8_t *)memchr(s, sep[0], slen);
        return p ? (size_t)(p - s) : (size_t)-1;
    }
    if (seplen == slen) return memcmp(s, sep, slen) == 0 ? 0 : (size_t)-1;

    if (seplen <= 8 || slen <= 64) {
        uint8_t c0 = sep[0];
        for (size_t i = 0; i <= slen - seplen; i++)
            if (s[i] == c0 && memcmp(s + i + 1, sep + 1, seplen - 1) == 0)
                return i;
        return (size_t)-1;
    }

    uint32_t h_sep = 0, h_win = 0, pw = 1;
    for (size_t i = 0; i < seplen; i++) {
        h_sep = h_sep * OLD_RK_PRIME + (uint32_t)sep[i];
        h_win = h_win * OLD_RK_PRIME + (uint32_t)s[i];
        pw *= OLD_RK_PRIME;
    }
    if (h_win == h_sep && memcmp(s, sep, seplen) == 0) return 0;
    for (size_t i = seplen; i < slen; i++) {
        h_win = h_win * OLD_RK_PRIME + (uint32_t)s[i]
              - pw * (uint32_t)s[i - seplen];
        if (h_win == h_sep) {
            size_t pos = i - seplen + 1;
            if (memcmp(s + pos, sep, seplen) == 0) return pos;
        }
    }
    return (size_t)-1;
}

__attribute__((noinline))
static int old_cmp_int(const void *a, const void *b) {
    int va = *(const int *)a, vb = *(const int *)b;
    return (va > vb) - (va < vb);
}

__attribute__((noinline))
static int old_binary_search_generic(const int *arr, size_t len, int target, int *found) {
    int lo = 0, hi = (int)len;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        int c = old_cmp_int(&arr[mid], &target);
        if (c < 0) lo = mid + 1;
        else hi = mid;
    }
    if (found) *found = (lo < (int)len && arr[lo] == target);
    return lo;
}

/* ============================================================
 * NEW implementations (from the library)
 * ============================================================ */
#include "neverc/std/bytes.h"
#include "neverc/std/slices.h"

/* New wyhash — duplicated for direct benchmark */
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

__attribute__((noinline))
static uint64_t new_hash_wyhash(const char *key) {
    const uint8_t *p = (const uint8_t *)key;

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
        for (; i + 16 <= len; i += 16)
            seed = nci_wymix(nci_read8(p + i) ^ NCI_WY_S1, nci_read8(p + i + 8) ^ seed);
        a = nci_read8(p + len - 16);
        b = nci_read8(p + len - 8);
    } else {
        uint64_t s1 = seed, s2 = seed;
        size_t i = 0;
        for (; i + 48 <= len; i += 48) {
            seed = nci_wymix(nci_read8(p + i) ^ NCI_WY_S0, nci_read8(p + i + 8) ^ seed);
            s1 = nci_wymix(nci_read8(p + i + 16) ^ NCI_WY_S1, nci_read8(p + i + 24) ^ s1);
            s2 = nci_wymix(nci_read8(p + i + 32) ^ NCI_WY_S2, nci_read8(p + i + 40) ^ s2);
        }
        seed ^= s1 ^ s2;
        for (; i + 16 <= len; i += 16)
            seed = nci_wymix(nci_read8(p + i) ^ NCI_WY_S1, nci_read8(p + i + 8) ^ seed);
        a = nci_read8(p + len - 16);
        b = nci_read8(p + len - 8);
    }
    return nci_wymix(NCI_WY_S1 ^ len, nci_wymix(a ^ NCI_WY_S1, b ^ seed));
}

/* ============================================================
 * Timing helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static volatile uint64_t sink64;
static volatile size_t sink_sz;
static volatile int sink_int;

/* ============================================================
 * Benchmark: Hash function
 * ============================================================ */
static void bench_hash(void) {
    printf("\n=== Hash: wyhash vs FNV-1a ===\n");
    printf("%-20s  %10s  %10s  %8s\n", "key_len", "FNV-1a", "wyhash", "speedup");

    #define N_HASH_KEYS 256
    char keys[N_HASH_KEYS][128];
    srand(42);
    int key_lens[] = {3, 8, 18, 38, 106};
    int nklens = sizeof(key_lens) / sizeof(key_lens[0]);

    for (int kl = 0; kl < nklens; kl++) {
        int klen = key_lens[kl];
        for (int i = 0; i < N_HASH_KEYS; i++) {
            for (int j = 0; j < klen; j++) keys[i][j] = 'a' + (rand() % 26);
            keys[i][klen] = '\0';
        }

        int iters = 5000000;
        double t0 = now_sec();
        for (int i = 0; i < iters; i++)
            sink64 = old_hash_fnv1a(keys[i & (N_HASH_KEYS-1)]);
        double t_old = now_sec() - t0;

        t0 = now_sec();
        for (int i = 0; i < iters; i++)
            sink64 = new_hash_wyhash(keys[i & (N_HASH_KEYS-1)]);
        double t_new = now_sec() - t0;

        printf("%-20d  %8.1f ms  %8.1f ms  %6.1fx\n",
               klen, t_old * 1000, t_new * 1000, t_old / t_new);
    }
}

/* ============================================================
 * Benchmark: Substring search
 * ============================================================ */
static void bench_substr(void) {
    printf("\n=== Substring Search: BMH vs Rabin-Karp ===\n");
    printf("%-25s  %10s  %10s  %8s\n", "case", "RK", "BMH", "speedup");

    size_t hlen = 100000;
    uint8_t *haystack = (uint8_t *)malloc(hlen);
    srand(42);
    for (size_t i = 0; i < hlen; i++) haystack[i] = 'a' + (rand() % 26);

    /* Use realistic diverse patterns (not all same char) */
    struct { const char *label; const char *pat; } cases[] = {
        {"div_pat=10 miss",   "QzXrT9pLm7"},
        {"div_pat=18 miss",   "QzXrT9pLm7kYuBw3Hn"},
        {"div_pat=30 miss",   "QzXrT9pLm7kYuBw3HnFcVdSj2A5eRg"},
        {"div_pat=50 miss",   "QzXrT9pLm7kYuBw3HnFcVdSj2A5eRgMiOl4Ks6NqWx8JyUp"},
    };
    int ncases = sizeof(cases) / sizeof(cases[0]);

    for (int c = 0; c < ncases; c++) {
        const uint8_t *pat = (const uint8_t *)cases[c].pat;
        size_t plen = strlen(cases[c].pat);
        int iters = 5000;

        /* Vary start offset to prevent compiler caching pure-function results */
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            size_t off = (size_t)(i & 0x7F);
            sink_sz = old_bytes_index_rk(haystack + off, hlen - off, pat, plen);
        }
        double t_old = now_sec() - t0;

        t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            size_t off = (size_t)(i & 0x7F);
            sink_sz = neverc_bytes_index(haystack + off, hlen - off, pat, plen);
        }
        double t_new = now_sec() - t0;

        printf("%-25s  %8.1f ms  %8.1f ms  %6.1fx\n",
               cases[c].label, t_old * 1000, t_new * 1000, t_old / t_new);
    }

    /* Pattern found at middle */
    printf("\n--- Pattern found case ---\n");
    const char *needle = "FINDTHISNEEDLEHERE";
    size_t nlen = strlen(needle);
    memcpy(haystack + hlen / 2, needle, nlen);

    int iters = 50000;
    double t0 = now_sec();
    for (int i = 0; i < iters; i++) {
        size_t off = (size_t)(i & 0x7F);
        sink_sz = old_bytes_index_rk(haystack + off, hlen - off, (const uint8_t *)needle, nlen);
    }
    double t_old = now_sec() - t0;

    t0 = now_sec();
    for (int i = 0; i < iters; i++) {
        size_t off = (size_t)(i & 0x7F);
        sink_sz = neverc_bytes_index(haystack + off, hlen - off, (const uint8_t *)needle, nlen);
    }
    double t_new = now_sec() - t0;

    printf("%-25s  %8.1f ms  %8.1f ms  %6.1fx\n",
           "found@50K (18 chars)", t_old * 1000, t_new * 1000, t_old / t_new);

    /* Worst case for BMH: pattern with repeated chars */
    printf("\n--- Worst case (repeated pattern) ---\n");
    uint8_t bad_pat[21];
    memset(bad_pat, 'a', 20);
    bad_pat[19] = 'Z';
    bad_pat[20] = '\0';

    iters = 5000;
    t0 = now_sec();
    for (int i = 0; i < iters; i++) {
        size_t off = (size_t)(i & 0x7F);
        sink_sz = old_bytes_index_rk(haystack + off, hlen - off, bad_pat, 20);
    }
    t_old = now_sec() - t0;

    t0 = now_sec();
    for (int i = 0; i < iters; i++) {
        size_t off = (size_t)(i & 0x7F);
        sink_sz = neverc_bytes_index(haystack + off, hlen - off, bad_pat, 20);
    }
    t_new = now_sec() - t0;

    printf("%-25s  %8.1f ms  %8.1f ms  %6.1fx\n",
           "repeat_a*19+Z miss", t_old * 1000, t_new * 1000, t_old / t_new);

    free(haystack);
}

/* ============================================================
 * Benchmark: Binary search
 * ============================================================ */
static void bench_bsearch(void) {
    printf("\n=== Binary Search: typed vs generic ===\n");
    printf("%-15s  %10s  %10s  %8s\n", "n", "generic", "typed", "speedup");

    int sizes[] = {100, 1000, 10000, 100000};
    int nsizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int s = 0; s < nsizes; s++) {
        int n = sizes[s];
        int *arr = (int *)malloc((size_t)n * sizeof(int));
        for (int i = 0; i < n; i++) arr[i] = i * 3;

        int iters = 5000000;
        int found;

        double t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            int target = (i * 7 + 13) % (n * 3);
            sink_int = old_binary_search_generic(arr, (size_t)n, target, &found);
        }
        double t_old = now_sec() - t0;

        t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            int target = (i * 7 + 13) % (n * 3);
            sink_int = neverc_slices_binary_search_int(arr, (size_t)n, target, &found);
        }
        double t_new = now_sec() - t0;

        printf("n=%-13d  %8.1f ms  %8.1f ms  %6.1fx\n",
               n, t_old * 1000, t_new * 1000, t_old / t_new);
        free(arr);
    }
}

int main(void) {
    printf("=== std algorithm optimization benchmarks ===\n");
    bench_hash();
    bench_substr();
    bench_bsearch();
    printf("\n=== Done ===\n");
    return 0;
}
