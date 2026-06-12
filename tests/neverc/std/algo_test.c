/*
 * Quick correctness tests for optimized std algorithms:
 * - bytes: memcpy/memcmp + Rabin-Karp substring search
 * - cstring: libc functions + Rabin-Karp substring search
 * - suffixarray: SA-IS construction
 * - slices: reverse with stack buffer
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

/* ---- bytes.c inline (for standalone test) ---- */
#include "neverc/std/bytes.h"
#include "neverc/std/cstring.h"
#include "neverc/std/index/suffixarray.h"
#include "neverc/std/slices.h"

static int tests_passed = 0;
static int tests_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { \
        printf("FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

/* ---- bytes tests ---- */
static void test_bytes_equal(void) {
    uint8_t a[] = {1, 2, 3, 4, 5};
    uint8_t b[] = {1, 2, 3, 4, 5};
    uint8_t c[] = {1, 2, 3, 4, 6};
    CHECK(neverc_bytes_equal(a, 5, b, 5), "bytes_equal same");
    CHECK(!neverc_bytes_equal(a, 5, c, 5), "bytes_equal diff");
    CHECK(!neverc_bytes_equal(a, 5, b, 3), "bytes_equal diff len");
    CHECK(neverc_bytes_equal(a, 0, b, 0), "bytes_equal empty");
}

static void test_bytes_compare(void) {
    uint8_t a[] = {1, 2, 3};
    uint8_t b[] = {1, 2, 4};
    uint8_t c[] = {1, 2, 3, 4};
    CHECK(neverc_bytes_compare(a, 3, a, 3) == 0, "bytes_compare equal");
    CHECK(neverc_bytes_compare(a, 3, b, 3) == -1, "bytes_compare less");
    CHECK(neverc_bytes_compare(b, 3, a, 3) == 1, "bytes_compare greater");
    CHECK(neverc_bytes_compare(a, 3, c, 4) == -1, "bytes_compare prefix shorter");
}

static void test_bytes_index_byte(void) {
    uint8_t s[] = "hello world";
    CHECK(neverc_bytes_index_byte(s, 11, 'h') == 0, "index_byte first");
    CHECK(neverc_bytes_index_byte(s, 11, 'o') == 4, "index_byte middle");
    CHECK(neverc_bytes_index_byte(s, 11, 'd') == 10, "index_byte last");
    CHECK(neverc_bytes_index_byte(s, 11, 'x') == (size_t)-1, "index_byte missing");
}

static void test_bytes_index(void) {
    uint8_t s[] = "the quick brown fox jumps over the lazy dog";
    size_t slen = strlen((char *)s);

    CHECK(neverc_bytes_index(s, slen, (uint8_t*)"", 0) == 0, "index empty");
    CHECK(neverc_bytes_index(s, slen, (uint8_t*)"q", 1) == 4, "index single");
    CHECK(neverc_bytes_index(s, slen, (uint8_t*)"fox", 3) == 16, "index short");
    CHECK(neverc_bytes_index(s, slen, (uint8_t*)"the", 3) == 0, "index prefix");
    CHECK(neverc_bytes_index(s, slen, (uint8_t*)"dog", 3) == 40, "index suffix");
    CHECK(neverc_bytes_index(s, slen, (uint8_t*)"cat", 3) == (size_t)-1, "index missing");

    /* Rabin-Karp path: pattern > 8 bytes, haystack > 64 */
    uint8_t big[256];
    memset(big, 'a', 256);
    memcpy(big + 200, "FINDMENOW!", 10);
    CHECK(neverc_bytes_index(big, 256, (uint8_t*)"FINDMENOW!", 10) == 200, "index rabin-karp");
    CHECK(neverc_bytes_index(big, 256, (uint8_t*)"NOTHERE!!!", 10) == (size_t)-1, "index rk miss");
}

static void test_bytes_last_index(void) {
    uint8_t s[] = "abcabc";
    CHECK(neverc_bytes_last_index(s, 6, (uint8_t*)"abc", 3) == 3, "last_index");
    CHECK(neverc_bytes_last_index(s, 6, (uint8_t*)"xyz", 3) == (size_t)-1, "last_index miss");
}

static void test_bytes_repeat(void) {
    size_t outlen;
    uint8_t *r = neverc_bytes_repeat((uint8_t*)"ab", 2, 4, &outlen);
    CHECK(outlen == 8, "repeat len");
    CHECK(memcmp(r, "abababab", 8) == 0, "repeat content");
    free(r);
}

static void test_bytes_clone(void) {
    uint8_t orig[] = {10, 20, 30, 40, 50};
    uint8_t *c = neverc_bytes_clone(orig, 5);
    CHECK(c != NULL, "clone non-null");
    CHECK(memcmp(c, orig, 5) == 0, "clone content");
    free(c);
}

static void test_bytes_join(void) {
    const uint8_t *parts[] = {(uint8_t*)"hello", (uint8_t*)"world"};
    size_t lens[] = {5, 5};
    size_t outlen;
    uint8_t *r = neverc_bytes_join(parts, lens, 2, (uint8_t*)", ", 2, &outlen);
    CHECK(outlen == 12, "join len");
    CHECK(memcmp(r, "hello, world", 12) == 0, "join content");
    free(r);
}

static void test_bytes_replace(void) {
    size_t outlen;
    uint8_t *r = neverc_bytes_replace((uint8_t*)"aabbcc", 6,
                                       (uint8_t*)"bb", 2,
                                       (uint8_t*)"XX", 2, -1, &outlen);
    CHECK(outlen == 6, "replace len");
    CHECK(memcmp(r, "aaXXcc", 6) == 0, "replace content");
    free(r);
}

static void test_bytes_trim(void) {
    size_t outlen;
    uint8_t *r = neverc_bytes_trim((uint8_t*)"  hello  ", 9, " ", &outlen);
    CHECK(outlen == 5, "trim len");
    CHECK(memcmp(r, "hello", 5) == 0, "trim content");
    free(r);
}

/* ---- cstring tests ---- */
static void test_cstring_index(void) {
    CHECK(neverc_cstring_index("hello world", "world") == 6, "cstring index");
    CHECK(neverc_cstring_index("hello world", "xyz") == -1, "cstring index miss");
    CHECK(neverc_cstring_index("hello world", "") == 0, "cstring index empty");
    CHECK(neverc_cstring_index("hello world", "h") == 0, "cstring index single");

    /* Rabin-Karp path */
    char big[256];
    memset(big, 'a', 255);
    big[255] = '\0';
    memcpy(big + 200, "FINDMENOW!", 10);
    CHECK(neverc_cstring_index(big, "FINDMENOW!") == 200, "cstring rk");
}

static void test_cstring_last_index(void) {
    CHECK(neverc_cstring_last_index("abcabc", "abc") == 3, "cstring last_index");
    CHECK(neverc_cstring_last_index("abcabc", "xyz") == -1, "cstring last_index miss");
    CHECK(neverc_cstring_last_index("abcabc", "") == 6, "cstring last_index empty");
}

static void test_cstring_replace(void) {
    char *r = neverc_cstring_replace("aabbcc", "bb", "XX", -1);
    CHECK(strcmp(r, "aaXXcc") == 0, "cstring replace");
    free(r);
}

static void test_cstring_functions(void) {
    CHECK(neverc_cstring_has_prefix("hello", "hel"), "has_prefix");
    CHECK(!neverc_cstring_has_prefix("hello", "xyz"), "no prefix");
    CHECK(neverc_cstring_has_suffix("hello", "llo"), "has_suffix");
    CHECK(neverc_cstring_compare("abc", "abd") == -1, "compare");

    char *upper = neverc_cstring_to_upper("hello");
    CHECK(strcmp(upper, "HELLO") == 0, "to_upper");
    free(upper);

    char *r = neverc_cstring_repeat("ab", 3);
    CHECK(strcmp(r, "ababab") == 0, "repeat");
    free(r);
}

/* ---- suffixarray tests ---- */
static void test_suffixarray_basic(void) {
    const char *text = "banana";
    size_t n = strlen(text);
    neverc_suffixarray_t idx;
    int rc = neverc_suffixarray_new(&idx, (const unsigned char *)text, n);
    CHECK(rc == 0, "sa new");

    /* SA of "banana" should be [5, 3, 1, 0, 4, 2] */
    /* (a, ana, anana, banana, na, nana) */
    int expected[] = {5, 3, 1, 0, 4, 2};
    int sa_correct = 1;
    for (size_t i = 0; i < n; i++) {
        if (neverc_suffixarray_at(&idx, i) != expected[i]) {
            sa_correct = 0;
            printf("  SA[%zu]=%d expected %d\n", i, neverc_suffixarray_at(&idx, i), expected[i]);
        }
    }
    CHECK(sa_correct, "sa banana order");

    int32_t results[10];
    size_t nresults;
    neverc_suffixarray_lookup(&idx, (const unsigned char *)"ana", 3, results, 10, &nresults);
    CHECK(nresults == 2, "sa lookup ana count");

    CHECK(neverc_suffixarray_count(&idx, (const unsigned char *)"na", 2) == 2, "sa count na");
    CHECK(neverc_suffixarray_count(&idx, (const unsigned char *)"xyz", 3) == 0, "sa count miss");

    neverc_suffixarray_free(&idx);
}

static void test_suffixarray_large(void) {
    size_t n = 10000;
    unsigned char *data = (unsigned char *)malloc(n);
    srand(42);
    for (size_t i = 0; i < n; i++) data[i] = (unsigned char)(rand() % 4 + 'a');

    neverc_suffixarray_t idx;
    int rc = neverc_suffixarray_new(&idx, data, n);
    CHECK(rc == 0, "sa large new");

    int sorted = 1;
    for (size_t i = 1; i < n; i++) {
        int32_t a = neverc_suffixarray_at(&idx, i - 1);
        int32_t b = neverc_suffixarray_at(&idx, i);
        size_t alen = n - (size_t)a, blen = n - (size_t)b;
        size_t clen = alen < blen ? alen : blen;
        int cmp = memcmp(data + a, data + b, clen);
        if (cmp > 0 || (cmp == 0 && alen > blen)) {
            sorted = 0;
            break;
        }
    }
    CHECK(sorted, "sa large sorted");

    neverc_suffixarray_free(&idx);
    free(data);
}

/* ---- slices tests ---- */
static void test_slices_reverse(void) {
    int a[] = {1, 2, 3, 4, 5};
    neverc_slices_reverse(a, 5, sizeof(int));
    CHECK(a[0] == 5 && a[1] == 4 && a[2] == 3 && a[3] == 2 && a[4] == 1,
          "slices reverse");

    int b[] = {10, 20};
    neverc_slices_reverse(b, 2, sizeof(int));
    CHECK(b[0] == 20 && b[1] == 10, "slices reverse 2");

    int c[] = {42};
    neverc_slices_reverse(c, 1, sizeof(int));
    CHECK(c[0] == 42, "slices reverse 1");
}

int main(void) {
    printf("=== std algorithm optimization tests ===\n\n");

    printf("--- bytes ---\n");
    test_bytes_equal();
    test_bytes_compare();
    test_bytes_index_byte();
    test_bytes_index();
    test_bytes_last_index();
    test_bytes_repeat();
    test_bytes_clone();
    test_bytes_join();
    test_bytes_replace();
    test_bytes_trim();

    printf("--- cstring ---\n");
    test_cstring_index();
    test_cstring_last_index();
    test_cstring_replace();
    test_cstring_functions();

    printf("--- suffixarray ---\n");
    test_suffixarray_basic();
    test_suffixarray_large();

    printf("--- slices ---\n");
    test_slices_reverse();

    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
