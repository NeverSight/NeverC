#include "neverc/std/crypto/dsa.h"
#include <stdio.h>
#include <string.h>

static unsigned entropy_calls;

static int entropy_fails(unsigned char *buffer, size_t length) {
    entropy_calls++;
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
    neverc_bigint_set_string(&key.pub.p,
        "A9B5B793FB4785793D246BAE77E8FF63CA52F442DA763C440259919FE1BC1D60"
        "65A9350637A04F75A2F039401D49F08E066C4D275A5A65DA5684BC563C14289D"
        "7AB8A67163BFBF79D85972619AD2CFF55AB0EE77A9002B0EF96293BDD0F42685"
        "EBB2C66C327079F6C98000FBCB79AACDE1BC6F9D5C7B1A97E3D9D54ED7951FEF", 16);
    neverc_bigint_set_string(&key.pub.q,
        "E1D3391245933D68A0714ED34BBCB7A1F422B9C1", 16);
    neverc_bigint_set_string(&key.pub.g,
        "634364FC25248933D01D1993ECABD0657CC0CB2CEED7ED2E3E8AECDFCDC4A25C"
        "3B15E9E3B163ACA2984B5539181F3EFF1A5E8903D71D5B95DA4F27202B77D2C4"
        "4B430BB53741A8D59A8F86887525C9F2A6A5980A195EAA7F2FF910064301DEF8"
        "9D3AA213E1FAC7768D89365318E370AF54A112EFBA9246D9158386BA1B4EEFDA", 16);
    neverc_bigint_set_int64(&key.x, 3);
    neverc_bigint_set_int64(&signature.r, 7);
    neverc_bigint_set_int64(&signature.s, 11);

    entropy_calls = 0;
    CHECK(neverc_dsa_sign(&key, hash, sizeof(hash), &signature) == -1);
    CHECK(neverc_bigint_is_zero(&signature.r));
    CHECK(neverc_bigint_is_zero(&signature.s));
    CHECK(entropy_calls == 1);

    neverc_bigint_set_int64(&key.pub.p, 7);
    neverc_bigint_set_int64(&key.pub.q, 3);
    neverc_bigint_set_int64(&key.pub.g, 2);
    neverc_bigint_set_int64(&key.x, 1);
    neverc_bigint_set_int64(&signature.r, 7);
    neverc_bigint_set_int64(&signature.s, 11);
    entropy_calls = 0;
    CHECK(neverc_dsa_sign(&key, hash, sizeof(hash), &signature) == -1);
    CHECK(neverc_bigint_is_zero(&signature.r));
    CHECK(neverc_bigint_is_zero(&signature.s));
    CHECK(entropy_calls == 0);

    neverc_dsa_signature_free(&signature);
    neverc_dsa_private_key_free(&key);
    puts("passed");
    return 0;
}
