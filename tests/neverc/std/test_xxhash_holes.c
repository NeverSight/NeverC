#include "neverc/std/hash/xxhash.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>

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

static void test_hash64_alias_matches_xxhash64(void) {
    const uint8_t data[] = { 'a', 'b', 'c' };

    check_u64("empty seed 0",
              neverc_xxhash_hash64(NULL, 0, 0),
              neverc_xxhash64(NULL, 0, 0));
    check_u64("empty seed 0 known",
              neverc_xxhash_hash64(NULL, 0, 0),
              UINT64_C(0xef46db3751d8e999));
    check_u64("null data with nonzero len",
              neverc_xxhash_hash64(NULL, 100, 7),
              neverc_xxhash64(NULL, 100, 7));
    check_u64("abc seed 0",
              neverc_xxhash_hash64(data, sizeof(data), 0),
              neverc_xxhash64(data, sizeof(data), 0));
    check_u64("abc seeded",
              neverc_xxhash_hash64(data, sizeof(data), 42),
              neverc_xxhash64(data, sizeof(data), 42));
}

int main(void) {
    printf("=== NeverC xxHash holes ===\n");
    test_hash64_alias_matches_xxhash64();
    printf("=== Results: %d/%d passed",
           tests_run - tests_failed, tests_run);
    if (tests_failed != 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    if (tests_failed == 0)
        puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
