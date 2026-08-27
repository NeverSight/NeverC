#include "neverc/std/hash/fnv.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static void check_u32(const char *name, uint32_t got, uint32_t expected) {
    tests_run++;
    if (got == expected) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s: got 0x%08x, expected 0x%08x\n", name, got, expected);
    }
}

static void check_u64(const char *name, uint64_t got, uint64_t expected) {
    tests_run++;
    if (got == expected) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s: got 0x%016llx, expected 0x%016llx\n",
               name, (unsigned long long)got, (unsigned long long)expected);
    }
}

static void test_fnv32(void) {
    printf("[fnv32]\n");
    check_u32("fnv32(empty)", neverc_fnv_32("", 0), 0x811c9dc5);
    check_u32("fnv32(a)",     neverc_fnv_32("a", 1), 0x050c5d7e);
    check_u32("fnv32(ab)",    neverc_fnv_32("ab", 2), 0x70772d38);
    check_u32("fnv32(abc)",   neverc_fnv_32("abc", 3), 0x439c2f4b);
}

static void test_fnv32a(void) {
    printf("[fnv32a]\n");
    check_u32("fnv32a(empty)", neverc_fnv_32a("", 0), 0x811c9dc5);
    check_u32("fnv32a(a)",     neverc_fnv_32a("a", 1), 0xe40c292c);
    check_u32("fnv32a(ab)",    neverc_fnv_32a("ab", 2), 0x4d2505ca);
    check_u32("fnv32a(abc)",   neverc_fnv_32a("abc", 3), 0x1a47e90b);
    check_u32("fnv32a(foobar)", neverc_fnv_32a("foobar", 6), 0xbf9cf968);
}

static void test_fnv64(void) {
    printf("[fnv64]\n");
    check_u64("fnv64(empty)", neverc_fnv_64("", 0), 0xcbf29ce484222325ULL);
    check_u64("fnv64(a)",     neverc_fnv_64("a", 1), 0xaf63bd4c8601b7beULL);
    check_u64("fnv64(ab)",    neverc_fnv_64("ab", 2), 0x08326707b4eb37b8ULL);
    check_u64("fnv64(abc)",   neverc_fnv_64("abc", 3), 0xd8dcca186bafadcbULL);
}

static void test_fnv64a(void) {
    printf("[fnv64a]\n");
    check_u64("fnv64a(empty)", neverc_fnv_64a("", 0), 0xcbf29ce484222325ULL);
    check_u64("fnv64a(a)",     neverc_fnv_64a("a", 1), 0xaf63dc4c8601ec8cULL);
    check_u64("fnv64a(ab)",    neverc_fnv_64a("ab", 2), 0x089c4407b545986aULL);
    check_u64("fnv64a(abc)",   neverc_fnv_64a("abc", 3), 0xe71fa2190541574bULL);
    check_u64("fnv64a(foobar)", neverc_fnv_64a("foobar", 6), 0x85944171f73967e8ULL);
}

static void check_128(const char *name, neverc_fnv_128_t got, uint64_t exp_hi, uint64_t exp_lo) {
    tests_run++;
    if (got.hi == exp_hi && got.lo == exp_lo) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s: got {0x%016llx, 0x%016llx}, expected {0x%016llx, 0x%016llx}\n",
               name, (unsigned long long)got.hi, (unsigned long long)got.lo,
               (unsigned long long)exp_hi, (unsigned long long)exp_lo);
    }
}

static void check_bytes(const char *name, const uint8_t *got,
                        const uint8_t *expected, size_t len) {
    tests_run++;
    if (memcmp(got, expected, len) == 0) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s\n", name);
    }
}

static void check_reversed(const char *name, const uint8_t *forward,
                           const uint8_t *reverse, size_t len) {
    tests_run++;
    for (size_t i = 0; i < len; i++) {
        if (forward[i] != reverse[len - i - 1]) {
            tests_failed++;
            printf("  FAIL: %s at byte %zu\n", name, i);
            return;
        }
    }
    tests_passed++;
}

static void test_fnv128(void) {
    printf("[fnv128]\n");
    neverc_fnv_128_t h0 = neverc_fnv_sum128("", 0);
    check_128("fnv128(empty)", h0, 0x6c62272e07bb0142ULL, 0x62b821756295c58dULL);

    check_128("fnv128(a)", neverc_fnv_sum128("a", 1),
              0xd228cb69101a8cafULL, 0x78912b704e4a141eULL);
    check_128("fnv128(ab)", neverc_fnv_sum128("ab", 2),
              0x0880945aeeab1be9ULL, 0x5aa073305526c088ULL);
    check_128("fnv128(abc)", neverc_fnv_sum128("abc", 3),
              0xa68bb2a4348b5822ULL, 0x836dbc78c6aee73bULL);

    /* Longer than 8 bytes exercises the unrolled loop and multiply carry. */
    static const uint8_t ff32[32] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };
    check_128("fnv128(32*0xff)", neverc_fnv_sum128(ff32, sizeof ff32),
              0xec8a9f9627439590ULL, 0x4eb76e4cc7af052dULL);
    check_128("fnv128(fox)",
              neverc_fnv_sum128("The quick brown fox jumps over the lazy dog", 43),
              0x185adb693e7c9784ULL, 0x4ecfa9497cb529b6ULL);
}

static void test_fnv128a(void) {
    printf("[fnv128a]\n");
    neverc_fnv_128_t h0 = neverc_fnv_sum128a("", 0);
    check_128("fnv128a(empty)", h0, 0x6c62272e07bb0142ULL, 0x62b821756295c58dULL);

    check_128("fnv128a(a)", neverc_fnv_sum128a("a", 1),
              0xd228cb696f1a8cafULL, 0x78912b704e4a8964ULL);
    check_128("fnv128a(ab)", neverc_fnv_sum128a("ab", 2),
              0x08809544bbab1be9ULL, 0x5aa0733055b69a62ULL);
    check_128("fnv128a(abc)", neverc_fnv_sum128a("abc", 3),
              0xa68d622cec8b5822ULL, 0x836dbc7977af7f3bULL);
    check_128("fnv128a(foobar)", neverc_fnv_sum128a("foobar", 6),
              0x343e1662793c64bfULL, 0x6f0d3597ba446f18ULL);
    static const uint8_t ff32[32] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff
    };
    check_128("fnv128a(32*0xff)", neverc_fnv_sum128a(ff32, sizeof ff32),
              0x435897b305839886ULL, 0xf6d3fe804b419a6dULL);
    check_128("fnv128a(fox)",
              neverc_fnv_sum128a("The quick brown fox jumps over the lazy dog", 43),
              0x68cce4cd885ea042ULL, 0x39f02af30e297870ULL);
}

static void test_fnv0(void) {
    printf("[fnv0]\n");
    check_u32("fnv0-32(empty)", neverc_fnv0_sum32("", 0), 0);
    check_u32("fnv0-32(foobar)", neverc_fnv0_sum32("foobar", 6),
              0xb74bb5efU);
    check_u64("fnv0-64(empty)", neverc_fnv0_sum64("", 0), 0);
    check_u64("fnv0-64(foobar)", neverc_fnv0_sum64("foobar", 6),
              UINT64_C(0x0b91ae3f7ccdc5ef));
    check_128("fnv0-128(empty)", neverc_fnv0_sum128("", 0), 0, 0);
    check_128("fnv0-128(foobar)", neverc_fnv0_sum128("foobar", 6),
              UINT64_C(0x9438ff4bea000000),
              UINT64_C(0x000120ab5188d04f));
}

static void test_serialization(void) {
    printf("[serialization]\n");
    uint8_t be32[4], le32[4], be64[8], le64[8], be128[16], le128[16];
    uint32_t h32 = UINT32_C(0x01234567);
    uint64_t h64 = UINT64_C(0x0123456789abcdef);
    neverc_fnv_128_t h128 = {
        UINT64_C(0x0123456789abcdef),
        UINT64_C(0xfedcba9876543210)
    };
    neverc_fnv_store32_be(be32, h32);
    neverc_fnv_store32_le(le32, h32);
    neverc_fnv_store64_be(be64, h64);
    neverc_fnv_store64_le(le64, h64);
    neverc_fnv_store128_be(be128, h128);
    neverc_fnv_store128_le(le128, h128);
    neverc_fnv_store32_be(NULL, h32);
    neverc_fnv_store64_le(NULL, h64);
    neverc_fnv_store128_be(NULL, h128);

    static const uint8_t expected32[] = {0x01, 0x23, 0x45, 0x67};
    static const uint8_t expected64[] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef
    };
    static const uint8_t expected128[] = {
        0x01, 0x23, 0x45, 0x67, 0x89, 0xab, 0xcd, 0xef,
        0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32, 0x10
    };
    check_bytes("fnv32 big-endian", be32, expected32, sizeof(be32));
    check_bytes("fnv64 big-endian", be64, expected64, sizeof(be64));
    check_bytes("fnv128 big-endian", be128, expected128, sizeof(be128));
    check_reversed("fnv32 little-endian", be32, le32, sizeof(be32));
    check_reversed("fnv64 little-endian", be64, le64, sizeof(be64));
    check_reversed("fnv128 little-endian", be128, le128, sizeof(be128));
}

static void test_binary_inputs(void) {
    printf("[binary inputs]\n");
    static const uint8_t a_nul[] = {'a', 0};
    check_u32("fnv32(a\\0)", neverc_fnv_sum32(a_nul, sizeof(a_nul)),
              0x70772d5aU);
    check_u32("fnv32a(a\\0)", neverc_fnv_sum32a(a_nul, sizeof(a_nul)),
              0x2b24d044U);
    check_u64("fnv64(a\\0)", neverc_fnv_sum64(a_nul, sizeof(a_nul)),
              0x08326707b4eb37daULL);
    check_u64("fnv64a(a\\0)", neverc_fnv_sum64a(a_nul, sizeof(a_nul)),
              0x089be207b544f1e4ULL);
}

static void test_long_vectors(void) {
    printf("[long vectors]\n");
    static const struct {
        const char *name;
        const char *data;
        size_t len;
        uint32_t fnv32;
        uint32_t fnv32a;
        uint64_t fnv64;
        uint64_t fnv64a;
    } vectors[] = {
        {"len8", "12345678", 8, 0x043ef075U, 0x0aa8abcdU,
         0x30d2b8e185b11fd5ULL, 0x173932c41a90a42dULL},
        {"len9", "123456789", 9, 0x24148816U, 0xbb86b11cU,
         0xa72ffc362bf916d6ULL, 0x06d5573923c6cdfcULL},
        {"len16", "0123456789abcdef", 16, 0x9e1b6f41U, 0x87bc333dU,
         0x4fa333c33b82ecc1ULL, 0x2e373913e5ad677dULL},
        {"fox", "The quick brown fox jumps over the lazy dog", 43,
         0xe9c86c6eU, 0x048fff90U, 0xa8b2f3117de37aceULL,
         0xf3f9b7f5e7e47110ULL}
    };

    for (size_t i = 0; i < sizeof(vectors) / sizeof(vectors[0]); i++) {
        char label[64];
        snprintf(label, sizeof(label), "fnv32(%s)", vectors[i].name);
        check_u32(label, neverc_fnv_sum32(vectors[i].data, vectors[i].len),
                  vectors[i].fnv32);
        snprintf(label, sizeof(label), "fnv32a(%s)", vectors[i].name);
        check_u32(label, neverc_fnv_sum32a(vectors[i].data, vectors[i].len),
                  vectors[i].fnv32a);
        snprintf(label, sizeof(label), "fnv64(%s)", vectors[i].name);
        check_u64(label, neverc_fnv_sum64(vectors[i].data, vectors[i].len),
                  vectors[i].fnv64);
        snprintf(label, sizeof(label), "fnv64a(%s)", vectors[i].name);
        check_u64(label, neverc_fnv_sum64a(vectors[i].data, vectors[i].len),
                  vectors[i].fnv64a);
    }
}

static void test_incremental(void) {
    printf("[incremental]\n");
    static const uint8_t data[] = {
        'f', 'o', 'o', 0, 'b', 'a', 'r', 0xff, 0x80,
        0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08
    };
    const size_t first = 3;
    const size_t second = 6;

    uint32_t h32 = NEVERC_FNV32_OFFSET_BASIS;
    h32 = neverc_fnv_update32(h32, data, first);
    h32 = neverc_fnv_update32(h32, data + first, second);
    h32 = neverc_fnv_update32(h32, data + first + second,
                              sizeof(data) - first - second);
    check_u32("fnv32 incremental", h32,
              neverc_fnv_sum32(data, sizeof(data)));

    uint32_t h32a = NEVERC_FNV32_OFFSET_BASIS;
    h32a = neverc_fnv_update32a(h32a, data, first);
    h32a = neverc_fnv_update32a(h32a, data + first, second);
    h32a = neverc_fnv_update32a(h32a, data + first + second,
                                sizeof(data) - first - second);
    check_u32("fnv32a incremental", h32a,
              neverc_fnv_sum32a(data, sizeof(data)));

    uint64_t h64 = NEVERC_FNV64_OFFSET_BASIS;
    h64 = neverc_fnv_update64(h64, data, first);
    h64 = neverc_fnv_update64(h64, data + first, second);
    h64 = neverc_fnv_update64(h64, data + first + second,
                              sizeof(data) - first - second);
    check_u64("fnv64 incremental", h64,
              neverc_fnv_sum64(data, sizeof(data)));

    uint64_t h64a = NEVERC_FNV64_OFFSET_BASIS;
    h64a = neverc_fnv_update64a(h64a, data, first);
    h64a = neverc_fnv_update64a(h64a, data + first, second);
    h64a = neverc_fnv_update64a(h64a, data + first + second,
                                sizeof(data) - first - second);
    check_u64("fnv64a incremental", h64a,
              neverc_fnv_sum64a(data, sizeof(data)));

    neverc_fnv_128_t h128 = {
        NEVERC_FNV128_OFFSET_BASIS_HI,
        NEVERC_FNV128_OFFSET_BASIS_LO
    };
    h128 = neverc_fnv_update128(h128, data, first);
    h128 = neverc_fnv_update128(h128, data + first, second);
    h128 = neverc_fnv_update128(h128, data + first + second,
                                sizeof(data) - first - second);
    neverc_fnv_128_t full128 = neverc_fnv_sum128(data, sizeof(data));
    check_128("fnv128 incremental", h128, full128.hi, full128.lo);

    neverc_fnv_128_t h128a = {
        NEVERC_FNV128_OFFSET_BASIS_HI,
        NEVERC_FNV128_OFFSET_BASIS_LO
    };
    h128a = neverc_fnv_update128a(h128a, data, first);
    h128a = neverc_fnv_update128a(h128a, data + first, second);
    h128a = neverc_fnv_update128a(h128a, data + first + second,
                                  sizeof(data) - first - second);
    neverc_fnv_128_t full128a = neverc_fnv_sum128a(data, sizeof(data));
    check_128("fnv128a incremental", h128a, full128a.hi, full128a.lo);
}

static void test_consistency(void) {
    printf("[consistency]\n");
    const char *data = "The quick brown fox jumps over the lazy dog";
    size_t len = strlen(data);

    uint32_t h1 = neverc_fnv_32(data, len);
    uint32_t h2 = neverc_fnv_32(data, len);
    tests_run++;
    if (h1 == h2) tests_passed++;
    else { tests_failed++; printf("  FAIL: fnv32 consistency\n"); }

    uint32_t h3 = neverc_fnv_32a(data, len);
    tests_run++;
    if (h1 != h3) tests_passed++;
    else { tests_failed++; printf("  FAIL: fnv32 vs fnv32a should differ\n"); }
}

static void test_null_data(void) {
    printf("[null data]\n");
    check_u32("fnv32(null)", neverc_fnv_32(NULL, 8), neverc_fnv_32("", 0));
    check_u32("fnv32a(null)", neverc_fnv_32a(NULL, 8), neverc_fnv_32a("", 0));
    check_u64("fnv64(null)", neverc_fnv_64(NULL, 8), neverc_fnv_64("", 0));
    check_u64("fnv64a(null)", neverc_fnv_64a(NULL, 8), neverc_fnv_64a("", 0));
    neverc_fnv_128_t z = neverc_fnv_sum128(NULL, 8);
    neverc_fnv_128_t e = neverc_fnv_sum128("", 0);
    check_128("fnv128(null)", z, e.hi, e.lo);
    z = neverc_fnv_sum128a(NULL, 8);
    e = neverc_fnv_sum128a("", 0);
    check_128("fnv128a(null)", z, e.hi, e.lo);

    check_u32("fnv32 update null",
              neverc_fnv_update32(0x12345678U, NULL, 8), 0x12345678U);
    check_u32("fnv32a update null",
              neverc_fnv_update32a(0x12345678U, NULL, 8), 0x12345678U);
    check_u64("fnv64 update null",
              neverc_fnv_update64(0x123456789abcdef0ULL, NULL, 8),
              0x123456789abcdef0ULL);
    check_u64("fnv64a update null",
              neverc_fnv_update64a(0x123456789abcdef0ULL, NULL, 8),
              0x123456789abcdef0ULL);
    neverc_fnv_128_t seed = {
        0x0123456789abcdefULL, 0xfedcba9876543210ULL
    };
    z = neverc_fnv_update128(seed, NULL, 8);
    check_128("fnv128 update null", z, seed.hi, seed.lo);
    z = neverc_fnv_update128a(seed, NULL, 8);
    check_128("fnv128a update null", z, seed.hi, seed.lo);
}

int main(void) {
    printf("=== NeverC FNV Library Tests ===\n\n");

    test_fnv32();
    test_fnv32a();
    test_fnv64();
    test_fnv64a();
    test_fnv128();
    test_fnv128a();
    test_fnv0();
    test_serialization();
    test_binary_inputs();
    test_long_vectors();
    test_incremental();
    test_consistency();
    test_null_data();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    if (tests_failed == 0)
        puts("passed");

    return tests_failed > 0 ? 1 : 0;
}
