#include "neverc/std/crypto/ecdsa.h"
#include <stdio.h>
#include <string.h>

static int entropy_fails(unsigned char *buffer, size_t length) {
    if (buffer) memset(buffer, 0xa5, length);
    return -1;
}

#define NCI_ECDSA_RANDOM entropy_fails
#include "../../../std/src/crypto/ecdsa/ecdsa.c"
#undef NCI_ECDSA_RANDOM

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
    const neverc_elliptic_curve_t *curve = neverc_elliptic_p256();
    CHECK(curve != NULL);

    neverc_ecdsa_private_key_t key;
    neverc_ecdsa_private_key_init(&key);
    neverc_bigint_set_int64(&key.d, 99);
    neverc_bigint_set_int64(&key.pub.pub.x, 99);
    neverc_bigint_set_int64(&key.pub.pub.y, 99);
    key.pub.curve = curve;

    CHECK(neverc_ecdsa_generate_key(&key, curve) == -1);
    CHECK(key.pub.curve == NULL);
    CHECK(neverc_bigint_is_zero(&key.d));
    CHECK(neverc_bigint_is_zero(&key.pub.pub.x));
    CHECK(neverc_bigint_is_zero(&key.pub.pub.y));
    neverc_ecdsa_private_key_free(&key);

    neverc_ecdsa_private_key_init(&key);
    key.pub.curve = curve;
    neverc_bigint_set_int64(&key.d, 1);
    neverc_elliptic_scalar_base_mult(curve, &key.pub.pub, &key.d);

    neverc_ecdsa_signature_t signature;
    neverc_ecdsa_signature_init(&signature);
    neverc_bigint_set_int64(&signature.r, 7);
    neverc_bigint_set_int64(&signature.s, 11);
    unsigned char hash[32] = {0x42};

    CHECK(neverc_ecdsa_sign(&key, hash, sizeof(hash), &signature) == -1);
    CHECK(neverc_bigint_is_zero(&signature.r));
    CHECK(neverc_bigint_is_zero(&signature.s));

    neverc_ecdsa_signature_free(&signature);
    neverc_ecdsa_private_key_free(&key);
    puts("passed");
    return 0;
}
