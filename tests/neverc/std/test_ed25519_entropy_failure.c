#include "neverc/std/crypto/ed25519.h"
#include <stdio.h>
#include <string.h>

static int entropy_fails(unsigned char *buffer, size_t length) {
    if (buffer) memset(buffer, 0xa5, length);
    return -1;
}

#define NCI_ED25519_RANDOM entropy_fails
#include "../../../std/src/crypto/ed25519/ed25519.c"
#undef NCI_ED25519_RANDOM

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int all_zero(const void *value, size_t length) {
    const unsigned char *bytes = (const unsigned char *)value;
    unsigned char combined = 0;
    for (size_t i = 0; i < length; i++) combined |= bytes[i];
    return combined == 0;
}

int main(void) {
    unsigned char public_key[32];
    unsigned char private_key[64];
    memset(public_key, 0x5a, sizeof(public_key));
    memset(private_key, 0x5a, sizeof(private_key));

    CHECK(neverc_ed25519_generate_key(public_key, private_key) == -1);
    CHECK(all_zero(public_key, sizeof(public_key)));
    CHECK(all_zero(private_key, sizeof(private_key)));

    puts("passed");
    return 0;
}
