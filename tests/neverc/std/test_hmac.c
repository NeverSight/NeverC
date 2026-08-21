#include "neverc/std/crypto/hmac.h"
#include "neverc/std/crypto/sha256.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_hex(const char *name, const uint8_t *got, const char *expected_hex, size_t len) {
    tests_run++;
    char got_hex[256];
    for (size_t i = 0; i < len; i++)
        sprintf(got_hex + i * 2, "%02x", got[i]);
    got_hex[len * 2] = '\0';
    if (strcmp(got_hex, expected_hex) == 0) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL: %s:\n    got  %s\n    want %s\n", name, got_hex, expected_hex);
    }
}

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

/*
 * RFC 4231 — HMAC-SHA-256 test vectors.
 * These are the official IETF test cases.
 */
static void test_hmac_sha256_rfc4231(void) {
    printf("[hmac-sha256 RFC 4231 vectors]\n");

    uint8_t mac[32];

    /* Test Case 1 */
    {
        uint8_t key[20]; memset(key, 0x0b, 20);
        const uint8_t *data = (const uint8_t *)"Hi There";
        neverc_hmac_sha256(key, 20, data, 8, mac);
        check_hex("TC1 sha256",  mac,
            "b0344c61d8db38535ca8afceaf0bf12b"
            "881dc200c9833da726e9376c2e32cff7", 32);
    }

    /* Test Case 2: "Jefe" */
    {
        const uint8_t *key = (const uint8_t *)"Jefe";
        const uint8_t *data = (const uint8_t *)"what do ya want for nothing?";
        neverc_hmac_sha256(key, 4, data, 28, mac);
        check_hex("TC2 sha256", mac,
            "5bdcc146bf60754e6a042426089575c7"
            "5a003f089d2739839dec58b964ec3843", 32);
    }

    /* Test Case 3 */
    {
        uint8_t key[20]; memset(key, 0xaa, 20);
        uint8_t data[50]; memset(data, 0xdd, 50);
        neverc_hmac_sha256(key, 20, data, 50, mac);
        check_hex("TC3 sha256", mac,
            "773ea91e36800e46854db8ebd09181a7"
            "2959098b3ef8c122d9635514ced565fe", 32);
    }

    /* Test Case 4 */
    {
        uint8_t key[25];
        for (int i = 0; i < 25; i++) key[i] = (uint8_t)(i + 1);
        uint8_t data[50]; memset(data, 0xcd, 50);
        neverc_hmac_sha256(key, 25, data, 50, mac);
        check_hex("TC4 sha256", mac,
            "82558a389a443c0ea4cc819899f2083a"
            "85f0faa3e578f8077a2e3ff46729665b", 32);
    }

    /* Test Case 5: truncation vector, full 256-bit MAC */
    {
        uint8_t key[20]; memset(key, 0x0c, 20);
        const uint8_t *data = (const uint8_t *)"Test With Truncation";
        neverc_hmac_sha256(key, 20, data, 20, mac);
        check_hex("TC5 sha256", mac,
            "a3b6167473100ee06e0c796c2955552b"
            "fa6f7c0a6a8aef8b93f860aab0cd20c5", 32);
    }

    /* Test Case 6: key > block_size (131 bytes) */
    {
        uint8_t key[131]; memset(key, 0xaa, 131);
        const uint8_t *data = (const uint8_t *)"Test Using Larger Than Block-Size Key - Hash Key First";
        neverc_hmac_sha256(key, 131, data, 54, mac);
        check_hex("TC6 sha256 (long key)", mac,
            "60e431591ee0b67f0d8a26aacbf5b77f"
            "8e0bc6213728c5140546040f0ee37f54", 32);
    }

    /* Test Case 7: key > block_size + longer data */
    {
        uint8_t key[131]; memset(key, 0xaa, 131);
        const uint8_t *data = (const uint8_t *)
            "This is a test using a larger than block-size key and a "
            "larger than block-size data. The key needs to be hashed "
            "before being used by the HMAC algorithm.";
        neverc_hmac_sha256(key, 131, data, 152, mac);
        check_hex("TC7 sha256 (long key+data)", mac,
            "9b09ffa71b942fcb27635fbcd5b0e944"
            "bfdc63644f0713938a7f51535c3a35e2", 32);
    }

    /* RFC 2104: key_len == block_size is padded, not hashed (>= would differ). */
    {
        uint8_t key[65]; memset(key, 0x0b, 65);
        const uint8_t *data = (const uint8_t *)"Hi There";
        neverc_hmac_sha256(key, 64, data, 8, mac);
        check_hex("sha256 key_len == 64 not hashed", mac,
            "21cd586aeca0579d99a1c938127c9252"
            "5a371f807bc5ba6eb78bc825bd4f2be3", 32);
        neverc_hmac_sha256(key, 65, data, 8, mac);
        check_hex("sha256 key_len == 65 hashed", mac,
            "727b82fba264393c5d67fd6d6ad783e9"
            "019a1fa6a857fccb70f5852f04be5d5d", 32);
    }
}

/*
 * RFC 2202 — HMAC-MD5 test vectors.
 */
static void test_hmac_md5_rfc2202(void) {
    printf("[hmac-md5 RFC 2202 vectors]\n");

    uint8_t mac[16];

    /* TC1 */
    {
        uint8_t key[16]; memset(key, 0x0b, 16);
        const uint8_t *data = (const uint8_t *)"Hi There";
        neverc_hmac_md5(key, 16, data, 8, mac);
        check_hex("TC1 md5", mac, "9294727a3638bb1c13f48ef8158bfc9d", 16);
    }

    /* TC2: "Jefe" */
    {
        const uint8_t *key = (const uint8_t *)"Jefe";
        const uint8_t *data = (const uint8_t *)"what do ya want for nothing?";
        neverc_hmac_md5(key, 4, data, 28, mac);
        check_hex("TC2 md5", mac, "750c783e6ab0b503eaa86e310a5db738", 16);
    }

    /* TC3 */
    {
        uint8_t key[16]; memset(key, 0xaa, 16);
        uint8_t data[50]; memset(data, 0xdd, 50);
        neverc_hmac_md5(key, 16, data, 50, mac);
        check_hex("TC3 md5", mac, "56be34521d144c88dbb8c733f0e8b3f6", 16);
    }

    /* TC6: key > block_size must be hashed first. */
    {
        uint8_t key[80]; memset(key, 0xaa, 80);
        const uint8_t *data = (const uint8_t *)
            "Test Using Larger Than Block-Size Key - Hash Key First";
        neverc_hmac_md5(key, 80, data, 54, mac);
        check_hex("TC6 md5 (long key)", mac, "6b1ab7fe4bd7bf8f0b62e6ce61b9d0cd", 16);
    }

    /* TC7: long key + long data */
    {
        uint8_t key[80]; memset(key, 0xaa, 80);
        const uint8_t *data = (const uint8_t *)
            "Test Using Larger Than Block-Size Key and Larger "
            "Than One Block-Size Data";
        neverc_hmac_md5(key, 80, data, 73, mac);
        check_hex("TC7 md5 (long key+data)", mac, "6f630fad67cda0ee1fb1f562db3aa53e", 16);
    }
}

/*
 * RFC 2202 — HMAC-SHA-1 test vectors.
 */
static void test_hmac_sha1_rfc2202(void) {
    printf("[hmac-sha1 RFC 2202 vectors]\n");

    uint8_t mac[20];

    /* TC1 */
    {
        uint8_t key[20]; memset(key, 0x0b, 20);
        const uint8_t *data = (const uint8_t *)"Hi There";
        neverc_hmac_sha1(key, 20, data, 8, mac);
        check_hex("TC1 sha1", mac, "b617318655057264e28bc0b6fb378c8ef146be00", 20);
    }

    /* TC2: "Jefe" */
    {
        const uint8_t *key = (const uint8_t *)"Jefe";
        const uint8_t *data = (const uint8_t *)"what do ya want for nothing?";
        neverc_hmac_sha1(key, 4, data, 28, mac);
        check_hex("TC2 sha1", mac, "effcdf6ae5eb2fa2d27416d5f184df9c259a7c79", 20);
    }

    /* TC3 */
    {
        uint8_t key[20]; memset(key, 0xaa, 20);
        uint8_t data[50]; memset(data, 0xdd, 50);
        neverc_hmac_sha1(key, 20, data, 50, mac);
        check_hex("TC3 sha1", mac, "125d7342b9ac11cd91a39af48aa17b4f63f175d3", 20);
    }

    /* TC6: key > block_size must be hashed first. */
    {
        uint8_t key[80]; memset(key, 0xaa, 80);
        const uint8_t *data = (const uint8_t *)
            "Test Using Larger Than Block-Size Key - Hash Key First";
        neverc_hmac_sha1(key, 80, data, 54, mac);
        check_hex("TC6 sha1 (long key)", mac,
            "aa4ae5e15272d00e95705637ce8a3b55ed402112", 20);
    }

    /* TC7: long key + long data */
    {
        uint8_t key[80]; memset(key, 0xaa, 80);
        const uint8_t *data = (const uint8_t *)
            "Test Using Larger Than Block-Size Key and Larger "
            "Than One Block-Size Data";
        neverc_hmac_sha1(key, 80, data, 73, mac);
        check_hex("TC7 sha1 (long key+data)", mac,
            "e8e99d0f45237d786d6bbaa7965c7808bbff1a91", 20);
    }
}

/*
 * HMAC-SHA-512 test from RFC 4231
 */
static void test_hmac_sha512_rfc4231(void) {
    printf("[hmac-sha512 RFC 4231 vectors]\n");

    uint8_t mac[64];

    /* TC1 */
    {
        uint8_t key[20]; memset(key, 0x0b, 20);
        const uint8_t *data = (const uint8_t *)"Hi There";
        neverc_hmac_sha512(key, 20, data, 8, mac);
        check_hex("TC1 sha512", mac,
            "87aa7cdea5ef619d4ff0b4241a1d6cb0"
            "2379f4e2ce4ec2787ad0b30545e17cde"
            "daa833b7d6b8a702038b274eaea3f4e4"
            "be9d914eeb61f1702e696c203a126854", 64);
    }

    /* TC2: "Jefe" */
    {
        const uint8_t *key = (const uint8_t *)"Jefe";
        const uint8_t *data = (const uint8_t *)"what do ya want for nothing?";
        neverc_hmac_sha512(key, 4, data, 28, mac);
        check_hex("TC2 sha512", mac,
            "164b7a7bfcf819e2e395fbe73b56e0a3"
            "87bd64222e831fd610270cd7ea250554"
            "9758bf75c05a994a6d034f65f8f0e6fd"
            "caeab1a34d4a6b4b636e070a38bce737", 64);
    }

    /* TC6: key longer than SHA-512 block size (128) must be hashed first. */
    {
        uint8_t key[131]; memset(key, 0xaa, 131);
        const uint8_t *data = (const uint8_t *)
            "Test Using Larger Than Block-Size Key - Hash Key First";
        neverc_hmac_sha512(key, 131, data, 54, mac);
        check_hex("TC6 sha512 (long key)", mac,
            "80b24263c7c1a3ebb71493c1dd7be8b4"
            "9b46d1f41b4aeec1121b013783f8f352"
            "6b56d037e05f2598bd0fd2215d6a1e52"
            "95e64f73f63f0aec8b915a985d786598", 64);
    }

    /* TC7: key and data both larger than SHA-512 block size. */
    {
        uint8_t key[131]; memset(key, 0xaa, 131);
        const uint8_t *data = (const uint8_t *)
            "This is a test using a larger than block-size key and a "
            "larger than block-size data. The key needs to be hashed "
            "before being used by the HMAC algorithm.";
        neverc_hmac_sha512(key, 131, data, 152, mac);
        check_hex("TC7 sha512 (long key+data)", mac,
            "e37b6a775dc87dbaa4dfa9f96e5e3ffd"
            "debd71f8867289865df5a32d20cdc944"
            "b6022cac3c4982b10d5eeb55c3e4de15"
            "134676fb6de0446065c97440fa8c6a58", 64);
    }

    /* RFC 2104: key_len == block_size is padded, not hashed. */
    {
        uint8_t key[129]; memset(key, 0x0b, 129);
        const uint8_t *data = (const uint8_t *)"Hi There";
        neverc_hmac_sha512(key, 128, data, 8, mac);
        check_hex("sha512 key_len == 128 not hashed", mac,
            "e0853e8ef09d70a6ae8431a46c5c8759"
            "0e12ad57f6ab11504a15bf500b431c11"
            "2501952fe1fdcdc6464e3b16d26a0702"
            "52abd243a0efafb5cd46fc11c6934658", 64);
        neverc_hmac_sha512(key, 129, data, 8, mac);
        check_hex("sha512 key_len == 129 hashed", mac,
            "aa1c23fe040c4f3e6545a9154e339d17"
            "ffb5272e0a545b84d38b9bf8e2c7464d"
            "f2d62bb5000557686f8510eb4302a0ca"
            "e6b5dd1f3700beaede755f86fdbeb48f", 64);
    }
}

static void test_hmac_equal(void) {
    printf("[hmac_equal]\n");

    uint8_t a[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t b[] = {0xDE, 0xAD, 0xBE, 0xEF};
    uint8_t c[] = {0xDE, 0xAD, 0xBE, 0x00};

    check_int("equal macs", neverc_hmac_equal(a, b, 4), 1);
    check_int("different macs", neverc_hmac_equal(a, c, 4), 0);
    check_int("zero-length equal", neverc_hmac_equal(a, c, 0), 1);
    check_int("null zero-length equal", neverc_hmac_equal(NULL, NULL, 0), 1);
    check_int("null pointer non-zero length", neverc_hmac_equal(a, NULL, 4), 0);
}

static void test_hmac_not_length_extendable(void) {
    printf("[hmac resists length extension]\n");

    /* HMAC(K, m) must not be H(K||m); the latter is length-extendable. */
    const uint8_t key[] = "secret";
    const uint8_t data[] = "message";
    uint8_t mac[32], naive[32];
    neverc_hmac_sha256(key, 6, data, 7, mac);

    neverc_sha256_ctx ctx;
    neverc_sha256_init(&ctx);
    neverc_sha256_update(&ctx, key, 6);
    neverc_sha256_update(&ctx, data, 7);
    neverc_sha256_final(&ctx, naive);
    check_int("HMAC-SHA256 != SHA256(key||msg)", memcmp(mac, naive, 32) != 0, 1);

    /* Inner hash H((K^ipad)||m) is length-extendable; HMAC must apply the outer hash. */
    uint8_t kpad[64];
    memset(kpad, 0, sizeof(kpad));
    memcpy(kpad, key, 6);
    for (int i = 0; i < 64; i++) kpad[i] ^= 0x36;
    neverc_sha256_init(&ctx);
    neverc_sha256_update(&ctx, kpad, 64);
    neverc_sha256_update(&ctx, data, 7);
    neverc_sha256_final(&ctx, naive);
    check_int("HMAC-SHA256 != inner hash", memcmp(mac, naive, 32) != 0, 1);

    neverc_hmac_sha256(NULL, 0, NULL, 0, mac);
    check_hex("HMAC-SHA256 empty key and data", mac,
        "b613679a0814d9ec772f95d778c35fc5"
        "ff1697c493715653c6c712144292c5ad", 32);
}

static void test_empty_and_invalid_inputs(void) {
    printf("[empty and invalid inputs]\n");
    uint8_t byte = 0;
    uint8_t a[64], b[64], zeros[64] = {0};

    neverc_hmac_sha256(NULL, 0, NULL, 0, a);
    neverc_hmac_sha256(&byte, 0, &byte, 0, b);
    check_int("sha256 accepts null empty spans", memcmp(a, b, 32) == 0, 1);

    neverc_hmac_sha512(NULL, 0, NULL, 0, a);
    neverc_hmac_sha512(&byte, 0, &byte, 0, b);
    check_int("sha512 accepts null empty spans", memcmp(a, b, 64) == 0, 1);

    neverc_hmac_sha1(NULL, 0, NULL, 0, a);
    neverc_hmac_sha1(&byte, 0, &byte, 0, b);
    check_int("sha1 accepts null empty spans", memcmp(a, b, 20) == 0, 1);

    neverc_hmac_md5(NULL, 0, NULL, 0, a);
    neverc_hmac_md5(&byte, 0, &byte, 0, b);
    check_int("md5 accepts null empty spans", memcmp(a, b, 16) == 0, 1);

    memset(a, 0xa5, sizeof(a));
    neverc_hmac_sha256(NULL, 1, &byte, 0, a);
    check_int("invalid key span clears output",
              memcmp(a, zeros, 32) == 0, 1);
    memset(a, 0xa5, sizeof(a));
    neverc_hmac_sha512(&byte, 0, NULL, 1, a);
    check_int("invalid data span clears output",
              memcmp(a, zeros, 64) == 0, 1);

    neverc_hmac_sha256(NULL, 0, NULL, 0, NULL);
    check_int("null output is ignored", 1, 1);
}

/* SHA update refuses a wrapping length without reading the buffer, then
 * finalize of the wiped ctx would emit a message-independent digest. HMAC
 * must clear `out` instead of returning that MAC. Dummy pointers are safe. */
static void test_hmac_wrapping_lengths(void) {
    printf("[wrapping hash lengths]\n");

#if SIZE_MAX > (UINT64_MAX / 8)
    {
        uint8_t key = 0x0b;
        uint8_t dummy = 0xaa;
        uint8_t mac[32];
        uint8_t zeros[32] = {0};

        memset(mac, 0xa5, sizeof(mac));
        neverc_hmac_sha256(&key, 1, &dummy, SIZE_MAX, mac);
        check_int("sha256 wrapping data_len clears output",
                  memcmp(mac, zeros, 32) == 0, 1);

        uint8_t mac20[20];
        memset(mac20, 0xa5, sizeof(mac20));
        neverc_hmac_sha1(&key, 1, &dummy, SIZE_MAX, mac20);
        check_int("sha1 wrapping data_len clears output",
                  memcmp(mac20, zeros, 20) == 0, 1);

        uint8_t mac16[16];
        memset(mac16, 0xa5, sizeof(mac16));
        neverc_hmac_md5(&key, 1, &dummy, SIZE_MAX, mac16);
        check_int("md5 wrapping data_len clears output",
                  memcmp(mac16, zeros, 16) == 0, 1);

        memset(mac, 0xa5, sizeof(mac));
        neverc_hmac_sha256(&dummy, (size_t)(UINT64_MAX / 8) + 1, &key, 1, mac);
        check_int("sha256 wrapping key_len clears output",
                  memcmp(mac, zeros, 32) == 0, 1);
    }
#endif

#if SIZE_MAX > (UINT64_MAX - 128)
    {
        uint8_t key = 0x0b;
        uint8_t dummy = 0xaa;
        uint8_t mac[64];
        uint8_t zeros[64] = {0};
        memset(mac, 0xa5, sizeof(mac));
        neverc_hmac_sha512(&key, 1, &dummy, SIZE_MAX, mac);
        check_int("sha512 wrapping data_len clears output",
                  memcmp(mac, zeros, 64) == 0, 1);

        memset(mac, 0xa5, sizeof(mac));
        neverc_hmac_sha512(&dummy, SIZE_MAX, &key, 1, mac);
        check_int("sha512 wrapping key_len clears output",
                  memcmp(mac, zeros, 64) == 0, 1);
    }
#endif
}

int main(void) {
    printf("=== NeverC HMAC Library Tests ===\n");
    printf("(RFC 4231 / RFC 2202 official test vectors)\n\n");

    test_hmac_sha256_rfc4231();
    test_hmac_md5_rfc2202();
    test_hmac_sha1_rfc2202();
    test_hmac_sha512_rfc4231();
    test_hmac_equal();
    test_hmac_not_length_extendable();
    test_empty_and_invalid_inputs();
    test_hmac_wrapping_lengths();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");

    return tests_failed > 0 ? 1 : 0;
}
