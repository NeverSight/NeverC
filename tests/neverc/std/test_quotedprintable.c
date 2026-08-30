#include "neverc/std/mime/quotedprintable.h"
#include <stdio.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define ASSERT_EQ(a, b) do { int _a=(a), _b=(b); tests_run++; \
    if (_a==_b) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: %s = %d, expected %d\n", __LINE__, #a, _a, _b); } \
} while(0)

#define ASSERT_MEMEQ(a, b, n) do { tests_run++; \
    if (memcmp(a,b,n)==0) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL [%d]: memory mismatch\n", __LINE__); } \
} while(0)

static void test_decode_basic(void) {
    printf("[decode basic]\n");
    unsigned char out[256];
    int n;

    n = neverc_qp_decode("Hello=20World", 13, out, sizeof(out));
    ASSERT_EQ(n, 11);
    ASSERT_MEMEQ(out, "Hello World", 11);

    n = neverc_qp_decode("=48=65=6C=6C=6F", 15, out, sizeof(out));
    ASSERT_EQ(n, 5);
    ASSERT_MEMEQ(out, "Hello", 5);

    n = neverc_qp_decode("line1\r\nline2", 12, out, sizeof(out));
    ASSERT_EQ(n, 12);
    ASSERT_MEMEQ(out, "line1\r\nline2", 12);

    /* RFC 2047 Q-encoding maps '_' to space; body QP must not. */
    n = neverc_qp_decode("hello_world", 11, out, sizeof(out));
    ASSERT_EQ(n, 11);
    ASSERT_MEMEQ(out, "hello_world", 11);

    /* Go quotedprintable: unescaped C0/DEL fail; encoded =00 is fine. */
    n = neverc_qp_decode("foo\x00" "bar", 7, out, sizeof(out));
    ASSERT_EQ(n, -1);
    n = neverc_qp_decode("foo\x0c" "bar", 7, out, sizeof(out));
    ASSERT_EQ(n, -1);
    n = neverc_qp_decode("foo\x7f" "bar", 7, out, sizeof(out));
    ASSERT_EQ(n, -1);
    n = neverc_qp_decode("foo=00bar", 9, out, sizeof(out));
    ASSERT_EQ(n, 7);

    /* golang/go#22597: raw bytes >= 0x80 pass through, because 8BITMIME
     * gateways emit them without re-encoding. */
    n = neverc_qp_decode("caf\xC3\xA9", 5, out, sizeof(out));
    ASSERT_EQ(n, 5);
    ASSERT_MEMEQ(out, "caf\xC3\xA9", 5);
    n = neverc_qp_decode("a\xFF" "b", 3, out, sizeof(out));
    ASSERT_EQ(n, 3);
    ASSERT_MEMEQ(out, "a\xFF" "b", 3);
    /* Mixed with a soft break so the bulk-copy scan is exercised too. */
    n = neverc_qp_decode("\xE4\xBD\xA0=\r\n\xE5\xA5\xBD", 9, out, sizeof(out));
    ASSERT_EQ(n, 6);
    ASSERT_MEMEQ(out, "\xE4\xBD\xA0\xE5\xA5\xBD", 6);
}

static void test_decode_soft_break(void) {
    printf("[decode soft break]\n");
    unsigned char out[256];
    int n;

    n = neverc_qp_decode("Hello=\r\n World", 14, out, sizeof(out));
    ASSERT_EQ(n, 11);
    ASSERT_MEMEQ(out, "Hello World", 11);

    n = neverc_qp_decode("abc=\ndef", 8, out, sizeof(out));
    ASSERT_EQ(n, 6);
    ASSERT_MEMEQ(out, "abcdef", 6);

    n = neverc_qp_decode("hello=\n", 7, out, sizeof(out));
    ASSERT_EQ(n, 5);
    ASSERT_MEMEQ(out, "hello", 5);

    n = neverc_qp_decode("hello=", 6, out, sizeof(out));
    ASSERT_EQ(n, 5);
    ASSERT_MEMEQ(out, "hello", 5);

    n = neverc_qp_decode("=", 1, out, sizeof(out));
    ASSERT_EQ(n, -1);

    /* Go issue 13219/15486: mid-line CR is content, so leftover '='
     * on "hello\\r=" is a soft break of that same line. */
    n = neverc_qp_decode("hello\r=", 7, out, sizeof(out));
    ASSERT_EQ(n, 6);
    ASSERT_MEMEQ(out, "hello\r", 6);

    n = neverc_qp_decode("hello=\r", 7, out, sizeof(out));
    ASSERT_EQ(n, -1);

    n = neverc_qp_decode("hello= \r", 8, out, sizeof(out));
    ASSERT_EQ(n, -1);

    n = neverc_qp_decode("=A", 2, out, sizeof(out));
    ASSERT_EQ(n, -1);

    /* RFC 2045: transport may pad "=\r\n" with WSP; still a soft break. */
    n = neverc_qp_decode("Hello= \r\nWorld", 14, out, sizeof(out));
    ASSERT_EQ(n, 10);
    ASSERT_MEMEQ(out, "HelloWorld", 10);

    n = neverc_qp_decode("Hello=\t\nWorld", 13, out, sizeof(out));
    ASSERT_EQ(n, 10);
    ASSERT_MEMEQ(out, "HelloWorld", 10);

    n = neverc_qp_decode("= \r\n", 4, out, sizeof(out));
    ASSERT_EQ(n, 0);

    /* Trailing WSP on a line is transport padding and must be deleted. */
    n = neverc_qp_decode("line1  \r\nline2", 14, out, sizeof(out));
    ASSERT_EQ(n, 12);
    ASSERT_MEMEQ(out, "line1\r\nline2", 12);

    n = neverc_qp_decode("hello  ", 7, out, sizeof(out));
    ASSERT_EQ(n, 5);
    ASSERT_MEMEQ(out, "hello", 5);

    /* Encoded trailing space survives; the extra literal spaces do not. */
    n = neverc_qp_decode("hello=20  \n", 11, out, sizeof(out));
    ASSERT_EQ(n, 7);
    ASSERT_MEMEQ(out, "hello \n", 7);

    n = neverc_qp_decode("ab= \t", 5, out, sizeof(out));
    ASSERT_EQ(n, 2);
    ASSERT_MEMEQ(out, "ab", 2);

    /* A bare CR after '=' in the middle of a line is not a soft break. */
    n = neverc_qp_decode("ab=\r cd", 7, out, sizeof(out));
    ASSERT_EQ(n, -1);

    /* WSP before a mid-line CR is not trailing line padding. */
    n = neverc_qp_decode("hello  \rworld", 13, out, sizeof(out));
    ASSERT_EQ(n, 13);
    ASSERT_MEMEQ(out, "hello  \rworld", 13);
}

static void test_encode_basic(void) {
    printf("[encode basic]\n");
    char out[1024];
    int n;

    ASSERT_EQ(neverc_qp_max_encoded_len(11) >= 11, 1);
    ASSERT_EQ(neverc_qp_max_encoded_len(0) >= 16, 1);

    n = neverc_qp_encode((const unsigned char*)"Hello World", 11, out, sizeof(out), 76);
    ASSERT_EQ(n > 0, 1);
    if (n > 0 && (size_t)n < sizeof(out)) out[n] = '\0';
    /* Space in the middle is not trailing, so it's kept as-is */

    n = neverc_qp_encode((const unsigned char*)"\x00\x01\xff", 3, out, sizeof(out), 76);
    ASSERT_EQ(n, 9); /* =00=01=FF */
    if (n > 0 && (size_t)n < sizeof(out)) {
        out[n] = '\0';
        ASSERT_MEMEQ(out, "=00=01=FF", 9);
    }

    char long_src[80];
    memset(long_src, 'A', sizeof(long_src));
    n = neverc_qp_encode((const unsigned char *)long_src, sizeof(long_src),
                         out, sizeof(out), 0);
    ASSERT_EQ(n, (int)sizeof(long_src));
    ASSERT_MEMEQ(out, long_src, sizeof(long_src));
    ASSERT_EQ(memchr(out, '=', (size_t)n) == NULL, 1);

    /* 74 A's + space + 10 B's: the space would sit in the last column
     * before a soft break if encoded as a literal. */
    char wrap_src[85];
    memset(wrap_src, 'A', 74);
    wrap_src[74] = ' ';
    memset(wrap_src + 75, 'B', 10);
    n = neverc_qp_encode((const unsigned char *)wrap_src, sizeof(wrap_src),
                         out, sizeof(out), 76);
    ASSERT_EQ(n > 0, 1);
    if (n <= 0 || (size_t)n >= sizeof(out))
        return;
    out[n] = '\0';
    ASSERT_EQ(strstr(out, " =\r\n") == NULL, 1);
    ASSERT_EQ(strstr(out, "=20") != NULL, 1);

    unsigned char wrap_dec[128];
    int dn = neverc_qp_decode(out, (size_t)n, wrap_dec, sizeof(wrap_dec));
    ASSERT_EQ(dn, (int)sizeof(wrap_src));
    ASSERT_MEMEQ(wrap_dec, wrap_src, sizeof(wrap_src));

    n = neverc_qp_encode((const unsigned char *)"a\nb", 3, out, sizeof(out), 76);
    ASSERT_EQ(n, 5);
    ASSERT_MEMEQ(out, "a=0Ab", 5);
    n = neverc_qp_encode((const unsigned char *)"a\r\nb", 4, out, sizeof(out), 76);
    ASSERT_EQ(n, 4);
    ASSERT_MEMEQ(out, "a\r\nb", 4);

    for (int width = 1; width <= 3; width++)
        ASSERT_EQ(neverc_qp_encode((const unsigned char *)"\x00", 1,
                                   out, sizeof(out), width), -1);
    {
        const unsigned char binary[2] = {0x00, 0xFF};
        n = neverc_qp_encode(binary, sizeof(binary), out, sizeof(out), 4);
        ASSERT_EQ(n > 0, 1);
        if (n > 0) {
            unsigned char decoded[2];
            int dn = neverc_qp_decode(out, (size_t)n, decoded,
                                      sizeof(decoded));
            ASSERT_EQ(dn, 2);
            ASSERT_MEMEQ(decoded, binary, sizeof(binary));
        }
    }
}

static void test_roundtrip(void) {
    printf("[roundtrip]\n");
    const char *original = "Hello, this is a test with special chars: =, \t, and \x80\x90\xff end.";
    size_t orig_len = strlen(original);

    char encoded[4096];
    int elen = neverc_qp_encode((const unsigned char*)original, orig_len, encoded, sizeof(encoded), 76);
    ASSERT_EQ(elen > 0, 1);

    unsigned char decoded[4096];
    int dlen = neverc_qp_decode(encoded, (size_t)elen, decoded, sizeof(decoded));
    ASSERT_EQ((size_t)dlen, orig_len);
    ASSERT_MEMEQ(decoded, original, orig_len);
}

int main(void) {
    printf("=== NeverC mime/quotedprintable Tests ===\n");
    test_decode_basic();
    test_decode_soft_break();
    test_encode_basic();
    test_roundtrip();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
