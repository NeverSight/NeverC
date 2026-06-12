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
#include "neverc/std/cstring.h"
#include "neverc/std/hash/crc32.h"
#include "neverc/std/hash/adler32.h"
#include "neverc/std/unicode/utf8.h"
#include "neverc/std/encoding/hex.h"
#include "neverc/std/encoding/base64.h"
#include "neverc/std/math/rand.h"

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

/* ============================================================
 * Benchmark: CRC32 Slicing-by-8 vs byte-at-a-time
 * ============================================================ */

__attribute__((noinline))
static uint32_t old_crc32_ieee(const void *data, size_t len) {
    static uint32_t tab[256];
    static int init = 0;
    if (!init) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t crc = i;
            for (int j = 0; j < 8; j++)
                crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320U : 0);
            tab[i] = crc;
        }
        init = 1;
    }
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = ~(uint32_t)0;
    for (size_t i = 0; i < len; i++)
        crc = tab[(crc ^ p[i]) & 0xFF] ^ (crc >> 8);
    return ~crc;
}

static volatile uint32_t sink32;

static void bench_crc32(void) {
    printf("\n=== CRC32: Slicing-by-8 vs byte-at-a-time ===\n");
    printf("%-15s  %10s  %10s  %8s\n", "size", "old", "new(s8)", "speedup");

    size_t sizes[] = {64, 1024, 16384, 65536};
    int nsizes = sizeof(sizes) / sizeof(sizes[0]);
    uint8_t *buf = (uint8_t *)malloc(65536);
    srand(42);
    for (size_t i = 0; i < 65536; i++) buf[i] = (uint8_t)(rand() & 0xFF);

    for (int s = 0; s < nsizes; s++) {
        size_t n = sizes[s];
        int iters = (int)(500000000 / (n + 1));
        if (iters < 100) iters = 100;

        double t0 = now_sec();
        for (int i = 0; i < iters; i++)
            sink32 = old_crc32_ieee(buf, n);
        double t_old = now_sec() - t0;

        t0 = now_sec();
        for (int i = 0; i < iters; i++)
            sink32 = neverc_crc32_ieee(buf, n);
        double t_new = now_sec() - t0;

        printf("n=%-13zu  %8.1f ms  %8.1f ms  %6.1fx\n",
               n, t_old * 1000, t_new * 1000, t_old / t_new);
    }
    free(buf);
}

/* ============================================================
 * Benchmark: Adler32 unrolled vs byte-at-a-time
 * ============================================================ */

__attribute__((noinline))
static uint32_t old_adler32(const uint8_t *data, size_t len) {
    uint32_t s1 = 1, s2 = 0;
    while (len > 0) {
        size_t block = len > 5552 ? 5552 : len;
        len -= block;
        for (size_t i = 0; i < block; i++) {
            s1 += data[i];
            s2 += s1;
        }
        data += block;
        s1 %= 65521U;
        s2 %= 65521U;
    }
    return (s2 << 16) | s1;
}

static void bench_adler32(void) {
    printf("\n=== Adler32: 16-way unrolled vs byte-at-a-time ===\n");
    printf("%-15s  %10s  %10s  %8s\n", "size", "old", "new", "speedup");

    size_t sizes[] = {64, 1024, 16384, 65536};
    int nsizes = sizeof(sizes) / sizeof(sizes[0]);
    uint8_t *buf = (uint8_t *)malloc(65536);
    for (size_t i = 0; i < 65536; i++) buf[i] = (uint8_t)(rand() & 0xFF);

    for (int s = 0; s < nsizes; s++) {
        size_t n = sizes[s];
        int iters = (int)(500000000 / (n + 1));
        if (iters < 100) iters = 100;

        double t0 = now_sec();
        for (int i = 0; i < iters; i++)
            sink32 = old_adler32(buf, n);
        double t_old = now_sec() - t0;

        t0 = now_sec();
        for (int i = 0; i < iters; i++)
            sink32 = neverc_adler32_checksum(buf, n);
        double t_new = now_sec() - t0;

        printf("n=%-13zu  %8.1f ms  %8.1f ms  %6.1fx\n",
               n, t_old * 1000, t_new * 1000, t_old / t_new);
    }
    free(buf);
}

/* ============================================================
 * Benchmark: UTF-8 rune_count word-at-a-time vs byte-at-a-time
 * ============================================================ */

__attribute__((noinline))
static size_t old_utf8_rune_count(const uint8_t *buf, size_t len) {
    size_t count = 0;
    size_t i = 0;
    while (i < len) {
        uint8_t b = buf[i];
        if (b < 0x80) { i++; count++; continue; }
        uint32_t r; int sz;
        neverc_utf8_decode_rune(buf + i, len - i, &r, &sz);
        i += (size_t)(sz > 0 ? sz : 1);
        count++;
    }
    return count;
}

static void bench_utf8(void) {
    printf("\n=== UTF-8 rune_count: word-at-a-time vs byte-at-a-time ===\n");
    printf("%-25s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    size_t n = 65536;
    uint8_t *ascii_buf = (uint8_t *)malloc(n);
    for (size_t i = 0; i < n; i++) ascii_buf[i] = 'A' + (uint8_t)(i % 26);
    int iters = 10000;

    double t0 = now_sec();
    for (int i = 0; i < iters; i++)
        sink_sz = old_utf8_rune_count(ascii_buf, n);
    double t_old = now_sec() - t0;

    t0 = now_sec();
    for (int i = 0; i < iters; i++)
        sink_sz = neverc_utf8_rune_count(ascii_buf, n);
    double t_new = now_sec() - t0;

    printf("%-25s  %8.1f ms  %8.1f ms  %6.1fx\n",
           "64KB pure ASCII", t_old * 1000, t_new * 1000, t_old / t_new);

    uint8_t *mixed = (uint8_t *)malloc(n);
    size_t j = 0;
    for (size_t i = 0; j < n; i++) {
        if (i % 10 < 7 && j < n) mixed[j++] = 'A' + (uint8_t)(i % 26);
        else if (j + 3 <= n) {
            mixed[j++] = 0xE4; mixed[j++] = 0xBD; mixed[j++] = 0xA0;
        }
    }
    size_t mlen = j;

    t0 = now_sec();
    for (int i = 0; i < iters; i++)
        sink_sz = old_utf8_rune_count(mixed, mlen);
    t_old = now_sec() - t0;

    t0 = now_sec();
    for (int i = 0; i < iters; i++)
        sink_sz = neverc_utf8_rune_count(mixed, mlen);
    t_new = now_sec() - t0;

    printf("%-25s  %8.1f ms  %8.1f ms  %6.1fx\n",
           "64KB 70% ASCII + CJK", t_old * 1000, t_new * 1000, t_old / t_new);

    free(ascii_buf);
    free(mixed);
}

/* ============================================================
 * Benchmark: Hex encode pair table vs byte-at-a-time
 * ============================================================ */

static const char old_hextable[] = "0123456789abcdef";

__attribute__((noinline))
static size_t old_hex_encode(char *dst, const uint8_t *src, size_t src_len) {
    size_t j = 0;
    for (size_t i = 0; i < src_len; i++) {
        dst[j]     = old_hextable[src[i] >> 4];
        dst[j + 1] = old_hextable[src[i] & 0x0f];
        j += 2;
    }
    dst[j] = '\0';
    return src_len * 2;
}

static void bench_hex(void) {
    printf("\n=== Hex encode: pair-table vs byte-at-a-time ===\n");
    printf("%-15s  %10s  %10s  %8s\n", "size", "old", "new", "speedup");

    size_t sizes[] = {32, 256, 4096, 65536};
    int nsizes = sizeof(sizes) / sizeof(sizes[0]);
    uint8_t *buf = (uint8_t *)malloc(65536);
    char *out = (char *)malloc(65536 * 2 + 1);
    for (size_t i = 0; i < 65536; i++) buf[i] = (uint8_t)(rand() & 0xFF);

    for (int s = 0; s < nsizes; s++) {
        size_t n = sizes[s];
        int iters = (int)(200000000 / (n + 1));
        if (iters < 100) iters = 100;

        double t0 = now_sec();
        for (int i = 0; i < iters; i++)
            sink_sz = old_hex_encode(out, buf, n);
        double t_old = now_sec() - t0;

        t0 = now_sec();
        for (int i = 0; i < iters; i++)
            sink_sz = neverc_hex_encode(out, buf, n);
        double t_new = now_sec() - t0;

        printf("n=%-13zu  %8.1f ms  %8.1f ms  %6.1fx\n",
               n, t_old * 1000, t_new * 1000, t_old / t_new);
    }
    free(buf);
    free(out);
}

/* ============================================================
 * Benchmark: to_upper SWAR vs byte-at-a-time
 * ============================================================ */

__attribute__((noinline))
static char *old_to_upper(const char *s) {
    size_t len = strlen(s);
    char *r = (char *)malloc(len + 1);
    for (size_t i = 0; i < len; i++)
        r[i] = (s[i] >= 'a' && s[i] <= 'z') ? (char)(s[i] - 32) : s[i];
    r[len] = '\0';
    return r;
}

static void bench_toupper(void) {
    printf("\n=== to_upper: auto-vectorized vs byte-at-a-time ===\n");
    printf("%-15s  %10s  %10s  %8s\n", "size", "old", "new", "speedup");

    int sizes[] = {16, 64, 256, 1024};
    int nsizes = sizeof(sizes) / sizeof(sizes[0]);

    for (int s = 0; s < nsizes; s++) {
        int n = sizes[s];
        char *input = (char *)malloc((size_t)n + 1);
        for (int i = 0; i < n; i++) input[i] = 'a' + (char)(i % 26);
        input[n] = '\0';

        int iters = 5000000 / (n + 1);
        if (iters < 1000) iters = 1000;

        double t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            char *r = old_to_upper(input);
            free(r);
        }
        double t_old = now_sec() - t0;

        t0 = now_sec();
        for (int i = 0; i < iters; i++) {
            char *r = neverc_cstring_to_upper(input);
            free(r);
        }
        double t_new = now_sec() - t0;

        printf("n=%-13d  %8.1f ms  %8.1f ms  %6.1fx\n",
               n, t_old * 1000, t_new * 1000, t_old / t_new);
        free(input);
    }
}

/* ============================================================
 * Benchmark: Lemire bounded random vs old modulo
 * ============================================================ */

__attribute__((noinline))
static uint32_t old_rand_uint32n(uint32_t n) {
    static uint64_t state[4] = {
        0x180ec6d33cfd0abaULL, 0xd5a61266f0c9392cULL,
        0xa9582618e03fc9aaULL, 0x39abdc4529b1661cULL
    };
    uint64_t result = ((state[1] * 5) << 7 | (state[1] * 5) >> 57) * 9;
    uint64_t t = state[1] << 17;
    state[2] ^= state[0]; state[3] ^= state[1];
    state[1] ^= state[2]; state[0] ^= state[3];
    state[2] ^= t; state[3] = (state[3] << 45) | (state[3] >> 19);
    return (uint32_t)((result >> 32) % (uint64_t)n);
}

static void bench_rand_bounded(void) {
    printf("\n=== Bounded Random: Lemire vs modulo ===\n");
    printf("%-15s  %10s  %10s  %8s\n", "bound", "old(mod)", "new(Lem)", "speedup");

    uint32_t bounds[] = {7, 100, 1000, 99991, 0xFFFFFFFFU};
    int nbounds = sizeof(bounds) / sizeof(bounds[0]);

    for (int b = 0; b < nbounds; b++) {
        uint32_t n = bounds[b];
        int iters = 20000000;

        double t0 = now_sec();
        for (int i = 0; i < iters; i++)
            sink32 = old_rand_uint32n(n);
        double t_old = now_sec() - t0;

        neverc_rand_seed(42);
        t0 = now_sec();
        for (int i = 0; i < iters; i++)
            sink32 = neverc_rand_uint32n(n);
        double t_new = now_sec() - t0;

        printf("n=%-13u  %8.1f ms  %8.1f ms  %6.1fx\n",
               n, t_old * 1000, t_new * 1000, t_old / t_new);
    }
}

/* ============================================================
 * Benchmark: Base64 encode unrolled vs single-group
 * ============================================================ */

__attribute__((noinline))
static size_t old_base64_encode(char *dst, const uint8_t *src, size_t src_len) {
    static const char tab[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t di = 0, si = 0;
    size_t n = (src_len / 3) * 3;
    while (si < n) {
        uint32_t val = ((uint32_t)src[si] << 16) |
                       ((uint32_t)src[si+1] << 8) | (uint32_t)src[si+2];
        dst[di]   = tab[(val >> 18) & 0x3f];
        dst[di+1] = tab[(val >> 12) & 0x3f];
        dst[di+2] = tab[(val >> 6)  & 0x3f];
        dst[di+3] = tab[val         & 0x3f];
        si += 3; di += 4;
    }
    size_t remain = src_len - si;
    if (remain == 1) {
        uint32_t val = (uint32_t)src[si] << 16;
        dst[di] = tab[(val>>18)&0x3f]; dst[di+1] = tab[(val>>12)&0x3f];
        dst[di+2] = '='; dst[di+3] = '='; di += 4;
    } else if (remain == 2) {
        uint32_t val = ((uint32_t)src[si] << 16) | ((uint32_t)src[si+1] << 8);
        dst[di] = tab[(val>>18)&0x3f]; dst[di+1] = tab[(val>>12)&0x3f];
        dst[di+2] = tab[(val>>6)&0x3f]; dst[di+3] = '='; di += 4;
    }
    dst[di] = '\0';
    return di;
}

static void bench_base64(void) {
    printf("\n=== Base64 Encode: 2x unrolled vs single-group ===\n");
    printf("%-15s  %10s  %10s  %8s\n", "size", "old", "new", "speedup");

    size_t sizes[] = {48, 300, 4096, 65536};
    int nsizes = sizeof(sizes) / sizeof(sizes[0]);
    uint8_t *buf = (uint8_t *)malloc(65536);
    char *out = (char *)malloc(65536 * 2);
    for (size_t i = 0; i < 65536; i++) buf[i] = (uint8_t)(rand() & 0xFF);

    for (int s = 0; s < nsizes; s++) {
        size_t n = sizes[s];
        int iters = (int)(200000000 / (n + 1));
        if (iters < 100) iters = 100;

        double t0 = now_sec();
        for (int i = 0; i < iters; i++)
            sink_sz = old_base64_encode(out, buf, n);
        double t_old = now_sec() - t0;

        t0 = now_sec();
        for (int i = 0; i < iters; i++)
            sink_sz = neverc_base64_encode(out, buf, n);
        double t_new = now_sec() - t0;

        printf("n=%-13zu  %8.1f ms  %8.1f ms  %6.1fx\n",
               n, t_old * 1000, t_new * 1000, t_old / t_new);
    }
    free(buf);
    free(out);
}

int main(void) {
    printf("=== std algorithm optimization benchmarks ===\n");
    bench_hash();
    bench_substr();
    bench_bsearch();
    bench_crc32();
    bench_adler32();
    bench_utf8();
    bench_hex();
    bench_toupper();
    bench_rand_bounded();
    bench_base64();
    printf("\n=== Done ===\n");
    return 0;
}
