#include "neverc/std/crypto/dsa.h"
#include <stdio.h>
#include <string.h>

static int entropy_fails(unsigned char *buffer, size_t length) {
    if (buffer) memset(buffer, 0xa5, length);
    return -1;
}

#define NCI_DSA_RANDOM entropy_fails
#include "../../../std/src/crypto/dsa/dsa.c"
#undef NCI_DSA_RANDOM

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
    neverc_dsa_private_key_t key;
    neverc_dsa_signature_t signature;
    const unsigned char hash[1] = {0x42};

    neverc_dsa_private_key_init(&key);
    neverc_dsa_signature_init(&signature);
    /* 4^23 ≡ 1 (mod 47): g=2 is a non-residue, so g=4 is in the q-subgroup.
     * Sign must reach the nonce RNG rather than fail group validation. */
    neverc_bigint_set_int64(&key.pub.p, 47);
    neverc_bigint_set_int64(&key.pub.q, 23);
    neverc_bigint_set_int64(&key.pub.g, 4);
    neverc_bigint_set_int64(&key.x, 3);
    neverc_bigint_set_int64(&signature.r, 7);
    neverc_bigint_set_int64(&signature.s, 11);

    CHECK(neverc_dsa_sign(&key, hash, sizeof(hash), &signature) == -1);
    CHECK(neverc_bigint_is_zero(&signature.r));
    CHECK(neverc_bigint_is_zero(&signature.s));

    neverc_dsa_signature_free(&signature);
    neverc_dsa_private_key_free(&key);
    puts("passed");
    return 0;
}
