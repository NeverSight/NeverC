#include "neverc/encoding/asn1.h"
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

int main(void) {
    printf("=== NeverC Encoding/ASN1 Module Tests ===\n\n");
    test_decode_integer();
    test_decode_bool();
    test_decode_oid();
    test_encode_integer();
    test_encode_bool();
    test_encode_octet_string();
    test_encode_null();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
