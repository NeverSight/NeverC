#include "neverc/std/encoding/ascii85.h"
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

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) { tests_passed++; }
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}

static void test_max_encoded_len(void) {
    printf("[ascii85 max_encoded_len]\n");
    check_int("len(0)", neverc_ascii85_max_encoded_len(0), 0);
    check_int("len(1)", neverc_ascii85_max_encoded_len(1), 5);
    check_int("len(4)", neverc_ascii85_max_encoded_len(4), 5);
    check_int("len(5)", neverc_ascii85_max_encoded_len(5), 10);
    check_int("len(8)", neverc_ascii85_max_encoded_len(8), 10);
    check_int("len(12)", neverc_ascii85_max_encoded_len(12), 15);
}

static void test_encode_decode(void) {
    printf("[ascii85 encode/decode]\n");

    /* Verified test vectors from Go ascii85 spec */
    struct { const unsigned char *decoded; size_t dec_len; const char *encoded; } vectors[] = {
        {(const unsigned char *)"",              0, ""},
        {(const unsigned char *)"\0",            1, "!!"},
        {(const unsigned char *)"\0\0",          2, "!!!"},
        {(const unsigned char *)"\0\0\0",        3, "!!!!"},
        {(const unsigned char *)"\0\0\0\0",      4, "z"},
        {(const unsigned char *)"Man ",          4, "9jqo^"},
        {(const unsigned char *)"sure",          4, "F*2M7"},
    };
    int nvec = sizeof(vectors) / sizeof(vectors[0]);

    for (int i = 0; i < nvec; i++) {
        const char *expected_enc = vectors[i].encoded;
        size_t expected_len = strlen(expected_enc);

        unsigned char enc_buf[128];
        int enc_len = neverc_ascii85_encode(enc_buf, vectors[i].decoded, vectors[i].dec_len);

        char buf[128];
        snprintf(buf, sizeof(buf), "encode[%d] len", i);
        check_int(buf, enc_len, (int)expected_len);

        snprintf(buf, sizeof(buf), "encode[%d] match", i);
        check_true(buf, enc_len == (int)expected_len &&
                        memcmp(enc_buf, expected_enc, expected_len) == 0);
    }

    /* Encode-then-decode round-trip for various lengths */
    for (int len = 1; len <= 20; len++) {
        unsigned char src[20];
        for (int i = 0; i < len; i++) src[i] = (unsigned char)(i * 37 + 11);

        unsigned char enc[64], dec[64];
        int enc_len = neverc_ascii85_encode(enc, src, len);
        neverc_ascii85_result_t r = neverc_ascii85_decode(dec, sizeof(dec), enc, enc_len, 1);

        char buf[128];
        snprintf(buf, sizeof(buf), "round-trip len=%d", len);
        check_true(buf, r.error == 0 && (int)r.ndst == len && memcmp(dec, src, len) == 0);
    }

    /* Round-trip: encode then decode */
    {
        const char *msg = "Hello, ASCII85! This is a test of the encoding.";
        size_t msg_len = strlen(msg);
        unsigned char enc[256], dec[256];

        int enc_len = neverc_ascii85_encode(enc, (const unsigned char *)msg, msg_len);
        check_true("round-trip encode > 0", enc_len > 0);

        neverc_ascii85_result_t r = neverc_ascii85_decode(dec, sizeof(dec), enc, enc_len, 1);
        check_true("round-trip no error", r.error == 0);
        check_int("round-trip ndst", (int)r.ndst, (int)msg_len);
        check_true("round-trip match", memcmp(dec, msg, msg_len) == 0);
    }

    /* Zero blocks → 'z' shorthand */
    {
        unsigned char zeros[8] = {0};
        unsigned char enc[32];
        int enc_len = neverc_ascii85_encode(enc, zeros, 8);
        check_true("zeros encode to zz", enc_len == 2 && enc[0] == 'z' && enc[1] == 'z');

        unsigned char dec[32];
        neverc_ascii85_result_t r = neverc_ascii85_decode(dec, sizeof(dec), enc, enc_len, 1);
        check_true("zeros round-trip", r.ndst == 8 && r.error == 0);
        int all_zero = 1;
        for (int i = 0; i < 8; i++) if (dec[i] != 0) all_zero = 0;
        check_true("zeros all zero", all_zero);
    }
}

static void test_decode_errors(void) {
    printf("[ascii85 decode errors]\n");

    /* Invalid character */
    {
        const unsigned char bad[] = "abc{d";
        unsigned char dec[32];
        neverc_ascii85_result_t r = neverc_ascii85_decode(dec, sizeof(dec), bad, 5, 1);
        check_true("invalid char error", r.error == 1);
    }

    /* Single trailing byte (invalid) */
    {
        const unsigned char bad[] = "!";
        unsigned char dec[32];
        neverc_ascii85_result_t r = neverc_ascii85_decode(dec, sizeof(dec), bad, 1, 1);
        check_true("single trailing byte error", r.error == 1);
    }

    /* Whitespace skipped */
    {
        const unsigned char spaced[] = "9j qo ^";
        unsigned char dec[32];
        neverc_ascii85_result_t r = neverc_ascii85_decode(dec, sizeof(dec), spaced, 7, 0);
        check_true("whitespace skip: ndst >= 0", r.error == 0);
    }
}

static void test_partial_decode(void) {
    printf("[ascii85 partial decode]\n");

    /* Non-flush: incomplete block should not be decoded */
    {
        const unsigned char partial[] = "9jqo";
        unsigned char dec[32];
        neverc_ascii85_result_t r = neverc_ascii85_decode(dec, sizeof(dec), partial, 4, 0);
        check_true("non-flush partial ndst=0", r.ndst == 0);
        check_true("non-flush partial no error", r.error == 0);
    }

    /* Flush: incomplete block should be decoded */
    {
        const unsigned char partial[] = "9jqo";
        unsigned char dec[32];
        neverc_ascii85_result_t r = neverc_ascii85_decode(dec, sizeof(dec), partial, 4, 1);
        check_true("flush partial ndst=3", r.ndst == 3);
        check_true("flush partial no error", r.error == 0);
    }
}

static void test_binary_data(void) {
    printf("[ascii85 binary data]\n");

    unsigned char bin[256];
    for (int i = 0; i < 256; i++) bin[i] = (unsigned char)i;

    unsigned char enc[512];
    int enc_len = neverc_ascii85_encode(enc, bin, 256);
    check_true("binary encode len > 0", enc_len > 0);
    check_true("binary encode len <= max",
               enc_len <= neverc_ascii85_max_encoded_len(256));

    unsigned char dec[512];
    neverc_ascii85_result_t r = neverc_ascii85_decode(dec, sizeof(dec), enc, enc_len, 1);
    check_true("binary round-trip no error", r.error == 0);
    check_int("binary round-trip ndst", (int)r.ndst, 256);
    check_true("binary round-trip match", memcmp(dec, bin, 256) == 0);
}

int main(void) {
    printf("=== NeverC ASCII85 Tests ===\n\n");

    test_max_encoded_len();
    test_encode_decode();
    test_decode_errors();
    test_partial_decode();
    test_binary_data();

    printf("\n=== Results: %d/%d passed", tests_passed, tests_run);
    if (tests_failed > 0) printf(", %d FAILED", tests_failed);
    printf(" ===\n");
    return tests_failed > 0 ? 1 : 0;
}
