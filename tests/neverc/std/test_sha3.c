/*
 * SHA-3 (Keccak) test suite — NIST FIPS 202 official test vectors.
 *
 * Test vectors sourced from:
 *   - NIST FIPS 202 examples (Appendix)
 *   - NIST CSRC SHA-3 test vectors
 *   - Go crypto/sha3 golden tests
 */
#include "neverc/std/crypto/sha3.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void hex_to_bytes(const char *hex, uint8_t *out, int len) {
    for (int i = 0; i < len; i++) {
        unsigned int b;
        sscanf(hex + 2*i, "%2x", &b);
        out[i] = (uint8_t)b;
    }
}

static void check_digest(const char *name, const uint8_t *got,
                         const char *expected_hex, int len) {
    uint8_t expected[64];
    hex_to_bytes(expected_hex, expected, len);
    tests_run++;
    if (memcmp(got, expected, (size_t)len) == 0) { tests_passed++; return; }
    tests_failed++;
    printf("  FAIL: %s\n    got: ", name);
    for (int i = 0; i < len; i++) printf("%02x", got[i]);
    printf("\n    exp: %s\n", expected_hex);
}

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

/* ===== SHA3-256 Tests ===== */

static void test_sha3_256(void) {
    printf("[SHA3-256]\n");
    uint8_t d[32];

    /* NIST: SHA3-256("") */
    neverc_sha3_256_sum((const uint8_t *)"", 0, d);
    check_digest("SHA3-256(\"\")", d,
        "a7ffc6f8bf1ed76651c14756a061d662f580ff4de43b49fa82d80a4b80f8434a", 32);

    /* NIST: SHA3-256("abc") */
    neverc_sha3_256_sum((const uint8_t *)"abc", 3, d);
    check_digest("SHA3-256(\"abc\")", d,
        "3a985da74fe225b2045c172d6bd390bd855f086e3e9d525b46bfe24511431532", 32);

    /* NIST: SHA3-256(448-bit message) */
    neverc_sha3_256_sum(
        (const uint8_t *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        56, d);
    check_digest("SHA3-256(448-bit)", d,
        "41c0dba2a9d6240849100376a8235e2c82e1b9998a999e21db32dd97496d3376", 32);

    /* NIST: SHA3-256(896-bit message) */
    neverc_sha3_256_sum(
        (const uint8_t *)"abcdefghbcdefghicdefghijdefghijkefghijklfghijklmghijklmn"
        "hijklmnoijklmnopjklmnopqklmnopqrlmnopqrsmnopqrstnopqrstu",
        112, d);
    check_digest("SHA3-256(896-bit)", d,
        "916f6061fe879741ca6469b43971dfdb28b1a32dc36cb3254e812be27aad1d18", 32);

    /* Incremental update consistency */
    {
        neverc_sha3_ctx ctx;
        neverc_sha3_256_init(&ctx);
        neverc_sha3_256_update(&ctx, (const uint8_t *)"a", 1);
        neverc_sha3_256_update(&ctx, (const uint8_t *)"bc", 2);
        uint8_t d2[32];
        neverc_sha3_256_final(&ctx, d2);

        neverc_sha3_256_sum((const uint8_t *)"abc", 3, d);
        check_true("SHA3-256 incremental == one-shot", memcmp(d, d2, 32) == 0);
    }

    /* Byte-by-byte update */
    {
        const char *msg = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
        neverc_sha3_ctx ctx;
        neverc_sha3_256_init(&ctx);
        for (size_t i = 0; i < 56; i++)
            neverc_sha3_256_update(&ctx, (const uint8_t *)msg + i, 1);
        uint8_t d2[32];
        neverc_sha3_256_final(&ctx, d2);

        neverc_sha3_256_sum((const uint8_t *)msg, 56, d);
        check_true("SHA3-256 byte-by-byte", memcmp(d, d2, 32) == 0);
    }
}

/* ===== SHA3-224 Tests ===== */

static void test_sha3_224(void) {
    printf("[SHA3-224]\n");
    uint8_t d[28];

    neverc_sha3_224_sum((const uint8_t *)"", 0, d);
    check_digest("SHA3-224(\"\")", d,
        "6b4e03423667dbb73b6e15454f0eb1abd4597f9a1b078e3f5b5a6bc7", 28);

    neverc_sha3_224_sum((const uint8_t *)"abc", 3, d);
    check_digest("SHA3-224(\"abc\")", d,
        "e642824c3f8cf24ad09234ee7d3c766fc9a3a5168d0c94ad73b46fdf", 28);

    neverc_sha3_224_sum(
        (const uint8_t *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        56, d);
    check_digest("SHA3-224(448-bit)", d,
        "8a24108b154ada21c9fd5574494479ba5c7e7ab76ef264ead0fcce33", 28);

    /* rate-1 leftover: suffix and the final pad bit share the last byte. */
    {
        uint8_t data[143];
        memset(data, 'A', sizeof(data));
        neverc_sha3_224_sum(data, 143, d);
        check_digest("SHA3-224(143 'A') same-byte pad", d,
            "dfe44c31a9ad9daa2f6556392ea339a7c33f1c5db31d7535cf46617a", 28);
    }

    {
        neverc_sha3_ctx ctx;
        neverc_sha3_224_init(&ctx);
        neverc_sha3_224_update(&ctx, (const uint8_t *)"a", 1);
        neverc_sha3_224_update(&ctx, (const uint8_t *)"bc", 2);
        uint8_t d2[28];
        neverc_sha3_224_final(&ctx, d2);
        neverc_sha3_224_sum((const uint8_t *)"abc", 3, d);
        check_true("SHA3-224 incremental", memcmp(d, d2, 28) == 0);
    }
}

/* ===== SHA3-384 Tests ===== */

static void test_sha3_384(void) {
    printf("[SHA3-384]\n");
    uint8_t d[48];

    neverc_sha3_384_sum((const uint8_t *)"", 0, d);
    check_digest("SHA3-384(\"\")", d,
        "0c63a75b845e4f7d01107d852e4c2485c51a50aaaa94fc61995e71bbee983a2a"
        "c3713831264adb47fb6bd1e058d5f004", 48);

    neverc_sha3_384_sum((const uint8_t *)"abc", 3, d);
    check_digest("SHA3-384(\"abc\")", d,
        "ec01498288516fc926459f58e2c6ad8df9b473cb0fc08c2596da7cf0e49be4b2"
        "98d88cea927ac7f539f1edf228376d25", 48);

    neverc_sha3_384_sum(
        (const uint8_t *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        56, d);
    check_digest("SHA3-384(448-bit)", d,
        "991c665755eb3a4b6bbdfb75c78a492e8c56a22c5c4d7e429bfdbc32b9d4ad5a"
        "a04a1f076e62fea19eef51acd0657c22", 48);

    {
        uint8_t data[103];
        memset(data, 'A', sizeof(data));
        neverc_sha3_384_sum(data, 103, d);
        check_digest("SHA3-384(103 'A') same-byte pad", d,
            "92113006f00608fb715f594bb342743bf45aa27683d03b5f88ef94b033c86f08"
            "9936d0244a96eaff36d00a01c3c82646", 48);
    }

    {
        neverc_sha3_ctx ctx;
        neverc_sha3_384_init(&ctx);
        neverc_sha3_384_update(&ctx, (const uint8_t *)"a", 1);
        neverc_sha3_384_update(&ctx, (const uint8_t *)"bc", 2);
        uint8_t d2[48];
        neverc_sha3_384_final(&ctx, d2);
        neverc_sha3_384_sum((const uint8_t *)"abc", 3, d);
        check_true("SHA3-384 incremental", memcmp(d, d2, 48) == 0);
    }
}

/* ===== SHA3-512 Tests ===== */

static void test_sha3_512(void) {
    printf("[SHA3-512]\n");
    uint8_t d[64];

    neverc_sha3_512_sum((const uint8_t *)"", 0, d);
    check_digest("SHA3-512(\"\")", d,
        "a69f73cca23a9ac5c8b567dc185a756e97c982164fe25859e0d1dcc1475c80a6"
        "15b2123af1f5f94c11e3e9402c3ac558f500199d95b6d3e301758586281dcd26", 64);

    neverc_sha3_512_sum((const uint8_t *)"abc", 3, d);
    check_digest("SHA3-512(\"abc\")", d,
        "b751850b1a57168a5693cd924b6b096e08f621827444f70d884f5d0240d2712e"
        "10e116e9192af3c91a7ec57647e3934057340b4cf408d5a56592f8274eec53f0", 64);

    neverc_sha3_512_sum(
        (const uint8_t *)"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
        56, d);
    check_digest("SHA3-512(448-bit)", d,
        "04a371e84ecfb5b8b77cb48610fca8182dd457ce6f326a0fd3d7ec2f1e91636d"
        "ee691fbe0c985302ba1b0d8dc78c086346b533b49c030d99a27daf1139d6e75e", 64);

    /* Incremental consistency */
    {
        neverc_sha3_ctx ctx;
        neverc_sha3_512_init(&ctx);
        neverc_sha3_512_update(&ctx, (const uint8_t *)"abc", 3);
        uint8_t d2[64];
        neverc_sha3_512_final(&ctx, d2);
        neverc_sha3_512_sum((const uint8_t *)"abc", 3, d);
        check_true("SHA3-512 incremental", memcmp(d, d2, 64) == 0);
    }
}

/* ===== SHAKE128 Tests ===== */

static void test_shake128(void) {
    printf("[SHAKE128]\n");
    uint8_t out[64];

    /* NIST: SHAKE128("", 256 bits) */
    {
        neverc_sha3_ctx ctx;
        neverc_shake128_init(&ctx);
        neverc_shake128_update(&ctx, (const uint8_t *)"", 0);
        neverc_shake128_squeeze(&ctx, out, 32);
        check_digest("SHAKE128(\"\",32)", out,
            "7f9c2ba4e88f827d616045507605853ed73b8093f6efbc88eb1a6eacfa66ef26", 32);
    }

    /* NIST: SHAKE128("abc", 256 bits) */
    {
        neverc_sha3_ctx ctx;
        neverc_shake128_init(&ctx);
        neverc_shake128_update(&ctx, (const uint8_t *)"abc", 3);
        neverc_shake128_squeeze(&ctx, out, 32);
        check_digest("SHAKE128(\"abc\",32)", out,
            "5881092dd818bf5cf8a3ddb793fbcba74097d5c526a6d35f97b83351940f2cc8", 32);
    }

    /* Extendable output: 64 bytes */
    {
        neverc_sha3_ctx ctx;
        neverc_shake128_init(&ctx);
        neverc_shake128_update(&ctx, (const uint8_t *)"abc", 3);
        neverc_shake128_squeeze(&ctx, out, 64);
        /* First 32 bytes should match the 32-byte output */
        uint8_t expected_32[32];
        hex_to_bytes("5881092dd818bf5cf8a3ddb793fbcba74097d5c526a6d35f97b83351940f2cc8",
                     expected_32, 32);
        check_true("SHAKE128 first 32 of 64 match", memcmp(out, expected_32, 32) == 0);
    }

    /* Incremental squeezing must be byte-for-byte identical across a rate
     * boundary, including one-byte calls used by rejection samplers. */
    {
        uint8_t one_shot[200], incremental[200];
        neverc_sha3_ctx ctx;
        neverc_shake128_init(&ctx);
        neverc_shake128_update(&ctx, (const uint8_t *)"abc", 3);
        neverc_shake128_squeeze(&ctx, one_shot, sizeof(one_shot));

        neverc_shake128_init(&ctx);
        neverc_shake128_update(&ctx, (const uint8_t *)"abc", 3);
        for (size_t i = 0; i < sizeof(incremental); i++)
            neverc_shake128_squeeze(&ctx, incremental + i, 1);
        check_true("SHAKE128 incremental squeeze",
                   memcmp(one_shot, incremental, sizeof(one_shot)) == 0);
    }
}

/* ===== SHAKE256 Tests ===== */

static void test_shake256(void) {
    printf("[SHAKE256]\n");
    uint8_t out[64];

    /* NIST: SHAKE256("", 512 bits) */
    {
        neverc_sha3_ctx ctx;
        neverc_shake256_init(&ctx);
        neverc_shake256_update(&ctx, (const uint8_t *)"", 0);
        neverc_shake256_squeeze(&ctx, out, 64);
        check_digest("SHAKE256(\"\",64)", out,
            "46b9dd2b0ba88d13233b3feb743eeb243fcd52ea62b81b82b50c27646ed5762f"
            "d75dc4ddd8c0f200cb05019d67b592f6fc821c49479ab48640292eacb3b7c4be", 64);
    }

    /* NIST: SHAKE256("abc", 512 bits) */
    {
        neverc_sha3_ctx ctx;
        neverc_shake256_init(&ctx);
        neverc_shake256_update(&ctx, (const uint8_t *)"abc", 3);
        neverc_shake256_squeeze(&ctx, out, 64);
        check_digest("SHAKE256(\"abc\",64)", out,
            "483366601360a8771c6863080cc4114d8db44530f8f1e1ee4f94ea37e78b5739"
            "d5a15bef186a5386c75744c0527e1faa9f8726e462a12a4feb06bd8801e751e4", 64);
    }

    {
        uint8_t one_shot[300], incremental[300];
        neverc_sha3_ctx ctx;
        neverc_shake256_init(&ctx);
        neverc_shake256_update(&ctx, (const uint8_t *)"abc", 3);
        neverc_shake256_squeeze(&ctx, one_shot, sizeof(one_shot));

        neverc_shake256_init(&ctx);
        neverc_shake256_update(&ctx, (const uint8_t *)"abc", 3);
        neverc_shake256_squeeze(&ctx, incremental, 17);
        neverc_shake256_squeeze(
            &ctx, incremental + 17, sizeof(incremental) - 17);
        check_true("SHAKE256 incremental squeeze",
                   memcmp(one_shot, incremental, sizeof(one_shot)) == 0);
    }
}

/* ===== SHA3-256 1M 'a' stress test ===== */

static void test_sha3_256_1m(void) {
    printf("[SHA3-256 1M stress]\n");

    neverc_sha3_ctx ctx;
    neverc_sha3_256_init(&ctx);
    uint8_t block[1000];
    memset(block, 'a', 1000);
    for (int i = 0; i < 1000; i++)
        neverc_sha3_256_update(&ctx, block, 1000);
    uint8_t d[32];
    neverc_sha3_256_final(&ctx, d);

    /* NIST: SHA3-256(1000000 x 'a') */
    check_digest("SHA3-256(1M 'a')", d,
        "5c8875ae474a3634ba4fd55ec85bffd661f32aca75c6d699d0cdcb6c115891c1", 32);
}

/* ===== SHA3 vs SHA2 distinctness ===== */

static void test_sha3_vs_sha2(void) {
    printf("[SHA3 vs SHA2 distinctness]\n");
    uint8_t sha3_d[32];
    neverc_sha3_256_sum((const uint8_t *)"abc", 3, sha3_d);

    /* SHA3-256("abc") must differ from SHA-256("abc")
       SHA-256("abc") = ba7816bf... */
    uint8_t sha2_expected[4];
    hex_to_bytes("ba7816bf", sha2_expected, 4);
    check_true("SHA3-256 != SHA-256", memcmp(sha3_d, sha2_expected, 4) != 0);
}

static void test_sha3_domain_separation(void) {
    printf("[SHA3 domain separation]\n");
    uint8_t sha3[32], shake[32];

    neverc_sha3_256_sum((const uint8_t *)"", 0, sha3);
    {
        neverc_sha3_ctx ctx;
        neverc_shake256_init(&ctx);
        neverc_shake256_update(&ctx, (const uint8_t *)"", 0);
        neverc_shake256_squeeze(&ctx, shake, 32);
    }
    check_true("SHA3-256(\"\") != SHAKE256(\"\",32)", memcmp(sha3, shake, 32) != 0);

    /* rate-1 leftover: suffix and the final pad bit share the last byte. */
    uint8_t data[167];
    memset(data, 'A', sizeof(data));
    neverc_sha3_256_sum(data, 135, sha3);
    check_digest("SHA3-256(135 'A') same-byte pad", sha3,
        "fb26b42ffe085f98796486a734c0639a8935a7845268ac2be76b6655d86d1d65", 32);

    {
        uint8_t shake128[32];
        neverc_sha3_ctx xof;
        neverc_shake128_init(&xof);
        neverc_shake128_update(&xof, data, 167); /* rate-1 leftover for SHAKE128 */
        neverc_shake128_squeeze(&xof, shake128, 32);
        check_digest("SHAKE128(167 'A',32) same-byte pad", shake128,
            "c9397bec1bf0c5cd659e62b20ea71524f539555f50ea8f9465b0fab401d9bce6", 32);
    }

    {
        uint8_t d512[64];
        neverc_sha3_512_sum(data, 71, d512); /* rate-1 leftover for SHA3-512 */
        check_digest("SHA3-512(71 'A') same-byte pad", d512,
            "e9e7f1016227a4d58c3a2c597adc2f58de10b6e78f17ff079624fede5eb8341b"
            "f0ebeda4f8296d5a070751ab3b7ffa48d35950f793e21f9c16c095b3b354da5e", 64);
    }

    {
        neverc_sha3_ctx ctx;
        neverc_shake256_init(&ctx);
        neverc_shake256_update(&ctx, data, 135);
        neverc_shake256_squeeze(&ctx, shake, 32);
    }
    check_digest("SHAKE256(135 'A',32)", shake,
        "a9fa2cdfbc5f74e049c2111143e35c69152266c4d66a56fa5164ea640c34b5f2", 32);
    check_true("SHA3-256(135 'A') != SHAKE256(135 'A',32)",
               memcmp(sha3, shake, 32) != 0);
}

/* ===== Multi-block boundary test ===== */

static void test_block_boundaries(void) {
    printf("[block boundary]\n");

    /* SHA3-256 rate = 136 bytes. Test inputs at boundary: 135, 136, 137 bytes */
    uint8_t data[200];
    memset(data, 'A', sizeof(data));

    uint8_t d_one[32], d_inc[32];
    int sizes[] = {135, 136, 137, 200};
    for (int k = 0; k < 4; k++) {
        int sz = sizes[k];
        neverc_sha3_256_sum(data, (size_t)sz, d_one);

        neverc_sha3_ctx ctx;
        neverc_sha3_256_init(&ctx);
        for (int i = 0; i < sz; i++)
            neverc_sha3_256_update(&ctx, data + i, 1);
        neverc_sha3_256_final(&ctx, d_inc);

        char name[64];
        snprintf(name, sizeof(name), "SHA3-256 boundary %d", sz);
        check_true(name, memcmp(d_one, d_inc, 32) == 0);
    }
}

static void test_sha3_lifecycle(void) {
    printf("[SHA3 lifecycle]\n");
    neverc_sha3_ctx ctx;
    uint8_t d1[32], d2[32], shake[32];

    neverc_sha3_256_init(&ctx);
    neverc_sha3_256_update(&ctx, (const uint8_t *)"abc", 3);
    neverc_sha3_256_final(&ctx, d1);
    neverc_sha3_256_update(&ctx, (const uint8_t *)"x", 1);
    neverc_sha3_256_final(&ctx, d2);
    check_true("SHA3-256 update after final ignored",
               memcmp(d1, d2, 32) == 0);

    neverc_sha3_256_init(&ctx);
    neverc_sha3_256_update(&ctx, (const uint8_t *)"abc", 3);
    neverc_sha3_256_update(&ctx, NULL, 5);
    neverc_sha3_256_final(&ctx, d2);
    check_true("SHA3-256 invalid data span ignored",
               memcmp(d1, d2, 32) == 0);

    neverc_sha3_256_init(&ctx);
    neverc_sha3_256_update(&ctx, (const uint8_t *)"abc", 3);
    neverc_sha3_256_final(&ctx, NULL);
    neverc_sha3_256_final(&ctx, d2);
    check_true("SHA3-256 NULL final is a no-op",
               memcmp(d1, d2, 32) == 0);

    neverc_sha3_256_init(NULL);
    neverc_sha3_256_update(NULL, (const uint8_t *)"abc", 3);
    memset(d2, 0xa5, sizeof(d2));
    neverc_sha3_256_final(NULL, d2);
    {
        uint8_t zeros[32] = {0};
        check_true("SHA3-256 NULL ctx final fails closed",
                   memcmp(d2, zeros, 32) == 0);
    }

    {
        uint8_t zeros[32] = {0};
        memset(shake, 0xa5, sizeof(shake));
        neverc_shake128_squeeze(NULL, shake, 32);
        check_true("SHAKE128 NULL ctx squeeze fails closed",
                   memcmp(shake, zeros, 32) == 0);
    }

    {
        uint8_t zeros[32] = {0};
        memset(d2, 0xa5, sizeof(d2));
        neverc_sha3_256_sum(NULL, 5, d2);
        check_true("SHA3-256 sum(NULL, n) fails closed",
                   memcmp(d2, zeros, 32) == 0);
        neverc_sha3_256_sum((const uint8_t *)"", 0, d1);
        check_true("SHA3-256 sum(NULL, n) != empty hash",
                   memcmp(d2, d1, 32) != 0);
    }

    /* Zeroed ctx: rate==0. Must not hang or memset SIZE_MAX. */
    {
        neverc_sha3_ctx raw;
        uint8_t zeros[32] = {0};
        memset(&raw, 0, sizeof(raw));
        memset(d2, 0xa5, sizeof(d2));
        neverc_sha3_256_update(&raw, (const uint8_t *)"abc", 3);
        neverc_sha3_256_final(&raw, d2);
        check_true("SHA3-256 zeroed ctx fails closed",
                   memcmp(d2, zeros, 32) == 0);
    }
    {
        neverc_sha3_ctx raw;
        uint8_t zeros[32] = {0};
        memset(&raw, 0, sizeof(raw));
        memset(shake, 0xa5, sizeof(shake));
        neverc_shake128_update(&raw, (const uint8_t *)"abc", 3);
        neverc_shake128_squeeze(&raw, shake, 32);
        check_true("SHAKE128 zeroed ctx fails closed",
                   memcmp(shake, zeros, 32) == 0);
    }

    {
        uint8_t expected[64];
        neverc_shake128_init(&ctx);
        neverc_shake128_update(&ctx, (const uint8_t *)"abc", 3);
        neverc_shake128_squeeze(&ctx, expected, 64);

        neverc_shake128_init(&ctx);
        neverc_shake128_update(&ctx, (const uint8_t *)"abc", 3);
        neverc_shake128_squeeze(&ctx, shake, 32);
        neverc_shake128_update(&ctx, (const uint8_t *)"oops", 4);
        {
            uint8_t after[32];
            neverc_shake128_squeeze(&ctx, after, 32);
            check_true("SHAKE128 update after squeeze ignored",
                       memcmp(shake, expected, 32) == 0 &&
                       memcmp(after, expected + 32, 32) == 0);
        }
    }

    {
        neverc_sha3_ctx mix;
        uint8_t zeros[32] = {0};
        uint8_t shake[32], digest[32];
        neverc_shake128_init(&mix);
        neverc_shake128_update(&mix, (const uint8_t *)"abc", 3);
        neverc_shake128_squeeze(&mix, shake, 32);
        memset(digest, 0xa5, sizeof(digest));
        neverc_sha3_256_final(&mix, digest);
        check_true("SHA3-256 final after SHAKE fails closed",
                   memcmp(digest, zeros, 32) == 0);

        neverc_sha3_256_init(&mix);
        neverc_sha3_256_update(&mix, (const uint8_t *)"abc", 3);
        neverc_sha3_256_final(&mix, digest);
        memset(shake, 0xa5, sizeof(shake));
        neverc_shake128_squeeze(&mix, shake, 32);
        check_true("SHAKE squeeze after SHA3-256 final fails closed",
                   memcmp(shake, zeros, 32) == 0);
    }

    {
        neverc_sha3_ctx raw;
        uint8_t zeros[32] = {0};
        neverc_sha3_256_init(&raw);
        neverc_sha3_256_update(&raw, (const uint8_t *)"abc", 3);
        raw.rate = 135; /* not a lane multiple; would drop the pad bit */
        raw.buf_len = 3;
        memset(d2, 0xa5, sizeof(d2));
        neverc_sha3_256_final(&raw, d2);
        check_true("SHA3-256 non-lane rate fails closed",
                   memcmp(d2, zeros, 32) == 0);
    }

    {
        neverc_sha3_ctx raw;
        uint8_t zeros[32] = {0};
        neverc_shake128_init(&raw);
        neverc_shake128_update(&raw, (const uint8_t *)"abc", 3);
        neverc_shake128_squeeze(&raw, shake, 1);
        raw.buf_len = raw.rate + 1;
        memset(shake, 0xa5, sizeof(shake));
        neverc_shake128_squeeze(&raw, shake, 32);
        check_true("SHAKE squeeze cursor past rate fails closed",
                   memcmp(shake, zeros, 32) == 0);
    }

    /* SHAKE ctx finalized as SHA-3 before any squeeze used to emit SHAKE output. */
    {
        neverc_sha3_ctx mix;
        uint8_t zeros[32] = {0};
        uint8_t digest[32];
        neverc_shake128_init(&mix);
        neverc_shake128_update(&mix, (const uint8_t *)"abc", 3);
        memset(digest, 0xa5, sizeof(digest));
        neverc_sha3_256_final(&mix, digest);
        check_true("SHA3-256 final on unsqueezed SHAKE fails closed",
                   memcmp(digest, zeros, 32) == 0);
    }

    /* SHA3-256 and SHAKE256 share rate 136; suffix is the only separator. */
    {
        neverc_sha3_ctx mix;
        uint8_t zeros[32] = {0};
        uint8_t out[32];
        neverc_sha3_256_init(&mix);
        neverc_sha3_256_update(&mix, (const uint8_t *)"abc", 3);
        memset(out, 0xa5, sizeof(out));
        neverc_shake256_squeeze(&mix, out, 32);
        check_true("SHAKE256 squeeze on SHA3-256 ctx fails closed",
                   memcmp(out, zeros, 32) == 0);
    }

    {
        neverc_sha3_ctx mix;
        uint8_t zeros[28] = {0};
        uint8_t d224[28];
        neverc_sha3_256_init(&mix);
        neverc_sha3_256_update(&mix, (const uint8_t *)"abc", 3);
        memset(d224, 0xa5, sizeof(d224));
        neverc_sha3_224_final(&mix, d224);
        check_true("SHA3-224 final on SHA3-256 ctx fails closed",
                   memcmp(d224, zeros, 28) == 0);
    }

    {
        neverc_sha3_ctx ctx, clone;
        uint8_t d1[32], d2[32];
        neverc_sha3_256_init(&ctx);
        neverc_sha3_256_update(&ctx, (const uint8_t *)"ab", 2);
        clone = ctx;
        neverc_sha3_256_update(&ctx, (const uint8_t *)"c", 1);
        neverc_sha3_256_update(&clone, (const uint8_t *)"c", 1);
        neverc_sha3_256_final(&ctx, d1);
        neverc_sha3_256_final(&clone, d2);
        neverc_sha3_256_sum((const uint8_t *)"abc", 3, shake);
        check_true("SHA3-256 clone after update",
                   memcmp(d1, d2, 32) == 0 && memcmp(d1, shake, 32) == 0);
    }

    {
        uint8_t one_shot[64], from_clone[32];
        neverc_sha3_ctx ctx, clone;
        neverc_shake128_init(&ctx);
        neverc_shake128_update(&ctx, (const uint8_t *)"abc", 3);
        neverc_shake128_squeeze(&ctx, one_shot, 64);
        neverc_shake128_init(&ctx);
        neverc_shake128_update(&ctx, (const uint8_t *)"abc", 3);
        neverc_shake128_squeeze(&ctx, shake, 32);
        clone = ctx;
        neverc_shake128_squeeze(&clone, from_clone, 32);
        check_true("SHAKE128 clone after squeeze",
                   memcmp(from_clone, one_shot + 32, 32) == 0);
    }

    {
        neverc_sha3_ctx ctx;
        uint8_t empty[32], after[32];
        neverc_sha3_256_sum((const uint8_t *)"", 0, empty);
        neverc_sha3_256_init(&ctx);
        neverc_sha3_256_update(&ctx, (const uint8_t *)"abc", 3);
        neverc_sha3_256_init(&ctx);
        neverc_sha3_256_final(&ctx, after);
        check_true("SHA3-256 re-init resets to empty hash",
                   memcmp(empty, after, 32) == 0);
    }
}

int main(void) {
    printf("=== NeverC SHA-3 (Keccak) Tests ===\n\n");
    test_sha3_256();
    test_sha3_224();
    test_sha3_384();
    test_sha3_512();
    test_shake128();
    test_shake256();
    test_sha3_256_1m();
    test_sha3_vs_sha2();
    test_sha3_domain_separation();
    test_block_boundaries();
    test_sha3_lifecycle();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
