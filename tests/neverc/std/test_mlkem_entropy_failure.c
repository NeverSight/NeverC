#include "neverc/std/crypto/mlkem.h"
#include <stdio.h>
#include <string.h>

static int test_crypto_rand_read(uint8_t *buffer, size_t length) {
    if (buffer) memset(buffer, 0xa5, length);
    return -1;
}

#define NEVERC_CRYPTO_RAND_H
#define neverc_crypto_rand_read test_crypto_rand_read
#include "../../../std/src/crypto/mlkem/mlkem.c"
#undef neverc_crypto_rand_read

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int all_zero(const void *value, size_t length) {
    const uint8_t *bytes = (const uint8_t *)value;
    uint8_t combined = 0;
    for (size_t i = 0; i < length; i++) combined |= bytes[i];
    return combined == 0;
}

int main(void) {
    /* FIPS 203: NTT product-sums must reduce into Z_q. These inputs make
     * 24-bit Barrett underestimate floor(a/q) by 2, so a single subtract
     * leaves a representative >= q. */
    CHECK(fe_reduce(22097920u) == 18);
    CHECK(fe_reduce(22154496u) == 1);
    {
        ntt_elem f, g, h;
        memset(f, 0, sizeof(f));
        memset(g, 0, sizeof(g));
        f[0] = 3328;
        f[1] = 3328;
        g[0] = 3312;
        g[1] = 3328;
        ntt_mul(f, g, h);
        CHECK(h[1] < Q);
        CHECK(h[1] == 18);
    }

    neverc_mlkem768_dk_t dk768;
    memset(&dk768, 0x5a, sizeof(dk768));
    CHECK(neverc_mlkem768_generate_key(&dk768) == -1);
    CHECK(all_zero(&dk768, sizeof(dk768)));

    neverc_mlkem1024_dk_t dk1024;
    memset(&dk1024, 0x5a, sizeof(dk1024));
    CHECK(neverc_mlkem1024_generate_key(&dk1024) == -1);
    CHECK(all_zero(&dk1024, sizeof(dk1024)));

    neverc_mlkem768_ek_t ek768 = {{0}};
    uint8_t shared768[NEVERC_MLKEM_SHARED_KEY_SIZE];
    uint8_t ciphertext768[NEVERC_MLKEM768_CT_SIZE];
    memset(shared768, 0x5a, sizeof(shared768));
    memset(ciphertext768, 0x5a, sizeof(ciphertext768));
    CHECK(neverc_mlkem768_encapsulate(
              &ek768, shared768, ciphertext768) == -1);
    CHECK(all_zero(shared768, sizeof(shared768)));
    CHECK(all_zero(ciphertext768, sizeof(ciphertext768)));

    /* All-zero ek is rejected before RAND. A canonical non-zero ek must
     * still fail closed when entropy acquisition returns -1. */
    ek768.ek[0] = 1;
    memset(shared768, 0x5a, sizeof(shared768));
    memset(ciphertext768, 0x5a, sizeof(ciphertext768));
    CHECK(neverc_mlkem768_encapsulate(
              &ek768, shared768, ciphertext768) == -1);
    CHECK(all_zero(shared768, sizeof(shared768)));
    CHECK(all_zero(ciphertext768, sizeof(ciphertext768)));

    neverc_mlkem1024_ek_t ek1024 = {{0}};
    uint8_t shared1024[NEVERC_MLKEM_SHARED_KEY_SIZE];
    uint8_t ciphertext1024[NEVERC_MLKEM1024_CT_SIZE];
    memset(shared1024, 0x5a, sizeof(shared1024));
    memset(ciphertext1024, 0x5a, sizeof(ciphertext1024));
    CHECK(neverc_mlkem1024_encapsulate(
              &ek1024, shared1024, ciphertext1024) == -1);
    CHECK(all_zero(shared1024, sizeof(shared1024)));
    CHECK(all_zero(ciphertext1024, sizeof(ciphertext1024)));

    ek1024.ek[0] = 1;
    memset(shared1024, 0x5a, sizeof(shared1024));
    memset(ciphertext1024, 0x5a, sizeof(ciphertext1024));
    CHECK(neverc_mlkem1024_encapsulate(
              &ek1024, shared1024, ciphertext1024) == -1);
    CHECK(all_zero(shared1024, sizeof(shared1024)));
    CHECK(all_zero(ciphertext1024, sizeof(ciphertext1024)));

    memset(shared768, 0x5a, sizeof(shared768));
    memset(ciphertext768, 0x5a, sizeof(ciphertext768));
    CHECK(neverc_mlkem768_encapsulate(NULL, shared768, ciphertext768) == -1);
    CHECK(all_zero(shared768, sizeof(shared768)));
    CHECK(all_zero(ciphertext768, sizeof(ciphertext768)));

    /* Truncated ciphertext must be rejected, not treated as a FO match
     * because a prefix compare of length 0 is vacuously equal. */
    {
        uint8_t seed[NEVERC_MLKEM_SEED_SIZE];
        memset(seed, 1, sizeof(seed));
        neverc_mlkem768_dk_t dk;
        CHECK(neverc_mlkem768_new_dk(&dk, seed) == 0);

        uint8_t ct[NEVERC_MLKEM768_CT_SIZE];
        uint8_t shared[NEVERC_MLKEM_SHARED_KEY_SIZE];
        memset(ct, 0x5a, sizeof(ct));

        memset(shared, 0x5a, sizeof(shared));
        mlkem_lock();
        int rc = mlkem_decaps(3, dk.seed, dk.ek, NEVERC_MLKEM768_EK_SIZE,
                              ct, 0, shared);
        wipe_mlkem_scratch();
        mlkem_unlock();
        CHECK(rc == -1);
        CHECK(all_zero(shared, sizeof(shared)));

        memset(shared, 0x5a, sizeof(shared));
        mlkem_lock();
        rc = mlkem_decaps(3, dk.seed, dk.ek, NEVERC_MLKEM768_EK_SIZE,
                          ct, NEVERC_MLKEM768_CT_SIZE - 1, shared);
        wipe_mlkem_scratch();
        mlkem_unlock();
        CHECK(rc == -1);
        CHECK(all_zero(shared, sizeof(shared)));
    }
    {
        uint8_t seed[NEVERC_MLKEM_SEED_SIZE];
        memset(seed, 2, sizeof(seed));
        neverc_mlkem1024_dk_t dk;
        CHECK(neverc_mlkem1024_new_dk(&dk, seed) == 0);

        uint8_t ct[NEVERC_MLKEM1024_CT_SIZE];
        uint8_t shared[NEVERC_MLKEM_SHARED_KEY_SIZE];
        memset(ct, 0x5a, sizeof(ct));
        memset(shared, 0x5a, sizeof(shared));
        mlkem_lock();
        int rc = mlkem_decaps(4, dk.seed, dk.ek, NEVERC_MLKEM1024_EK_SIZE,
                              ct, NEVERC_MLKEM1024_CT_SIZE - 1, shared);
        wipe_mlkem_scratch();
        mlkem_unlock();
        CHECK(rc == -1);
        CHECK(all_zero(shared, sizeof(shared)));
    }

    puts("passed");
    return 0;
}
