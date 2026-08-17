#include <stdio.h>
#include <stdlib.h>

static int fail_realloc;

static void *controlled_realloc(void *ptr, size_t size) {
    return fail_realloc ? NULL : realloc(ptr, size);
}

#define realloc controlled_realloc
#include "../../../std/src/math/big/big.c"
#undef realloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

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
    neverc_bigint_t empty_parse;
    neverc_bigint_init(&empty_parse);
    CHECK(neverc_bigint_set_string(&empty_parse, "1", 10) == -1);
    CHECK(empty_parse.digits == NULL);
    CHECK(empty_parse.len == 0);
    neverc_bigint_free(&parsed);
    neverc_bigint_free(&empty_parse);

    puts("passed");
    return 0;
}
