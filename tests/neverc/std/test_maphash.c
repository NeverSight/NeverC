/*
 * NeverC hash/maphash tests.
 * Tests wyhash-based non-cryptographic hash: determinism, distribution, streaming.
 */
#include "neverc/std/hash/maphash.h"
#include "../../../std/src/hash/_wyhash_final3.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_TRUE(expr) do { tests_run++; \
    if (expr) tests_passed++; \
    else { tests_failed++; printf("  FAIL: %s\n", #expr); } } while(0)

#define ASSERT_U64_EQ(expr, expected) do { \
    uint64_t _v = (expr); uint64_t _e = (expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %llu, expected %llu\n", #expr, \
                  (unsigned long long)_v, (unsigned long long)_e); } \
} while(0)

#define ASSERT_U64_NE(expr, unexpected) do { \
    uint64_t _v = (expr); uint64_t _u = (unexpected); tests_run++; \
    if (_v != _u) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s should not be %llu\n", #expr, (unsigned long long)_u); } \
} while(0)

static void test_determinism(void) {
    printf("[determinism]\n");
    uint64_t seed = 12345;
    uint64_t h1 = neverc_maphash_bytes(seed, "hello", 5);
    uint64_t h2 = neverc_maphash_bytes(seed, "hello", 5);
    ASSERT_U64_EQ(h1, h2);

    uint64_t h3 = neverc_maphash_string(seed, "hello");
    ASSERT_U64_EQ(h1, h3);
}

static void test_pinned_core_parity(void) {
    printf("[pinned_core_parity]\n");
    uint8_t data[128];
    for (size_t i = 0; i < sizeof(data); i++)
        data[i] = (uint8_t)(i * 37U + 11U);
    static const size_t sizes[] = {0, 1, 3, 4, 16, 17, 48, 49, 127, 128};
    static const uint64_t seeds[] = {0, UINT64_C(0x0123456789abcdef)};
    for (size_t s = 0; s < sizeof(seeds) / sizeof(seeds[0]); s++) {
        for (size_t n = 0; n < sizeof(sizes) / sizeof(sizes[0]); n++) {
            ASSERT_U64_EQ(neverc_maphash_bytes(seeds[s], data, sizes[n]),
                          nci_wyhash_final3(data, sizes[n], seeds[s]));
        }
    }
}

static void test_different_seeds(void) {
    printf("[different_seeds]\n");
    uint64_t h1 = neverc_maphash_string(111, "test");
    uint64_t h2 = neverc_maphash_string(222, "test");
    ASSERT_U64_NE(h1, h2);
}

static void test_different_data(void) {
    printf("[different_data]\n");
    uint64_t seed = 42;
    uint64_t h1 = neverc_maphash_string(seed, "abc");
    uint64_t h2 = neverc_maphash_string(seed, "abd");
    ASSERT_U64_NE(h1, h2);

    uint64_t h3 = neverc_maphash_string(seed, "");
    uint64_t h4 = neverc_maphash_string(seed, "a");
    ASSERT_U64_NE(h3, h4);
}

static void test_streaming(void) {
    printf("[streaming]\n");
    uint64_t seed = 99999;

    /* one-shot */
    uint64_t oneshot = neverc_maphash_string(seed, "hello world");

    /* streaming in chunks */
    neverc_maphash_t h;
    neverc_maphash_init(&h, seed);
    ASSERT_U64_EQ(neverc_maphash_write(&h, "hello ", 6), 6);
    ASSERT_U64_EQ(neverc_maphash_write(&h, "world", 5), 5);
    uint64_t streamed = neverc_maphash_sum64(&h);

    /* streaming byte-by-byte */
    neverc_maphash_init(&h, seed);
    const char *s = "hello world";
    for (int i = 0; s[i]; i++)
        neverc_maphash_write_byte(&h, (uint8_t)s[i]);
    uint64_t bytebyb = neverc_maphash_sum64(&h);

    ASSERT_U64_EQ(oneshot, streamed);
    ASSERT_U64_EQ(oneshot, bytebyb);
}

static void test_reset(void) {
    printf("[reset]\n");
    neverc_maphash_t h;
    neverc_maphash_init(&h, 42);
    neverc_maphash_write_string(&h, "abc");
    uint64_t h1 = neverc_maphash_sum64(&h);

    neverc_maphash_reset(&h);
    neverc_maphash_write_string(&h, "abc");
    uint64_t h2 = neverc_maphash_sum64(&h);
    ASSERT_U64_EQ(h1, h2);

    neverc_maphash_reset(&h);
    neverc_maphash_write_string(&h, "xyz");
    uint64_t h3 = neverc_maphash_sum64(&h);
    ASSERT_U64_NE(h1, h3);
}

static void test_make_seed(void) {
    printf("[make_seed]\n");
    uint64_t s1 = neverc_maphash_make_seed();
    uint64_t s2 = neverc_maphash_make_seed();
    ASSERT_U64_NE(s1, 0);
    ASSERT_U64_NE(s2, 0);
    ASSERT_U64_NE(s1, s2);
}

static void test_seed_zero(void) {
    printf("[seed_zero]\n");
    uint64_t oneshot = neverc_maphash_bytes(0, "hello", 5);
    ASSERT_U64_EQ(neverc_maphash_string(0, "hello"), oneshot);
    ASSERT_U64_NE(oneshot, 0);

    neverc_maphash_t h;
    neverc_maphash_init(&h, 0);
    ASSERT_U64_EQ(neverc_maphash_sum64(&h), neverc_maphash_bytes(0, "", 0));
    neverc_maphash_write(&h, "hello", 5);
    ASSERT_U64_EQ(neverc_maphash_sum64(&h), oneshot);

    neverc_maphash_init(&h, 0);
    const char *s = "hello";
    for (int i = 0; s[i]; i++)
        neverc_maphash_write_byte(&h, (uint8_t)s[i]);
    ASSERT_U64_EQ(neverc_maphash_sum64(&h), oneshot);

    neverc_maphash_reset(&h);
    ASSERT_U64_EQ(neverc_maphash_sum64(&h), neverc_maphash_bytes(0, "", 0));
    neverc_maphash_write_string(&h, "hello");
    ASSERT_U64_EQ(neverc_maphash_sum64(&h), oneshot);

    char buf[200];
    memset(buf, 'Z', sizeof(buf));
    uint64_t long_oneshot = neverc_maphash_bytes(0, buf, sizeof(buf));
    neverc_maphash_init(&h, 0);
    neverc_maphash_write(&h, buf, sizeof(buf));
    ASSERT_U64_EQ(neverc_maphash_sum64(&h), long_oneshot);
}

static void test_empty_data(void) {
    printf("[empty_data]\n");
    uint64_t h1 = neverc_maphash_bytes(42, "", 0);
    uint64_t h2 = neverc_maphash_bytes(42, "a", 1);
    ASSERT_U64_NE(h1, h2);
    /* Empty input must still mix the seed; returning the seed leaks it. */
    ASSERT_U64_NE(h1, 42);
    ASSERT_U64_EQ(neverc_maphash_bytes(42, NULL, 0), h1);
    ASSERT_U64_EQ(neverc_maphash_string(42, NULL), h1);

    neverc_maphash_t h;
    neverc_maphash_init(&h, 42);
    neverc_maphash_write(&h, NULL, 0);
    ASSERT_U64_EQ(neverc_maphash_sum64(&h), h1);

    neverc_maphash_init(&h, 42);
    neverc_maphash_write_string(&h, NULL);
    ASSERT_U64_EQ(neverc_maphash_sum64(&h), h1);
}

static void test_buf_size_boundary(void) {
    printf("[buf_size_boundary]\n");
    uint64_t seed = 0x9e3779b97f4a7c15ULL;
    char buf[256];
    memset(buf, 'Q', sizeof(buf));

    const size_t sizes[] = {127, 128, 129, 256};
    for (size_t s = 0; s < sizeof(sizes) / sizeof(sizes[0]); s++) {
        size_t n = sizes[s];
        uint64_t oneshot = neverc_maphash_bytes(seed, buf, n);

        neverc_maphash_t h;
        neverc_maphash_init(&h, seed);
        ASSERT_U64_EQ(neverc_maphash_write(&h, buf, n), n);
        ASSERT_U64_EQ(neverc_maphash_sum64(&h), oneshot);

        neverc_maphash_init(&h, seed);
        for (size_t i = 0; i < n; i++)
            neverc_maphash_write_byte(&h, (uint8_t)buf[i]);
        ASSERT_U64_EQ(neverc_maphash_sum64(&h), oneshot);

        /* Split so the first write lands exactly on the 128-byte flush. */
        neverc_maphash_init(&h, seed);
        size_t first = n > 128 ? 128 : n;
        neverc_maphash_write(&h, buf, first);
        uint64_t mid = neverc_maphash_sum64(&h);
        ASSERT_U64_EQ(mid, neverc_maphash_bytes(seed, buf, first));
        if (first < n) {
            neverc_maphash_write(&h, buf + first, n - first);
            ASSERT_U64_EQ(neverc_maphash_sum64(&h), oneshot);
        }
    }
}

static void test_large_data(void) {
    printf("[large_data]\n");
    char buf[1024];
    memset(buf, 'A', sizeof(buf));
    uint64_t seed = 777;

    uint64_t h1 = neverc_maphash_bytes(seed, buf, sizeof(buf));

    neverc_maphash_t h;
    neverc_maphash_init(&h, seed);
    neverc_maphash_write(&h, buf, sizeof(buf));
    uint64_t h2 = neverc_maphash_sum64(&h);
    ASSERT_U64_EQ(h1, h2);

    /* streaming in 100-byte chunks */
    neverc_maphash_init(&h, seed);
    for (int i = 0; i < 1024; i += 100) {
        int chunk = 100;
        if (i + chunk > 1024) chunk = 1024 - i;
        neverc_maphash_write(&h, buf + i, (size_t)chunk);
    }
    uint64_t h3 = neverc_maphash_sum64(&h);
    ASSERT_U64_EQ(h1, h3);
}

static void test_write_n(void) {
    printf("[write_n]\n");
    neverc_maphash_t h;
    neverc_maphash_init(&h, 1);
    ASSERT_U64_EQ(neverc_maphash_write(&h, "abc", 3), 3);
    uint64_t after_abc = neverc_maphash_sum64(&h);
    ASSERT_U64_EQ(neverc_maphash_write(&h, "abc", 0), 0);
    ASSERT_U64_EQ(neverc_maphash_write(&h, NULL, 0), 0);
    /* Error: NULL data with len>0 must not claim n==len, and must not
     * consume bytes. */
    ASSERT_U64_EQ(neverc_maphash_write(&h, NULL, 4), 0);
    ASSERT_U64_EQ(neverc_maphash_sum64(&h), after_abc);
    ASSERT_U64_EQ(after_abc, neverc_maphash_bytes(1, "abc", 3));
    ASSERT_U64_EQ(neverc_maphash_write(NULL, "abc", 3), 0);
    ASSERT_U64_EQ(neverc_maphash_write_byte(NULL, 'x'), 0);
    ASSERT_U64_EQ(neverc_maphash_write_string(NULL, "abc"), 0);
    ASSERT_U64_EQ(neverc_maphash_write_byte(&h, 'x'), 1);
    ASSERT_U64_EQ(neverc_maphash_write_string(&h, "yz"), 2);
}

static void test_sum_reuse(void) {
    printf("[sum_reuse]\n");
    uint64_t seed = 7;
    neverc_maphash_t h;
    neverc_maphash_init(&h, seed);
    neverc_maphash_write(&h, "hello", 5);
    uint64_t s1 = neverc_maphash_sum64(&h);
    uint64_t s1b = neverc_maphash_sum64(&h);
    ASSERT_U64_EQ(s1, s1b);
    ASSERT_U64_EQ(s1, neverc_maphash_bytes(seed, "hello", 5));

    /* Sum must not freeze the hasher: Write after Sum continues the stream. */
    neverc_maphash_write(&h, " world", 6);
    uint64_t s2 = neverc_maphash_sum64(&h);
    ASSERT_U64_EQ(s2, neverc_maphash_bytes(seed, "hello world", 11));
    ASSERT_U64_NE(s1, s2);
}

static void test_distribution(void) {
    printf("[distribution]\n");
    /* Hash 1000 sequential integers and check that bits are well distributed */
    uint64_t seed = 31415;
    uint64_t bit_counts[64];
    memset(bit_counts, 0, sizeof(bit_counts));

    for (int i = 0; i < 1000; i++) {
        uint64_t h = neverc_maphash_bytes(seed, &i, sizeof(i));
        for (int b = 0; b < 64; b++)
            if (h & (1ULL << b))
                bit_counts[b]++;
    }

    int good_bits = 0;
    for (int b = 0; b < 64; b++) {
        /* each bit should be set ~500 times out of 1000 (±15% tolerance) */
        if (bit_counts[b] > 350 && bit_counts[b] < 650)
            good_bits++;
    }
    /* at least 58/64 bits should pass the distribution check */
    tests_run++;
    if (good_bits >= 58) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: only %d/64 bits well-distributed\n", good_bits);
    }
}

int main(void) {
    printf("=== NeverC hash/maphash Tests ===\n");
    test_determinism();
    test_pinned_core_parity();
    test_different_seeds();
    test_different_data();
    test_streaming();
    test_reset();
    test_make_seed();
    test_seed_zero();
    test_empty_data();
    test_buf_size_boundary();
    test_large_data();
    test_write_n();
    test_sum_reuse();
    test_distribution();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0)
        puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
