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

/*
 * Reference values computed from the FNV spec / Go hash/fnv test vectors.
 * See http://www.isthe.com/chongo/tech/comp/fnv/
 */
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
    /* Go hash/fnv golden32a */
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
    /* Go hash/fnv golden64a */
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

static void test_fnv128(void) {
    printf("[fnv128]\n");
    neverc_fnv_128_t h0 = neverc_fnv_sum128("", 0);
    check_128("fnv128(empty)", h0, 0x6c62272e07bb0142ULL, 0x62b821756295c58dULL);

    /* Go hash/fnv golden128 */
    check_128("fnv128(a)", neverc_fnv_sum128("a", 1),
              0xd228cb69101a8cafULL, 0x78912b704e4a141eULL);
    check_128("fnv128(ab)", neverc_fnv_sum128("ab", 2),
              0x0880945aeeab1be9ULL, 0x5aa073305526c088ULL);
    check_128("fnv128(abc)", neverc_fnv_sum128("abc", 3),
              0xa68bb2a4348b5822ULL, 0x836dbc78c6aee73bULL);

    /* Longer than 8 bytes: exercises the unrolled loop and 128-bit mul carry.
     * Independent of this implementation: FNV-1-128 with offset/prime from
     * Go hash/fnv (prime = 2^88 + 0x13b). */
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

    /* Go hash/fnv golden128a */
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
}

int main(void) {
    printf("=== NeverC FNV Library Tests ===\n\n");

    test_fnv32();
    test_fnv32a();
    test_fnv64();
    test_fnv64a();
    test_fnv128();
    test_fnv128a();
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
