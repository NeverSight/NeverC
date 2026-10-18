#include "neverc/std/crypto/aes.h"
#include "../../../std/src/crypto/aes/aes_ct_field.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

static void hex_to_bytes(const char *hex, uint8_t *out, int len) {
    for (int i = 0; i < len; i++) {
        unsigned int v;
        sscanf(hex + 2 * i, "%02x", &v);
        out[i] = (uint8_t)v;
    }
}

/* Independent scalar oracle in the AES polynomial basis. This deliberately
 * uses repeated multiplication, not the runtime's composite-field formulas. */
static uint8_t reference_mul(uint8_t a, uint8_t b) {
    unsigned product = 0, value = a;
    for (unsigned i = 0; i < 8; i++) {
        if ((b >> i) & 1u) product ^= value;
        value <<= 1;
        if (value & 0x100u) value ^= 0x11bu;
    }
    return (uint8_t)product;
}

static uint8_t reference_sbox(uint8_t x) {
    uint8_t inverse = 1;
    for (unsigned i = 0; i < 254; i++)
        inverse = reference_mul(inverse, x);
    unsigned result = inverse ^ 0x63u;
    for (unsigned shift = 1; shift <= 4; shift++)
        result ^= (unsigned)inverse << shift | (unsigned)inverse >> (8 - shift);
    return (uint8_t)result;
}

static void test_bit_transpose(void) {
    printf("[AES bit-plane transpose]\n");
    /* Test every basis vector against its matrix coordinates. Since the
     * transpose is linear, these cover every input/output bit connection. */
    for (unsigned row = 0; row < 8; row++) {
        for (unsigned column = 0; column < 8; column++) {
            uint64_t input = UINT64_C(1) << (8 * row + column);
            uint64_t expected = UINT64_C(1) << (8 * column + row);
            check_true("transpose basis vector", aes_transpose8(input) == expected);
        }
    }
    check_true("transpose zero", aes_transpose8(0) == 0);
    check_true("transpose all ones", aes_transpose8(UINT64_MAX) == UINT64_MAX);
}

static void test_substitution(void) {
    printf("[AES exhaustive substitution, all 32 bit lanes]\n");
    uint8_t forward[256], inverse[256] = {0};
    for (unsigned x = 0; x < 256; x++) {
        forward[x] = reference_sbox((uint8_t)x);
        inverse[forward[x]] = (uint8_t)x;
    }
    check_true("FIPS S-box oracle anchors",
               forward[0] == 0x63 && forward[0x53] == 0xed &&
               forward[0xff] == 0x16);

    for (unsigned base = 0; base < 256; base++) {
        uint32_t enc[8] = {0}, dec[8];
        /* Distinct lane values also catch accidental cross-lane operations. */
        for (unsigned lane = 0; lane < 32; lane++) {
            uint8_t x = (uint8_t)(base + 17 * lane);
            for (unsigned bit = 0; bit < 8; bit++)
                enc[bit] |= (uint32_t)((x >> bit) & 1u) << lane;
        }
        memcpy(dec, enc, sizeof(dec));
        aes_field_sbox(enc);
        aes_field_inv_sbox(dec);
        for (unsigned lane = 0; lane < 32; lane++) {
            uint8_t x = (uint8_t)(base + 17 * lane), got_enc = 0, got_dec = 0;
            for (unsigned bit = 0; bit < 8; bit++) {
                got_enc |= (uint8_t)(((enc[bit] >> lane) & 1u) << bit);
                got_dec |= (uint8_t)(((dec[bit] >> lane) & 1u) << bit);
            }
            char label[80];
            snprintf(label, sizeof(label), "S-box x=%u lane=%u", x, lane);
            check_true(label, got_enc == forward[x]);
            snprintf(label, sizeof(label), "inverse S-box x=%u lane=%u", x, lane);
            check_true(label, got_dec == inverse[x]);
        }
    }
}

static void test_key_schedule(void) {
    printf("[AES key schedules — FIPS 197 Appendix A]\n");
    static const struct {
        const char *key;
        int key_len;
        int rounds;
        uint32_t final_round[4];
    } cases[] = {
        {"2b7e151628aed2a6abf7158809cf4f3c", 16, 10,
         {0xd014f9a8, 0xc9ee2589, 0xe13f0cc8, 0xb6630ca6}},
        {"8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b", 24, 12,
         {0xe98ba06f, 0x448c773c, 0x8ecc7204, 0x01002202}},
        {"603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", 32, 14,
         {0xfe4890d1, 0xe6188d0b, 0x046df344, 0x706c631e}}
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        neverc_aes_ctx_t ctx;
        uint8_t key[32];
        hex_to_bytes(cases[i].key, key, cases[i].key_len);
        check_true("key schedule init", neverc_aes_init(&ctx, key, cases[i].key_len) == 0);
        check_true("round count", ctx.rounds == cases[i].rounds);
        check_true("final encryption round key",
                   memcmp(ctx.enc_key + 4 * cases[i].rounds,
                          cases[i].final_round, sizeof(cases[i].final_round)) == 0);
        check_true("first decryption round key",
                   memcmp(ctx.dec_key, cases[i].final_round,
                          sizeof(cases[i].final_round)) == 0);
    }
}

static void test_full_key_schedule(void) {
    printf("[AES complete key schedules against scalar byte oracle]\n");
    uint8_t sbox[256];
    for (unsigned i = 0; i < 256; i++)
        sbox[i] = reference_sbox((uint8_t)i);

    uint32_t random = 0xc51a7e39u;
    for (unsigned key_len = 16; key_len <= 32; key_len += 8) {
        unsigned rounds = key_len / 4 + 6;
        unsigned expanded_len = 16 * (rounds + 1);
        for (unsigned sample = 0; sample < key_len * 8 + 34; sample++) {
            uint8_t key[33] = {0}, expanded[240] = {0};
            /* Zero, all ones, every individual key bit, then dense keys.
             * key+1 also exercises unaligned key loads. */
            if (sample == 1) memset(key + 1, 0xff, key_len);
            if (sample >= 2 && sample < key_len * 8 + 2)
                key[1 + (sample - 2) / 8] = (uint8_t)(1u << ((sample - 2) % 8));
            if (sample >= key_len * 8 + 2) {
                for (unsigned i = 1; i <= key_len; i++) {
                    random ^= random << 13;
                    random ^= random >> 17;
                    random ^= random << 5;
                    key[i] = (uint8_t)random;
                }
            }
            memcpy(expanded, key + 1, key_len);
            uint8_t rcon = 1;
            for (unsigned offset = key_len; offset < expanded_len; offset += 4) {
                uint8_t word[4];
                memcpy(word, expanded + offset - 4, 4);
                if (offset % key_len == 0) {
                    uint8_t first = word[0];
                    for (unsigned i = 0; i < 3; i++) word[i] = sbox[word[i + 1]];
                    word[3] = sbox[first];
                    word[0] ^= rcon;
                    rcon = reference_mul(rcon, 2);
                } else if (key_len == 32 && offset % key_len == 16) {
                    for (unsigned i = 0; i < 4; i++) word[i] = sbox[word[i]];
                }
                for (unsigned i = 0; i < 4; i++)
                    expanded[offset + i] = expanded[offset - key_len + i] ^ word[i];
            }

            neverc_aes_ctx_t ctx;
            memset(&ctx, 0xa5, sizeof(ctx));
            check_true("full schedule init", neverc_aes_init(&ctx, key + 1, (int)key_len) == 0);
            check_true("full schedule round count", ctx.rounds == (int)rounds);
            for (unsigned i = 0; i < 60; i++) {
                const uint8_t *p = expanded + 4 * i;
                uint32_t expected = (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 |
                                    (uint32_t)p[2] << 8 | p[3];
                check_true("every encryption key word", ctx.enc_key[i] == expected);
                unsigned dec_index = i < expanded_len / 4
                    ? 4 * (rounds - i / 4) + i % 4 : i;
                check_true("every decryption key word", ctx.dec_key[dec_index] == expected);
            }
        }
    }
}

static void test_aes128(void) {
    printf("[AES-128 FIPS 197]\n");

    /* FIPS 197 Appendix B */
    uint8_t key[16], plain[16], cipher[16], result[16];
    hex_to_bytes("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
    hex_to_bytes("3243f6a8885a308d313198a2e0370734", plain, 16);
    hex_to_bytes("3925841d02dc09fbdc118597196a0b32", cipher, 16);

    neverc_aes_ctx_t ctx;
    check_true("init AES-128", neverc_aes_init(&ctx, key, 16) == 0);

    neverc_aes_encrypt_block(&ctx, result, plain);
    check_true("AES-128 encrypt", memcmp(result, cipher, 16) == 0);

    neverc_aes_decrypt_block(&ctx, result, cipher);
    check_true("AES-128 decrypt", memcmp(result, plain, 16) == 0);

    /* NIST SP 800-38A F.1.1 — ECB-AES128.Encrypt */
    hex_to_bytes("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
    neverc_aes_init(&ctx, key, 16);

    struct { const char *pt; const char *ct; } nist_128[] = {
        {"6bc1bee22e409f96e93d7e117393172a", "3ad77bb40d7a3660a89ecaf32466ef97"},
        {"ae2d8a571e03ac9c9eb76fac45af8e51", "f5d3d58503b9699de785895a96fdbaaf"},
        {"30c81c46a35ce411e5fbc1191a0a52ef", "43b1cd7f598ece23881b00e3ed030688"},
        {"f69f2445df4f9b17ad2b417be66c3710", "7b0c785e27e8ad3f8223207104725dd4"},
    };

    for (int i = 0; i < 4; i++) {
        hex_to_bytes(nist_128[i].pt, plain, 16);
        hex_to_bytes(nist_128[i].ct, cipher, 16);

        neverc_aes_encrypt_block(&ctx, result, plain);
        char buf[64];
        snprintf(buf, sizeof(buf), "NIST-128 encrypt block %d", i);
        check_true(buf, memcmp(result, cipher, 16) == 0);

        neverc_aes_decrypt_block(&ctx, result, cipher);
        snprintf(buf, sizeof(buf), "NIST-128 decrypt block %d", i);
        check_true(buf, memcmp(result, plain, 16) == 0);
    }
}

static void test_aes192(void) {
    printf("[AES-192 NIST SP 800-38A]\n");

    uint8_t key[24], plain[16], cipher[16], result[16];
    hex_to_bytes("8e73b0f7da0e6452c810f32b809079e562f8ead2522c6b7b", key, 24);

    neverc_aes_ctx_t ctx;
    check_true("init AES-192", neverc_aes_init(&ctx, key, 24) == 0);

    struct { const char *pt; const char *ct; } nist_192[] = {
        {"6bc1bee22e409f96e93d7e117393172a", "bd334f1d6e45f25ff712a214571fa5cc"},
        {"ae2d8a571e03ac9c9eb76fac45af8e51", "974104846d0ad3ad7734ecb3ecee4eef"},
        {"30c81c46a35ce411e5fbc1191a0a52ef", "ef7afd2270e2e60adce0ba2face6444e"},
        {"f69f2445df4f9b17ad2b417be66c3710", "9a4b41ba738d6c72fb16691603c18e0e"},
    };

    for (int i = 0; i < 4; i++) {
        hex_to_bytes(nist_192[i].pt, plain, 16);
        hex_to_bytes(nist_192[i].ct, cipher, 16);

        neverc_aes_encrypt_block(&ctx, result, plain);
        char buf[64];
        snprintf(buf, sizeof(buf), "NIST-192 encrypt block %d", i);
        check_true(buf, memcmp(result, cipher, 16) == 0);

        neverc_aes_decrypt_block(&ctx, result, cipher);
        snprintf(buf, sizeof(buf), "NIST-192 decrypt block %d", i);
        check_true(buf, memcmp(result, plain, 16) == 0);
    }
}

static void test_aes256(void) {
    printf("[AES-256 NIST SP 800-38A]\n");

    uint8_t key[32], plain[16], cipher[16], result[16];
    hex_to_bytes("603deb1015ca71be2b73aef0857d77811f352c073b6108d72d9810a30914dff4", key, 32);

    neverc_aes_ctx_t ctx;
    check_true("init AES-256", neverc_aes_init(&ctx, key, 32) == 0);

    struct { const char *pt; const char *ct; } nist_256[] = {
        {"6bc1bee22e409f96e93d7e117393172a", "f3eed1bdb5d2a03c064b5a7e3db181f8"},
        {"ae2d8a571e03ac9c9eb76fac45af8e51", "591ccb10d410ed26dc5ba74a31362870"},
        {"30c81c46a35ce411e5fbc1191a0a52ef", "b6ed21b99ca6f4f9f153e7b1beafed1d"},
        {"f69f2445df4f9b17ad2b417be66c3710", "23304b7a39f9f3ff067d8d8f9e24ecc7"},
    };

    for (int i = 0; i < 4; i++) {
        hex_to_bytes(nist_256[i].pt, plain, 16);
        hex_to_bytes(nist_256[i].ct, cipher, 16);

        neverc_aes_encrypt_block(&ctx, result, plain);
        char buf[64];
        snprintf(buf, sizeof(buf), "NIST-256 encrypt block %d", i);
        check_true(buf, memcmp(result, cipher, 16) == 0);

        neverc_aes_decrypt_block(&ctx, result, cipher);
        snprintf(buf, sizeof(buf), "NIST-256 decrypt block %d", i);
        check_true(buf, memcmp(result, plain, 16) == 0);
    }
}

static void test_invalid_key(void) {
    printf("[AES invalid key]\n");
    neverc_aes_ctx_t ctx;
    uint8_t key[16] = {0};
    check_true("reject 15-byte key", neverc_aes_init(&ctx, key, 15) == -1);
    check_true("reject 0-byte key",  neverc_aes_init(&ctx, key, 0) == -1);
    check_true("reject null context", neverc_aes_init(NULL, key, 16) == -1);
    check_true("reject null key", neverc_aes_init(&ctx, NULL, 16) == -1);
}

static void test_failed_reinit_wipes_key(void) {
    printf("[AES failed re-init wipes key]\n");
    neverc_aes_ctx_t ctx;
    uint8_t key[16], pt[16], ct[16], leftover[16], aa[16];
    hex_to_bytes("2b7e151628aed2a6abf7158809cf4f3c", key, 16);
    hex_to_bytes("3243f6a8885a308d313198a2e0370734", pt, 16);

    check_true("init", neverc_aes_init(&ctx, key, 16) == 0);
    neverc_aes_encrypt_block(&ctx, ct, pt);

    check_true("re-init bad length fails", neverc_aes_init(&ctx, key, 15) == -1);
    memset(leftover, 0xAA, 16);
    memset(aa, 0xAA, 16);
    neverc_aes_encrypt_block(&ctx, leftover, pt);
    check_true("encrypt after failed re-init is a no-op",
               memcmp(leftover, aa, 16) == 0);

    check_true("re-init null key fails", neverc_aes_init(&ctx, NULL, 16) == -1);
    memset(leftover, 0xAA, 16);
    neverc_aes_encrypt_block(&ctx, leftover, pt);
    check_true("encrypt after null-key re-init is a no-op",
               memcmp(leftover, aa, 16) == 0);
    check_true("failed re-init does not keep the old key",
               memcmp(leftover, ct, 16) != 0);
}

static void test_successful_reinit_wipes_unused_schedule(void) {
    printf("[AES successful re-init wipes unused schedule]\n");
    neverc_aes_ctx_t ctx;
    uint8_t key256[32], key128[16], plain[16], expected[16], out[16];
    hex_to_bytes(
        "603deb1015ca71be2b73aef0857d77811"
        "1f352c073b6108d72d9810a30914dff4",
        key256, 32);
    hex_to_bytes("2b7e151628aed2a6abf7158809cf4f3c", key128, 16);
    hex_to_bytes("6bc1bee22e409f96e93d7e117393172a", plain, 16);
    hex_to_bytes("3ad77bb40d7a3660a89ecaf32466ef97", expected, 16);

    memset(&ctx, 0xA5, sizeof(ctx));
    check_true("init AES-256 before downgrade",
               neverc_aes_init(&ctx, key256, 32) == 0);
    check_true("AES-256 uses tail of encryption schedule",
               ctx.enc_key[NEVERC_AES_MAX_RK - 1] != 0);

    check_true("re-init AES-128", neverc_aes_init(&ctx, key128, 16) == 0);
    int tail_zero = 1;
    for (int i = 44; i < NEVERC_AES_MAX_RK; i++) {
        if (ctx.enc_key[i] != 0 || ctx.dec_key[i] != 0) {
            tail_zero = 0;
            break;
        }
    }
    check_true("AES-128 unused schedules are wiped", tail_zero);
    neverc_aes_encrypt_block(&ctx, out, plain);
    check_true("AES-128 still matches NIST after re-init",
               memcmp(out, expected, sizeof(out)) == 0);
}

static void test_round_trip(void) {
    printf("[AES round-trip]\n");

    uint8_t key[32];
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)(i * 7 + 13);

    int key_sizes[] = {16, 24, 32};
    for (int k = 0; k < 3; k++) {
        neverc_aes_ctx_t ctx;
        neverc_aes_init(&ctx, key, key_sizes[k]);

        for (int t = 0; t < 16; t++) {
            uint8_t plain[16], enc[16], dec[16];
            for (int i = 0; i < 16; i++) plain[i] = (uint8_t)(t * 17 + i * 3);

            neverc_aes_encrypt_block(&ctx, enc, plain);
            neverc_aes_decrypt_block(&ctx, dec, enc);

            char buf[64];
            snprintf(buf, sizeof(buf), "AES-%d round-trip %d", key_sizes[k] * 8, t);
            check_true(buf, memcmp(dec, plain, 16) == 0);

            neverc_aes_encrypt_block(&ctx, dec, dec);
            check_true("in-place encrypt", memcmp(dec, enc, 16) == 0);
            neverc_aes_decrypt_block(&ctx, dec, dec);
            check_true("in-place decrypt", memcmp(dec, plain, 16) == 0);

            uint8_t overlap[23];
            memcpy(overlap, plain, 16);
            neverc_aes_encrypt_block(&ctx, overlap + 7, overlap);
            check_true("forward overlapping encrypt", memcmp(overlap + 7, enc, 16) == 0);
            neverc_aes_decrypt_block(&ctx, overlap, overlap + 7);
            check_true("backward overlapping decrypt", memcmp(overlap, plain, 16) == 0);
            memcpy(overlap + 7, plain, 16);
            neverc_aes_encrypt_block(&ctx, overlap, overlap + 7);
            check_true("backward overlapping encrypt", memcmp(overlap, enc, 16) == 0);
            neverc_aes_decrypt_block(&ctx, overlap + 7, overlap);
            check_true("forward overlapping decrypt", memcmp(overlap + 7, plain, 16) == 0);
        }
    }
}

int main(void) {
    printf("=== NeverC AES Tests ===\n\n");

    test_bit_transpose();
    test_substitution();
    test_key_schedule();
    test_full_key_schedule();
    test_aes128();
    test_aes192();
    test_aes256();
    test_invalid_key();
    test_failed_reinit_wipes_key();
    test_successful_reinit_wipes_unused_schedule();
    test_round_trip();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
