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

    neverc_mlkem1024_ek_t ek1024 = {{0}};
    uint8_t shared1024[NEVERC_MLKEM_SHARED_KEY_SIZE];
    uint8_t ciphertext1024[NEVERC_MLKEM1024_CT_SIZE];
    memset(shared1024, 0x5a, sizeof(shared1024));
    memset(ciphertext1024, 0x5a, sizeof(ciphertext1024));
    CHECK(neverc_mlkem1024_encapsulate(
              &ek1024, shared1024, ciphertext1024) == -1);
    CHECK(all_zero(shared1024, sizeof(shared1024)));
    CHECK(all_zero(ciphertext1024, sizeof(ciphertext1024)));

    puts("passed");
    return 0;
}
