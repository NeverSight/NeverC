#include "neverc/hmac.h"
#include "neverc/sha256.h"
#include "neverc/sha512.h"
#include "neverc/sha1.h"
#include "neverc/md5.h"
#include "neverc/subtle.h"
#include <string.h>

/*
 * HMAC(K, m) = H((K' ^ opad) || H((K' ^ ipad) || m))
 *
 * Where K' is the key padded/hashed to block_size bytes,
 * ipad = 0x36 repeated, opad = 0x5c repeated.
 *
 * RFC 2104 / FIPS 198-1 compliant.
 */

void neverc_hmac_sha256(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t out[32])
{
    const size_t block_size = 64;
    uint8_t k_prime[64];
    memset(k_prime, 0, block_size);

    if (key_len > block_size) {
        neverc_sha256_sum(key, key_len, k_prime);
    } else {
        memcpy(k_prime, key, key_len);
    }

    uint8_t ipad[64], opad[64];
    for (size_t i = 0; i < block_size; i++) {
        ipad[i] = k_prime[i] ^ 0x36;
        opad[i] = k_prime[i] ^ 0x5c;
    }

    neverc_sha256_ctx inner;
    neverc_sha256_init(&inner);
    neverc_sha256_update(&inner, ipad, block_size);
    neverc_sha256_update(&inner, data, data_len);
    uint8_t inner_hash[32];
    neverc_sha256_final(&inner, inner_hash);

    neverc_sha256_ctx outer;
    neverc_sha256_init(&outer);
    neverc_sha256_update(&outer, opad, block_size);
    neverc_sha256_update(&outer, inner_hash, 32);
    neverc_sha256_final(&outer, out);
}

void neverc_hmac_sha512(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t out[64])
{
    const size_t block_size = 128;
    uint8_t k_prime[128];
    memset(k_prime, 0, block_size);

    if (key_len > block_size) {
        neverc_sha512_sum(key, key_len, k_prime);
    } else {
        memcpy(k_prime, key, key_len);
    }

    uint8_t ipad[128], opad[128];
    for (size_t i = 0; i < block_size; i++) {
        ipad[i] = k_prime[i] ^ 0x36;
        opad[i] = k_prime[i] ^ 0x5c;
    }

    neverc_sha512_ctx inner;
    neverc_sha512_init(&inner);
    neverc_sha512_update(&inner, ipad, block_size);
    neverc_sha512_update(&inner, data, data_len);
    uint8_t inner_hash[64];
    neverc_sha512_final(&inner, inner_hash);

    neverc_sha512_ctx outer;
    neverc_sha512_init(&outer);
    neverc_sha512_update(&outer, opad, block_size);
    neverc_sha512_update(&outer, inner_hash, 64);
    neverc_sha512_final(&outer, out);
}

void neverc_hmac_sha1(const uint8_t *key, size_t key_len,
                      const uint8_t *data, size_t data_len,
                      uint8_t out[20])
{
    const size_t block_size = 64;
    uint8_t k_prime[64];
    memset(k_prime, 0, block_size);

    if (key_len > block_size) {
        neverc_sha1_sum(key, key_len, k_prime);
    } else {
        memcpy(k_prime, key, key_len);
    }

    uint8_t ipad[64], opad[64];
    for (size_t i = 0; i < block_size; i++) {
        ipad[i] = k_prime[i] ^ 0x36;
        opad[i] = k_prime[i] ^ 0x5c;
    }

    neverc_sha1_ctx inner;
    neverc_sha1_init(&inner);
    neverc_sha1_update(&inner, ipad, block_size);
    neverc_sha1_update(&inner, data, data_len);
    uint8_t inner_hash[20];
    neverc_sha1_final(&inner, inner_hash);

    neverc_sha1_ctx outer;
    neverc_sha1_init(&outer);
    neverc_sha1_update(&outer, opad, block_size);
    neverc_sha1_update(&outer, inner_hash, 20);
    neverc_sha1_final(&outer, out);
}

void neverc_hmac_md5(const uint8_t *key, size_t key_len,
                     const uint8_t *data, size_t data_len,
                     uint8_t out[16])
{
    const size_t block_size = 64;
    uint8_t k_prime[64];
    memset(k_prime, 0, block_size);

    if (key_len > block_size) {
        neverc_md5_sum(key, key_len, k_prime);
    } else {
        memcpy(k_prime, key, key_len);
    }

    uint8_t ipad[64], opad[64];
    for (size_t i = 0; i < block_size; i++) {
        ipad[i] = k_prime[i] ^ 0x36;
        opad[i] = k_prime[i] ^ 0x5c;
    }

    neverc_md5_ctx inner;
    neverc_md5_init(&inner);
    neverc_md5_update(&inner, ipad, block_size);
    neverc_md5_update(&inner, data, data_len);
    uint8_t inner_hash[16];
    neverc_md5_final(&inner, inner_hash);

    neverc_md5_ctx outer;
    neverc_md5_init(&outer);
    neverc_md5_update(&outer, opad, block_size);
    neverc_md5_update(&outer, inner_hash, 16);
    neverc_md5_final(&outer, out);
}

int neverc_hmac_equal(const uint8_t *mac1, const uint8_t *mac2, size_t len) {
    return neverc_subtle_constant_time_compare(mac1, mac2, len);
}
