#include "neverc/std/index/suffixarray.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_INT_EQ(expr, expected) do { \
    int _v = (int)(expr); int _e = (int)(expected); tests_run++; \
    if (_v == _e) { tests_passed++; } \
    else { tests_failed++; \
           printf("  FAIL: %s = %d, expected %d (line %d)\n", #expr, _v, _e, __LINE__); } \
} while(0)

#define ASSERT_TRUE(expr) do { tests_run++; \
    if (expr) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: %s (line %d)\n", #expr, __LINE__); } \
} while(0)

static void test_new_free(void) {
    printf("[new_free]\n");
    neverc_suffixarray_t idx;
    const char *text = "banana";
    ASSERT_INT_EQ(neverc_suffixarray_new(&idx, (const unsigned char *)text, strlen(text)), 0);
    ASSERT_INT_EQ(idx.sa_len, 6);
    neverc_suffixarray_free(&idx);
    ASSERT_TRUE(idx.sa == NULL);
}

static void test_empty(void) {
    printf("[empty]\n");
    neverc_suffixarray_t idx;
    ASSERT_INT_EQ(neverc_suffixarray_new(&idx, NULL, 0), 0);
    ASSERT_INT_EQ(neverc_suffixarray_count(&idx, (const unsigned char *)"a", 1), 0);
    neverc_suffixarray_free(&idx);
}

static void test_lookup_basic(void) {
    printf("[lookup_basic]\n");
    neverc_suffixarray_t idx;
    const char *text = "abracadabra";
    neverc_suffixarray_new(&idx, (const unsigned char *)text, strlen(text));

    size_t count = neverc_suffixarray_count(&idx, (const unsigned char *)"abra", 4);
    ASSERT_INT_EQ(count, 2);

    count = neverc_suffixarray_count(&idx, (const unsigned char *)"a", 1);
    ASSERT_INT_EQ(count, 5);

    count = neverc_suffixarray_count(&idx, (const unsigned char *)"bra", 3);
    ASSERT_INT_EQ(count, 2);

    count = neverc_suffixarray_count(&idx, (const unsigned char *)"xyz", 3);
    ASSERT_INT_EQ(count, 0);

    neverc_suffixarray_free(&idx);
}

static void test_lookup_positions(void) {
    printf("[lookup_positions]\n");
    neverc_suffixarray_t idx;
    const char *text = "mississippi";
    neverc_suffixarray_new(&idx, (const unsigned char *)text, strlen(text));

    int32_t results[10];
    size_t nresults;
    neverc_suffixarray_lookup(&idx, (const unsigned char *)"issi", 4,
                              results, 10, &nresults);
    ASSERT_INT_EQ(nresults, 2);

    int found1 = 0, found4 = 0;
    for (size_t i = 0; i < nresults; i++) {
        if (results[i] == 1) found1 = 1;
        if (results[i] == 4) found4 = 1;
    }
    ASSERT_TRUE(found1);
    ASSERT_TRUE(found4);

    neverc_suffixarray_free(&idx);
}

static void test_single_char(void) {
    printf("[single_char]\n");
    neverc_suffixarray_t idx;
    const char *text = "aaaa";
    neverc_suffixarray_new(&idx, (const unsigned char *)text, strlen(text));

    size_t count = neverc_suffixarray_count(&idx, (const unsigned char *)"a", 1);
    ASSERT_INT_EQ(count, 4);

    count = neverc_suffixarray_count(&idx, (const unsigned char *)"aa", 2);
    ASSERT_INT_EQ(count, 3);

    count = neverc_suffixarray_count(&idx, (const unsigned char *)"aaa", 3);
    ASSERT_INT_EQ(count, 2);

    count = neverc_suffixarray_count(&idx, (const unsigned char *)"aaaa", 4);
    ASSERT_INT_EQ(count, 1);

    neverc_suffixarray_free(&idx);
}

static void test_suffix_order(void) {
    printf("[suffix_order]\n");
    neverc_suffixarray_t idx;
    const char *text = "banana";
    neverc_suffixarray_new(&idx, (const unsigned char *)text, strlen(text));

    for (size_t i = 1; i < idx.sa_len; i++) {
        int a = idx.sa[i - 1], b = idx.sa[i];
        int cmp = strcmp(text + a, text + b);
        ASSERT_TRUE(cmp < 0);
    }

    neverc_suffixarray_free(&idx);
}

static void test_max_results(void) {
    printf("[max_results]\n");
    neverc_suffixarray_t idx;
    const char *text = "aababababab";
    neverc_suffixarray_new(&idx, (const unsigned char *)text, strlen(text));

    int32_t results[2];
    size_t nresults;
    neverc_suffixarray_lookup(&idx, (const unsigned char *)"ab", 2,
                              results, 2, &nresults);
    ASSERT_TRUE(nresults >= 2);

    neverc_suffixarray_free(&idx);
}

static void test_at(void) {
    printf("[at]\n");
    neverc_suffixarray_t idx;
    const char *text = "abc";
    neverc_suffixarray_new(&idx, (const unsigned char *)text, strlen(text));

    ASSERT_TRUE(neverc_suffixarray_at(&idx, 0) >= 0);
    ASSERT_INT_EQ(neverc_suffixarray_at(&idx, 100), -1);

    neverc_suffixarray_free(&idx);
}

/* Brute-force occurrence count, the oracle for the LCP-accelerated search. */
static size_t naive_count(const unsigned char *t, size_t n,
                          const unsigned char *p, size_t m) {
    if (m == 0 || m > n) return 0;
    size_t c = 0;
    for (size_t i = 0; i + m <= n; i++)
        if (memcmp(t + i, p, m) == 0) c++;
    return c;
}

static int cmp_i32(const void *a, const void *b) {
    int32_t x = *(const int32_t *)a, y = *(const int32_t *)b;
    return (x > y) - (x < y);
}

/*
 * Randomized differential test: the LCP-accelerated count/lookup must agree
 * with a naive scan for every (text, pattern) pair. Small/periodic alphabets
 * are emphasized because long shared prefixes are exactly where the LCP path
 * diverges most from a compare-from-zero search. Fixed seed -> reproducible.
 */
static void test_random_oracle(void) {
    printf("[random_oracle]\n");
    srand(987654321u);
    const int alphas[] = {1, 2, 3, 4, 26};
    int count_ok = 1, pos_ok = 1;

    for (int trial = 0; trial < 20000; trial++) {
        int a = alphas[rand() % (int)(sizeof(alphas) / sizeof(alphas[0]))];
        size_t n = (size_t)(rand() % 48);
        unsigned char t[48], p[64];
        for (size_t i = 0; i < n; i++) t[i] = (unsigned char)('a' + rand() % a);

        size_t m;
        int mode = rand() % 3;
        if (mode == 0 && n > 0) {                 /* a genuine substring */
            size_t start = (size_t)(rand() % (int)n);
            size_t maxm = n - start;
            m = (size_t)(rand() % (int)(maxm + 1));
            memcpy(p, t + start, m);
        } else {                                  /* random (often a miss / too long) */
            m = (size_t)(rand() % 10);
            for (size_t i = 0; i < m; i++) p[i] = (unsigned char)('a' + rand() % a);
        }
        if (m == 0) { m = 1; p[0] = 'a'; }

        neverc_suffixarray_t idx;
        neverc_suffixarray_new(&idx, t, n);

        size_t want = naive_count(t, n, p, m);
        if (neverc_suffixarray_count(&idx, p, m) != want) count_ok = 0;

        if (want > 0) {
            int32_t res[48], exp[48];
            size_t nres = 0, e = 0;
            neverc_suffixarray_lookup(&idx, p, m, res, want, &nres);
            for (size_t i = 0; i + m <= n; i++)
                if (memcmp(t + i, p, m) == 0) exp[e++] = (int32_t)i;
            qsort(res, nres, sizeof(int32_t), cmp_i32);
            if (nres != want) pos_ok = 0;
            else for (size_t i = 0; i < want; i++)
                if (res[i] != exp[i]) { pos_ok = 0; break; }
        }
        neverc_suffixarray_free(&idx);
    }
    ASSERT_TRUE(count_ok);
    ASSERT_TRUE(pos_ok);
}

int main(void) {
    printf("=== NeverC index/suffixarray Tests ===\n");
    test_new_free();
    test_empty();
    test_lookup_basic();
    test_lookup_positions();
    test_single_char();
    test_suffix_order();
    test_max_results();
    test_at();
    test_random_oracle();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
