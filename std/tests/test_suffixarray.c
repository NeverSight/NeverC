#include "neverc/index/suffixarray.h"
#include <stdio.h>
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
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
