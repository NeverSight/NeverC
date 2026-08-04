#include "neverc/std/uuid.h"
#include <stdio.h>
#include <string.h>

static int entropy_fails;

static int test_platform_random(unsigned char *buf, size_t len) {
    memset(buf, entropy_fails ? 0xa5 : 0x00, len);
    return entropy_fails ? -1 : 0;
}

#define NEVERC_PLATFORM_H
#define neverc_platform_random test_platform_random
#include "../../../std/src/uuid/uuid.c"
#undef neverc_platform_random

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
    neverc_uuid_t failed = neverc_uuid_new();
    CHECK(neverc_uuid_is_nil(failed));

    entropy_fails = 0;
    neverc_uuid_t generated = neverc_uuid_new();
    CHECK(!neverc_uuid_is_nil(generated));
    CHECK(neverc_uuid_version(generated) == 4);
    CHECK(neverc_uuid_variant(generated) == 1);
    puts("passed");
    return 0;
}
