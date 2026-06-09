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
    test_null_handling();
    test_count();
    test_many_strings();
    neverc_unique_destroy();
    printf("\nunique: %d/%d passed", tests_passed, tests_run);
    if (tests_failed) printf(", %d FAILED", tests_failed);
    printf("\n");
    return tests_failed ? 1 : 0;
}
