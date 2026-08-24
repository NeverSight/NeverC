#include "neverc/std/crypto/rsa.h"
#include "neverc/std/_platform.h"
#include <stdio.h>
#include <string.h>

enum test_random_mode {
    TEST_RANDOM_REAL,
    TEST_RANDOM_FAIL,
    TEST_RANDOM_SCRIPT_ONCE_THEN_FAIL,
    TEST_RANDOM_SCRIPT_REPEAT,
    TEST_RANDOM_REJECT_THEN_TWO
};

static enum test_random_mode random_mode;
static unsigned char scripted_random[512];
static size_t scripted_random_len;
static int random_calls;

static int test_random(unsigned char *buffer, size_t length) {
    random_calls++;
    if (random_mode == TEST_RANDOM_REAL)
        return neverc_platform_random(buffer, length);
    if (random_mode == TEST_RANDOM_FAIL ||
        (random_mode == TEST_RANDOM_SCRIPT_ONCE_THEN_FAIL &&
         random_calls > 1)) {
        if (buffer) memset(buffer, 0xa5, length);
        return -1;
    }
    if (random_mode == TEST_RANDOM_REJECT_THEN_TWO) {
        if (!buffer) return -1;
        memset(buffer, 0, length);
        if (random_calls == 1 && length > 0) {
            buffer[length - 1] = 1;
        } else if (random_calls == 2 && length >= 2) {
            buffer[length - 2] = 0x03;
            buffer[length - 1] = 0xfa; /* 1019 - 1 */
        } else if (random_calls >= 3 && length > 0) {
            buffer[length - 1] = 2;
        }
        return 0;
    }
    if (!buffer || scripted_random_len > length) return -1;
    memset(buffer, 0, length);
    memcpy(buffer + length - scripted_random_len,
           scripted_random, scripted_random_len);
    return 0;
}

#define NCI_RSA_RANDOM test_random
#include "../../../std/src/crypto/rsa/rsa.c"
#undef NCI_RSA_RANDOM

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
    random_mode = TEST_RANDOM_FAIL;
    random_calls = 0;
    neverc_rsa_private_key_t private_key;
    neverc_rsa_private_key_init(&private_key);
    neverc_bigint_set_int64(&private_key.d, 3);
    neverc_bigint_set_int64(&private_key.p, 5);
    neverc_bigint_set_int64(&private_key.q, 7);

    CHECK(neverc_rsa_generate_key(&private_key, 512) == -1);
    CHECK(neverc_bigint_is_zero(&private_key.pub.n));
    CHECK(neverc_bigint_is_zero(&private_key.d));
    CHECK(neverc_bigint_is_zero(&private_key.p));
    CHECK(neverc_bigint_is_zero(&private_key.q));
    neverc_rsa_private_key_free(&private_key);

    neverc_rsa_public_key_t public_key;
    neverc_rsa_public_key_init(&public_key);
    CHECK(neverc_bigint_set_string(
              &public_key.n, "ffffffffffffffffffffffff", 16) == 0);
    neverc_bigint_set_int64(&public_key.e, 3);

    const unsigned char message = 0x42;
    unsigned char ciphertext[12];
    memset(ciphertext, 0x5a, sizeof(ciphertext));
    size_t ciphertext_len = 99;
    CHECK(neverc_rsa_encrypt_pkcs1v15(
              &public_key, &message, 1, ciphertext, sizeof(ciphertext),
              &ciphertext_len) == -1);
    CHECK(ciphertext_len == 0);
    CHECK(ciphertext[0] == 0x5a);

    neverc_rsa_public_key_free(&public_key);

    /* Arnault's published composite is a strong pseudoprime to every prime
     * base below 200, so the former fixed bases 2..37 all accepted it.  Its
     * published non-trivial factor is a certain rejecting witness: powers of
     * the factor are zero modulo that factor and therefore cannot be +/-1
     * modulo the composite. */
    neverc_bigint_t arnault, factor;
    neverc_bigint_init(&arnault);
    neverc_bigint_init(&factor);
    CHECK(neverc_bigint_set_string(
              &arnault,
              "80383745745363949125707961434194210813883768828755814583748891752229"
              "74273765333652186502336163960045457915042023603208766569966760987284"
              "0439654082329287387918508691668573282677617710293896977394701670823"
              "0428687109997439976544144845341155872450633409279022275296229414984"
              "2306881685404326457534018329786111298960644845216191652872597534901",
              10) == 0);
    CHECK(neverc_bigint_set_string(
              &factor,
              "400958216639499605418306452084546853005188166041132508774"
              "50620473800321707011962427162231915972197335821631650853"
              "58166969145233813917169287527980445796800452592031836601",
              10) == 0);
    scripted_random_len =
        (size_t)(neverc_bigint_bit_len(&factor) + 7) / 8U;
    CHECK(scripted_random_len <= sizeof(scripted_random));
    CHECK(bigint_to_bytes(&factor, scripted_random,
                          (int)scripted_random_len) == 0);
    random_mode = TEST_RANDOM_SCRIPT_REPEAT;
    random_calls = 0;
    CHECK(miller_rabin(&arnault, 12) == 0);
    CHECK(random_calls == 1);
    rsa_bigint_secure_free(&arnault);
    rsa_bigint_secure_free(&factor);

    /* The witness sampler must discard both endpoints and draw again. */
    neverc_bigint_t small_prime;
    neverc_bigint_init(&small_prime);
    neverc_bigint_set_int64(&small_prime, 1019);
    random_mode = TEST_RANDOM_REJECT_THEN_TWO;
    random_calls = 0;
    CHECK(miller_rabin(&small_prime, 1) == 1);
    CHECK(random_calls == 3);
    rsa_bigint_secure_free(&small_prime);

    /* Entropy failure after candidate generation must not be mistaken for a
     * successful primality result.  The P-256 field modulus is a convenient
     * 256-bit prime whose top two bits and low bit are already set, so
     * random_bigint leaves the scripted candidate unchanged. */
    neverc_bigint_t witness_candidate;
    neverc_bigint_init(&witness_candidate);
    CHECK(neverc_bigint_set_string(
              &witness_candidate,
              "ffffffff00000001000000000000000000000000ffffffffffffffffffffffff",
              16) == 0);
    CHECK(has_small_factor(&witness_candidate) == 0);
    scripted_random_len =
        (size_t)(neverc_bigint_bit_len(&witness_candidate) + 7) / 8U;
    CHECK(scripted_random_len <= sizeof(scripted_random));
    CHECK(bigint_to_bytes(&witness_candidate, scripted_random,
                          (int)scripted_random_len) == 0);
    rsa_bigint_secure_reset(&witness_candidate);
    random_mode = TEST_RANDOM_SCRIPT_ONCE_THEN_FAIL;
    random_calls = 0;
    CHECK(gen_prime(&witness_candidate, 256) == -1);
    CHECK(random_calls == 2);
    rsa_bigint_secure_free(&witness_candidate);

    random_mode = TEST_RANDOM_REAL;
    neverc_bigint_t candidate;
    neverc_bigint_init(&candidate);
    CHECK(random_bigint(&candidate, 257) == 0);
    CHECK(neverc_bigint_bit_len(&candidate) == 257);
    CHECK(neverc_bigint_bit(&candidate, 255) == 1);
    rsa_bigint_secure_free(&candidate);

    neverc_rsa_private_key_init(&private_key);
    CHECK(neverc_rsa_generate_key(&private_key, 512) == 0);
    const unsigned char plaintext[] = "blind";
    unsigned char encrypted[64];
    ciphertext_len = 0;
    CHECK(neverc_rsa_encrypt_pkcs1v15(
              &private_key.pub, plaintext, sizeof(plaintext) - 1,
              encrypted, sizeof(encrypted), &ciphertext_len) == 0);

    random_mode = TEST_RANDOM_FAIL;
    unsigned char decrypted[64];
    size_t decrypted_len = 99;
    CHECK(neverc_rsa_decrypt_pkcs1v15(
              &private_key, encrypted, ciphertext_len,
              decrypted, sizeof(decrypted), &decrypted_len) == -1);
    CHECK(decrypted_len == 0);

    unsigned char hash[NEVERC_SHA256_DIGEST_SIZE] = {0};
    unsigned char signature[64];
    size_t signature_len = 99;
    CHECK(neverc_rsa_sign_pkcs1v15_sha256(
              &private_key, hash, sizeof(hash), signature,
              sizeof(signature), &signature_len) == -1);
    CHECK(signature_len == 0);
    neverc_rsa_private_key_free(&private_key);

    puts("passed");
    return 0;
}
