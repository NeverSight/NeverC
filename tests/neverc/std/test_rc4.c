#include "neverc/std/crypto/rc4.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_bytes(const char *name, const uint8_t *got, const uint8_t *expected, size_t len) {
    tests_run++;
    if (memcmp(got, expected, len) == 0) { tests_passed++; }
    else {
        tests_failed++;
        printf("  FAIL: %s: got [", name);
        for (size_t i = 0; i < len && i < 16; i++) printf("%02x ", got[i]);
        printf("], expected [");
        for (size_t i = 0; i < len && i < 16; i++) printf("%02x ", expected[i]);
        printf("]\n");
    }
}

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

/*
 * Wikipedia RC4 test vectors, verified against Go crypto/rc4 output.
 *
 * Key: "Key"    Plaintext: "Plaintext"
 * Key: "Wiki"   Plaintext: "pedia"
 * Key: "Secret" Plaintext: "Attack at dawn"
 */
static void test_known_vectors(void) {
    printf("[rc4 known vectors]\n");

    {
        const uint8_t key[] = "Key";
        const uint8_t pt[] = "Plaintext";
        const uint8_t expected[] = {0xBB, 0xF3, 0x16, 0xE8, 0xD9, 0x40, 0xAF, 0x0A, 0xD3};

        neverc_rc4_cipher_t c;
        check_int("init Key", neverc_rc4_init(&c, key, 3), 0);
        uint8_t ct[9];
        neverc_rc4_xor_keystream(&c, ct, pt, 9);
        check_bytes("Key/Plaintext", ct, expected, 9);
    }

    {
        const uint8_t key[] = "Wiki";
        const uint8_t pt[] = "pedia";
        const uint8_t expected[] = {0x10, 0x21, 0xBF, 0x04, 0x20};

        neverc_rc4_cipher_t c;
        check_int("init Wiki", neverc_rc4_init(&c, key, 4), 0);
        uint8_t ct[5];
        neverc_rc4_xor_keystream(&c, ct, pt, 5);
        check_bytes("Wiki/pedia", ct, expected, 5);
    }

    {
        const uint8_t key[] = "Secret";
        const uint8_t pt[] = "Attack at dawn";
        const uint8_t expected[] = {0x45, 0xA0, 0x1F, 0x64, 0x5F, 0xC3, 0x5B, 0x38,
                                     0x35, 0x52, 0x54, 0x4B, 0x9B, 0xF5};

        neverc_rc4_cipher_t c;
        check_int("init Secret", neverc_rc4_init(&c, key, 6), 0);
        uint8_t ct[14];
        neverc_rc4_xor_keystream(&c, ct, pt, 14);
        check_bytes("Secret/Attack", ct, expected, 14);
    }
}

static void test_encrypt_decrypt_roundtrip(void) {
    printf("[rc4 encrypt/decrypt roundtrip]\n");

    const uint8_t key[] = "test key for roundtrip";
    const uint8_t plaintext[] = "Hello, World! This is a roundtrip test for RC4 cipher.";
    size_t len = sizeof(plaintext) - 1;

    uint8_t ciphertext[128], decrypted[128];
    neverc_rc4_cipher_t enc, dec;

    neverc_rc4_init(&enc, key, sizeof(key) - 1);
    neverc_rc4_xor_keystream(&enc, ciphertext, plaintext, len);

    check_true("ciphertext differs from plaintext",
               memcmp(ciphertext, plaintext, len) != 0);

    neverc_rc4_init(&dec, key, sizeof(key) - 1);
    neverc_rc4_xor_keystream(&dec, decrypted, ciphertext, len);

    check_bytes("decrypt matches plaintext", decrypted, plaintext, len);
}

static void test_streaming(void) {
    printf("[rc4 streaming]\n");

    const uint8_t key[] = "streaming key";
    const uint8_t plaintext[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    size_t len = 26;

    uint8_t ct_one_shot[26], ct_streaming[26];

    neverc_rc4_cipher_t c1;
    neverc_rc4_init(&c1, key, sizeof(key) - 1);
    neverc_rc4_xor_keystream(&c1, ct_one_shot, plaintext, len);

    neverc_rc4_cipher_t c2;
    neverc_rc4_init(&c2, key, sizeof(key) - 1);
    neverc_rc4_xor_keystream(&c2, ct_streaming, plaintext, 10);
    neverc_rc4_xor_keystream(&c2, ct_streaming + 10, plaintext + 10, 16);

    check_bytes("streaming matches one-shot", ct_streaming, ct_one_shot, len);
}

static void test_byte_at_a_time(void) {
    printf("[rc4 byte-at-a-time]\n");

    const uint8_t key[] = "byte key";
    const uint8_t plaintext[] = "Hello!";
    size_t len = 6;

    uint8_t ct_batch[6], ct_single[6];

    neverc_rc4_cipher_t c1;
    neverc_rc4_init(&c1, key, sizeof(key) - 1);
    neverc_rc4_xor_keystream(&c1, ct_batch, plaintext, len);

    neverc_rc4_cipher_t c2;
    neverc_rc4_init(&c2, key, sizeof(key) - 1);
    for (size_t i = 0; i < len; i++)
        neverc_rc4_xor_keystream(&c2, ct_single + i, plaintext + i, 1);

    check_bytes("byte-at-a-time matches batch", ct_single, ct_batch, len);
}

static void test_invalid_key(void) {
    printf("[rc4 invalid key]\n");

    neverc_rc4_cipher_t c;
    check_int("key too short (0)", neverc_rc4_init(&c, (const uint8_t *)"", 0), -1);
    check_int("null cipher rejected", neverc_rc4_init(NULL, (const uint8_t *)"A", 1), -1);
    check_int("null key rejected", neverc_rc4_init(&c, NULL, 1), -1);

    uint8_t big_key[257];
    memset(big_key, 'A', 257);
    check_int("key too long (257)", neverc_rc4_init(&c, big_key, 257), -1);

    check_int("key len=1 ok", neverc_rc4_init(&c, (const uint8_t *)"A", 1), 0);

    uint8_t max_key[256];
    memset(max_key, 'B', 256);
    check_int("key len=256 ok", neverc_rc4_init(&c, max_key, 256), 0);
}

static void test_failed_reinit_and_reset_no_identity(void) {
    printf("[rc4 failed re-init / reset do not emit identity]\n");

    const uint8_t key[] = "Key";
    const uint8_t pt[] = "Plaintext";
    uint8_t dst[9], aa[9], old_ct[9];
    neverc_rc4_cipher_t c;

    neverc_rc4_init(&c, key, 3);
    neverc_rc4_xor_keystream(&c, old_ct, pt, 9);

    check_int("re-init empty key fails", neverc_rc4_init(&c, key, 0), -1);
    memset(dst, 0xAA, sizeof(dst));
    memset(aa, 0xAA, sizeof(aa));
    neverc_rc4_xor_keystream(&c, dst, pt, 9);
    check_bytes("xor after failed re-init is a no-op", dst, aa, 9);
    check_true("failed re-init does not keep the old keystream",
               memcmp(dst, old_ct, 9) != 0);

    neverc_rc4_init(&c, key, 3);
    neverc_rc4_reset(&c);
    memset(dst, 0xAA, sizeof(dst));
    neverc_rc4_xor_keystream(&c, dst, pt, 9);
    check_bytes("xor after reset is a no-op", dst, aa, 9);
    check_true("reset does not emit plaintext as ciphertext",
               memcmp(dst, pt, 9) != 0);
}

static void test_reset(void) {
    printf("[rc4 reset]\n");

    const uint8_t key[] = "reset key";
    neverc_rc4_cipher_t c;
    neverc_rc4_init(&c, key, sizeof(key) - 1);

    uint8_t dummy[4] = {0};
    neverc_rc4_xor_keystream(&c, dummy, dummy, 4);

    neverc_rc4_reset(&c);

    int all_zero = 1;
    for (int i = 0; i < 256; i++)
        if (c.s[i] != 0) { all_zero = 0; break; }

    check_true("reset zeroes S-box", all_zero);
    check_true("reset zeroes i", c.i == 0);
    check_true("reset zeroes j", c.j == 0);
}

static void test_different_keys_different_output(void) {
    printf("[rc4 different keys]\n");

    const uint8_t pt[] = "Same plaintext for both";
    size_t len = sizeof(pt) - 1;
    uint8_t ct1[32], ct2[32];

    neverc_rc4_cipher_t c1, c2;
    neverc_rc4_init(&c1, (const uint8_t *)"key1", 4);
    neverc_rc4_init(&c2, (const uint8_t *)"key2", 4);

    neverc_rc4_xor_keystream(&c1, ct1, pt, len);
    neverc_rc4_xor_keystream(&c2, ct2, pt, len);

    check_true("different keys produce different ciphertext",
               memcmp(ct1, ct2, len) != 0);
}

static void test_in_place(void) {
    printf("[rc4 in-place XOR]\n");

    const uint8_t key[] = "inplace";
    uint8_t buf[] = "Hello, in-place!";
    size_t len = sizeof(buf) - 1;

    uint8_t original[32];
    memcpy(original, buf, len);

    neverc_rc4_cipher_t c;
    neverc_rc4_init(&c, key, sizeof(key) - 1);
    neverc_rc4_xor_keystream(&c, buf, buf, len);

    check_true("in-place encrypt differs", memcmp(buf, original, len) != 0);

    neverc_rc4_init(&c, key, sizeof(key) - 1);
    neverc_rc4_xor_keystream(&c, buf, buf, len);

    check_bytes("in-place decrypt recovers", buf, original, len);
}

int main(void) {
    printf("=== NeverC RC4 Tests ===\n\n");
    test_known_vectors();
    test_encrypt_decrypt_roundtrip();
    test_streaming();
    test_byte_at_a_time();
    test_invalid_key();
    test_failed_reinit_and_reset_no_identity();
    test_reset();
    test_different_keys_different_output();
    test_in_place();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
