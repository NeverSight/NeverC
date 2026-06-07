#include "neverc/encoding/base32.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (strcmp(got, expected) == 0) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got, expected); }
}

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void check_bytes(const char *name, const uint8_t *got, size_t got_len,
                        const uint8_t *expected, size_t exp_len) {
    tests_run++;
    if (got_len == exp_len && memcmp(got, expected, exp_len) == 0) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: length got=%zu expected=%zu\n", name, got_len, exp_len); }
}

static void check_true(const char *name, int cond) {
    tests_run++;
    if (cond) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s\n", name); }
}

/*
 * RFC 4648 Section 10 — Base32 test vectors.
 * These are the authoritative test cases from the standard.
 */
static void test_rfc4648_vectors(void) {
    printf("[base32 RFC 4648 vectors]\n");

    static const struct {
        const char *input;
        const char *std_encoded;
        const char *hex_encoded;
    } vectors[] = {
        { "",       "",                 ""                 },
        { "f",      "MY======",         "CO======",        },
        { "fo",     "MZXQ====",         "CPNG====",        },
        { "foo",    "MZXW6===",         "CPNMU===",        },
        { "foob",   "MZXW6YQ=",         "CPNMUOG=",        },
        { "fooba",  "MZXW6YTB",         "CPNMUOJ1",        },
        { "foobar", "MZXW6YTBOI======", "CPNMUOJ1E8======" },
    };
    int n = sizeof(vectors) / sizeof(vectors[0]);

    char enc[64];
    uint8_t dec[64];

    for (int i = 0; i < n; i++) {
        const uint8_t *input = (const uint8_t *)vectors[i].input;
        size_t input_len = strlen(vectors[i].input);

        char name[128];

        snprintf(name, sizeof(name), "std encode \"%s\"", vectors[i].input);
        neverc_base32_encode(enc, input, input_len);
        check_str(name, enc, vectors[i].std_encoded);

        if (input_len > 0) {
            snprintf(name, sizeof(name), "std decode \"%s\"", vectors[i].std_encoded);
            int decoded_len = neverc_base32_decode(dec, vectors[i].std_encoded,
                                                   strlen(vectors[i].std_encoded));
            check_int(name, decoded_len, (int)input_len);
            if (decoded_len > 0)
                check_bytes(name, dec, (size_t)decoded_len, input, input_len);
        }

        snprintf(name, sizeof(name), "hex encode \"%s\"", vectors[i].input);
        neverc_base32_hex_encode(enc, input, input_len);
        check_str(name, enc, vectors[i].hex_encoded);

        if (input_len > 0) {
            snprintf(name, sizeof(name), "hex decode \"%s\"", vectors[i].hex_encoded);
            int decoded_len = neverc_base32_hex_decode(dec, vectors[i].hex_encoded,
                                                       strlen(vectors[i].hex_encoded));
            check_int(name, decoded_len, (int)input_len);
            if (decoded_len > 0)
                check_bytes(name, dec, (size_t)decoded_len, input, input_len);
        }
    }
}

static void test_roundtrip(void) {
    printf("[base32 roundtrip]\n");
    char enc[256];
    uint8_t dec[256];

    for (size_t len = 0; len <= 32; len++) {
        uint8_t input[32];
        for (size_t j = 0; j < len; j++)
            input[j] = (uint8_t)(j * 37 + 13);

        neverc_base32_encode(enc, input, len);
        int decoded_len = neverc_base32_decode(dec, enc, strlen(enc));

        char name[64];
        snprintf(name, sizeof(name), "roundtrip len=%zu", len);
        check_int(name, decoded_len, (int)len);
        if (decoded_len == (int)len)
            check_bytes(name, dec, len, input, len);
    }
}

static void test_hex_roundtrip(void) {
    printf("[base32 hex roundtrip]\n");
    char enc[256];
    uint8_t dec[256];

    for (size_t len = 0; len <= 32; len++) {
        uint8_t input[32];
        for (size_t j = 0; j < len; j++)
            input[j] = (uint8_t)(j * 41 + 7);

        neverc_base32_hex_encode(enc, input, len);
        int decoded_len = neverc_base32_hex_decode(dec, enc, strlen(enc));

        char name[64];
        snprintf(name, sizeof(name), "hex roundtrip len=%zu", len);
        check_int(name, decoded_len, (int)len);
        if (decoded_len == (int)len)
            check_bytes(name, dec, len, input, len);
    }
}

static void test_encoded_decoded_len(void) {
    printf("[base32 length calculations]\n");
    check_int("encoded_len(0)", (int)neverc_base32_encoded_len(0), 0);
    check_int("encoded_len(1)", (int)neverc_base32_encoded_len(1), 8);
    check_int("encoded_len(5)", (int)neverc_base32_encoded_len(5), 8);
    check_int("encoded_len(6)", (int)neverc_base32_encoded_len(6), 16);
    check_int("encoded_len(10)", (int)neverc_base32_encoded_len(10), 16);

    check_int("decoded_len(0)", (int)neverc_base32_decoded_len(0), 0);
    check_int("decoded_len(8)", (int)neverc_base32_decoded_len(8), 5);
    check_int("decoded_len(16)", (int)neverc_base32_decoded_len(16), 10);
}

static void test_invalid_input(void) {
    printf("[base32 invalid input]\n");
    uint8_t dec[64];

    check_int("decode invalid char", neverc_base32_decode(dec, "1AAAAAAA", 8), -1);
    check_int("decode bad length 1", neverc_base32_decode(dec, "A", 1), -1);
    check_int("decode bad length 3", neverc_base32_decode(dec, "AAA", 3), -1);
    check_int("decode bad length 6", neverc_base32_decode(dec, "AAAAAA", 6), -1);
}

static void test_case_insensitive(void) {
    printf("[base32 case insensitive decode]\n");
    uint8_t dec_upper[64], dec_lower[64];

    int len_upper = neverc_base32_decode(dec_upper, "MZXW6YTBOI======", 16);
    int len_lower = neverc_base32_decode(dec_lower, "mzxw6ytboi======", 16);

    check_int("case insensitive same length", len_upper, len_lower);
    if (len_upper > 0 && len_upper == len_lower)
        check_bytes("case insensitive same bytes", dec_upper, (size_t)len_upper,
                    dec_lower, (size_t)len_lower);
}

static void test_binary_data(void) {
    printf("[base32 binary data]\n");
    uint8_t binary[256];
    for (int i = 0; i < 256; i++) binary[i] = (uint8_t)i;

    char enc[512];
    uint8_t dec[256];

    neverc_base32_encode(enc, binary, 256);
    int decoded_len = neverc_base32_decode(dec, enc, strlen(enc));
    check_int("binary 256 bytes decode len", decoded_len, 256);
    if (decoded_len == 256)
        check_bytes("binary 256 bytes roundtrip", dec, 256, binary, 256);
}

static void test_empty(void) {
    printf("[base32 empty]\n");
    char enc[8] = "XXXXXXX";
    neverc_base32_encode(enc, (const uint8_t *)"", 0);
    check_str("encode empty", enc, "");
    check_true("encoded len of empty is 0", neverc_base32_encoded_len(0) == 0);
}

int main(void) {
    printf("=== NeverC Base32 Tests ===\n\n");
    test_rfc4648_vectors();
    test_roundtrip();
    test_hex_roundtrip();
    test_encoded_decoded_len();
    test_invalid_input();
    test_case_insensitive();
    test_binary_data();
    test_empty();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
