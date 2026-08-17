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
    neverc_uuid_t checked;
    memset(&checked, 0x5a, sizeof(checked));
    CHECK(neverc_uuid_generate(&checked) == -1);
    CHECK(neverc_uuid_is_nil(checked));

    neverc_uuid_t failed = neverc_uuid_new();
    CHECK(neverc_uuid_is_nil(failed));
    CHECK(neverc_uuid_version(failed) == 0);

    entropy_fails = 0;
    CHECK(neverc_uuid_generate(&checked) == 0);
    CHECK(!neverc_uuid_is_nil(checked));
    CHECK(neverc_uuid_version(checked) == 4);
    CHECK(neverc_uuid_variant(checked) == 1);

    neverc_uuid_t generated = neverc_uuid_new();
    CHECK(!neverc_uuid_is_nil(generated));
    CHECK(neverc_uuid_version(generated) == 4);
    CHECK(neverc_uuid_variant(generated) == 1);

    entropy_fails = 1;
    memset(&checked, 0x5a, sizeof(checked));
    CHECK(neverc_uuid_generate_v7(&checked) == -1);
    CHECK(neverc_uuid_is_nil(checked));
    neverc_uuid_t failed_v7 = neverc_uuid_new_v7();
    CHECK(neverc_uuid_is_nil(failed_v7));
    CHECK(neverc_uuid_version(failed_v7) == 0);

    entropy_fails = 0;
    CHECK(neverc_uuid_generate_v7(&checked) == 0);
    CHECK(!neverc_uuid_is_nil(checked));
    CHECK(neverc_uuid_version(checked) == 7);
    CHECK(neverc_uuid_variant(checked) == 1);
    CHECK((checked.bytes[6] & 0xF0) == 0x70);
    CHECK((checked.bytes[8] & 0xC0) == 0x80);

    puts("passed");
    return 0;
}
