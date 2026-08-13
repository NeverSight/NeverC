/*
 * PBKDF2 — Password-Based Key Derivation Function 2 (RFC 2898 / RFC 8018).
 * Uses HMAC-SHA256 as the PRF.
 *
 * Performance: the HMAC key (the password) is constant across every one of
 * the (often 100k+) iterations.  HMAC absorbs one fixed block for (key^ipad)
 * and one for (key^opad) on *every* call, so a naive `hmac_sha256` loop pays
 * 4 SHA-256 compressions per iteration (ipad-block, inner-msg, opad-block,
 * outer-msg).  We precompute the two midstates once and resume from them,
 * cutting the steady-state cost to 2 compressions per iteration — ~2x.
 */
#include "neverc/std/crypto/pbkdf2.h"
#include "neverc/std/crypto/sha256.h"
#include "neverc/std/_platform.h"
#include <string.h>

/* HMAC-SHA256 with the key's ipad/opad blocks already absorbed. */
typedef struct {
    neverc_sha256_ctx ipad; /* state after SHA256(key' ^ ipad) */
    neverc_sha256_ctx opad; /* state after SHA256(key' ^ opad) */
} hmac_sha256_pre;

static void hmac_sha256_pre_init(hmac_sha256_pre *p,
                                 const uint8_t *key, size_t key_len) {
    uint8_t k[64];
    memset(k, 0, sizeof(k));
    if (key_len > 64)
        neverc_sha256_sum(key, key_len, k);
    else if (key_len > 0)
        memcpy(k, key, key_len);

    uint8_t pad[64];
    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x36;
    neverc_sha256_init(&p->ipad);
    neverc_sha256_update(&p->ipad, pad, 64);

    for (int i = 0; i < 64; i++) pad[i] = k[i] ^ 0x5c;
    neverc_sha256_init(&p->opad);
    neverc_sha256_update(&p->opad, pad, 64);
    neverc_platform_secure_zero(k, sizeof(k));
    neverc_platform_secure_zero(pad, sizeof(pad));
}

/* out = HMAC-SHA256(key, msg), resuming from the precomputed midstates. */
static void hmac_sha256_pre_compute(const hmac_sha256_pre *p,
                                    const uint8_t *msg, size_t msg_len,
                                    uint8_t out[32]) {
    neverc_sha256_ctx c = p->ipad;   /* resume after (key^ipad) block */
    neverc_sha256_update(&c, msg, msg_len);
    uint8_t inner[32];
    neverc_sha256_final(&c, inner);

    c = p->opad;                     /* resume after (key^opad) block */
    neverc_sha256_update(&c, inner, 32);
    neverc_sha256_final(&c, out);
    neverc_platform_secure_zero(inner, sizeof(inner));
    neverc_platform_secure_zero(&c, sizeof(c));
}

int neverc_pbkdf2_sha256(uint8_t *dk, size_t dk_len,
                         const uint8_t *password, size_t password_len,
                         const uint8_t *salt, size_t salt_len,
                         int iterations) {
    const uint64_t max_dk_len = (uint64_t)UINT32_MAX * 32U;
    if (!dk || (!password && password_len != 0) ||
        (!salt && salt_len != 0) || iterations < 1 || dk_len == 0 ||
        salt_len > 256 || (uint64_t)dk_len > max_dk_len)
        return -1;

    hmac_sha256_pre pre;
    hmac_sha256_pre_init(&pre, password, password_len);

    uint32_t block_num = 1;
    size_t off = 0;
    uint8_t salt_block[256 + 4] = {0};
    if (salt_len > 0) memcpy(salt_block, salt, salt_len);
    uint8_t u[32], t[32];

    while (off < dk_len) {
        /* U_1 = HMAC(password, salt || INT_32_BE(block_num)) */
        size_t sb_len = salt_len + 4;
        salt_block[salt_len]     = (uint8_t)(block_num >> 24);
        salt_block[salt_len + 1] = (uint8_t)(block_num >> 16);
        salt_block[salt_len + 2] = (uint8_t)(block_num >> 8);
        salt_block[salt_len + 3] = (uint8_t)(block_num);

        hmac_sha256_pre_compute(&pre, salt_block, sb_len, u);
        memcpy(t, u, 32);

        for (int i = 1; i < iterations; i++) {
            hmac_sha256_pre_compute(&pre, u, 32, u);
            for (int j = 0; j < 32; j++) t[j] ^= u[j];
        }

        size_t n = dk_len - off;
        if (n > 32) n = 32;
        memcpy(dk + off, t, n);
        off += n;
        if (off < dk_len) block_num++;
    }

    neverc_platform_secure_zero(salt_block, sizeof(salt_block));
    neverc_platform_secure_zero(u, sizeof(u));
    neverc_platform_secure_zero(t, sizeof(t));
    neverc_platform_secure_zero(&pre, sizeof(pre));
    return 0;
}
