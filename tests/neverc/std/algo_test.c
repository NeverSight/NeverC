/*
 * Quick correctness tests for optimized std algorithms:
 * - bytes: memcpy/memcmp + BMH substring search + SWAR case conversion
 * - cstring: libc functions + BMH substring search + SWAR case conversion
 * - suffixarray: SA-IS construction
 * - slices: reverse with stack buffer + branchless binary search
 * - crc32: slicing-by-8, crc64: slicing-by-8
 * - fnv: 8-way unrolled, hex: batch encode/decode
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
#include "neverc/std/hash/crc32.h"
#include "neverc/std/hash/crc64.h"
#include "neverc/std/hash/adler32.h"
#include "neverc/std/hash/fnv.h"
#include "neverc/std/unicode/utf8.h"
#include "neverc/std/encoding/hex.h"
#include "neverc/std/encoding/base64.h"
#include "neverc/std/math/rand.h"

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

/* ---- CRC32 slicing-by-8 tests ---- */
static void test_crc32_ieee(void) {
    CHECK(neverc_crc32_ieee("", 0) == 0, "crc32 empty");

    const char *hello = "hello world";
    uint32_t crc = neverc_crc32_ieee(hello, strlen(hello));
    CHECK(crc == 0x0D4A1185, "crc32 hello world");

    CHECK(neverc_crc32_ieee("a", 1) == 0xE8B7BE43, "crc32 single a");

    uint8_t buf[256];
    for (int i = 0; i < 256; i++) buf[i] = (uint8_t)i;
    uint32_t c256 = neverc_crc32_ieee(buf, 256);
    CHECK(c256 == 0x29058C73, "crc32 0..255");

    uint32_t inc = neverc_crc32_update(0, (const uint32_t *)NULL, NULL, 0);
    (void)inc;
    neverc_crc32_table_t tab;
    neverc_crc32_make_table(NEVERC_CRC32_IEEE, tab);
    uint32_t c1 = neverc_crc32_update(0, tab, hello, 5);
    uint32_t c2 = neverc_crc32_update(c1, tab, hello + 5, 6);
    CHECK(c2 == crc, "crc32 incremental == full");

    uint8_t big[4096];
    memset(big, 'x', sizeof(big));
    uint32_t cbig = neverc_crc32_ieee(big, sizeof(big));
    uint32_t cbig_ref = neverc_crc32_checksum(tab, big, sizeof(big));
    CHECK(cbig == cbig_ref, "crc32 4KB slicing8 == byte-at-a-time");
}

/* ---- Adler32 unrolled tests ---- */
static void test_adler32(void) {
    CHECK(neverc_adler32_checksum((const uint8_t *)"", 0) == 1, "adler32 empty");

    const uint8_t *hello = (const uint8_t *)"hello world";
    uint32_t a = neverc_adler32_checksum(hello, 11);
    CHECK(a == 0x1A0B045D, "adler32 hello world");

    uint32_t inc = neverc_adler32_update(NEVERC_ADLER32_INIT, hello, 5);
    inc = neverc_adler32_update(inc, hello + 5, 6);
    CHECK(inc == a, "adler32 incremental == full");

    uint8_t big[8192];
    memset(big, 'A', sizeof(big));
    uint32_t abig = neverc_adler32_checksum(big, sizeof(big));
    uint32_t abig2 = neverc_adler32_update(NEVERC_ADLER32_INIT, big, 4096);
    abig2 = neverc_adler32_update(abig2, big + 4096, 4096);
    CHECK(abig == abig2, "adler32 8KB incremental");
    CHECK(abig != 0 && abig != 1, "adler32 8KB non-trivial");
}

/* ---- UTF-8 word-at-a-time tests ---- */
static void test_utf8_fast(void) {
    CHECK(neverc_utf8_rune_count((const uint8_t *)"", 0) == 0, "utf8 count empty");
    CHECK(neverc_utf8_rune_count((const uint8_t *)"hello", 5) == 5, "utf8 count ascii");

    const uint8_t ascii64[64] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+-";
    CHECK(neverc_utf8_rune_count(ascii64, 64) == 64, "utf8 count 64 ascii");

    uint8_t big[1024];
    memset(big, 'x', sizeof(big));
    CHECK(neverc_utf8_rune_count(big, sizeof(big)) == 1024, "utf8 count 1K ascii");

    CHECK(neverc_utf8_valid(big, sizeof(big)), "utf8 valid 1K ascii");
    CHECK(neverc_utf8_valid((const uint8_t *)"", 0), "utf8 valid empty");

    const uint8_t utf8_hello[] = {0xE4, 0xBD, 0xA0, 0xE5, 0xA5, 0xBD};
    CHECK(neverc_utf8_rune_count(utf8_hello, 6) == 2, "utf8 count chinese");
    CHECK(neverc_utf8_valid(utf8_hello, 6), "utf8 valid chinese");

    uint8_t mixed[32];
    memcpy(mixed, "hello ", 6);
    memcpy(mixed + 6, utf8_hello, 6);
    memcpy(mixed + 12, " world", 6);
    CHECK(neverc_utf8_rune_count(mixed, 18) == 14, "utf8 count mixed");
    CHECK(neverc_utf8_valid(mixed, 18), "utf8 valid mixed");

    uint8_t bad[] = {0xFF, 0xFE};
    CHECK(!neverc_utf8_valid(bad, 2), "utf8 invalid bytes");
}

/* ---- Hex encode pair-table tests ---- */
static void test_hex_encode(void) {
    char out[64];
    uint8_t data[] = {0x00, 0x01, 0x0F, 0x10, 0xFF, 0xAB, 0xCD, 0xEF};
    neverc_hex_encode(out, data, 8);
    CHECK(strcmp(out, "00010f10ffabcdef") == 0, "hex encode");

    neverc_hex_encode(out, (const uint8_t *)"", 0);
    CHECK(out[0] == '\0', "hex encode empty");

    uint8_t single[] = {0x42};
    neverc_hex_encode(out, single, 1);
    CHECK(strcmp(out, "42") == 0, "hex encode single");

    uint8_t all[256];
    char all_hex[513];
    for (int i = 0; i < 256; i++) all[i] = (uint8_t)i;
    neverc_hex_encode(all_hex, all, 256);
    int hex_ok = 1;
    for (int i = 0; i < 256; i++) {
        uint8_t hi = (uint8_t)(all_hex[i*2] <= '9' ? all_hex[i*2] - '0' : all_hex[i*2] - 'a' + 10);
        uint8_t lo = (uint8_t)(all_hex[i*2+1] <= '9' ? all_hex[i*2+1] - '0' : all_hex[i*2+1] - 'a' + 10);
        if ((hi << 4 | lo) != (uint8_t)i) { hex_ok = 0; break; }
    }
    CHECK(hex_ok, "hex encode all 256 bytes");
}

/* ---- to_upper/to_lower SWAR tests ---- */
static void test_swar_case(void) {
    char *up = neverc_cstring_to_upper("hello world 123 !@#");
    CHECK(strcmp(up, "HELLO WORLD 123 !@#") == 0, "swar to_upper");
    free(up);

    char *lo = neverc_cstring_to_lower("HELLO WORLD 123 !@#");
    CHECK(strcmp(lo, "hello world 123 !@#") == 0, "swar to_lower");
    free(lo);

    char long_str[129];
    for (int i = 0; i < 128; i++) long_str[i] = 'a' + (char)(i % 26);
    long_str[128] = '\0';
    up = neverc_cstring_to_upper(long_str);
    int upper_ok = 1;
    for (int i = 0; i < 128; i++)
        if (up[i] != 'A' + (char)(i % 26)) { upper_ok = 0; break; }
    CHECK(upper_ok, "swar to_upper 128 chars");
    free(up);

    size_t outlen;
    uint8_t *bup = neverc_bytes_to_upper((const uint8_t *)"abcDEF123", 9, &outlen);
    CHECK(outlen == 9 && memcmp(bup, "ABCDEF123", 9) == 0, "bytes swar to_upper");
    free(bup);

    uint8_t *blo = neverc_bytes_to_lower((const uint8_t *)"ABCdef456", 9, &outlen);
    CHECK(outlen == 9 && memcmp(blo, "abcdef456", 9) == 0, "bytes swar to_lower");
    free(blo);

    up = neverc_cstring_to_upper("");
    CHECK(strcmp(up, "") == 0, "swar to_upper empty");
    free(up);

    up = neverc_cstring_to_upper("A");
    CHECK(strcmp(up, "A") == 0, "swar to_upper single");
    free(up);
}

/* ---- Lemire bounded random tests ---- */
static void test_rand_bounded(void) {
    neverc_rand_seed(42);

    for (int i = 0; i < 10000; i++) {
        uint32_t v = neverc_rand_uint32n(100);
        if (v >= 100) { CHECK(0, "uint32n range"); return; }
    }
    CHECK(1, "uint32n all in range [0,100)");

    for (int i = 0; i < 10000; i++) {
        int64_t v = neverc_rand_intn(1000);
        if (v < 0 || v >= 1000) { CHECK(0, "intn range"); return; }
    }
    CHECK(1, "intn all in range [0,1000)");

    CHECK(neverc_rand_uint32n(1) == 0, "uint32n(1) always 0");
    CHECK(neverc_rand_intn(1) == 0, "intn(1) always 0");
    CHECK(neverc_rand_uint32n(0) == 0, "uint32n(0) returns 0");
    CHECK(neverc_rand_intn(0) == 0, "intn(0) returns 0");
    CHECK(neverc_rand_intn(-5) == 0, "intn(-5) returns 0");

    int buckets[10] = {0};
    neverc_rand_seed(12345);
    for (int i = 0; i < 100000; i++)
        buckets[neverc_rand_uint32n(10)]++;
    int uniform = 1;
    for (int i = 0; i < 10; i++)
        if (buckets[i] < 8000 || buckets[i] > 12000) { uniform = 0; break; }
    CHECK(uniform, "uint32n(10) roughly uniform");

    neverc_rand_seed(99);
    for (int i = 0; i < 10000; i++) {
        uint64_t v = neverc_rand_uint64n(1000000000ULL);
        if (v >= 1000000000ULL) { CHECK(0, "uint64n range"); return; }
    }
    CHECK(1, "uint64n all in range [0,1B)");

    for (int i = 0; i < 1000; i++) {
        uint32_t v = neverc_rand_uint32n(0xFFFFFFFFU);
        (void)v;
    }
    CHECK(1, "uint32n(MAX) no crash");
}

/* ---- Base64 encode/decode roundtrip tests ---- */
static void test_base64_roundtrip(void) {
    char enc[256];
    uint8_t dec[256];

    uint8_t data6[] = {0, 1, 2, 3, 4, 5};
    size_t elen = neverc_base64_encode(enc, data6, 6);
    CHECK(elen == 8, "b64 6-byte encode len");
    int dlen = neverc_base64_decode(dec, enc, elen);
    CHECK(dlen == 6 && memcmp(dec, data6, 6) == 0, "b64 6-byte roundtrip");

    uint8_t data7[] = {0, 1, 2, 3, 4, 5, 6};
    elen = neverc_base64_encode(enc, data7, 7);
    dlen = neverc_base64_decode(dec, enc, elen);
    CHECK(dlen == 7 && memcmp(dec, data7, 7) == 0, "b64 7-byte roundtrip");

    uint8_t data12[12];
    for (int i = 0; i < 12; i++) data12[i] = (uint8_t)(i * 17);
    elen = neverc_base64_encode(enc, data12, 12);
    CHECK(elen == 16, "b64 12-byte encode len");
    dlen = neverc_base64_decode(dec, enc, elen);
    CHECK(dlen == 12 && memcmp(dec, data12, 12) == 0, "b64 12-byte roundtrip");

    elen = neverc_base64_encode(enc, (const uint8_t *)"", 0);
    CHECK(elen == 0 && enc[0] == '\0', "b64 empty encode");

    elen = neverc_base64_encode(enc, (const uint8_t *)"A", 1);
    dlen = neverc_base64_decode(dec, enc, elen);
    CHECK(dlen == 1 && dec[0] == 'A', "b64 1-byte roundtrip");

    uint8_t big[200];
    for (int i = 0; i < 200; i++) big[i] = (uint8_t)i;
    char bigenc[300];
    elen = neverc_base64_encode(bigenc, big, 200);
    uint8_t bigdec[200];
    dlen = neverc_base64_decode(bigdec, bigenc, elen);
    CHECK(dlen == 200 && memcmp(bigdec, big, 200) == 0, "b64 200-byte roundtrip");
}

/* ---- CRC64 slicing-by-8 tests ---- */
static void test_crc64_ecma(void) {
    neverc_crc64_table_t tab;
    neverc_crc64_make_table(NEVERC_CRC64_ECMA, tab);

    CHECK(neverc_crc64_checksum(tab, (const uint8_t *)"", 0) == 0, "crc64 empty");

    const char *hello = "hello world";
    uint64_t c = neverc_crc64_checksum(tab, (const uint8_t *)hello, strlen(hello));
    CHECK(c != 0, "crc64 hello non-zero");

    uint64_t c1 = neverc_crc64_update(0, tab, (const uint8_t *)hello, 5);
    uint64_t c2 = neverc_crc64_update(c1, tab, (const uint8_t *)hello + 5, 6);
    CHECK(c2 == c, "crc64 incremental == full");

    uint8_t big[4096];
    memset(big, 'x', sizeof(big));
    uint64_t cbig1 = neverc_crc64_checksum(tab, big, sizeof(big));
    uint64_t cbig2 = neverc_crc64_checksum(tab, big, sizeof(big));
    CHECK(cbig1 == cbig2, "crc64 deterministic");
    CHECK(cbig1 != 0, "crc64 4KB non-zero");

    uint64_t csmall = neverc_crc64_checksum(tab, big, 7);
    CHECK(csmall != 0, "crc64 small non-zero");
}

/* ---- FNV 8-way unrolled tests ---- */
static void test_fnv_hash(void) {
    CHECK(neverc_fnv_sum32a("", 0) == 0x811c9dc5U, "fnv32a empty");
    CHECK(neverc_fnv_sum64a("", 0) == 0xcbf29ce484222325ULL, "fnv64a empty");

    uint32_t h32 = neverc_fnv_sum32a("hello world", 11);
    CHECK(h32 != 0 && h32 != 0x811c9dc5U, "fnv32a hello non-trivial");

    uint64_t h64 = neverc_fnv_sum64a("hello world", 11);
    CHECK(h64 != 0 && h64 != 0xcbf29ce484222325ULL, "fnv64a hello non-trivial");

    uint8_t buf[256];
    for (int i = 0; i < 256; i++) buf[i] = (uint8_t)i;
    uint32_t h1 = neverc_fnv_sum32a(buf, 256);
    uint32_t h2 = neverc_fnv_sum32a(buf, 256);
    CHECK(h1 == h2, "fnv32a deterministic");

    uint64_t h3 = neverc_fnv_sum64a(buf, 256);
    uint64_t h4 = neverc_fnv_sum64a(buf, 256);
    CHECK(h3 == h4, "fnv64a deterministic");

    CHECK(neverc_fnv_sum32(buf, 256) != neverc_fnv_sum32a(buf, 256),
          "fnv1 != fnv1a");
    CHECK(neverc_fnv_sum64(buf, 256) != neverc_fnv_sum64a(buf, 256),
          "fnv1-64 != fnv1a-64");

    neverc_fnv_128_t r128 = neverc_fnv_sum128a(buf, 256);
    CHECK(r128.hi != 0 || r128.lo != 0, "fnv128a non-zero");
}

/* ---- hex decode batch tests ---- */
static void test_hex_decode(void) {
    uint8_t out[256];
    int n;

    n = neverc_hex_decode(out, "48656c6c6f", 10);
    CHECK(n == 5 && memcmp(out, "Hello", 5) == 0, "hex decode Hello");

    n = neverc_hex_decode(out, "", 0);
    CHECK(n == 0, "hex decode empty");

    n = neverc_hex_decode(out, "ff", 2);
    CHECK(n == 1 && out[0] == 0xFF, "hex decode ff");

    n = neverc_hex_decode(out, "0", 1);
    CHECK(n == -1, "hex decode odd length");

    n = neverc_hex_decode(out, "zz", 2);
    CHECK(n == -1, "hex decode invalid");

    char all_hex[513];
    uint8_t all_data[256];
    for (int i = 0; i < 256; i++) all_data[i] = (uint8_t)i;
    neverc_hex_encode(all_hex, all_data, 256);
    uint8_t decoded[256];
    n = neverc_hex_decode(decoded, all_hex, 512);
    CHECK(n == 256 && memcmp(decoded, all_data, 256) == 0, "hex roundtrip all 256");

    n = neverc_hex_decode(out, "ABCDEF", 6);
    CHECK(n == 3 && out[0] == 0xAB && out[1] == 0xCD && out[2] == 0xEF,
          "hex decode uppercase");
}

/* ---- rand_read memcpy tests ---- */
static void test_rand_read(void) {
    neverc_rand_seed(42);
    uint8_t buf1[64], buf2[64];
    neverc_rand_read(buf1, sizeof(buf1));

    neverc_rand_seed(42);
    neverc_rand_read(buf2, sizeof(buf2));
    CHECK(memcmp(buf1, buf2, sizeof(buf1)) == 0, "rand_read deterministic");

    int all_zero = 1;
    for (int i = 0; i < 64; i++) if (buf1[i] != 0) { all_zero = 0; break; }
    CHECK(!all_zero, "rand_read non-trivial");

    neverc_rand_seed(12345);
    uint8_t small[3];
    neverc_rand_read(small, 3);
    int small_all_zero = (small[0] == 0 && small[1] == 0 && small[2] == 0);
    CHECK(!small_all_zero, "rand_read small non-trivial");
}

/* ---- rand_uintn Lemire tests ---- */
static void test_rand_uintn_lemire(void) {
    neverc_rand_seed(42);

    for (int i = 0; i < 10000; i++) {
        unsigned int v = neverc_rand_uintn(100);
        if (v >= 100) { CHECK(0, "uintn range"); return; }
    }
    CHECK(1, "uintn all in range [0,100)");

    CHECK(neverc_rand_uintn(1) == 0, "uintn(1) always 0");
    CHECK(neverc_rand_uintn(0) == 0, "uintn(0) returns 0");

    int buckets[10] = {0};
    neverc_rand_seed(54321);
    for (int i = 0; i < 100000; i++)
        buckets[neverc_rand_uintn(10)]++;
    int uniform = 1;
    for (int i = 0; i < 10; i++)
        if (buckets[i] < 8000 || buckets[i] > 12000) { uniform = 0; break; }
    CHECK(uniform, "uintn(10) roughly uniform (Lemire)");

    neverc_rand_seed(77);
    for (int i = 0; i < 1000; i++) {
        unsigned int v = neverc_rand_uintn(UINT32_MAX);
        (void)v;
    }
    CHECK(1, "uintn(MAX) no crash");
}

/* ---- FNV-128 unrolled consistency tests ---- */
static void test_fnv128_unrolled(void) {
    neverc_fnv_128_t r1 = neverc_fnv_sum128("", 0);
    CHECK(r1.hi != 0 || r1.lo != 0, "fnv128 empty non-zero");

    neverc_fnv_128_t r2 = neverc_fnv_sum128a("", 0);
    CHECK(r2.hi != 0 || r2.lo != 0, "fnv128a empty non-zero");

    uint8_t buf[256];
    for (int i = 0; i < 256; i++) buf[i] = (uint8_t)i;

    neverc_fnv_128_t h1 = neverc_fnv_sum128(buf, 256);
    neverc_fnv_128_t h2 = neverc_fnv_sum128(buf, 256);
    CHECK(h1.hi == h2.hi && h1.lo == h2.lo, "fnv128 deterministic");

    neverc_fnv_128_t h3 = neverc_fnv_sum128a(buf, 256);
    neverc_fnv_128_t h4 = neverc_fnv_sum128a(buf, 256);
    CHECK(h3.hi == h4.hi && h3.lo == h4.lo, "fnv128a deterministic");

    CHECK(h1.hi != h3.hi || h1.lo != h3.lo, "fnv128 != fnv128a");

    neverc_fnv_128_t short1 = neverc_fnv_sum128(buf, 7);
    neverc_fnv_128_t short2 = neverc_fnv_sum128(buf, 7);
    CHECK(short1.hi == short2.hi && short1.lo == short2.lo,
          "fnv128 short deterministic");

    neverc_fnv_128_t full = neverc_fnv_sum128(buf, 16);
    neverc_fnv_128_t part = neverc_fnv_sum128(buf, 8);
    CHECK(full.hi != part.hi || full.lo != part.lo,
          "fnv128 different lengths differ");
}

/* ---- CRC32 update slicing-by-8 tests ---- */
static void test_crc32_update_slicing(void) {
    neverc_crc32_table_t tab;
    neverc_crc32_make_table(NEVERC_CRC32_IEEE, tab);

    uint8_t big[4096];
    memset(big, 'A', sizeof(big));

    uint32_t c_ieee = neverc_crc32_ieee(big, sizeof(big));
    uint32_t c_update = neverc_crc32_update(0, tab, big, sizeof(big));
    CHECK(c_ieee == c_update, "crc32 update 4KB == ieee (slicing-by-8)");

    uint8_t small[32];
    memset(small, 'B', sizeof(small));
    uint32_t c_small_ieee = neverc_crc32_ieee(small, sizeof(small));
    uint32_t c_small_upd = neverc_crc32_update(0, tab, small, sizeof(small));
    CHECK(c_small_ieee == c_small_upd, "crc32 update 32B == ieee (byte path)");

    uint32_t c1 = neverc_crc32_update(0, tab, big, 2048);
    uint32_t c2 = neverc_crc32_update(c1, tab, big + 2048, 2048);
    CHECK(c2 == c_update, "crc32 update incremental 2x2KB == full 4KB");

    uint8_t varying[1024];
    for (int i = 0; i < 1024; i++) varying[i] = (uint8_t)(i * 37);
    uint32_t cv1 = neverc_crc32_ieee(varying, sizeof(varying));
    uint32_t cv2 = neverc_crc32_update(0, tab, varying, sizeof(varying));
    CHECK(cv1 == cv2, "crc32 update 1KB varying == ieee");
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

    printf("--- crc32 (slicing-by-8) ---\n");
    test_crc32_ieee();

    printf("--- adler32 (16-way unrolled) ---\n");
    test_adler32();

    printf("--- utf8 (word-at-a-time) ---\n");
    test_utf8_fast();

    printf("--- hex encode (pair table) ---\n");
    test_hex_encode();

    printf("--- to_upper/to_lower (SWAR) ---\n");
    test_swar_case();

    printf("--- rand bounded (Lemire) ---\n");
    test_rand_bounded();

    printf("--- crc64 (slicing-by-8) ---\n");
    test_crc64_ecma();

    printf("--- fnv (8-way unrolled) ---\n");
    test_fnv_hash();

    printf("--- hex decode (batch) ---\n");
    test_hex_decode();

    printf("--- base64 roundtrip ---\n");
    test_base64_roundtrip();

    printf("--- rand_read (memcpy) ---\n");
    test_rand_read();

    printf("--- rand_uintn (Lemire fix) ---\n");
    test_rand_uintn_lemire();

    printf("--- fnv128 (8-way unrolled) ---\n");
    test_fnv128_unrolled();

    printf("--- crc32 update (slicing-by-8) ---\n");
    test_crc32_update_slicing();

    printf("\n=== Results: %d passed, %d failed ===\n",
           tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
