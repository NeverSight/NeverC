#include "neverc/std/unique.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;
#define ASSERT_TRUE(expr) do { tests_run++; if(expr)tests_passed++; else{tests_failed++; printf("  FAIL: %s (line %d)\n",#expr,__LINE__);}} while(0)
#define ASSERT_INT_EQ(expr, expected) do { long long _v=(long long)(expr); long long _e=(long long)(expected); tests_run++; if(_v==_e)tests_passed++; else{tests_failed++; printf("  FAIL: %s = %lld, expected %lld (line %d)\n",#expr,_v,_e,__LINE__);}} while(0)
#define ASSERT_STR_EQ(a, b) do { tests_run++; if(strcmp(a,b)==0)tests_passed++; else{tests_failed++; printf("  FAIL: \"%s\" != \"%s\" (line %d)\n",(a),(b),__LINE__);}} while(0)

static void test_string_interning(void) {
    printf("[string_interning]\n");
    neverc_unique_handle_t h1 = neverc_unique_make_string("hello");
    neverc_unique_handle_t h2 = neverc_unique_make_string("hello");
    neverc_unique_handle_t h3 = neverc_unique_make_string("world");

    ASSERT_TRUE(neverc_unique_handle_valid(h1));
    ASSERT_TRUE(neverc_unique_handle_valid(h2));
    ASSERT_TRUE(neverc_unique_handle_equal(h1, h2));
    ASSERT_TRUE(!neverc_unique_handle_equal(h1, h3));
    ASSERT_STR_EQ(neverc_unique_string_value(h1), "hello");
    ASSERT_STR_EQ(neverc_unique_string_value(h3), "world");
}

static void test_int64_interning(void) {
    printf("[int64_interning]\n");
    neverc_unique_handle_t h1 = neverc_unique_make_int64(42);
    neverc_unique_handle_t h2 = neverc_unique_make_int64(42);
    neverc_unique_handle_t h3 = neverc_unique_make_int64(99);

    ASSERT_TRUE(neverc_unique_handle_equal(h1, h2));
    ASSERT_TRUE(!neverc_unique_handle_equal(h1, h3));
    ASSERT_INT_EQ(neverc_unique_int64_value(h1), 42);
    ASSERT_INT_EQ(neverc_unique_int64_value(h3), 99);
}

static void test_uint64_interning(void) {
    printf("[uint64_interning]\n");
    neverc_unique_handle_t h1 = neverc_unique_make_uint64(1234567890ULL);
    neverc_unique_handle_t h2 = neverc_unique_make_uint64(1234567890ULL);
    ASSERT_TRUE(neverc_unique_handle_equal(h1, h2));
    ASSERT_INT_EQ((long long)neverc_unique_uint64_value(h1), 1234567890LL);
}

static void test_bytes_interning(void) {
    printf("[bytes_interning]\n");
    unsigned char data1[] = {0xDE, 0xAD, 0xBE, 0xEF};
    unsigned char data2[] = {0xDE, 0xAD, 0xBE, 0xEF};
    unsigned char data3[] = {0xCA, 0xFE};

    neverc_unique_handle_t h1 = neverc_unique_make_bytes(data1, 4);
    neverc_unique_handle_t h2 = neverc_unique_make_bytes(data2, 4);
    neverc_unique_handle_t h3 = neverc_unique_make_bytes(data3, 2);

    ASSERT_TRUE(neverc_unique_handle_equal(h1, h2));
    ASSERT_TRUE(!neverc_unique_handle_equal(h1, h3));

    const unsigned char *out = (const unsigned char *)neverc_unique_bytes_value(h1, NULL);
    ASSERT_TRUE(memcmp(out, data1, 4) == 0);
}

/* Exercises the O(1) length header: bytes_value with a non-NULL len out-param
 * (the previous table-scan implementation left this path untested) plus the
 * string/int64 length-consistency that the shared [len][data] block guarantees. */
static void test_bytes_length(void) {
    printf("[bytes_length]\n");
    neverc_unique_destroy();
    neverc_unique_init();

    unsigned char blob[300];
    for (int i = 0; i < 300; i++) blob[i] = (unsigned char)(i * 7 + 1);

    size_t sizes[] = {1, 2, 7, 8, 16, 64, 255, 256, 300};
    for (int s = 0; s < (int)(sizeof(sizes) / sizeof(sizes[0])); s++) {
        size_t n = sizes[s];
        neverc_unique_handle_t h = neverc_unique_make_bytes(blob, n);
        ASSERT_TRUE(neverc_unique_handle_valid(h));
        size_t got = 0;
        const unsigned char *p = (const unsigned char *)neverc_unique_bytes_value(h, &got);
        ASSERT_INT_EQ((long long)got, (long long)n);            /* O(1) header length */
        ASSERT_TRUE(p && memcmp(p, blob, n) == 0);              /* data intact */
        /* Re-interning the same prefix returns the identical canonical pointer. */
        neverc_unique_handle_t h2 = neverc_unique_make_bytes(blob, n);
        ASSERT_TRUE(neverc_unique_handle_equal(h, h2));
    }

    /* Length header is correct for non-byte kinds too (string => strlen+1). */
    neverc_unique_handle_t hs = neverc_unique_make_string("hello");
    size_t slen = 0;
    neverc_unique_bytes_value(hs, &slen);
    ASSERT_INT_EQ((long long)slen, 6);

    neverc_unique_handle_t hi = neverc_unique_make_int64(42);
    size_t ilen = 0;
    neverc_unique_bytes_value(hi, &ilen);
    ASSERT_INT_EQ((long long)ilen, (long long)sizeof(int64_t));
}

/* Stress the power-of-two probing + grow path with many distinct byte values of
 * varying length, then verify every handle still reports the right length/data. */
static void test_bytes_stress(void) {
    printf("[bytes_stress]\n");
    neverc_unique_destroy();
    neverc_unique_init();

    enum { N = 800 };
    static neverc_unique_handle_t hs[N];
    static unsigned char ref[N][24];
    static size_t reflen[N];
    for (int i = 0; i < N; i++) {
        size_t n = (size_t)(i % 24) + 1;
        for (size_t j = 0; j < n; j++) ref[i][j] = (unsigned char)(i * 31 + (int)j);
        reflen[i] = n;
        hs[i] = neverc_unique_make_bytes(ref[i], n);
        ASSERT_TRUE(neverc_unique_handle_valid(hs[i]));
    }
    int ok = 1;
    for (int i = 0; i < N; i++) {
        size_t got = 0;
        const unsigned char *p = (const unsigned char *)neverc_unique_bytes_value(hs[i], &got);
        if (got != reflen[i] || !p || memcmp(p, ref[i], reflen[i]) != 0) { ok = 0; break; }
        /* idempotent interning: same bytes -> same canonical handle */
        neverc_unique_handle_t again = neverc_unique_make_bytes(ref[i], reflen[i]);
        if (!neverc_unique_handle_equal(again, hs[i])) { ok = 0; break; }
    }
    ASSERT_TRUE(ok);
}

static void test_null_handling(void) {
    printf("[null_handling]\n");
    neverc_unique_handle_t h = neverc_unique_make_string(NULL);
    ASSERT_TRUE(!neverc_unique_handle_valid(h));
    h = neverc_unique_make_bytes(NULL, 0);
    ASSERT_TRUE(!neverc_unique_handle_valid(h));
}

static void test_count(void) {
    printf("[count]\n");
    neverc_unique_destroy();
    neverc_unique_init();
    ASSERT_INT_EQ((long long)neverc_unique_count(), 0);
    neverc_unique_make_string("alpha");
    neverc_unique_make_string("beta");
    neverc_unique_make_string("alpha");
    ASSERT_INT_EQ((long long)neverc_unique_count(), 2);
    neverc_unique_make_int64(100);
    ASSERT_INT_EQ((long long)neverc_unique_count(), 3);
}

static void test_many_strings(void) {
    printf("[many_strings]\n");
    neverc_unique_destroy();
    neverc_unique_init();
    char buf[32];
    for (int i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "str_%d", i);
        neverc_unique_handle_t h = neverc_unique_make_string(buf);
        ASSERT_TRUE(neverc_unique_handle_valid(h));
    }
    ASSERT_INT_EQ((long long)neverc_unique_count(), 1000);

    for (int i = 0; i < 1000; i++) {
        snprintf(buf, sizeof(buf), "str_%d", i);
        neverc_unique_handle_t h1 = neverc_unique_make_string(buf);
        neverc_unique_handle_t h2 = neverc_unique_make_string(buf);
        ASSERT_TRUE(neverc_unique_handle_equal(h1, h2));
    }
    ASSERT_INT_EQ((long long)neverc_unique_count(), 1000);
}

int main(void) {
    neverc_unique_init();
    test_string_interning();
    test_int64_interning();
    test_uint64_interning();
    test_bytes_interning();
    test_bytes_length();
    test_bytes_stress();
    test_null_handling();
    test_count();
    test_many_strings();
    neverc_unique_destroy();
    printf("\nunique: %d/%d passed", tests_passed, tests_run);
    if (tests_failed) printf(", %d FAILED", tests_failed);
    printf("\n");
    return tests_failed ? 1 : 0;
}
