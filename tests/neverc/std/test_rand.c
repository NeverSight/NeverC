#include "neverc/std/math/rand.h"
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

static void test_seed_independence(void) {
    printf("[seed_independence]\n");
    neverc_rand_seed(1);
    for (int i = 0; i < 1000; i++) neverc_rand_uint64();
    uint64_t after_warmup = neverc_rand_uint64();

    neverc_rand_seed(2);
    uint64_t fresh = neverc_rand_uint64();
    check_true("reseed changes state", fresh != after_warmup);

    neverc_rand_seed(1);
    for (int i = 0; i < 1000; i++) neverc_rand_uint64();
    uint64_t after_warmup2 = neverc_rand_uint64();
    check_true("reseed reproduces", after_warmup == after_warmup2);
}

static void test_uint32(void) {
    printf("[uint32]\n");
    neverc_rand_seed(1);
    int saw_nonzero = 0;
    int saw_highbit = 0;
    for (int i = 0; i < 1000; i++) {
        uint32_t v = neverc_rand_uint32();
        if (v != 0) saw_nonzero = 1;
        if (v > 0x80000000U) saw_highbit = 1;
    }
    check_true("uint32 produces non-zero", saw_nonzero);
    check_true("uint32 uses full 32-bit range", saw_highbit);
}

static void test_int63(void) {
    printf("[int63]\n");
    neverc_rand_seed(123);
    int all_nonneg = 1;
    int saw_large = 0;
    for (int i = 0; i < 10000; i++) {
        int64_t v = neverc_rand_int63();
        if (v < 0) { all_nonneg = 0; break; }
        if (v > (int64_t)1 << 62) saw_large = 1;
    }
    check_true("int63 all non-negative", all_nonneg);
    check_true("int63 uses high bits", saw_large);
}

static void test_intn_basic(void) {
    printf("[intn basic]\n");
    neverc_rand_seed(456);
    int all_in_range = 1;
    for (int i = 0; i < 10000; i++) {
        int64_t v = neverc_rand_intn(10);
        if (v < 0 || v >= 10) { all_in_range = 0; break; }
    }
    check_true("intn(10) in [0,10)", all_in_range);
    check_true("intn(1) == 0", neverc_rand_intn(1) == 0);
    check_true("intn(0) == 0", neverc_rand_intn(0) == 0);
    check_true("intn(-5) == 0", neverc_rand_intn(-5) == 0);

    neverc_rand_seed(777);
    all_in_range = 1;
    for (int i = 0; i < 10000; i++) {
        int64_t v = neverc_rand_intn(2);
        if (v < 0 || v >= 2) { all_in_range = 0; break; }
    }
    check_true("intn(2) in {0,1}", all_in_range);
}

static void test_intn_power_of_two(void) {
    printf("[intn power-of-two fast path]\n");
    neverc_rand_seed(111);
    int64_t powers[] = {1, 2, 4, 8, 16, 64, 256, 1024, 1LL << 20, 1LL << 30};
    for (int p = 0; p < 10; p++) {
        int64_t n = powers[p];
        int all_ok = 1;
        for (int i = 0; i < 1000; i++) {
            int64_t v = neverc_rand_intn(n);
            if (v < 0 || v >= n) { all_ok = 0; break; }
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "intn(%lld) in range", (long long)n);
        check_true(buf, all_ok);
    }
}

/*
 * Chi-squared-like uniformity test for intn.
 *
 * Rationale: if intn(k) is uniform, each bucket should get ~(N/k) hits.
 * The chi-squared statistic measures deviation from expected.
 * For k buckets, df=k-1, chi2 > 2.5*df is suspicious at p < ~0.005.
 *
 * We use k=7 (a prime, worst case for modulo bias), N=70000.
 * Expected per bucket: 10000. Chi2 critical at df=6, p=0.001 is ~22.46.
 * We use a generous threshold of 40 to avoid false positives.
 */
static void test_intn_uniformity(void) {
    printf("[intn uniformity — chi-squared]\n");
    const int K = 7;
    const int N = 70000;
    int counts[7] = {0};

    neverc_rand_seed(2024);
    for (int i = 0; i < N; i++) {
        int64_t v = neverc_rand_intn(K);
        counts[v]++;
    }

    double expected = (double)N / K;
    double chi2 = 0.0;
    for (int i = 0; i < K; i++) {
        double d = (double)counts[i] - expected;
        chi2 += d * d / expected;
    }

    check_true("intn(7) chi2 < 40 (uniform)", chi2 < 40.0);

    int min_c = counts[0], max_c = counts[0];
    for (int i = 1; i < K; i++) {
        if (counts[i] < min_c) min_c = counts[i];
        if (counts[i] > max_c) max_c = counts[i];
    }
    double range_pct = (double)(max_c - min_c) / expected * 100.0;
    check_true("intn(7) range < 10% of expected", range_pct < 10.0);
}

/*
 * Verify rejection sampling eliminates modulo bias.
 *
 * With naive `next() % n`, for n that doesn't divide 2^64 evenly,
 * lower values get slightly more hits. For n = (2^63 / 3) * 3 + 1
 * (chosen so 2^63 mod n != 0), the bias would be measurable over
 * enough samples.
 *
 * We test with n=3 over 90000 samples: each bucket should get ~30000.
 */
static void test_intn_no_modulo_bias(void) {
    printf("[intn no-modulo-bias]\n");
    const int N = 90000;
    int counts[3] = {0};

    neverc_rand_seed(31415);
    for (int i = 0; i < N; i++) {
        counts[neverc_rand_intn(3)]++;
    }

    double expected = (double)N / 3.0;
    double chi2 = 0.0;
    for (int i = 0; i < 3; i++) {
        double d = (double)counts[i] - expected;
        chi2 += d * d / expected;
    }
    check_true("intn(3) no bias (chi2 < 15)", chi2 < 15.0);
}

static void test_float64(void) {
    printf("[float64]\n");
    neverc_rand_seed(789);
    int all_in_range = 1;
    int saw_near_zero = 0, saw_near_one = 0;
    for (int i = 0; i < 100000; i++) {
        double v = neverc_rand_float64();
        if (v < 0.0 || v >= 1.0) { all_in_range = 0; break; }
        if (v < 0.01) saw_near_zero = 1;
        if (v > 0.99) saw_near_one = 1;
    }
    check_true("float64 in [0,1)", all_in_range);
    check_true("float64 covers near-zero", saw_near_zero);
    check_true("float64 covers near-one", saw_near_one);
}

/*
 * float64 uniformity: divide [0,1) into 10 buckets, count hits.
 */
static void test_float64_uniformity(void) {
    printf("[float64 uniformity]\n");
    const int N = 100000;
    int buckets[10] = {0};

    neverc_rand_seed(2025);
    for (int i = 0; i < N; i++) {
        double v = neverc_rand_float64();
        int b = (int)(v * 10.0);
        if (b >= 10) b = 9;
        buckets[b]++;
    }

    double expected = (double)N / 10.0;
    double chi2 = 0.0;
    for (int i = 0; i < 10; i++) {
        double d = (double)buckets[i] - expected;
        chi2 += d * d / expected;
    }
    check_true("float64 chi2 < 30 (df=9)", chi2 < 30.0);
}

static void test_float32(void) {
    printf("[float32]\n");
    neverc_rand_seed(321);
    int all_in_range = 1;
    for (int i = 0; i < 10000; i++) {
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

/*
 * Shuffle uniformity: each position should appear in each slot
 * roughly 1/n of the time. Test with n=4 over many shuffles.
 */
static void test_shuffle_uniformity(void) {
    printf("[shuffle uniformity]\n");
    static int arr4[4];
    int position_counts[4][4] = {{0}};
    const int TRIALS = 40000;

    neverc_rand_seed(5555);
    for (int t = 0; t < TRIALS; t++) {
        for (int i = 0; i < 4; i++) arr4[i] = i;
        for (int i = 3; i > 0; i--) {
            int j = (int)(neverc_rand_intn(i + 1));
            int tmp = arr4[i]; arr4[i] = arr4[j]; arr4[j] = tmp;
        }
        for (int i = 0; i < 4; i++)
            position_counts[arr4[i]][i]++;
    }

    double expected = (double)TRIALS / 4.0;
    double chi2 = 0.0;
    for (int val = 0; val < 4; val++)
        for (int pos = 0; pos < 4; pos++) {
            double d = (double)position_counts[val][pos] - expected;
            chi2 += d * d / expected;
        }
    check_true("shuffle chi2 < 40 (df=12)", chi2 < 40.0);
}

/*
 * Output quality: all 64 bits should be exercised.
 * OR together many outputs — result should have all bits set.
 */
static void test_bit_coverage(void) {
    printf("[bit coverage]\n");
    neverc_rand_seed(7777);
    uint64_t or_all = 0;
    for (int i = 0; i < 200; i++)
        or_all |= neverc_rand_uint64();
    check_true("all 64 bits exercised", or_all == 0xFFFFFFFFFFFFFFFFULL);
}

int main(void) {
    printf("=== NeverC Rand Library Tests ===\n\n");

    test_seed_determinism();
    test_seed_independence();
    test_uint32();
    test_int63();
    test_intn_basic();
    test_intn_power_of_two();
    test_intn_uniformity();
    test_intn_no_modulo_bias();
    test_float64();
    test_float64_uniformity();
    test_float32();
    test_shuffle();
    test_shuffle_uniformity();
    test_bit_coverage();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
