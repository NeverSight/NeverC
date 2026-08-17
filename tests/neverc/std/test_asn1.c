#include "neverc/std/encoding/asn1.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}
static void check_int64(const char *name, int64_t got, int64_t expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %lld, expected %lld\n", name, (long long)got, (long long)expected); }
}
static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && expected && strcmp(got, expected) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got?got:"(null)", expected); }
}

static void test_decode_integer(void) {
    printf("[decode integer]\n");
    /* DER: 02 01 05 = INTEGER 5 */
    uint8_t data1[] = {0x02, 0x01, 0x05};
    neverc_asn1_element_t elem;
    int n = neverc_asn1_decode_element(data1, sizeof(data1), &elem);
    check_int("decode ok", n > 0, 1);
    check_int("tag int", elem.tag_number, NEVERC_ASN1_INTEGER);
    int64_t val;
    neverc_asn1_decode_int64(&elem, &val);
    check_int64("value 5", val, 5);

    /* DER: 02 01 FF = INTEGER -1 */
    uint8_t data2[] = {0x02, 0x01, 0xFF};
    n = neverc_asn1_decode_element(data2, sizeof(data2), &elem);
    neverc_asn1_decode_int64(&elem, &val);
    check_int64("value -1", val, -1);

    /* DER: 02 02 01 00 = INTEGER 256 */
    uint8_t data3[] = {0x02, 0x02, 0x01, 0x00};
    n = neverc_asn1_decode_element(data3, sizeof(data3), &elem);
    neverc_asn1_decode_int64(&elem, &val);
    check_int64("value 256", val, 256);

    /* DER: 02 01 00 = INTEGER 0 */
    uint8_t data4[] = {0x02, 0x01, 0x00};
    n = neverc_asn1_decode_element(data4, sizeof(data4), &elem);
    neverc_asn1_decode_int64(&elem, &val);
    check_int64("value 0", val, 0);
    (void)n;
}

static void test_decode_bool(void) {
    printf("[decode boolean]\n");
    uint8_t data_true[] = {0x01, 0x01, 0xFF};
    uint8_t data_false[] = {0x01, 0x01, 0x00};

    neverc_asn1_element_t elem;
    int bval;

    neverc_asn1_decode_element(data_true, sizeof(data_true), &elem);
    neverc_asn1_decode_bool(&elem, &bval);
    check_int("true", bval, 1);

    neverc_asn1_decode_element(data_false, sizeof(data_false), &elem);
    neverc_asn1_decode_bool(&elem, &bval);
    check_int("false", bval, 0);
}

static void test_decode_oid(void) {
    printf("[decode OID]\n");
    /* OID 1.2.840.113549.1.1.1 (rsaEncryption) */
    uint8_t oid_data[] = {0x06, 0x09, 0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
    neverc_asn1_element_t elem;
    neverc_asn1_decode_element(oid_data, sizeof(oid_data), &elem);
    char *oid = neverc_asn1_decode_oid(&elem);
    check_str("oid rsa", oid, "1.2.840.113549.1.1.1");
    free(oid);

    /* OID 2.5.4.3 (commonName) */
    uint8_t oid2[] = {0x06, 0x03, 0x55, 0x04, 0x03};
    neverc_asn1_decode_element(oid2, sizeof(oid2), &elem);
    oid = neverc_asn1_decode_oid(&elem);
    check_str("oid cn", oid, "2.5.4.3");
    free(oid);

    /* The first two arcs form one base-128 subidentifier. Values above 79
     * require multiple bytes (2.999 => 1079 => 0x88 0x37). */
    uint8_t oid3[] = {0x06, 0x03, 0x88, 0x37, 0x03};
    neverc_asn1_decode_element(oid3, sizeof(oid3), &elem);
    oid = neverc_asn1_decode_oid(&elem);
    check_str("oid multi-byte first subidentifier", oid, "2.999.3");
    free(oid);

    uint8_t unterminated[] = {0x06, 0x02, 0x2A, 0x86};
    neverc_asn1_decode_element(unterminated, sizeof(unterminated), &elem);
    oid = neverc_asn1_decode_oid(&elem);
    check_int("reject unterminated OID arc", oid == NULL, 1);
    free(oid);
}

static void test_decode_rejects_malformed_lengths(void) {
    printf("[decode malformed lengths]\n");
    neverc_asn1_element_t elem;
    uint8_t bytes[2] = {0};
    check_int("reject NULL data",
              neverc_asn1_decode_element(NULL, 2, &elem), -1);
    check_int("reject NULL element",
              neverc_asn1_decode_element(bytes, sizeof(bytes), NULL), -1);

    uint8_t indefinite[] = {0x04, 0x80};
    memset(&elem, 0xa5, sizeof(elem));
    check_int("reject indefinite length",
              neverc_asn1_decode_element(
                  indefinite, sizeof(indefinite), &elem), -1);
    neverc_asn1_element_t empty_elem = {0};
    check_int("failed decode clears element",
              memcmp(&elem, &empty_elem, sizeof(elem)) == 0, 1);

    uint8_t indefinite_seq[] = {0x30, 0x80};
    check_int("reject indefinite SEQUENCE",
              neverc_asn1_decode_element(
                  indefinite_seq, sizeof(indefinite_seq), &elem), -1);

    /* High-tag form that overflows the int tag_number field (X.690 8.1.2.4). */
    uint8_t high_tag_overflow[] = {
        0x1f, 0xff, 0xff, 0xff, 0xff, 0x7f, 0x00
    };
    check_int("reject high-tag overflow",
              neverc_asn1_decode_element(
                  high_tag_overflow, sizeof(high_tag_overflow), &elem), -1);

    uint8_t high_tag_trunc[] = {0x1f, 0xff};
    check_int("reject truncated high-tag",
              neverc_asn1_decode_element(
                  high_tag_trunc, sizeof(high_tag_trunc), &elem), -1);

    uint8_t high_tag_too_small[] = {0x1f, 0x05, 0x00};
    check_int("reject high-tag for number < 31",
              neverc_asn1_decode_element(
                  high_tag_too_small, sizeof(high_tag_too_small), &elem), -1);

    uint8_t end_of_contents[] = {0x00, 0x00};
    check_int("reject BER end-of-contents marker",
              neverc_asn1_decode_element(
                  end_of_contents, sizeof(end_of_contents), &elem), -1);

    uint8_t noncanonical[] = {0x04, 0x81, 0x01, 0x00};
    check_int("reject noncanonical long length",
              neverc_asn1_decode_element(
                  noncanonical, sizeof(noncanonical), &elem), -1);

    uint8_t oversized[] = {0x04, 0x84, 0xff, 0xff, 0xff, 0xff};
    check_int("reject oversized value length",
              neverc_asn1_decode_element(
                  oversized, sizeof(oversized), &elem), -1);

    uint8_t five_byte_len[] = {0x04, 0x85, 0x01, 0x00, 0x00, 0x00, 0x00};
    check_int("reject 5-byte length form",
              neverc_asn1_decode_element(
                  five_byte_len, sizeof(five_byte_len), &elem), -1);

    uint8_t noncanonical_int[] = {0x02, 0x02, 0x00, 0x01};
    int64_t integer;
    check_int("decode redundant integer wrapper",
              neverc_asn1_decode_element(
                  noncanonical_int, sizeof(noncanonical_int), &elem),
              (int)sizeof(noncanonical_int));
    check_int("reject noncanonical integer",
              neverc_asn1_decode_int64(&elem, &integer), -1);

    /* 9-byte INTEGER 2^63. Fits in uint64_t, not int64_t; decode_int64
     * only accepts payloads of at most 8 bytes. */
    uint8_t uint64_min_out_of_range[] = {
        0x02, 0x09, 0x00, 0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    check_int("decode 2^63 wrapper",
              neverc_asn1_decode_element(uint64_min_out_of_range,
                                         sizeof(uint64_min_out_of_range),
                                         &elem),
              (int)sizeof(uint64_min_out_of_range));
    check_int("reject integer 2^63 as int64",
              neverc_asn1_decode_int64(&elem, &integer), -1);

    uint8_t noncanonical_bool[] = {0x01, 0x01, 0x01};
    int boolean;
    check_int("decode noncanonical boolean wrapper",
              neverc_asn1_decode_element(
                  noncanonical_bool, sizeof(noncanonical_bool), &elem),
              (int)sizeof(noncanonical_bool));
    check_int("reject noncanonical boolean",
              neverc_asn1_decode_bool(&elem, &boolean), -1);
}

static void test_encode_integer(void) {
    printf("[encode integer]\n");
    uint8_t buf[32];

    int n = neverc_asn1_encode_int64(buf, sizeof(buf), 5);
    check_int("enc 5 len", n, 3);
    check_int("enc 5 tag", buf[0], 0x02);
    check_int("enc 5 val", buf[2], 0x05);

    /* Roundtrip */
    neverc_asn1_element_t elem;
    neverc_asn1_decode_element(buf, n, &elem);
    int64_t val;
    neverc_asn1_decode_int64(&elem, &val);
    check_int64("roundtrip 5", val, 5);

    n = neverc_asn1_encode_int64(buf, sizeof(buf), 256);
    neverc_asn1_decode_element(buf, n, &elem);
    neverc_asn1_decode_int64(&elem, &val);
    check_int64("roundtrip 256", val, 256);

    n = neverc_asn1_encode_int64(buf, sizeof(buf), -128);
    neverc_asn1_decode_element(buf, n, &elem);
    neverc_asn1_decode_int64(&elem, &val);
    check_int64("roundtrip -128", val, -128);

    n = neverc_asn1_encode_int64(buf, sizeof(buf), 0);
    neverc_asn1_decode_element(buf, n, &elem);
    neverc_asn1_decode_int64(&elem, &val);
    check_int64("roundtrip 0", val, 0);

    static const struct {
        int64_t value;
        int encoded_len;
    } boundaries[] = {
        {INT64_MIN, 10},
        {-129, 4},
        {-128, 3},
        {-1, 3},
        {0, 3},
        {127, 3},
        {128, 4},
        {INT64_MAX, 10},
    };
    for (size_t i = 0; i < sizeof(boundaries) / sizeof(boundaries[0]); i++) {
        char name[64];
        n = neverc_asn1_encode_int64(
            buf, sizeof(buf), boundaries[i].value);
        snprintf(name, sizeof(name), "boundary %zu minimal length", i);
        check_int(name, n, boundaries[i].encoded_len);
        if (n > 0) {
            int consumed = neverc_asn1_decode_element(
                buf, (size_t)n, &elem);
            snprintf(name, sizeof(name), "boundary %zu wrapper", i);
            check_int(name, consumed, n);
            if (consumed == n) {
                int decode_status = neverc_asn1_decode_int64(&elem, &val);
                snprintf(name, sizeof(name), "boundary %zu decode", i);
                check_int(name, decode_status, 0);
                if (decode_status != 0)
                    continue;
                snprintf(name, sizeof(name), "boundary %zu roundtrip", i);
                check_int64(name, val, boundaries[i].value);
            }
        }
    }
}

static void test_encode_bool(void) {
    printf("[encode boolean]\n");
    uint8_t buf[8];
    int n = neverc_asn1_encode_bool(buf, sizeof(buf), 1);
    check_int("enc true len", n, 3);
    check_int("enc true tag", buf[0], 0x01);
    check_int("enc true val", buf[2], 0xFF);

    n = neverc_asn1_encode_bool(buf, sizeof(buf), 0);
    check_int("enc false val", buf[2], 0x00);
    (void)n;
}

static void test_encode_octet_string(void) {
    printf("[encode octet string]\n");
    uint8_t buf[64];
    const uint8_t data[] = "hello";
    int n = neverc_asn1_encode_octet_string(buf, sizeof(buf), data, 5);
    check_int("enc ostr tag", buf[0], 0x04);
    check_int("enc ostr len", buf[1], 5);

    neverc_asn1_element_t elem;
    neverc_asn1_decode_element(buf, n, &elem);
    check_int("dec ostr tag", elem.tag_number, NEVERC_ASN1_OCTET_STRING);
    check_int("dec ostr vlen", (int)elem.value_len, 5);
    tests_run++;
    if (memcmp(elem.value, "hello", 5) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: ostr value mismatch\n"); }
}

static void test_encode_null(void) {
    printf("[encode null]\n");
    uint8_t buf[8];
    int n = neverc_asn1_encode_null(buf, sizeof(buf));
    check_int("enc null len", n, 2);
    check_int("enc null tag", buf[0], 0x05);
    check_int("enc null vlen", buf[1], 0x00);
}

static void test_helpers_and_invalid_api(void) {
    printf("[helpers and invalid API]\n");
    uint8_t buf[16];

    int n = neverc_asn1_encode_tag(
        buf, sizeof(buf), NEVERC_ASN1_UNIVERSAL, 0, 31);
    check_int("high tag length", n, 2);
    check_int("high tag marker", buf[0], 0x1f);
    check_int("high tag value", buf[1], 0x1f);
    check_int("reject negative tag",
              neverc_asn1_encode_tag(
                  buf, sizeof(buf), NEVERC_ASN1_UNIVERSAL, 0, -1), -1);
    {
        uint8_t scratch[1] = {0xaa};
        check_int("high tag short cap",
                  neverc_asn1_encode_tag(
                      scratch, 1, NEVERC_ASN1_UNIVERSAL, 0, 31), -1);
        check_int("high tag short cap is fail-closed", scratch[0], 0xaa);
    }
    {
        uint8_t scratch[1] = {0xaa};
        check_int("int64 short cap",
                  neverc_asn1_encode_int64(scratch, 1, 5), -1);
        check_int("int64 short cap is fail-closed", scratch[0], 0xaa);
    }
    {
        uint8_t scratch[1] = {0xaa};
        const uint8_t payload[] = {1, 2, 3};
        check_int("octet string short cap",
                  neverc_asn1_encode_octet_string(
                      scratch, 1, payload, sizeof(payload)), -1);
        check_int("octet string short cap is fail-closed", scratch[0], 0xaa);
    }
    {
        uint8_t scratch[1] = {0xaa};
        check_int("oid short cap",
                  neverc_asn1_encode_oid(scratch, 1, "1.2.3"), -1);
        check_int("oid short cap is fail-closed", scratch[0], 0xaa);
    }
    check_int("reject DER end-of-contents tag",
              neverc_asn1_encode_tag(
                  buf, sizeof(buf), NEVERC_ASN1_UNIVERSAL, 0, 0), -1);
    check_int("reject NULL tag buffer",
              neverc_asn1_encode_tag(
                  NULL, sizeof(buf), NEVERC_ASN1_UNIVERSAL, 0, 1), -1);

    n = neverc_asn1_encode_length(buf, sizeof(buf), 127);
    check_int("short length size", n, 1);
    check_int("short length value", buf[0], 127);
    n = neverc_asn1_encode_length(buf, sizeof(buf), 128);
    check_int("long length size", n, 2);
    check_int("long length marker", buf[0], 0x81);
    check_int("long length value", buf[1], 0x80);
    check_int("reject short length capacity",
              neverc_asn1_encode_length(buf, 1, 128), -1);
    check_int("reject unrepresentable length",
              neverc_asn1_encode_length(
                  buf, sizeof(buf), (size_t)INT_MAX + 1U), -1);

    neverc_asn1_element_t elem = {
        .tag_class = NEVERC_ASN1_UNIVERSAL,
        .tag_number = NEVERC_ASN1_INTEGER,
        .constructed = 0,
        .value = NULL,
        .value_len = 1,
        .full = NULL,
        .full_len = 0
    };
    int64_t value;
    check_int("reject NULL integer value",
              neverc_asn1_decode_int64(&elem, &value), -1);
    check_int("reject NULL integer element",
              neverc_asn1_decode_int64(NULL, &value), -1);
    check_int("reject NULL bool output",
              neverc_asn1_decode_bool(&elem, NULL), -1);
    check_int("reject NULL boolean buffer",
              neverc_asn1_encode_bool(NULL, 3, 1), -1);
    check_int("reject NULL octet data",
              neverc_asn1_encode_octet_string(buf, sizeof(buf), NULL, 1), -1);
    check_int("reject oversized octet length",
              neverc_asn1_encode_octet_string(
                  buf, sizeof(buf), buf, (size_t)INT_MAX + 1U), -1);
    check_int("reject INT_MAX octet result overflow",
              neverc_asn1_encode_octet_string(
                  buf, sizeof(buf), buf, (size_t)INT_MAX), -1);
    check_int("reject NULL encoding buffer",
              neverc_asn1_encode_null(NULL, 2), -1);

    uint8_t value_byte = 1;
    elem.value = &value_byte;
    elem.tag_class = NEVERC_ASN1_CONTEXT;
    check_int("reject context-specific integer",
              neverc_asn1_decode_int64(&elem, &value), -1);
    elem.tag_class = NEVERC_ASN1_UNIVERSAL;
    elem.constructed = 1;
    check_int("reject constructed integer",
              neverc_asn1_decode_int64(&elem, &value), -1);
    elem.constructed = 0;
    elem.tag_number = NEVERC_ASN1_BOOLEAN;
    value_byte = 0xff;
    check_int("accept primitive boolean",
              neverc_asn1_decode_bool(&elem, &n), 0);
    elem.tag_class = NEVERC_ASN1_CONTEXT;
    check_int("reject context-specific boolean",
              neverc_asn1_decode_bool(&elem, &n), -1);
    elem.tag_number = NEVERC_ASN1_OID;
    check_int("reject context-specific OID",
              neverc_asn1_decode_oid(&elem) == NULL, 1);
}

static void test_oid_bit_string_and_text(void) {
    printf("[oid/bit string/text]\n");
    uint8_t buf[64];
    neverc_asn1_element_t elem;

    int n = neverc_asn1_encode_oid(buf, sizeof(buf), "1.2.840.113549.1.1.1");
    check_int("encode rsa OID len", n, 11);
    check_int("encode rsa OID tag", buf[0], 0x06);
    neverc_asn1_decode_element(buf, (size_t)n, &elem);
    char *oid = neverc_asn1_decode_oid(&elem);
    check_str("encode/decode rsa OID", oid, "1.2.840.113549.1.1.1");
    free(oid);

    n = neverc_asn1_encode_oid(buf, sizeof(buf), "2.999.3");
    check_int("encode 2.999.3", n > 0, 1);
    neverc_asn1_decode_element(buf, (size_t)n, &elem);
    oid = neverc_asn1_decode_oid(&elem);
    check_str("roundtrip 2.999.3", oid, "2.999.3");
    free(oid);

    check_int("reject leading-zero OID arc",
              neverc_asn1_encode_oid(buf, sizeof(buf), "01.2"), -1);
    check_int("reject first arc 3",
              neverc_asn1_encode_oid(buf, sizeof(buf), "3.0"), -1);
    check_int("reject first arc 0 with second 40",
              neverc_asn1_encode_oid(buf, sizeof(buf), "0.40"), -1);
    check_int("reject trailing dot",
              neverc_asn1_encode_oid(buf, sizeof(buf), "1.2."), -1);

    const uint8_t bits[] = {0x0a, 0x80};
    n = neverc_asn1_encode_bit_string(buf, sizeof(buf), bits, sizeof(bits), 1);
    check_int("bit string len", n, 5);
    check_int("bit string tag", buf[0], NEVERC_ASN1_BIT_STRING);
    neverc_asn1_decode_element(buf, (size_t)n, &elem);
    const uint8_t *payload = NULL;
    size_t payload_len = 0;
    int unused = -1;
    check_int("decode bit string",
              neverc_asn1_decode_bit_string(&elem, &payload, &payload_len,
                                            &unused),
              0);
    check_int("bit string unused", unused, 1);
    check_int("bit string bytes", (int)payload_len, 2);

    uint8_t dirty[] = {0x03, 0x02, 0x01, 0x01}; /* unused bit not zero */
    neverc_asn1_decode_element(dirty, sizeof(dirty), &elem);
    check_int("reject non-zero unused bits",
              neverc_asn1_decode_bit_string(&elem, &payload, &payload_len,
                                            &unused),
              -1);
    {
        const uint8_t bad[] = {0x01};
        check_int("reject dirty unused on encode",
                  neverc_asn1_encode_bit_string(buf, sizeof(buf), bad, 1, 1),
                  -1);
    }

    uint8_t constructed_bits[] = {0x23, 0x03, 0x00, 0x0a, 0x80};
    neverc_asn1_decode_element(constructed_bits, sizeof(constructed_bits),
                               &elem);
    check_int("reject constructed BIT STRING",
              neverc_asn1_decode_bit_string(&elem, &payload, &payload_len,
                                            &unused),
              -1);

    const uint8_t hello[] = "hello";
    n = neverc_asn1_encode_utf8_string(buf, sizeof(buf), hello, 5);
    check_int("utf8 tag", buf[0], NEVERC_ASN1_UTF8_STRING);
    neverc_asn1_decode_element(buf, (size_t)n, &elem);
    const uint8_t *text = NULL;
    size_t tlen = 0;
    check_int("decode utf8",
              neverc_asn1_decode_utf8_string(&elem, &text, &tlen), 0);
    check_int("utf8 len", (int)tlen, 5);

    static const uint8_t overlong[] = {0xc0, 0xaf};
    check_int("reject overlong utf8 encode",
              neverc_asn1_encode_utf8_string(buf, sizeof(buf), overlong, 2),
              -1);
    {
        uint8_t overlong_tlv[] = {0x0c, 0x02, 0xc0, 0xaf};
        neverc_asn1_decode_element(overlong_tlv, sizeof(overlong_tlv), &elem);
        check_int("reject overlong utf8 decode",
                  neverc_asn1_decode_utf8_string(&elem, &text, &tlen), -1);
        uint8_t surrogate_tlv[] = {0x0c, 0x03, 0xed, 0xa0, 0x80};
        neverc_asn1_decode_element(surrogate_tlv, sizeof(surrogate_tlv),
                                   &elem);
        check_int("reject utf8 surrogate decode",
                  neverc_asn1_decode_utf8_string(&elem, &text, &tlen), -1);
    }

    n = neverc_asn1_encode_printable_string(buf, sizeof(buf),
                                            (const uint8_t *)"CN", 2);
    neverc_asn1_decode_element(buf, (size_t)n, &elem);
    check_int("decode printable",
              neverc_asn1_decode_printable_string(&elem, &text, &tlen), 0);
    check_int("reject star in printable",
              neverc_asn1_encode_printable_string(
                  buf, sizeof(buf), (const uint8_t *)"*", 1),
              -1);

    n = neverc_asn1_encode_ia5_string(buf, sizeof(buf),
                                      (const uint8_t *)"user@host", 9);
    neverc_asn1_decode_element(buf, (size_t)n, &elem);
    check_int("decode ia5",
              neverc_asn1_decode_ia5_string(&elem, &text, &tlen), 0);
    static const uint8_t hi_bit[] = {0x80};
    check_int("reject 8-bit ia5",
              neverc_asn1_encode_ia5_string(buf, sizeof(buf), hi_bit, 1),
              -1);

    uint8_t trailing[] = {0x02, 0x01, 0x05, 0x00};
    n = neverc_asn1_decode_element(trailing, sizeof(trailing), &elem);
    check_int("element stops before trailing data", n, 3);
}

int main(void) {
    printf("=== NeverC Encoding/ASN1 Module Tests ===\n\n");
    test_decode_integer();
    test_decode_bool();
    test_decode_oid();
    test_decode_rejects_malformed_lengths();
    test_encode_integer();
    test_encode_bool();
    test_encode_octet_string();
    test_encode_null();
    test_helpers_and_invalid_api();
    test_oid_bit_string_and_text();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
