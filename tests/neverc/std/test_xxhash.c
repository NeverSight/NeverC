#include "neverc/std/hash/xxhash.h"

#include <stdint.h>
#include <stdio.h>

#define XXH_TEST_PRIME32 UINT32_C(2654435761)
#define XXH_TEST_PRIME64 UINT64_C(11400714785074694797)

static int tests_run;
static int tests_failed;

static void check_u64(const char *name, uint64_t got, uint64_t expected) {
    tests_run++;
    if (got != expected) {
        tests_failed++;
        printf("  FAIL: %s: got 0x%016llx, expected 0x%016llx\n",
               name, (unsigned long long)got,
               (unsigned long long)expected);
    }
}

/*
 * These vectors and the deterministic input generator come from the upstream
 * xxHash sanity check. Keeping the generator here exercises every XXH64 input
 * path without deriving expected values from this implementation.
 */
static void fill_sanity_buffer(uint8_t *buffer, size_t length) {
    uint64_t byte_generator = XXH_TEST_PRIME32;
    for (size_t i = 0; i < length; i++) {
        buffer[i] = (uint8_t)(byte_generator >> 56);
        byte_generator *= XXH_TEST_PRIME64;
    }
}

static void test_upstream_vectors(void) {
    uint8_t buffer[222];
    fill_sanity_buffer(buffer, sizeof(buffer));

    check_u64("empty, seed 0",
              neverc_xxhash64(NULL, 0, 0),
              UINT64_C(0xef46db3751d8e999));
    check_u64("empty, seeded",
              neverc_xxhash64(NULL, 0, XXH_TEST_PRIME32),
              UINT64_C(0xac75fda2929b17ef));
    check_u64("length 1",
              neverc_xxhash64(buffer, 1, 0),
              UINT64_C(0xe934a84adb052768));
    check_u64("length 4",
              neverc_xxhash64(buffer, 4, 0),
              UINT64_C(0x9136a0dca57457ee));
    check_u64("length 14",
              neverc_xxhash64(buffer, 14, 0),
              UINT64_C(0x8282dcc4994e35c8));
    check_u64("length 14, seeded",
              neverc_xxhash64(buffer, 14, XXH_TEST_PRIME32),
              UINT64_C(0xc3bd6bf63deb6df0));
    check_u64("length 222",
              neverc_xxhash64(buffer, sizeof(buffer), 0),
              UINT64_C(0xb641ae8cb691c174));
    check_u64("length 222, seeded",
              neverc_xxhash64(buffer, sizeof(buffer), XXH_TEST_PRIME32),
              UINT64_C(0x20cb8ab7ae10c14a));
}

int main(void) {
    printf("=== NeverC xxHash64 Tests ===\n\n");
    test_upstream_vectors();
    printf("\n=== Results: %d/%d passed",
           tests_run - tests_failed, tests_run);
    if (tests_failed != 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    if (tests_failed == 0)
        puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
