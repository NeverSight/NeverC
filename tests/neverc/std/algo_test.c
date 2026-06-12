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

#include "neverc/std/bytes.h"
#include "neverc/std/cstring.h"
#include "neverc/std/index/suffixarray.h"
#include "neverc/std/slices.h"
#include "neverc/std/math/bits.h"

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

/* ---- branchless binary search tests ---- */
static int cmp_int(const void *a, const void *b) {
    int va = *(const int *)a, vb = *(const int *)b;
    return (va > vb) - (va < vb);
}

static void test_binary_search_int(void) {
    int arr[] = {1, 3, 5, 7, 9, 11, 13, 15, 17, 19};
    int found;

    int idx = neverc_slices_binary_search_int(arr, 10, 7, &found);
    CHECK(idx == 3 && found, "bsearch_int found 7");

    idx = neverc_slices_binary_search_int(arr, 10, 1, &found);
    CHECK(idx == 0 && found, "bsearch_int found first");

    idx = neverc_slices_binary_search_int(arr, 10, 19, &found);
    CHECK(idx == 9 && found, "bsearch_int found last");

    idx = neverc_slices_binary_search_int(arr, 10, 6, &found);
    CHECK(!found && idx == 3, "bsearch_int miss insert=3");

    idx = neverc_slices_binary_search_int(arr, 10, 0, &found);
    CHECK(!found && idx == 0, "bsearch_int miss before all");

    idx = neverc_slices_binary_search_int(arr, 10, 20, &found);
    CHECK(!found && idx == 10, "bsearch_int miss after all");

    idx = neverc_slices_binary_search_int(arr, 0, 5, &found);
    CHECK(!found && idx == 0, "bsearch_int empty");

    int single[] = {42};
    idx = neverc_slices_binary_search_int(single, 1, 42, &found);
    CHECK(idx == 0 && found, "bsearch_int single found");
    idx = neverc_slices_binary_search_int(single, 1, 41, &found);
    CHECK(!found && idx == 0, "bsearch_int single miss left");
    idx = neverc_slices_binary_search_int(single, 1, 43, &found);
    CHECK(!found && idx == 1, "bsearch_int single miss right");

    int dup[] = {1, 3, 3, 3, 5};
    idx = neverc_slices_binary_search_int(dup, 5, 3, &found);
    CHECK(found && idx >= 1 && idx <= 3, "bsearch_int duplicates");

    int two[] = {10, 20};
    idx = neverc_slices_binary_search_int(two, 2, 10, &found);
    CHECK(found && idx == 0, "bsearch_int two first");
    idx = neverc_slices_binary_search_int(two, 2, 20, &found);
    CHECK(found && idx == 1, "bsearch_int two second");
    idx = neverc_slices_binary_search_int(two, 2, 15, &found);
    CHECK(!found && idx == 1, "bsearch_int two mid miss");
}

static void test_binary_search_generic(void) {
    int arr[] = {2, 4, 6, 8, 10};
    int found;

    int target = 6;
    int idx = neverc_slices_binary_search(arr, 5, &target, sizeof(int), cmp_int, &found);
    CHECK(found && idx == 2, "bsearch_gen found 6");

    target = 5;
    idx = neverc_slices_binary_search(arr, 5, &target, sizeof(int), cmp_int, &found);
    CHECK(!found && idx == 2, "bsearch_gen miss 5");

    target = 1;
    idx = neverc_slices_binary_search(arr, 5, &target, sizeof(int), cmp_int, &found);
    CHECK(!found && idx == 0, "bsearch_gen miss before");

    target = 11;
    idx = neverc_slices_binary_search(arr, 5, &target, sizeof(int), cmp_int, &found);
    CHECK(!found && idx == 5, "bsearch_gen miss after");

    target = 2;
    idx = neverc_slices_binary_search(arr, 5, &target, sizeof(int), cmp_int, &found);
    CHECK(found && idx == 0, "bsearch_gen found first");

    target = 10;
    idx = neverc_slices_binary_search(arr, 5, &target, sizeof(int), cmp_int, &found);
    CHECK(found && idx == 4, "bsearch_gen found last");
}

static void test_binary_search_large(void) {
    int n = 10000;
    int *arr = (int *)malloc((size_t)n * sizeof(int));
    for (int i = 0; i < n; i++) arr[i] = i * 2;

    int found;
    for (int i = 0; i < n; i++) {
        int idx = neverc_slices_binary_search_int(arr, (size_t)n, i * 2, &found);
        if (!found || idx != i) {
            CHECK(0, "bsearch large miss existing");
            free(arr);
            return;
        }
    }
    CHECK(1, "bsearch large all found");

    for (int i = 0; i < n; i++) {
        int idx = neverc_slices_binary_search_int(arr, (size_t)n, i * 2 + 1, &found);
        if (found || idx != i + 1) {
            CHECK(0, "bsearch large miss nonexistent");
            free(arr);
            return;
        }
    }
    CHECK(1, "bsearch large all misses correct");

    free(arr);
}

/* ---- memchr-based count tests ---- */
static void test_bytes_count(void) {
    uint8_t s[] = "aababcabcdabcde";
    size_t slen = strlen((char *)s);
    CHECK(neverc_bytes_count(s, slen, (uint8_t*)"a", 1) == 5, "count single a");
    CHECK(neverc_bytes_count(s, slen, (uint8_t*)"b", 1) == 4, "count single b");
    CHECK(neverc_bytes_count(s, slen, (uint8_t*)"z", 1) == 0, "count single miss");
    CHECK(neverc_bytes_count(s, slen, (uint8_t*)"ab", 2) == 4, "count pair ab");
    CHECK(neverc_bytes_count(s, slen, (uint8_t*)"", 0) == slen + 1, "count empty");

    uint8_t all_x[256];
    memset(all_x, 'x', 256);
    CHECK(neverc_bytes_count(all_x, 256, (uint8_t*)"x", 1) == 256, "count all same");
    CHECK(neverc_bytes_count(all_x, 256, (uint8_t*)"y", 1) == 0, "count all diff");
}

static void test_cstring_count(void) {
    CHECK(neverc_cstring_count("aababcabcdabcde", "a") == 5, "ccount single a");
    CHECK(neverc_cstring_count("aababcabcdabcde", "b") == 4, "ccount single b");
    CHECK(neverc_cstring_count("aababcabcdabcde", "z") == 0, "ccount single miss");
    CHECK(neverc_cstring_count("aababcabcdabcde", "ab") == 4, "ccount pair ab");
    CHECK(neverc_cstring_count("aababcabcdabcde", "") == 16, "ccount empty");
}

/* ---- equal_fold tests ---- */
static void test_bytes_equal_fold(void) {
    CHECK(neverc_bytes_equal_fold((uint8_t*)"Hello", 5, (uint8_t*)"hello", 5),
          "fold basic");
    CHECK(neverc_bytes_equal_fold((uint8_t*)"HELLO", 5, (uint8_t*)"hello", 5),
          "fold all upper");
    CHECK(!neverc_bytes_equal_fold((uint8_t*)"hello", 5, (uint8_t*)"world", 5),
          "fold different");
    CHECK(!neverc_bytes_equal_fold((uint8_t*)"hello", 5, (uint8_t*)"hell", 4),
          "fold diff len");
    CHECK(neverc_bytes_equal_fold((uint8_t*)"", 0, (uint8_t*)"", 0),
          "fold empty");

    uint8_t long_a[128], long_b[128];
    for (int i = 0; i < 128; i++) {
        long_a[i] = 'A' + (uint8_t)(i % 26);
        long_b[i] = 'a' + (uint8_t)(i % 26);
    }
    CHECK(neverc_bytes_equal_fold(long_a, 128, long_b, 128), "fold long case-diff");

    memset(long_a, 'x', 128);
    memset(long_b, 'x', 128);
    CHECK(neverc_bytes_equal_fold(long_a, 128, long_b, 128), "fold long identical (fast-path)");

    long_b[120] = 'y';
    CHECK(!neverc_bytes_equal_fold(long_a, 128, long_b, 128), "fold long diff at end");
}

static void test_cstring_equal_fold(void) {
    CHECK(neverc_cstring_equal_fold("Hello", "hello"), "cfold basic");
    CHECK(neverc_cstring_equal_fold("WORLD", "world"), "cfold all upper");
    CHECK(!neverc_cstring_equal_fold("hello", "world"), "cfold different");
    CHECK(!neverc_cstring_equal_fold("hello", "hell"), "cfold diff len");
    CHECK(neverc_cstring_equal_fold("", ""), "cfold empty");

    char long_a[129], long_b[129];
    for (int i = 0; i < 128; i++) {
        long_a[i] = 'A' + (char)(i % 26);
        long_b[i] = 'a' + (char)(i % 26);
    }
    long_a[128] = '\0';
    long_b[128] = '\0';
    CHECK(neverc_cstring_equal_fold(long_a, long_b), "cfold long case-diff");
}

/* ---- math/bits tests ---- */
static void test_bits_len(void) {
    CHECK(neverc_bits_len8(0) == 0, "len8 0");
    CHECK(neverc_bits_len8(1) == 1, "len8 1");
    CHECK(neverc_bits_len8(0x80) == 8, "len8 0x80");
    CHECK(neverc_bits_len8(0xFF) == 8, "len8 0xFF");
    CHECK(neverc_bits_len8(0x10) == 5, "len8 0x10");

    CHECK(neverc_bits_len16(0) == 0, "len16 0");
    CHECK(neverc_bits_len16(1) == 1, "len16 1");
    CHECK(neverc_bits_len16(0x8000) == 16, "len16 0x8000");
    CHECK(neverc_bits_len16(0x100) == 9, "len16 0x100");

    CHECK(neverc_bits_len32(0) == 0, "len32 0");
    CHECK(neverc_bits_len32(1) == 1, "len32 1");
    CHECK(neverc_bits_len32(0x80000000U) == 32, "len32 high");
    CHECK(neverc_bits_len32(0x1234) == 13, "len32 0x1234");

    CHECK(neverc_bits_len64(0) == 0, "len64 0");
    CHECK(neverc_bits_len64(1) == 1, "len64 1");
    CHECK(neverc_bits_len64(0x8000000000000000ULL) == 64, "len64 high");
    CHECK(neverc_bits_len64(0x100000000ULL) == 33, "len64 33");
}

static void test_bits_leading_zeros(void) {
    CHECK(neverc_bits_leading_zeros8(0) == 8, "clz8 0");
    CHECK(neverc_bits_leading_zeros8(1) == 7, "clz8 1");
    CHECK(neverc_bits_leading_zeros8(0x80) == 0, "clz8 0x80");
    CHECK(neverc_bits_leading_zeros8(0x0F) == 4, "clz8 0x0F");

    CHECK(neverc_bits_leading_zeros16(0) == 16, "clz16 0");
    CHECK(neverc_bits_leading_zeros16(1) == 15, "clz16 1");

    CHECK(neverc_bits_leading_zeros32(0) == 32, "clz32 0");
    CHECK(neverc_bits_leading_zeros32(1) == 31, "clz32 1");
    CHECK(neverc_bits_leading_zeros32(0x80000000U) == 0, "clz32 high");

    CHECK(neverc_bits_leading_zeros64(0) == 64, "clz64 0");
    CHECK(neverc_bits_leading_zeros64(1) == 63, "clz64 1");
    CHECK(neverc_bits_leading_zeros64(0x8000000000000000ULL) == 0, "clz64 high");
}

static void test_bits_trailing_zeros(void) {
    CHECK(neverc_bits_trailing_zeros8(0) == 8, "ctz8 0");
    CHECK(neverc_bits_trailing_zeros8(1) == 0, "ctz8 1");
    CHECK(neverc_bits_trailing_zeros8(0x80) == 7, "ctz8 0x80");
    CHECK(neverc_bits_trailing_zeros8(0x10) == 4, "ctz8 0x10");

    CHECK(neverc_bits_trailing_zeros16(0) == 16, "ctz16 0");
    CHECK(neverc_bits_trailing_zeros16(0x100) == 8, "ctz16 0x100");

    CHECK(neverc_bits_trailing_zeros32(0) == 32, "ctz32 0");
    CHECK(neverc_bits_trailing_zeros32(1) == 0, "ctz32 1");
    CHECK(neverc_bits_trailing_zeros32(0x80000000U) == 31, "ctz32 high");
    CHECK(neverc_bits_trailing_zeros32(12) == 2, "ctz32 12");

    CHECK(neverc_bits_trailing_zeros64(0) == 64, "ctz64 0");
    CHECK(neverc_bits_trailing_zeros64(1) == 0, "ctz64 1");
    CHECK(neverc_bits_trailing_zeros64(0x100000000ULL) == 32, "ctz64 2^32");
}

static void test_bits_ones_count(void) {
    CHECK(neverc_bits_ones_count8(0) == 0, "pop8 0");
    CHECK(neverc_bits_ones_count8(0xFF) == 8, "pop8 0xFF");
    CHECK(neverc_bits_ones_count8(0x55) == 4, "pop8 0x55");

    CHECK(neverc_bits_ones_count16(0) == 0, "pop16 0");
    CHECK(neverc_bits_ones_count16(0xFFFF) == 16, "pop16 0xFFFF");

    CHECK(neverc_bits_ones_count32(0) == 0, "pop32 0");
    CHECK(neverc_bits_ones_count32(0xFFFFFFFFU) == 32, "pop32 all");
    CHECK(neverc_bits_ones_count32(0xAAAAAAAAU) == 16, "pop32 alt");
    CHECK(neverc_bits_ones_count32(7) == 3, "pop32 7");

    CHECK(neverc_bits_ones_count64(0) == 0, "pop64 0");
    CHECK(neverc_bits_ones_count64(0xFFFFFFFFFFFFFFFFULL) == 64, "pop64 all");
    CHECK(neverc_bits_ones_count64(0x123456789ABCDEF0ULL) == 32, "pop64 mixed");
}

static void test_bits_reverse_bytes(void) {
    CHECK(neverc_bits_reverse_bytes16(0x1234) == 0x3412, "bswap16");
    CHECK(neverc_bits_reverse_bytes32(0x12345678U) == 0x78563412U, "bswap32");
    CHECK(neverc_bits_reverse_bytes64(0x0102030405060708ULL) == 0x0807060504030201ULL, "bswap64");
}

static void test_bits_mul64(void) {
    uint64_t hi, lo;
    neverc_bits_mul64(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, &hi, &lo);
    CHECK(hi == 0xFFFFFFFFFFFFFFFEULL, "mul64 hi");
    CHECK(lo == 1, "mul64 lo");

    neverc_bits_mul64(0x100000000ULL, 0x100000000ULL, &hi, &lo);
    CHECK(hi == 1 && lo == 0, "mul64 2^32 * 2^32");

    neverc_bits_mul64(123456789ULL, 987654321ULL, &hi, &lo);
    CHECK(lo == 123456789ULL * 987654321ULL, "mul64 small lo");
    CHECK(hi == 0, "mul64 small hi");
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
    test_bytes_count();
    test_bytes_equal_fold();

    printf("--- cstring ---\n");
    test_cstring_index();
    test_cstring_last_index();
    test_cstring_replace();
    test_cstring_functions();
    test_cstring_count();
    test_cstring_equal_fold();

    printf("--- suffixarray ---\n");
    test_suffixarray_basic();
    test_suffixarray_large();

    printf("--- slices ---\n");
    test_slices_reverse();

    printf("--- binary search (branchless) ---\n");
    test_binary_search_int();
    test_binary_search_generic();
    test_binary_search_large();

    printf("--- math/bits (builtin) ---\n");
    test_bits_len();
    test_bits_leading_zeros();
    test_bits_trailing_zeros();
    test_bits_ones_count();
    test_bits_reverse_bytes();
    test_bits_mul64();

    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
