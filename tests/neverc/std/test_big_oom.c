#include <stdio.h>
#include <stdlib.h>

static int fail_realloc;
static int count_allocations;
static int allocation_failed;
static size_t allocation_index;
static size_t fail_allocation = (size_t)-1;

static int allocation_should_fail(void) {
    if (!count_allocations) return 0;
    if (allocation_index++ != fail_allocation) return 0;
    allocation_failed = 1;
    return 1;
}

static void *controlled_malloc(size_t size) {
    return allocation_should_fail() ? NULL : malloc(size);
}

static void *controlled_calloc(size_t count, size_t size) {
    return allocation_should_fail() ? NULL : calloc(count, size);
}

static void *controlled_realloc(void *ptr, size_t size) {
    return fail_realloc || allocation_should_fail() ? NULL : realloc(ptr, size);
}

#define malloc controlled_malloc
#define calloc controlled_calloc
#define realloc controlled_realloc
#include "../../../std/src/math/big/big.c"
#undef malloc
#undef calloc
#undef realloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)


static int test_string_allocation_failures(void) {
    uint32_t word = 123;
    neverc_bigint_t small = { &word, 1, 1, 0 };
    char small_buf[16];
    char small_unchanged[sizeof(small_buf)];
    memset(small_unchanged, '!', sizeof(small_unchanged));

    /* The output buffer allocation succeeds, but the magnitude copy fails.
     * Both signs must report an error, not an empty string or lone '-'. */
    fail_realloc = 1;
    for (int neg = 0; neg < 2; neg++) {
        small.neg = neg;
        memcpy(small_buf, small_unchanged, sizeof(small_buf));
        CHECK(neverc_bigint_string(&small, 16, small_buf,
                                  sizeof(small_buf)) == -1);
        CHECK(memcmp(small_buf, small_unchanged, sizeof(small_buf)) == 0);
        CHECK(word == 123);
        CHECK(bigint_oom == NULL);
    }
    small.len = 0;
    CHECK(neverc_bigint_string(&small, 16, small_buf,
                              sizeof(small_buf)) == 1);
    CHECK(strcmp(small_buf, "0") == 0);
    fail_realloc = 0;

    /* 160 words select D&C at the default threshold. All-one limbs have an
     * independent exact hexadecimal spelling, including both signs. Sweep
     * every allocation in the healthy path: power setup, the magnitude copy,
     * and recursive division, including Knuth's malloc/calloc scratch.
     * Recoverable scratch/table failures may fall back and succeed, but must
     * produce the exact value; an error must not publish partial output. */
    enum { WORDS = 160, HEX_DIGITS = WORDS * 8 };
    uint32_t words[WORDS];
    for (size_t i = 0; i < WORDS; i++) words[i] = UINT32_MAX;
    neverc_bigint_t large = { words, WORDS, WORDS, 0 };
    char expected[HEX_DIGITS + 2];
    char out[sizeof(expected)];
    char unchanged[sizeof(out)];
    memset(unchanged, '!', sizeof(unchanged));

    for (int neg = 0; neg < 2; neg++) {
        large.neg = neg;
        size_t sign = (size_t)neg;
        if (neg) expected[0] = '-';
        memset(expected + sign, 'f', HEX_DIGITS);
        expected[sign + HEX_DIGITS] = '\0';

        allocation_index = 0;
        allocation_failed = 0;
        fail_allocation = (size_t)-1;
        count_allocations = 1;
        int result = neverc_bigint_string(&large, 16, out, sizeof(out));
        count_allocations = 0;
        size_t allocations = allocation_index;
        CHECK(result == HEX_DIGITS + neg);
        CHECK(strcmp(out, expected) == 0);
        CHECK(allocations > 0);
        CHECK(!allocation_failed);
        CHECK(bigint_oom == NULL);

        for (size_t failure = 0; failure < allocations; failure++) {
            memcpy(out, unchanged, sizeof(out));
            allocation_index = 0;
            allocation_failed = 0;
            fail_allocation = failure;
            count_allocations = 1;
            result = neverc_bigint_string(&large, 16, out, sizeof(out));
            count_allocations = 0;
            CHECK(allocation_failed);
            CHECK(bigint_oom == NULL);
            if (result == -1) {
                CHECK(memcmp(out, unchanged, sizeof(out)) == 0);
            } else {
                CHECK(result == HEX_DIGITS + neg);
                CHECK(strcmp(out, expected) == 0);
            }
            for (size_t i = 0; i < WORDS; i++) CHECK(words[i] == UINT32_MAX);
        }

        /* A failed call must not leave a dangling TLS failure tracker. */
        fail_allocation = (size_t)-1;
        CHECK(neverc_bigint_string(&large, 16, out,
                                  sizeof(out)) == HEX_DIGITS + neg);
        CHECK(strcmp(out, expected) == 0);
        CHECK(bigint_oom == NULL);
    }
    return 0;
}

int main(void) {
    neverc_bigint_t empty;
    neverc_bigint_init(&empty);
    fail_realloc = 1;
    neverc_bigint_set_uint64(&empty, 42);
    CHECK(empty.digits == NULL);
    CHECK(empty.len == 0);

    fail_realloc = 0;
    neverc_bigint_t x, y, destination;
    neverc_bigint_init(&x);
    neverc_bigint_init(&y);
    neverc_bigint_init(&destination);
    neverc_bigint_set_uint64(&x, 123456789);
    neverc_bigint_set_uint64(&y, 987654321);
    neverc_bigint_set_uint64(&destination, 7);

    fail_realloc = 1;
    neverc_bigint_mul(&destination, &x, &y);
    CHECK(neverc_bigint_uint64(&destination) == 7);
    neverc_bigint_lsh(&destination, &x, 64);
    CHECK(neverc_bigint_uint64(&destination) == 7);

    neverc_bigint_free(&empty);
    neverc_bigint_free(&x);
    neverc_bigint_free(&y);
    neverc_bigint_free(&destination);

    neverc_bigint_t parsed;
    neverc_bigint_init(&parsed);
    fail_realloc = 0;
    neverc_bigint_set_uint64(&parsed, 42);
    fail_realloc = 1;
    CHECK(neverc_bigint_set_string(&parsed, "0x", 0) == -1);
    CHECK(neverc_bigint_uint64(&parsed) == 42);
    /* Valid input + dest that already has capacity: ensure_cap(z, 1) used
     * to succeed, zero z, then return 0 after a failed mul/add (Go SetString
     * leaves z unchanged on failure). */
    CHECK(neverc_bigint_set_string(&parsed,
        "999999999999999999999999999999", 10) == -1);
    CHECK(neverc_bigint_uint64(&parsed) == 42);
    neverc_bigint_t empty_parse;
    neverc_bigint_init(&empty_parse);
    CHECK(neverc_bigint_set_string(&empty_parse, "1", 10) == -1);
    CHECK(empty_parse.digits == NULL);
    CHECK(empty_parse.len == 0);
    fail_realloc = 0;
    CHECK(neverc_bigint_set_string(&parsed, "123", 10) == 0);
    CHECK(neverc_bigint_uint64(&parsed) == 123);
    neverc_bigint_free(&parsed);
    neverc_bigint_free(&empty_parse);

    fail_realloc = 0;
    neverc_bigint_t dest, src;
    neverc_bigint_init(&dest);
    neverc_bigint_init(&src);
    neverc_bigint_set_uint64(&dest, 7);
    CHECK(neverc_bigint_set_string(&src,
        "999999999999999999999999999999999999999999999999999999999999", 10) == 0);
    src.neg = 1;
    fail_realloc = 1;
    neverc_bigint_neg(&dest, &src);
    CHECK(neverc_bigint_uint64(&dest) == 7);
    neverc_bigint_abs(&dest, &src);
    CHECK(neverc_bigint_uint64(&dest) == 7);

    fail_realloc = 0;
    neverc_bigint_t small, large;
    neverc_bigint_init(&small);
    neverc_bigint_init(&large);
    neverc_bigint_set_uint64(&dest, 7);
    neverc_bigint_set_uint64(&small, 3);
    CHECK(neverc_bigint_set_string(&large,
        "999999999999999999999999999999", 10) == 0);
    fail_realloc = 1;
    neverc_bigint_mod(&dest, &small, &large);
    CHECK(neverc_bigint_uint64(&dest) == 7);

    fail_realloc = 0;
    neverc_bigint_set_uint64(&dest, 7);
    neverc_bigint_set_uint64(&small, 1);
    small.neg = 1;
    fail_realloc = 1;
    neverc_bigint_mod(&dest, &small, &large);
    CHECK(neverc_bigint_uint64(&dest) == 7);

    fail_realloc = 0;
    neverc_bigint_set_uint64(&dest, 7);
    neverc_bigint_set_uint64(&small, 5);
    neverc_bigint_set_uint64(&large, 5);
    fail_realloc = 1;
    neverc_bigint_div(&dest, NULL, &small, &large);
    CHECK(neverc_bigint_uint64(&dest) == 7);

    fail_realloc = 0;
    neverc_bigint_free(&dest);
    neverc_bigint_free(&src);
    neverc_bigint_free(&small);
    neverc_bigint_free(&large);

    CHECK(test_string_allocation_failures() == 0);

    puts("passed");
    return 0;
}
