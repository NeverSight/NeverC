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

    neverc_fnv_128_t ha = neverc_fnv_sum128("a", 1);
    tests_run++;
    if (ha.hi != h0.hi || ha.lo != h0.lo) tests_passed++;
    else { tests_failed++; printf("  FAIL: fnv128(a) should differ from empty\n"); }

    neverc_fnv_128_t h1 = neverc_fnv_sum128("test", 4);
    neverc_fnv_128_t h2 = neverc_fnv_sum128("test", 4);
    check_128("fnv128 deterministic", h2, h1.hi, h1.lo);
}

static void test_fnv128a(void) {
    printf("[fnv128a]\n");
    neverc_fnv_128_t h0 = neverc_fnv_sum128a("", 0);
    check_128("fnv128a(empty)", h0, 0x6c62272e07bb0142ULL, 0x62b821756295c58dULL);

    neverc_fnv_128_t ha = neverc_fnv_sum128a("a", 1);
    tests_run++;
    if (ha.hi != h0.hi || ha.lo != h0.lo) tests_passed++;
    else { tests_failed++; printf("  FAIL: fnv128a(a) should differ from empty\n"); }

    neverc_fnv_128_t h1_128 = neverc_fnv_sum128("abc", 3);
    neverc_fnv_128_t h1a_128 = neverc_fnv_sum128a("abc", 3);
    tests_run++;
    if (h1_128.hi != h1a_128.hi || h1_128.lo != h1a_128.lo) tests_passed++;
    else { tests_failed++; printf("  FAIL: fnv128 vs fnv128a should differ\n"); }
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

int main(void) {
    printf("=== NeverC FNV Library Tests ===\n\n");

    test_fnv32();
    test_fnv32a();
    test_fnv64();
    test_fnv64a();
    test_fnv128();
    test_fnv128a();
    test_consistency();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
