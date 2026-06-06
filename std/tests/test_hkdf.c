#include "neverc/hkdf.h"
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

int main(void) {
    printf("=== NeverC HKDF Tests ===\n\n");
    test_rfc5869_case1();
    test_rfc5869_case2();
    test_rfc5869_case3();
    test_deterministic();
    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
