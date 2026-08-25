#include "neverc/std/crypto/aes.h"
#include "neverc/std/_platform.h"
#include <string.h>
#include "aes_ct_sbox.h"

/*
 * AES (Rijndael) block cipher — FIPS 197.
 * Pure C implementation, supports AES-128/192/256.
 *
 * The portable backend uses a Boyar-Peralta bitsliced S-box and byte-oriented
 * linear layers. Memory addresses and branches are independent of key and
 * block contents; hardware-specific acceleration can be added above this safe
 * fallback without exposing a table-based path.
 */

static void aes_sub_bytes(uint8_t *data, size_t len, int inverse) {
    uint32_t q[8] = {0};
    for (size_t i = 0; i < len; i++)
        for (unsigned bit = 0; bit < 8; bit++)
            q[bit] |= (uint32_t)((data[i] >> bit) & 1u) << i;
    if (inverse)
        aes_ct_inv_sbox(q);
    else
        aes_ct_sbox(q);
    for (size_t i = 0; i < len; i++) {
        uint8_t value = 0;
        for (unsigned bit = 0; bit < 8; bit++)
            value |= (uint8_t)(((q[bit] >> i) & 1u) << bit);
        data[i] = value;
    }
}

static const uint32_t rcon[10] = {
    0x01000000, 0x02000000, 0x04000000, 0x08000000, 0x10000000,
    0x20000000, 0x40000000, 0x80000000, 0x1b000000, 0x36000000,
};

static uint32_t sub_word(uint32_t w) {
    uint8_t bytes[4] = {
        (uint8_t)(w >> 24), (uint8_t)(w >> 16),
        (uint8_t)(w >> 8), (uint8_t)w
    };
    aes_sub_bytes(bytes, sizeof(bytes), 0);
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static uint32_t rot_word(uint32_t w) {
    return (w << 8) | (w >> 24);
}

static uint32_t get_u32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] <<  8) | ((uint32_t)p[3]);
}

static uint8_t aes_xtime(uint8_t x) {
    return (uint8_t)((x << 1) ^ (0x1bu & (uint8_t)(0u - (x >> 7))));
}

static void aes_add_round_key(uint8_t state[16], const uint32_t *key) {
    for (int column = 0; column < 4; column++) {
        uint32_t word = key[column];
        state[4 * column] ^= (uint8_t)(word >> 24);
        state[4 * column + 1] ^= (uint8_t)(word >> 16);
        state[4 * column + 2] ^= (uint8_t)(word >> 8);
        state[4 * column + 3] ^= (uint8_t)word;
    }
}

static void aes_shift_rows(uint8_t state[16], int inverse) {
    uint8_t shifted[16];
    for (int row = 0; row < 4; row++)
        for (int column = 0; column < 4; column++) {
            int source = inverse ? ((column - row + 4) & 3)
                                 : ((column + row) & 3);
            shifted[4 * column + row] = state[4 * source + row];
        }
    memcpy(state, shifted, sizeof(shifted));
}

static void aes_mix_columns(uint8_t state[16]) {
    for (int column = 0; column < 4; column++) {
        uint8_t *s = state + 4 * column;
        uint8_t a = s[0], b = s[1], c = s[2], d = s[3];
        uint8_t sum = a ^ b ^ c ^ d;
        s[0] ^= sum ^ aes_xtime((uint8_t)(a ^ b));
        s[1] ^= sum ^ aes_xtime((uint8_t)(b ^ c));
        s[2] ^= sum ^ aes_xtime((uint8_t)(c ^ d));
        s[3] ^= sum ^ aes_xtime((uint8_t)(d ^ a));
    }
}

static void aes_inv_mix_columns(uint8_t state[16]) {
    for (int column = 0; column < 4; column++) {
        uint8_t *s = state + 4 * column;
        uint8_t u = aes_xtime(aes_xtime((uint8_t)(s[0] ^ s[2])));
        uint8_t v = aes_xtime(aes_xtime((uint8_t)(s[1] ^ s[3])));
        s[0] ^= u; s[1] ^= v; s[2] ^= u; s[3] ^= v;
    }
    aes_mix_columns(state);
}

int neverc_aes_init(neverc_aes_ctx_t *ctx, const uint8_t *key, int key_len) {
    if (!ctx) return -1;
    int nk, nr;
    switch (key_len) {
    case 16: nk = 4; nr = 10; break;
    case 24: nk = 6; nr = 12; break;
    case 32: nk = 8; nr = 14; break;
    default:
        neverc_platform_secure_zero(ctx, sizeof(*ctx));
        return -1;
    }
    if (!key) {
        neverc_platform_secure_zero(ctx, sizeof(*ctx));
        return -1;
    }
    ctx->rounds = nr;

    uint32_t *ek = ctx->enc_key;
    for (int i = 0; i < nk; i++)
        ek[i] = get_u32be(key + 4 * i);

    int total = 4 * (nr + 1);
    for (int i = nk; i < total; i++) {
        uint32_t t = ek[i - 1];
        if (i % nk == 0)
            t = sub_word(rot_word(t)) ^ rcon[i / nk - 1];
        else if (nk > 6 && i % nk == 4)
            t = sub_word(t);
        ek[i] = ek[i - nk] ^ t;
    }

    /* Retain a reverse schedule for ABI/source compatibility. The constant-time
     * inverse cipher below uses enc_key directly so its linear layers remain
     * explicit and auditable. */
    uint32_t *dk = ctx->dec_key;
    for (int i = 0; i < total; i++)
        dk[i] = ek[total - 4 - (i & ~3) + (i & 3)];
    if (total < NEVERC_AES_MAX_RK) {
        size_t tail_bytes =
            (size_t)(NEVERC_AES_MAX_RK - total) * sizeof(*ek);
        neverc_platform_secure_zero(ek + total, tail_bytes);
        neverc_platform_secure_zero(dk + total, tail_bytes);
    }

    return 0;
}

void neverc_aes_encrypt_block(const neverc_aes_ctx_t *ctx, uint8_t dst[16], const uint8_t src[16]) {
    if (!ctx || !dst || !src ||
        (ctx->rounds != 10 && ctx->rounds != 12 && ctx->rounds != 14))
        return;
    uint8_t state[16];
    memcpy(state, src, sizeof(state));
    aes_add_round_key(state, ctx->enc_key);
    for (int round = 1; round < ctx->rounds; round++) {
        aes_sub_bytes(state, sizeof(state), 0);
        aes_shift_rows(state, 0);
        aes_mix_columns(state);
        aes_add_round_key(state, ctx->enc_key + 4 * round);
    }
    aes_sub_bytes(state, sizeof(state), 0);
    aes_shift_rows(state, 0);
    aes_add_round_key(state, ctx->enc_key + 4 * ctx->rounds);
    memcpy(dst, state, sizeof(state));
    neverc_platform_secure_zero(state, sizeof(state));
}

void neverc_aes_decrypt_block(const neverc_aes_ctx_t *ctx, uint8_t dst[16], const uint8_t src[16]) {
    if (!ctx || !dst || !src ||
        (ctx->rounds != 10 && ctx->rounds != 12 && ctx->rounds != 14))
        return;
    uint8_t state[16];
    memcpy(state, src, sizeof(state));
    aes_add_round_key(state, ctx->enc_key + 4 * ctx->rounds);
    for (int round = ctx->rounds - 1; round > 0; round--) {
        aes_shift_rows(state, 1);
        aes_sub_bytes(state, sizeof(state), 1);
        aes_add_round_key(state, ctx->enc_key + 4 * round);
        aes_inv_mix_columns(state);
    }
    aes_shift_rows(state, 1);
    aes_sub_bytes(state, sizeof(state), 1);
    aes_add_round_key(state, ctx->enc_key);
    memcpy(dst, state, sizeof(state));
    neverc_platform_secure_zero(state, sizeof(state));
}
