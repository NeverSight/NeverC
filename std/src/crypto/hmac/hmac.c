#include "neverc/std/crypto/hmac.h"
#include "neverc/std/crypto/sha256.h"
#include "neverc/std/crypto/sha512.h"
#include "neverc/std/crypto/sha1.h"
#include "neverc/std/crypto/md5.h"
#include "neverc/std/crypto/subtle.h"
#include "neverc/std/_platform.h"
#include <stdint.h>
#include <string.h>

/* SHA-256/SHA-1/MD5 length fields are 64 bits (max 2^61-1 bytes). The inner
 * hash already absorbed the 64-byte ipad, so a wrapping data_len would make
 * SHA zero the midstate and finalize to a message-independent digest. */
static int hmac_sha256_family_len_ok(size_t key_len, size_t data_len) {
    const uint64_t max_bytes = UINT64_MAX / 8;
    if ((uint64_t)key_len > max_bytes)
        return 0;
    return max_bytes >= 64 && (uint64_t)data_len <= max_bytes - 64;
}

/* SHA-512's implementation refuses a wrapping 64-bit byte counter. After the
 * 128-byte ipad, data_len > UINT64_MAX-128 would wipe the midstate. */
static int hmac_sha512_len_ok(size_t data_len) {
    return (uint64_t)data_len <= UINT64_MAX - 128;
}

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
    if (!out) return;
    if ((!key && key_len != 0) || (!data && data_len != 0) ||
        !hmac_sha256_family_len_ok(key_len, data_len)) {
        memset(out, 0, 32);
        return;
    }
    const size_t block_size = 64;
    uint8_t k_prime[64];
    memset(k_prime, 0, block_size);

    if (key_len > block_size) {
        neverc_sha256_sum(key, key_len, k_prime);
    } else if (key_len > 0) {
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
    if (data_len > 0)
        neverc_sha256_update(&inner, data, data_len);
    uint8_t inner_hash[32];
    neverc_sha256_final(&inner, inner_hash);

    neverc_sha256_ctx outer;
    neverc_sha256_init(&outer);
    neverc_sha256_update(&outer, opad, block_size);
    neverc_sha256_update(&outer, inner_hash, 32);
    neverc_sha256_final(&outer, out);
    neverc_platform_secure_zero(k_prime, sizeof(k_prime));
    neverc_platform_secure_zero(ipad, sizeof(ipad));
    neverc_platform_secure_zero(opad, sizeof(opad));
    neverc_platform_secure_zero(inner_hash, sizeof(inner_hash));
    neverc_platform_secure_zero(&inner, sizeof(inner));
    neverc_platform_secure_zero(&outer, sizeof(outer));
}

void neverc_hmac_sha512(const uint8_t *key, size_t key_len,
                        const uint8_t *data, size_t data_len,
                        uint8_t out[64])
{
    if (!out) return;
    if ((!key && key_len != 0) || (!data && data_len != 0) ||
        !hmac_sha512_len_ok(data_len)) {
        memset(out, 0, 64);
        return;
    }
    const size_t block_size = 128;
    uint8_t k_prime[128];
    memset(k_prime, 0, block_size);

    if (key_len > block_size) {
        neverc_sha512_sum(key, key_len, k_prime);
    } else if (key_len > 0) {
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
    if (data_len > 0)
        neverc_sha512_update(&inner, data, data_len);
    uint8_t inner_hash[64];
    neverc_sha512_final(&inner, inner_hash);

    neverc_sha512_ctx outer;
    neverc_sha512_init(&outer);
    neverc_sha512_update(&outer, opad, block_size);
    neverc_sha512_update(&outer, inner_hash, 64);
    neverc_sha512_final(&outer, out);
    neverc_platform_secure_zero(k_prime, sizeof(k_prime));
    neverc_platform_secure_zero(ipad, sizeof(ipad));
    neverc_platform_secure_zero(opad, sizeof(opad));
    neverc_platform_secure_zero(inner_hash, sizeof(inner_hash));
    neverc_platform_secure_zero(&inner, sizeof(inner));
    neverc_platform_secure_zero(&outer, sizeof(outer));
}

void neverc_hmac_sha1(const uint8_t *key, size_t key_len,
                      const uint8_t *data, size_t data_len,
                      uint8_t out[20])
{
    if (!out) return;
    if ((!key && key_len != 0) || (!data && data_len != 0) ||
        !hmac_sha256_family_len_ok(key_len, data_len)) {
        memset(out, 0, 20);
        return;
    }
    const size_t block_size = 64;
    uint8_t k_prime[64];
    memset(k_prime, 0, block_size);

    if (key_len > block_size) {
        neverc_sha1_sum(key, key_len, k_prime);
    } else if (key_len > 0) {
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
    if (data_len > 0)
        neverc_sha1_update(&inner, data, data_len);
    uint8_t inner_hash[20];
    neverc_sha1_final(&inner, inner_hash);

    neverc_sha1_ctx outer;
    neverc_sha1_init(&outer);
    neverc_sha1_update(&outer, opad, block_size);
    neverc_sha1_update(&outer, inner_hash, 20);
    neverc_sha1_final(&outer, out);
    neverc_platform_secure_zero(k_prime, sizeof(k_prime));
    neverc_platform_secure_zero(ipad, sizeof(ipad));
    neverc_platform_secure_zero(opad, sizeof(opad));
    neverc_platform_secure_zero(inner_hash, sizeof(inner_hash));
    neverc_platform_secure_zero(&inner, sizeof(inner));
    neverc_platform_secure_zero(&outer, sizeof(outer));
}

void neverc_hmac_md5(const uint8_t *key, size_t key_len,
                     const uint8_t *data, size_t data_len,
                     uint8_t out[16])
{
    if (!out) return;
    if ((!key && key_len != 0) || (!data && data_len != 0) ||
        !hmac_sha256_family_len_ok(key_len, data_len)) {
        memset(out, 0, 16);
        return;
    }
    const size_t block_size = 64;
    uint8_t k_prime[64];
    memset(k_prime, 0, block_size);

    if (key_len > block_size) {
        neverc_md5_sum(key, key_len, k_prime);
    } else if (key_len > 0) {
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
    if (data_len > 0)
        neverc_md5_update(&inner, data, data_len);
    uint8_t inner_hash[16];
    neverc_md5_final(&inner, inner_hash);

    neverc_md5_ctx outer;
    neverc_md5_init(&outer);
    neverc_md5_update(&outer, opad, block_size);
    neverc_md5_update(&outer, inner_hash, 16);
    neverc_md5_final(&outer, out);
    neverc_platform_secure_zero(k_prime, sizeof(k_prime));
    neverc_platform_secure_zero(ipad, sizeof(ipad));
    neverc_platform_secure_zero(opad, sizeof(opad));
    neverc_platform_secure_zero(inner_hash, sizeof(inner_hash));
    neverc_platform_secure_zero(&inner, sizeof(inner));
    neverc_platform_secure_zero(&outer, sizeof(outer));
}

int neverc_hmac_equal(const uint8_t *mac1, const uint8_t *mac2, size_t len) {
    return neverc_subtle_constant_time_compare(mac1, mac2, len);
}
