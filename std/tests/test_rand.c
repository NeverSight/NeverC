#include "neverc/rand.h"
#include <stdio.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

static void test_seed_determinism(void) {
    printf("[seed_determinism]\n");
    neverc_rand_seed(42);
    uint64_t a1 = neverc_rand_uint64();
    uint64_t a2 = neverc_rand_uint64();

    neverc_rand_seed(42);
    uint64_t b1 = neverc_rand_uint64();
    uint64_t b2 = neverc_rand_uint64();

    check_true("same seed same seq[0]", a1 == b1);
    check_true("same seed same seq[1]", a2 == b2);

    neverc_rand_seed(99);
    uint64_t c1 = neverc_rand_uint64();
    check_true("diff seed diff output", c1 != a1);
}

static void test_uint32(void) {
    printf("[uint32]\n");
    neverc_rand_seed(1);
    int saw_nonzero = 0;
    for (int i = 0; i < 100; i++) {
        uint32_t v = neverc_rand_uint32();
        if (v != 0) saw_nonzero = 1;
    }
    check_true("uint32 produces non-zero", saw_nonzero);
}

static void test_int63(void) {
    printf("[int63]\n");
    neverc_rand_seed(123);
    for (int i = 0; i < 1000; i++) {
        int64_t v = neverc_rand_int63();
        if (v < 0) {
            tests_run++;
            tests_failed++;
            printf("  FAIL: int63 returned negative: %lld\n", (long long)v);
            return;
        }
    }
    tests_run++;
    tests_passed++;
}

static void test_intn(void) {
    printf("[intn]\n");
    neverc_rand_seed(456);
    int all_in_range = 1;
    for (int i = 0; i < 1000; i++) {
        int64_t v = neverc_rand_intn(10);
        if (v < 0 || v >= 10) { all_in_range = 0; break; }
    }
    check_true("intn(10) in [0,10)", all_in_range);

    check_true("intn(1) == 0", neverc_rand_intn(1) == 0);
}

static void test_float64(void) {
    printf("[float64]\n");
    neverc_rand_seed(789);
    int all_in_range = 1;
    for (int i = 0; i < 1000; i++) {
        double v = neverc_rand_float64();
        if (v < 0.0 || v >= 1.0) { all_in_range = 0; break; }
    }
    check_true("float64 in [0,1)", all_in_range);
}

static void test_float32(void) {
    printf("[float32]\n");
    neverc_rand_seed(321);
    int all_in_range = 1;
    for (int i = 0; i < 1000; i++) {
        float v = neverc_rand_float32();
        if (v < 0.0f || v >= 1.0f) { all_in_range = 0; break; }
    }
    check_true("float32 in [0,1)", all_in_range);
}

static int shuffle_arr[10];
static void swap_int(int i, int j) {
    int tmp = shuffle_arr[i];
    shuffle_arr[i] = shuffle_arr[j];
    shuffle_arr[j] = tmp;
}

static void test_shuffle(void) {
    printf("[shuffle]\n");
    for (int i = 0; i < 10; i++) shuffle_arr[i] = i;

    neverc_rand_seed(999);
    neverc_rand_shuffle(10, swap_int);

    int sum = 0;
    int identity = 1;
    for (int i = 0; i < 10; i++) {
        sum += shuffle_arr[i];
        if (shuffle_arr[i] != i) identity = 0;
    }
    check_true("shuffle preserves elements (sum)", sum == 45);
    check_true("shuffle actually reorders", !identity);
}

int main(void) {
    printf("=== NeverC Rand Library Tests ===\n\n");

    test_seed_determinism();
    test_uint32();
    test_int63();
    test_intn();
    test_float64();
    test_float32();
    test_shuffle();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
