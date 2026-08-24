#include "neverc/std/hash/maphash.h"
#include <stdio.h>
#include <string.h>

static int entropy_fails;
static unsigned random_calls;

static int test_platform_random(unsigned char *buf, size_t len) {
    random_calls++;
    if (entropy_fails) {
        memset(buf, 0xa5, len);
        return -1;
    }
    memset(buf, (int)random_calls, len);
    return 0;
}

#define NEVERC_PLATFORM_H
#define NCI_MAPHASH_RANDOM test_platform_random
#include "../../../std/src/hash/maphash/maphash.c"
#undef NCI_MAPHASH_RANDOM

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
    entropy_fails = 1;
    random_calls = 0;
    CHECK(neverc_maphash_make_seed() == 0);
    CHECK(random_calls == 1);

    entropy_fails = 0;
    uint64_t first = neverc_maphash_make_seed();
    uint64_t second = neverc_maphash_make_seed();
    CHECK(random_calls == 3);
    CHECK(first != 0);
    CHECK(second != 0);
    CHECK(first != second);

    puts("passed");
    return 0;
}
