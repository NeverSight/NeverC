#include "neverc/std/crypto/hkdf.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

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

static void test_rfc5869_case1(void) {
    printf("[HKDF RFC 5869 Test Case 1]\n");

    uint8_t ikm[22], salt[13], info[10];
    hex_to_bytes("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b", ikm, 22);
    hex_to_bytes("000102030405060708090a0b0c", salt, 13);
    hex_to_bytes("f0f1f2f3f4f5f6f7f8f9", info, 10);

    /* Expected PRK */
    uint8_t expected_prk[32];
    hex_to_bytes("077709362c2e32df0ddc3f0dc47bba6390b6c73bb50f9c3122ec844ad7c2b3e5",
                 expected_prk, 32);

    uint8_t prk[32];
    neverc_hkdf_extract_sha256(prk, salt, 13, ikm, 22);
    check_true("TC1 extract PRK", memcmp(prk, expected_prk, 32) == 0);

    /* Expected OKM (42 bytes) */
    uint8_t expected_okm[42];
    hex_to_bytes("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf"
                 "34007208d5b887185865", expected_okm, 42);

    uint8_t okm[42];
    neverc_hkdf_expand_sha256(okm, 42, prk, info, 10);
    check_true("TC1 expand OKM", memcmp(okm, expected_okm, 42) == 0);

    /* Full HKDF */
    uint8_t okm2[42];
    neverc_hkdf_sha256(okm2, 42, ikm, 22, salt, 13, info, 10);
    check_true("TC1 full HKDF", memcmp(okm2, expected_okm, 42) == 0);
}

static void test_rfc5869_case2(void) {
    printf("[HKDF RFC 5869 Test Case 2]\n");

    uint8_t ikm[80], salt[80], info[80];
    for (int i = 0; i < 80; i++) {
        ikm[i] = (uint8_t)i;
        salt[i] = (uint8_t)(0x60 + i);
        info[i] = (uint8_t)(0xb0 + i);
    }

    uint8_t expected_prk[32];
    hex_to_bytes("06a6b88c5853361a06104c9ceb35b45cef760014904671014a193f40c15fc244",
                 expected_prk, 32);

    uint8_t prk[32];
    neverc_hkdf_extract_sha256(prk, salt, 80, ikm, 80);
    check_true("TC2 extract PRK", memcmp(prk, expected_prk, 32) == 0);

    uint8_t expected_okm[82];
    hex_to_bytes("b11e398dc80327a1c8e7f78c596a4934"
                 "4f012eda2d4efad8a050cc4c19afa97c"
                 "59045a99cac7827271cb41c65e590e09"
                 "da3275600c2f09b8367793a9aca3db71"
                 "cc30c58179ec3e87c14c01d5c1f3434f"
                 "1d87", expected_okm, 82);

    uint8_t okm[82];
    neverc_hkdf_sha256(okm, 82, ikm, 80, salt, 80, info, 80);
    check_true("TC2 full HKDF", memcmp(okm, expected_okm, 82) == 0);
}

static void test_rfc5869_case3(void) {
    printf("[HKDF RFC 5869 Test Case 3 — no salt, no info]\n");

    uint8_t ikm[22];
    hex_to_bytes("0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b", ikm, 22);

    uint8_t expected_prk[32];
    hex_to_bytes("19ef24a32c717b167f33a91d6f648bdf96596776afdb6377ac434c1c293ccb04",
                 expected_prk, 32);

    uint8_t prk[32];
    neverc_hkdf_extract_sha256(prk, NULL, 0, ikm, 22);
    check_true("TC3 extract PRK (no salt)", memcmp(prk, expected_prk, 32) == 0);

    uint8_t expected_okm[42];
    hex_to_bytes("8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d"
                 "9d201395faa4b61a96c8", expected_okm, 42);

    uint8_t okm[42];
    neverc_hkdf_sha256(okm, 42, ikm, 22, NULL, 0, NULL, 0);
    check_true("TC3 full HKDF (no salt, no info)", memcmp(okm, expected_okm, 42) == 0);

    uint8_t empty = 0;
    uint8_t zeros[32];
    memset(zeros, 0, sizeof(zeros));
    uint8_t prk_empty[32], prk_zeros[32];
    neverc_hkdf_extract_sha256(prk_empty, &empty, 0, ikm, 22);
    neverc_hkdf_extract_sha256(prk_zeros, zeros, sizeof(zeros), ikm, 22);
    check_true("NULL salt == empty salt", memcmp(prk, prk_empty, 32) == 0);
    check_true("empty salt == HashLen zeros", memcmp(prk_empty, prk_zeros, 32) == 0);

    uint8_t okm_empty[42];
    neverc_hkdf_expand_sha256(okm_empty, 42, prk, &empty, 0);
    check_true("NULL info == empty info", memcmp(okm, okm_empty, 42) == 0);
}

static void test_deterministic(void) {
    printf("[HKDF deterministic]\n");
    uint8_t key[] = "master-key";
    uint8_t salt[] = "application-salt";
    uint8_t info[] = "derived-key-1";

    uint8_t okm1[32], okm2[32];
    neverc_hkdf_sha256(okm1, 32, key, sizeof(key)-1, salt, sizeof(salt)-1, info, sizeof(info)-1);
    neverc_hkdf_sha256(okm2, 32, key, sizeof(key)-1, salt, sizeof(salt)-1, info, sizeof(info)-1);
    check_true("same inputs → same output", memcmp(okm1, okm2, 32) == 0);

    uint8_t info2[] = "derived-key-2";
    neverc_hkdf_sha256(okm2, 32, key, sizeof(key)-1, salt, sizeof(salt)-1, info2, sizeof(info2)-1);
    check_true("diff info → diff output", memcmp(okm1, okm2, 32) != 0);
}

static void test_rfc8448_tls13_early_secret(void) {
    printf("[TLS 1.3 RFC 8448 Early Secret]\n");

    /* HKDF-Extract(0s, 0s) with SHA-256 — absent PSK uses Hash.length zeros. */
    uint8_t zeros[32];
    memset(zeros, 0, sizeof(zeros));

    uint8_t expected[32];
    hex_to_bytes("33ad0a1c607ec03b09e6cd9893680ce210adf300aa1f2660e1b22e10f170f92a",
                 expected, 32);

    uint8_t early[32];
    neverc_hkdf_extract_sha256(early, NULL, 0, zeros, sizeof(zeros));
    check_true("early secret with zero IKM",
               memcmp(early, expected, 32) == 0);

    uint8_t wrong[32];
    neverc_hkdf_extract_sha256(wrong, NULL, 0, NULL, 0);
    check_true("empty IKM is not the TLS early secret",
               memcmp(wrong, expected, 32) != 0);
}

static void test_large_info_sha256_sha512(void) {
    printf("[HKDF large info]\n");

    uint8_t info[300];
    uint8_t prk256[32], prk512[64];
    for (int i = 0; i < 300; i++) info[i] = (uint8_t)(i * 7 + 3);
    for (int i = 0; i < 32; i++) prk256[i] = (uint8_t)i;
    for (int i = 0; i < 64; i++) prk512[i] = (uint8_t)i;

    uint8_t expected256[64];
    hex_to_bytes(
        "521af4a732d1d7b3b9a5113bf5f801217cbe1bfce48d3cc3ba4486ad0f900e1b"
        "470dc2d9fb367824a8cc88d2aa6ef91cac2d4a85290dfc30a0280eb46aa276cb",
        expected256, 64);
    uint8_t okm256[64];
    check_true("SHA-256 large info succeeds",
               neverc_hkdf_expand_sha256(okm256, sizeof(okm256), prk256,
                                         info, sizeof(info)) == 0);
    check_true("SHA-256 large info output",
               memcmp(okm256, expected256, sizeof(okm256)) == 0);

    uint8_t expected512[96];
    hex_to_bytes(
        "b60f0563b3d1ef761aed4d438c8b6fd772eb84443e3062159f1b4e07e1380a0a"
        "eb61706636c326aa34aff6b7abdb0231ee035a758f91583473df885b0c2501982"
        "0b2d573147e3259d8f42ce6de81dadd430fb9dc6df0082593b8e5ca9156642a",
        expected512, 96);
    uint8_t okm512[96];
    check_true("SHA-512 large info succeeds",
               neverc_hkdf_expand_sha512(okm512, sizeof(okm512), prk512,
                                         info, sizeof(info)) == 0);
    check_true("SHA-512 large info output",
               memcmp(okm512, expected512, sizeof(okm512)) == 0);
}

static void test_sha512_extract_and_full(void) {
    printf("[HKDF SHA-512 oracle]\n");

    uint8_t ikm[22], salt[13], info[10];
    memset(ikm, 0x0b, sizeof(ikm));
    for (int i = 0; i < 13; i++) salt[i] = (uint8_t)i;
    for (int i = 0; i < 10; i++) info[i] = (uint8_t)(0xf0 + i);

    uint8_t expected_prk[64];
    hex_to_bytes(
        "665799823737ded04a88e47e54a5890bb2c3d247c7a4254a8e61350723590a26c"
        "36238127d8661b88cf80ef802d57e2f7cebcf1e00e083848be19929c61b4237",
        expected_prk, 64);
    uint8_t prk[64];
    check_true("SHA-512 extract succeeds",
               neverc_hkdf_extract_sha512(prk, salt, sizeof(salt),
                                           ikm, sizeof(ikm)) == 0);
    check_true("SHA-512 extract output",
               memcmp(prk, expected_prk, sizeof(prk)) == 0);

    uint8_t expected_okm[42];
    hex_to_bytes(
        "832390086cda71fb47625bb5ceb168e4c8e26a1a16ed34d9fc7fe92c148157933"
        "8da362cb8d9f925d7cb",
        expected_okm, 42);
    uint8_t okm[42];
    check_true("SHA-512 full succeeds",
               neverc_hkdf_sha512(okm, sizeof(okm), ikm, sizeof(ikm),
                                   salt, sizeof(salt), info,
                                   sizeof(info)) == 0);
    check_true("SHA-512 full output",
               memcmp(okm, expected_okm, sizeof(okm)) == 0);
}

static void test_invalid_spans_and_lengths(void) {
    printf("[HKDF invalid spans and lengths]\n");
    uint8_t byte = 0;
    uint8_t prk256[32] = {0}, prk512[64] = {0};
    uint8_t okm[64];

    check_true("SHA-256 rejects null PRK output",
               neverc_hkdf_extract_sha256(
                   NULL, NULL, 0, NULL, 0) == -1);
    check_true("SHA-256 rejects invalid salt span",
               neverc_hkdf_extract_sha256(
                   prk256, NULL, 1, &byte, 1) == -1);
    check_true("SHA-256 rejects invalid IKM span",
               neverc_hkdf_extract_sha256(
                   prk256, &byte, 1, NULL, 1) == -1);
    check_true("SHA-256 rejects invalid info span",
               neverc_hkdf_expand_sha256(
                   okm, sizeof(okm), prk256, NULL, 1) == -1);
    check_true("SHA-256 rejects invalid output span",
               neverc_hkdf_expand_sha256(
                   NULL, 1, prk256, NULL, 0) == -1);
    check_true("SHA-256 accepts empty output span",
               neverc_hkdf_expand_sha256(
                   NULL, 0, prk256, NULL, 0) == 0);
    check_true("SHA-256 enforces RFC output limit",
               neverc_hkdf_expand_sha256(
                   okm, 255U * 32U + 1U, prk256, NULL, 0) == -1);

    check_true("SHA-512 rejects invalid salt span",
               neverc_hkdf_extract_sha512(
                   prk512, NULL, 1, &byte, 1) == -1);
    check_true("SHA-512 rejects invalid info span",
               neverc_hkdf_expand_sha512(
                   okm, sizeof(okm), prk512, NULL, 1) == -1);
    check_true("SHA-512 enforces RFC output limit",
               neverc_hkdf_expand_sha512(
                   okm, 255U * 64U + 1U, prk512, NULL, 0) == -1);

#if SIZE_MAX > (UINT64_MAX / 8)
    {
        uint8_t sentinel[32];
        uint8_t zeros[32] = {0};
        memset(sentinel, 0xa5, sizeof(sentinel));
        memcpy(prk256, sentinel, sizeof(prk256));
        check_true("SHA-256 extract rejects wrapping IKM",
                   neverc_hkdf_extract_sha256(
                       prk256, &byte, 1, &byte, SIZE_MAX) == -1);
        check_true("SHA-256 wrapping IKM wipes PRK",
                   memcmp(prk256, zeros, sizeof(prk256)) == 0);
        memcpy(prk256, sentinel, sizeof(prk256));
        check_true("SHA-256 extract rejects wrapping salt",
                   neverc_hkdf_extract_sha256(
                       prk256, &byte, SIZE_MAX, &byte, 1) == -1);
        check_true("SHA-256 wrapping salt wipes PRK",
                   memcmp(prk256, zeros, sizeof(prk256)) == 0);
        check_true("SHA-256 expand rejects wrapping info",
                   neverc_hkdf_expand_sha256(
                       okm, sizeof(okm), prk256, &byte, SIZE_MAX) == -1);
        check_true("SHA-256 full rejects wrapping IKM",
                   neverc_hkdf_sha256(
                       okm, 32, &byte, SIZE_MAX, &byte, 1, &byte, 0) == -1);
    }
#endif

#if SIZE_MAX > (UINT64_MAX - 128)
    check_true("SHA-512 extract rejects wrapping IKM",
               neverc_hkdf_extract_sha512(
                   prk512, &byte, 1, &byte, SIZE_MAX) == -1);
    check_true("SHA-512 extract rejects wrapping salt",
               neverc_hkdf_extract_sha512(
                   prk512, &byte, SIZE_MAX, &byte, 1) == -1);
    check_true("SHA-512 expand rejects wrapping info",
               neverc_hkdf_expand_sha512(
                   okm, sizeof(okm), prk512, &byte, SIZE_MAX) == -1);
    check_true("SHA-512 full rejects wrapping salt",
               neverc_hkdf_sha512(
                   okm, 64, &byte, 1, &byte, SIZE_MAX, &byte, 0) == -1);
    {
        /* UINT64_MAX-128 is the first length that makes the inner HMAC chain
         * reach SHA-512's finalized sentinel, so extract must refuse it rather
         * than hand back a PRK derived from a wiped midstate. */
        uint8_t sentinel[64];
        uint8_t zeros[64] = {0};
        memset(sentinel, 0xa5, sizeof(sentinel));
        memcpy(prk512, sentinel, sizeof(prk512));
        check_true("SHA-512 extract rejects sentinel IKM",
                   neverc_hkdf_extract_sha512(
                       prk512, &byte, 1, &byte,
                       (size_t)(UINT64_MAX - 128)) == -1);
        check_true("SHA-512 sentinel IKM wipes PRK",
                   memcmp(prk512, zeros, sizeof(prk512)) == 0);
        check_true("SHA-512 full rejects sentinel IKM",
                   neverc_hkdf_sha512(
                       okm, 64, &byte, (size_t)(UINT64_MAX - 128),
                       &byte, 1, &byte, 0) == -1);
    }
#endif
}

static void test_expand_info_overlap(void) {
    printf("[HKDF overlapping info/output]\n");
    uint8_t prk256[32], prk512[64];
    uint8_t info[16];
    for (int i = 0; i < 16; i++) info[i] = (uint8_t)(0xa0 + i);
    for (int i = 0; i < 32; i++) prk256[i] = (uint8_t)i;
    for (int i = 0; i < 64; i++) prk512[i] = (uint8_t)(0x40 + i);

    uint8_t expected256[64], aliased256[64] = {0};
    memcpy(aliased256, info, sizeof(info));
    check_true("SHA-256 disjoint overlap oracle succeeds",
               neverc_hkdf_expand_sha256(expected256, sizeof(expected256),
                                         prk256, info, sizeof(info)) == 0);
    check_true("SHA-256 overlapping info succeeds",
               neverc_hkdf_expand_sha256(aliased256, sizeof(aliased256),
                                         prk256, aliased256,
                                         sizeof(info)) == 0);
    check_true("SHA-256 overlapping info matches disjoint",
               memcmp(aliased256, expected256, sizeof(expected256)) == 0);

    uint8_t expected512[128], aliased512[128] = {0};
    memcpy(aliased512, info, sizeof(info));
    check_true("SHA-512 disjoint overlap oracle succeeds",
               neverc_hkdf_expand_sha512(expected512, sizeof(expected512),
                                         prk512, info, sizeof(info)) == 0);
    check_true("SHA-512 overlapping info succeeds",
               neverc_hkdf_expand_sha512(aliased512, sizeof(aliased512),
                                         prk512, aliased512,
                                         sizeof(info)) == 0);
    check_true("SHA-512 overlapping info matches disjoint",
               memcmp(aliased512, expected512, sizeof(expected512)) == 0);
}

int main(void) {
    printf("=== NeverC HKDF Tests ===\n\n");
    test_rfc5869_case1();
    test_rfc5869_case2();
    test_rfc5869_case3();
    test_deterministic();
    test_rfc8448_tls13_early_secret();
    test_large_info_sha256_sha512();
    test_sha512_extract_and_full();
    test_invalid_spans_and_lengths();
    test_expand_info_overlap();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
