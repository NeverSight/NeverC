/*
 * X.509 certificate parser.
 * Parses DER-encoded certificates (ASN.1 structure).
 * Self-contained implementation without external ASN.1 library dependency.
 */
#include "neverc/std/crypto/x509.h"
#include "x509_internal.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

/* ===== Minimal ASN.1 DER Reader ===== */

typedef struct {
    const uint8_t *data;
    size_t         len;
    size_t         pos;
} asn1_reader_t;

#define ASN1_TAG_INTEGER       0x02
#define ASN1_TAG_BOOLEAN       0x01
#define ASN1_TAG_BIT_STRING    0x03
#define ASN1_TAG_OCTET_STRING  0x04
#define ASN1_TAG_NULL          0x05
#define ASN1_TAG_OID           0x06
#define ASN1_TAG_UTF8STRING    0x0C
#define ASN1_TAG_PRINTABLE_STR 0x13
#define ASN1_TAG_IA5STRING     0x16
#define ASN1_TAG_UTCTIME       0x17
#define ASN1_TAG_GENERALTIME   0x18
#define ASN1_TAG_SEQUENCE      0x30
#define ASN1_TAG_SET           0x31
#define ASN1_TAG_CONTEXT_0     0xA0
#define ASN1_TAG_CONTEXT_1     0xA1
#define ASN1_TAG_CONTEXT_1_IMPLICIT 0x81
#define ASN1_TAG_CONTEXT_2     0xA2
#define ASN1_TAG_CONTEXT_2_IMPLICIT 0x82
#define ASN1_TAG_CONTEXT_3     0xA3

static int asn1_read_tag(asn1_reader_t *r, uint8_t *tag) {
    if (r->pos >= r->len) return -1;
    *tag = r->data[r->pos++];
    return 0;
}

static int asn1_read_length(asn1_reader_t *r, size_t *length) {
    if (r->pos >= r->len) return -1;
    uint8_t first = r->data[r->pos++];
    if (first < 0x80) {
        *length = first;
    } else {
        int nbytes = first & 0x7f;
        if (nbytes == 0 || nbytes > 4 ||
            (size_t)nbytes > r->len - r->pos)
            return -1;
        if (r->data[r->pos] == 0) return -1;
        *length = 0;
        for (int i = 0; i < nbytes; i++) {
            if (*length > (SIZE_MAX >> 8)) return -1;
            *length = (*length << 8) | r->data[r->pos++];
        }
        if (*length < 0x80) return -1;
    }
    return 0;
}

static int asn1_read_tlv(asn1_reader_t *r, uint8_t *tag,
                          const uint8_t **value, size_t *vlen) {
    if (asn1_read_tag(r, tag) < 0) return -1;
    if (asn1_read_length(r, vlen) < 0) return -1;
    if (*vlen > r->len - r->pos) return -1;
    *value = r->data + r->pos;
    r->pos += *vlen;
    return 0;
}

static int asn1_enter_sequence(asn1_reader_t *r, asn1_reader_t *inner) {
    uint8_t tag;
    const uint8_t *val;
    size_t vlen;
    if (asn1_read_tlv(r, &tag, &val, &vlen) < 0) return -1;
    if (tag != ASN1_TAG_SEQUENCE) return -1;
    inner->data = val;
    inner->len = vlen;
    inner->pos = 0;
    return 0;
}

static int valid_implicit_bit_string(const uint8_t *value, size_t len) {
    if (!value || len == 0 || value[0] > 7)
        return 0;
    if (len == 1)
        return value[0] == 0;
    return value[0] == 0 ||
           (value[len - 1] & ((1u << value[0]) - 1u)) == 0;
}

static int copy_string_value(char *dst, size_t dstsz,
                             const uint8_t *src, size_t srclen) {
    if (!dst || dstsz == 0 || (!src && srclen != 0))
        return -1;
    /* Truncation or an embedded NUL would hide bytes from C-string
     * consumers, including CN-as-DNS name constraint checks. */
    if (srclen >= dstsz)
        return -1;
    for (size_t i = 0; i < srclen; ++i) {
        if (src[i] == 0)
            return -1;
    }
    if (srclen > 0)
        memcpy(dst, src, srclen);
    dst[srclen] = '\0';
    return 0;
}

/* ===== OID Matching ===== */

static int oid_equals(const uint8_t *oid, size_t oid_len,
                       const uint8_t *expected, size_t exp_len) {
    return oid_len == exp_len && memcmp(oid, expected, oid_len) == 0;
}

/* id-at-commonName: 2.5.4.3 */
static const uint8_t OID_CN[] = {0x55, 0x04, 0x03};
/* id-at-countryName: 2.5.4.6 */
static const uint8_t OID_C[] = {0x55, 0x04, 0x06};
/* id-at-organizationName: 2.5.4.10 */
static const uint8_t OID_O[] = {0x55, 0x04, 0x0A};
/* id-at-organizationalUnitName: 2.5.4.11 */
static const uint8_t OID_OU[] = {0x55, 0x04, 0x0B};
/* id-at-localityName: 2.5.4.7 */
static const uint8_t OID_L[] = {0x55, 0x04, 0x07};
/* id-at-stateOrProvinceName: 2.5.4.8 */
static const uint8_t OID_ST[] = {0x55, 0x04, 0x08};
/* id-at-serialNumber: 2.5.4.5 */
static const uint8_t OID_SN[] = {0x55, 0x04, 0x05};

/* sha256WithRSAEncryption: 1.2.840.113549.1.1.11 */
static const uint8_t OID_SHA256_RSA[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0B};
/* sha1WithRSAEncryption: 1.2.840.113549.1.1.5 */
static const uint8_t OID_SHA1_RSA[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x05};
/* sha384WithRSAEncryption: 1.2.840.113549.1.1.12 */
static const uint8_t OID_SHA384_RSA[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0C};
/* sha512WithRSAEncryption: 1.2.840.113549.1.1.13 */
static const uint8_t OID_SHA512_RSA[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0D};
/* ecdsaWithSHA256: 1.2.840.10045.4.3.2 */
static const uint8_t OID_ECDSA_SHA256[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x02};
/* ecdsaWithSHA384: 1.2.840.10045.4.3.3 */
static const uint8_t OID_ECDSA_SHA384[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x04, 0x03, 0x03};
/* ed25519: 1.3.101.112 */
static const uint8_t OID_ED25519[] = {0x2B, 0x65, 0x70};
/* rsassa-pss: 1.2.840.113549.1.1.10 */
static const uint8_t OID_RSA_PSS[] =
    {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x0A};
/* id-mgf1: 1.2.840.113549.1.1.8 */
static const uint8_t OID_MGF1[] =
    {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x08};
/* SHA-256/384/512: 2.16.840.1.101.3.4.2.{1,2,3} */
static const uint8_t OID_SHA256[] =
    {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x01};
static const uint8_t OID_SHA384[] =
    {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x02};
static const uint8_t OID_SHA512[] =
    {0x60, 0x86, 0x48, 0x01, 0x65, 0x03, 0x04, 0x02, 0x03};

/* rsaEncryption: 1.2.840.113549.1.1.1 */
static const uint8_t OID_RSA[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
/* ecPublicKey: 1.2.840.10045.2.1 */
static const uint8_t OID_EC[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};
/* prime256v1: 1.2.840.10045.3.1.7 */
static const uint8_t OID_P256[] =
    {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x03, 0x01, 0x07};
/* secp384r1: 1.3.132.0.34 */
static const uint8_t OID_P384[] = {0x2B, 0x81, 0x04, 0x00, 0x22};

/* basicConstraints: 2.5.29.19 */
static const uint8_t OID_BASIC_CONSTRAINTS[] = {0x55, 0x1D, 0x13};
/* subjectAltName: 2.5.29.17 */
static const uint8_t OID_SUBJECT_ALT_NAME[] = {0x55, 0x1D, 0x11};
/* keyUsage: 2.5.29.15 */
static const uint8_t OID_KEY_USAGE[] = {0x55, 0x1D, 0x0F};
/* extKeyUsage: 2.5.29.37 */
static const uint8_t OID_EXT_KEY_USAGE[] = {0x55, 0x1D, 0x25};
/* nameConstraints: 2.5.29.30 */
static const uint8_t OID_NAME_CONSTRAINTS[] = {0x55, 0x1D, 0x1E};
/* id-kp-serverAuth/clientAuth/codeSigning/emailProtection/timeStamping/OCSP */
static const uint8_t OID_EKU_SERVER_AUTH[] =
    {0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x01};
static const uint8_t OID_EKU_CLIENT_AUTH[] =
    {0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x02};
static const uint8_t OID_EKU_CODE_SIGNING[] =
    {0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x03};
static const uint8_t OID_EKU_EMAIL_PROTECTION[] =
    {0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x04};
static const uint8_t OID_EKU_TIME_STAMPING[] =
    {0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x08};
static const uint8_t OID_EKU_OCSP_SIGNING[] =
    {0x2B, 0x06, 0x01, 0x05, 0x05, 0x07, 0x03, 0x09};
/* RFC 5280: anyExtendedKeyUsage ::= { id-ce-extKeyUsage 0 } = 2.5.29.37.0 */
static const uint8_t OID_EKU_ANY[] = {0x55, 0x1D, 0x25, 0x00};

static int identify_sig_algorithm(const uint8_t *oid, size_t len) {
    if (oid_equals(oid, len, OID_SHA256_RSA, sizeof(OID_SHA256_RSA)))
        return NEVERC_X509_SIG_SHA256_RSA;
    if (oid_equals(oid, len, OID_SHA1_RSA, sizeof(OID_SHA1_RSA)))
        return NEVERC_X509_SIG_SHA1_RSA;
    if (oid_equals(oid, len, OID_SHA384_RSA, sizeof(OID_SHA384_RSA)))
        return NEVERC_X509_SIG_SHA384_RSA;
    if (oid_equals(oid, len, OID_SHA512_RSA, sizeof(OID_SHA512_RSA)))
        return NEVERC_X509_SIG_SHA512_RSA;
    if (oid_equals(oid, len, OID_ECDSA_SHA256, sizeof(OID_ECDSA_SHA256)))
        return NEVERC_X509_SIG_ECDSA_SHA256;
    if (oid_equals(oid, len, OID_ECDSA_SHA384, sizeof(OID_ECDSA_SHA384)))
        return NEVERC_X509_SIG_ECDSA_SHA384;
    if (oid_equals(oid, len, OID_ED25519, sizeof(OID_ED25519)))
        return NEVERC_X509_SIG_ED25519;
    return 0;
}

static int parse_digest_algorithm(asn1_reader_t *reader) {
    asn1_reader_t sequence;
    if (asn1_enter_sequence(reader, &sequence) != 0)
        return 0;

    uint8_t tag;
    const uint8_t *oid;
    size_t oid_len;
    if (asn1_read_tlv(&sequence, &tag, &oid, &oid_len) != 0 ||
        tag != ASN1_TAG_OID)
        return 0;

    int hash = 0;
    if (oid_equals(oid, oid_len, OID_SHA256, sizeof(OID_SHA256)))
        hash = 256;
    else if (oid_equals(oid, oid_len, OID_SHA384, sizeof(OID_SHA384)))
        hash = 384;
    else if (oid_equals(oid, oid_len, OID_SHA512, sizeof(OID_SHA512)))
        hash = 512;
    else
        return 0;

    if (sequence.pos < sequence.len) {
        const uint8_t *parameters;
        size_t parameters_len;
        if (asn1_read_tlv(&sequence, &tag, &parameters,
                          &parameters_len) != 0 ||
            tag != ASN1_TAG_NULL || parameters_len != 0)
            return 0;
        (void)parameters;
    }
    return sequence.pos == sequence.len ? hash : 0;
}

static int parse_der_uint(const uint8_t *value, size_t value_len,
                          int *out) {
    if (!value || !out || value_len == 0 || value_len > sizeof(int) ||
        (value[0] & 0x80) != 0 ||
        (value_len > 1 && value[0] == 0 && (value[1] & 0x80) == 0))
        return -1;
    unsigned result = 0;
    for (size_t i = 0; i < value_len; ++i)
        result = (result << 8) | value[i];
    if (result > (unsigned)INT_MAX)
        return -1;
    *out = (int)result;
    return 0;
}

/* RFC 4055 RSASSA-PSS-params. SHA-1 defaults are rejected. Salt length
 * must equal the hash length so it matches neverc_rsa_verify_pss_sha*. */
static int parse_pss_parameters(const uint8_t *data, size_t len) {
    if (!data || len == 0)
        return 0;

    asn1_reader_t params = {data, len, 0};
    int hash = 0;
    int mgf_hash = 0;
    int salt_len = 20;
    int trailer = 1;
    uint8_t last_tag = 0;

    while (params.pos < params.len) {
        uint8_t tag;
        const uint8_t *value;
        size_t value_len;
        if (asn1_read_tlv(&params, &tag, &value, &value_len) != 0 ||
            tag <= last_tag)
            return 0;
        last_tag = tag;
        asn1_reader_t inner = {value, value_len, 0};

        if (tag == ASN1_TAG_CONTEXT_0) {
            hash = parse_digest_algorithm(&inner);
            if (hash == 0 || inner.pos != inner.len)
                return 0;
        } else if (tag == ASN1_TAG_CONTEXT_1) {
            asn1_reader_t mgf;
            uint8_t mgf_tag;
            const uint8_t *mgf_oid;
            size_t mgf_oid_len;
            if (asn1_enter_sequence(&inner, &mgf) != 0 ||
                asn1_read_tlv(&mgf, &mgf_tag, &mgf_oid,
                              &mgf_oid_len) != 0 ||
                mgf_tag != ASN1_TAG_OID ||
                !oid_equals(mgf_oid, mgf_oid_len, OID_MGF1,
                            sizeof(OID_MGF1)))
                return 0;
            mgf_hash = parse_digest_algorithm(&mgf);
            if (mgf_hash == 0 || mgf.pos != mgf.len ||
                inner.pos != inner.len)
                return 0;
        } else if (tag == ASN1_TAG_CONTEXT_2) {
            uint8_t integer_tag;
            const uint8_t *integer;
            size_t integer_len;
            if (asn1_read_tlv(&inner, &integer_tag, &integer,
                              &integer_len) != 0 ||
                integer_tag != ASN1_TAG_INTEGER ||
                parse_der_uint(integer, integer_len, &salt_len) != 0 ||
                inner.pos != inner.len)
                return 0;
        } else if (tag == ASN1_TAG_CONTEXT_3) {
            uint8_t integer_tag;
            const uint8_t *integer;
            size_t integer_len;
            if (asn1_read_tlv(&inner, &integer_tag, &integer,
                              &integer_len) != 0 ||
                integer_tag != ASN1_TAG_INTEGER ||
                parse_der_uint(integer, integer_len, &trailer) != 0 ||
                trailer != 1 || inner.pos != inner.len)
                return 0;
        } else {
            return 0;
        }
    }

    if (hash == 0 || mgf_hash == 0 || mgf_hash != hash || trailer != 1)
        return 0;
    if (hash == 256 && salt_len == 32)
        return NEVERC_X509_SIG_RSA_PSS_SHA256;
    if (hash == 384 && salt_len == 48)
        return NEVERC_X509_SIG_RSA_PSS_SHA384;
    if (hash == 512 && salt_len == 64)
        return NEVERC_X509_SIG_RSA_PSS_SHA512;
    return 0;
}

static int identify_key_algorithm(const uint8_t *oid, size_t len) {
    if (oid_equals(oid, len, OID_RSA, sizeof(OID_RSA)))
        return NEVERC_X509_KEY_RSA;
    if (oid_equals(oid, len, OID_EC, sizeof(OID_EC)))
        return NEVERC_X509_KEY_ECDSA;
    if (oid_equals(oid, len, OID_ED25519, sizeof(OID_ED25519)))
        return NEVERC_X509_KEY_ED25519;
    return 0;
}

static int parse_public_key_algorithm(asn1_reader_t *reader,
                                      neverc_x509_cert_t *cert) {
    asn1_reader_t sequence;
    if (asn1_enter_sequence(reader, &sequence) != 0)
        return -1;

    uint8_t tag;
    const uint8_t *oid;
    size_t oid_len;
    if (asn1_read_tlv(&sequence, &tag, &oid, &oid_len) != 0 ||
        tag != ASN1_TAG_OID)
        return -1;
    cert->key_algorithm = identify_key_algorithm(oid, oid_len);
    cert->public_key_curve = NEVERC_X509_CURVE_NONE;

    if (cert->key_algorithm == NEVERC_X509_KEY_RSA) {
        const uint8_t *parameters;
        size_t parameters_len;
        if (asn1_read_tlv(&sequence, &tag, &parameters,
                          &parameters_len) != 0 ||
            tag != ASN1_TAG_NULL || parameters_len != 0)
            return -1;
        (void)parameters;
    } else if (cert->key_algorithm == NEVERC_X509_KEY_ECDSA) {
        const uint8_t *curve;
        size_t curve_len;
        if (asn1_read_tlv(&sequence, &tag, &curve, &curve_len) != 0 ||
            tag != ASN1_TAG_OID)
            return -1;
        if (oid_equals(curve, curve_len, OID_P256, sizeof(OID_P256)))
            cert->public_key_curve = NEVERC_X509_CURVE_P256;
        else if (oid_equals(curve, curve_len, OID_P384,
                            sizeof(OID_P384)))
            cert->public_key_curve = NEVERC_X509_CURVE_P384;
        else
            return -1;
    } else if (cert->key_algorithm != NEVERC_X509_KEY_ED25519) {
        return -1;
    }

    return sequence.pos == sequence.len ? 0 : -1;
}

static int parse_signature_algorithm(asn1_reader_t *reader, int *algorithm,
                                     const uint8_t **raw, size_t *raw_len) {
    size_t start = reader->pos;
    asn1_reader_t sequence;
    if (asn1_enter_sequence(reader, &sequence) < 0)
        return -1;
    uint8_t tag;
    const uint8_t *oid;
    size_t oid_len;
    if (asn1_read_tlv(&sequence, &tag, &oid, &oid_len) < 0 ||
        tag != ASN1_TAG_OID)
        return -1;

    int identified = 0;
    if (oid_equals(oid, oid_len, OID_RSA_PSS, sizeof(OID_RSA_PSS))) {
        const uint8_t *parameters;
        size_t parameters_len;
        if (sequence.pos >= sequence.len ||
            asn1_read_tlv(&sequence, &tag, &parameters,
                          &parameters_len) < 0 ||
            tag != ASN1_TAG_SEQUENCE)
            return -1;
        identified = parse_pss_parameters(parameters, parameters_len);
        if (identified == 0)
            return -1;
    } else {
        identified = identify_sig_algorithm(oid, oid_len);
        if (identified == 0) return -1;

        if (sequence.pos < sequence.len) {
            int rsa_algorithm =
                identified == NEVERC_X509_SIG_SHA1_RSA ||
                identified == NEVERC_X509_SIG_SHA256_RSA ||
                identified == NEVERC_X509_SIG_SHA384_RSA ||
                identified == NEVERC_X509_SIG_SHA512_RSA;
            const uint8_t *parameters;
            size_t parameters_len;
            if (!rsa_algorithm ||
                asn1_read_tlv(&sequence, &tag, &parameters,
                              &parameters_len) < 0 ||
                tag != ASN1_TAG_NULL || parameters_len != 0)
                return -1;
            (void)parameters;
        }
    }
    if (sequence.pos != sequence.len) return -1;
    *algorithm = identified;
    if (raw)
        *raw = reader->data + start;
    if (raw_len)
        *raw_len = reader->pos - start;
    return 0;
}

/* ===== Extension Parsing ===== */

#define X509_MAX_SAN_ENTRIES 256U
#define X509_MAX_DNS_NAME_LEN 253U

static int append_dns_name(neverc_x509_cert_t *cert,
                           const uint8_t *value, size_t len) {
    if (!cert || !value || len == 0 || len > X509_MAX_DNS_NAME_LEN ||
        cert->dns_name_count >= X509_MAX_SAN_ENTRIES)
        return -1;
    for (size_t i = 0; i < len; ++i) {
        if (value[i] == 0 || value[i] > 0x7f)
            return -1;
    }
    if (cert->dns_name_count == SIZE_MAX / sizeof(*cert->dns_names))
        return -1;

    char *name = (char *)malloc(len + 1);
    if (!name) return -1;
    memcpy(name, value, len);
    name[len] = '\0';

    size_t count = cert->dns_name_count + 1;
    char **names = (char **)realloc(
        cert->dns_names, count * sizeof(*cert->dns_names));
    if (!names) {
        free(name);
        return -1;
    }
    cert->dns_names = names;
    cert->dns_names[cert->dns_name_count] = name;
    cert->dns_name_count = count;
    return 0;
}

static int x509_ip_is_v4_mapped(const uint8_t *ip, size_t len) {
    static const uint8_t prefix[12] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff
    };
    return len == 16 && ip && memcmp(ip, prefix, 12) == 0;
}

static int append_ip_address(neverc_x509_cert_t *cert,
                             const uint8_t *value, size_t len) {
    if (!cert || !value || (len != 4 && len != 16) ||
        x509_ip_is_v4_mapped(value, len) ||
        cert->ip_address_count >= X509_MAX_SAN_ENTRIES ||
        cert->ip_address_count ==
            SIZE_MAX / sizeof(*cert->ip_addresses))
        return -1;

    size_t count = cert->ip_address_count + 1;
    neverc_x509_ip_address_t *addresses =
        (neverc_x509_ip_address_t *)realloc(
            cert->ip_addresses,
            count * sizeof(*cert->ip_addresses));
    if (!addresses) return -1;
    cert->ip_addresses = addresses;
    neverc_x509_ip_address_t *address =
        &cert->ip_addresses[cert->ip_address_count];
    memset(address, 0, sizeof(*address));
    memcpy(address->bytes, value, len);
    address->len = (uint8_t)len;
    cert->ip_address_count = count;
    return 0;
}

/* Returns 1 when the SAN parsed but no dNSName/iPAddress was stored
 * (Go crypto/x509: a critical SAN with only skipped GeneralNames is
 * unhandled). Negative is a parse error. */
static int parse_subject_alt_name(neverc_x509_cert_t *cert,
                                  const uint8_t *data, size_t len) {
    asn1_reader_t wrapper = {data, len, 0};
    asn1_reader_t names;
    size_t extracted = 0;
    if (asn1_enter_sequence(&wrapper, &names) < 0 ||
        wrapper.pos != wrapper.len || names.len == 0)
        return -1;

    while (names.pos < names.len) {
        uint8_t tag;
        const uint8_t *value;
        size_t value_len;
        if (asn1_read_tlv(&names, &tag, &value, &value_len) < 0)
            return -1;
        if (tag == 0x82) {
            if (append_dns_name(cert, value, value_len) != 0)
                return -1;
            extracted++;
        } else if (tag == 0x87) {
            if (append_ip_address(cert, value, value_len) != 0)
                return -1;
            extracted++;
        }
    }
    return extracted > 0 ? 0 : 1;
}

static int parse_basic_constraints(neverc_x509_cert_t *cert,
                                   const uint8_t *data, size_t len) {
    asn1_reader_t wrapper = {data, len, 0};
    asn1_reader_t constraints;
    if (asn1_enter_sequence(&wrapper, &constraints) < 0 ||
        wrapper.pos != wrapper.len)
        return -1;
    cert->basic_constraints_valid = 1;
    cert->max_path_len = -1;

    if (constraints.pos < constraints.len &&
        constraints.data[constraints.pos] == ASN1_TAG_BOOLEAN) {
        uint8_t tag;
        const uint8_t *value;
        size_t value_len;
        if (asn1_read_tlv(&constraints, &tag, &value,
                          &value_len) < 0 ||
            value_len != 1 ||
            (value[0] != 0x00 && value[0] != 0xff))
            return -1;
        cert->is_ca = value[0] != 0;
    }

    if (constraints.pos < constraints.len) {
        uint8_t tag;
        const uint8_t *value;
        size_t value_len;
        if (asn1_read_tlv(&constraints, &tag, &value,
                          &value_len) < 0 ||
            tag != ASN1_TAG_INTEGER || value_len == 0 ||
            value_len > sizeof(int) ||
            (value[0] & 0x80) != 0 ||
            (value_len > 1 && value[0] == 0 &&
             (value[1] & 0x80) == 0) ||
            !cert->is_ca)
            return -1;
        unsigned path_len = 0;
        for (size_t i = 0; i < value_len; ++i)
            path_len = (path_len << 8) | value[i];
        if (path_len > INT_MAX) return -1;
        cert->max_path_len = (int)path_len;
    }
    if (constraints.pos != constraints.len) return -1;
    return 0;
}

static int parse_key_usage(neverc_x509_cert_t *cert,
                           const uint8_t *data, size_t len) {
    asn1_reader_t reader = {data, len, 0};
    uint8_t tag;
    const uint8_t *value;
    size_t value_len;
    if (asn1_read_tlv(&reader, &tag, &value, &value_len) < 0 ||
        tag != ASN1_TAG_BIT_STRING || reader.pos != reader.len ||
        value_len < 2 || value_len > 3 || value[0] > 7)
        return -1;

    size_t bytes_len = value_len - 1;
    unsigned unused = value[0];
    if (unused > 0 &&
        (value[value_len - 1] & ((1u << unused) - 1u)) != 0)
        return -1;
    unsigned bit_count = (unsigned)(bytes_len * 8) - unused;
    if (bit_count == 0 || bit_count > 9) return -1;

    cert->key_usage = 0;
    for (unsigned bit = 0; bit < bit_count; ++bit) {
        if ((value[1 + bit / 8] &
             (uint8_t)(0x80u >> (bit % 8))) != 0)
            cert->key_usage |= (uint16_t)(1u << bit);
    }
    cert->key_usage_present = 1;
    return 0;
}

static int parse_ext_key_usage(neverc_x509_cert_t *cert,
                               const uint8_t *data, size_t len) {
    asn1_reader_t wrapper = {data, len, 0};
    asn1_reader_t purposes;
    if (asn1_enter_sequence(&wrapper, &purposes) < 0 ||
        wrapper.pos != wrapper.len || purposes.pos == purposes.len)
        return -1;

    cert->ext_key_usage = 0;
    while (purposes.pos < purposes.len) {
        uint8_t tag;
        const uint8_t *oid;
        size_t oid_len;
        if (asn1_read_tlv(&purposes, &tag, &oid, &oid_len) < 0 ||
            tag != ASN1_TAG_OID)
            return -1;
        if (oid_equals(oid, oid_len, OID_EKU_SERVER_AUTH,
                       sizeof(OID_EKU_SERVER_AUTH)))
            cert->ext_key_usage |= NEVERC_X509_EXT_KEY_USAGE_SERVER_AUTH;
        else if (oid_equals(oid, oid_len, OID_EKU_CLIENT_AUTH,
                            sizeof(OID_EKU_CLIENT_AUTH)))
            cert->ext_key_usage |= NEVERC_X509_EXT_KEY_USAGE_CLIENT_AUTH;
        else if (oid_equals(oid, oid_len, OID_EKU_CODE_SIGNING,
                            sizeof(OID_EKU_CODE_SIGNING)))
            cert->ext_key_usage |= NEVERC_X509_EXT_KEY_USAGE_CODE_SIGNING;
        else if (oid_equals(oid, oid_len, OID_EKU_EMAIL_PROTECTION,
                            sizeof(OID_EKU_EMAIL_PROTECTION)))
            cert->ext_key_usage |= NEVERC_X509_EXT_KEY_USAGE_EMAIL_PROTECTION;
        else if (oid_equals(oid, oid_len, OID_EKU_TIME_STAMPING,
                            sizeof(OID_EKU_TIME_STAMPING)))
            cert->ext_key_usage |= NEVERC_X509_EXT_KEY_USAGE_TIME_STAMPING;
        else if (oid_equals(oid, oid_len, OID_EKU_OCSP_SIGNING,
                            sizeof(OID_EKU_OCSP_SIGNING)))
            cert->ext_key_usage |= NEVERC_X509_EXT_KEY_USAGE_OCSP_SIGNING;
        else if (oid_equals(oid, oid_len, OID_EKU_ANY,
                            sizeof(OID_EKU_ANY)))
            cert->ext_key_usage |= NEVERC_X509_EXT_KEY_USAGE_ANY;
    }
    cert->ext_key_usage_present = 1;
    return 0;
}

static int append_constraint_dns(char ***names, size_t *count,
                                 const uint8_t *value, size_t len) {
    if (!names || !count || !value || len == 0 ||
        len > X509_MAX_DNS_NAME_LEN ||
        *count >= X509_MAX_SAN_ENTRIES)
        return -1;
    /* "." is a leading-dot constraint with an empty remainder and would
     * otherwise match every DNS name. */
    if (len == 1 && value[0] == '.')
        return -1;
    for (size_t i = 0; i < len; ++i) {
        if (value[i] == 0 || value[i] > 0x7f)
            return -1;
    }
    if (*count == SIZE_MAX / sizeof(**names))
        return -1;

    char *name = (char *)malloc(len + 1);
    if (!name) return -1;
    if (len > 0)
        memcpy(name, value, len);
    name[len] = '\0';

    size_t next = *count + 1;
    char **resized = (char **)realloc(*names, next * sizeof(*resized));
    if (!resized) {
        free(name);
        return -1;
    }
    *names = resized;
    (*names)[*count] = name;
    *count = next;
    return 0;
}

static int append_ip_network(neverc_x509_ip_network_t **networks,
                             size_t *count, const uint8_t *value,
                             size_t len) {
    if (!networks || !count || !value || (len != 8 && len != 32) ||
        (len == 32 && x509_ip_is_v4_mapped(value, 16)) ||
        *count >= X509_MAX_SAN_ENTRIES ||
        *count == SIZE_MAX / sizeof(**networks))
        return -1;

    size_t next = *count + 1;
    neverc_x509_ip_network_t *resized =
        (neverc_x509_ip_network_t *)realloc(
            *networks, next * sizeof(*resized));
    if (!resized) return -1;
    *networks = resized;

    neverc_x509_ip_network_t *network = &(*networks)[*count];
    memset(network, 0, sizeof(*network));
    size_t addr_len = len / 2;
    memcpy(network->bytes, value, addr_len);
    memcpy(network->mask, value + addr_len, addr_len);
    network->len = (uint8_t)addr_len;
    *count = next;
    return 0;
}

static int parse_general_subtrees(asn1_reader_t *trees,
                                  neverc_x509_name_constraints_t *constraints,
                                  int excluded) {
    if (!trees || trees->len == 0)
        return -1;

    while (trees->pos < trees->len) {
        asn1_reader_t subtree;
        if (asn1_enter_sequence(trees, &subtree) != 0)
            return -1;

        uint8_t tag;
        const uint8_t *value;
        size_t value_len;
        if (asn1_read_tlv(&subtree, &tag, &value, &value_len) != 0)
            return -1;
        /* RFC 5280 minimum DEFAULT 0 / maximum OPTIONAL: reject both so
         * a non-zero minimum cannot silently widen a constraint. */
        if (subtree.pos != subtree.len)
            return -1;

        if (tag == 0x82) {
            char ***names = excluded ? &constraints->excluded_dns_names
                                     : &constraints->permitted_dns_names;
            size_t *count = excluded
                                ? &constraints->excluded_dns_name_count
                                : &constraints->permitted_dns_name_count;
            if (append_constraint_dns(names, count, value, value_len) != 0)
                return -1;
        } else if (tag == 0x87) {
            neverc_x509_ip_network_t **networks =
                excluded ? &constraints->excluded_ip_networks
                         : &constraints->permitted_ip_networks;
            size_t *count = excluded
                                ? &constraints->excluded_ip_network_count
                                : &constraints->permitted_ip_network_count;
            if (append_ip_network(networks, count, value, value_len) != 0)
                return -1;
        } else {
            /* RFC 5280 4.2.1.10: unrecognized name forms must not be
             * dropped. A critical nameConstraints with only rfc822Name
             * or directoryName would otherwise present as unrestricted. */
            return -1;
        }
    }
    return 0;
}

static int parse_name_constraints(neverc_x509_name_constraints_t *constraints,
                                  const uint8_t *data, size_t len) {
    asn1_reader_t wrapper = {data, len, 0};
    asn1_reader_t sequence;
    if (asn1_enter_sequence(&wrapper, &sequence) != 0 ||
        wrapper.pos != wrapper.len || sequence.len == 0)
        return -1;

    int saw_permitted = 0;
    int saw_excluded = 0;
    while (sequence.pos < sequence.len) {
        uint8_t tag;
        const uint8_t *value;
        size_t value_len;
        if (asn1_read_tlv(&sequence, &tag, &value, &value_len) != 0)
            return -1;
        asn1_reader_t trees = {value, value_len, 0};
        if (tag == ASN1_TAG_CONTEXT_0) {
            if (saw_permitted || saw_excluded)
                return -1;
            saw_permitted = 1;
            if (parse_general_subtrees(&trees, constraints, 0) != 0 ||
                trees.pos != trees.len)
                return -1;
        } else if (tag == ASN1_TAG_CONTEXT_1) {
            if (saw_excluded)
                return -1;
            saw_excluded = 1;
            if (parse_general_subtrees(&trees, constraints, 1) != 0 ||
                trees.pos != trees.len)
                return -1;
        } else {
            return -1;
        }
    }
    if (!saw_permitted && !saw_excluded)
        return -1;
    constraints->present = 1;
    return 0;
}

void neverc_x509_name_constraints_clear(
    neverc_x509_name_constraints_t *constraints) {
    if (!constraints) return;
    for (size_t i = 0; i < constraints->permitted_dns_name_count; ++i)
        free(constraints->permitted_dns_names[i]);
    free(constraints->permitted_dns_names);
    for (size_t i = 0; i < constraints->excluded_dns_name_count; ++i)
        free(constraints->excluded_dns_names[i]);
    free(constraints->excluded_dns_names);
    free(constraints->permitted_ip_networks);
    free(constraints->excluded_ip_networks);
    memset(constraints, 0, sizeof(*constraints));
}

static int parse_extensions(neverc_x509_cert_t *cert,
                            const uint8_t *data, size_t len,
                            int *san_critical) {
    if (san_critical)
        *san_critical = 0;
    asn1_reader_t wrapper = {data, len, 0};
    asn1_reader_t extensions;
    /* RFC 5280: Extensions ::= SEQUENCE SIZE (1..MAX) OF Extension */
    if (asn1_enter_sequence(&wrapper, &extensions) < 0 ||
        wrapper.pos != wrapper.len || extensions.len == 0)
        return -1;

    int saw_basic_constraints = 0;
    int saw_subject_alt_name = 0;
    int saw_key_usage = 0;
    int saw_ext_key_usage = 0;
    int saw_name_constraints = 0;
    while (extensions.pos < extensions.len) {
        asn1_reader_t extension;
        if (asn1_enter_sequence(&extensions, &extension) < 0)
            return -1;

        uint8_t tag;
        const uint8_t *oid;
        size_t oid_len;
        if (asn1_read_tlv(&extension, &tag, &oid, &oid_len) < 0 ||
            tag != ASN1_TAG_OID)
            return -1;

        int critical_flag = 0;
        if (extension.pos < extension.len &&
            extension.data[extension.pos] == ASN1_TAG_BOOLEAN) {
            const uint8_t *critical;
            size_t critical_len;
            if (asn1_read_tlv(&extension, &tag, &critical,
                              &critical_len) < 0 ||
                critical_len != 1 ||
                (critical[0] != 0x00 && critical[0] != 0xff))
                return -1;
            critical_flag = critical[0] != 0;
        }

        const uint8_t *contents;
        size_t contents_len;
        if (asn1_read_tlv(&extension, &tag, &contents,
                          &contents_len) < 0 ||
            tag != ASN1_TAG_OCTET_STRING ||
            extension.pos != extension.len)
            return -1;

        int handled = 1;
        if (oid_equals(oid, oid_len, OID_BASIC_CONSTRAINTS,
                       sizeof(OID_BASIC_CONSTRAINTS))) {
            if (saw_basic_constraints ||
                parse_basic_constraints(cert, contents,
                                        contents_len) != 0)
                return -1;
            saw_basic_constraints = 1;
        } else if (oid_equals(oid, oid_len, OID_SUBJECT_ALT_NAME,
                              sizeof(OID_SUBJECT_ALT_NAME))) {
            int san_rc;
            if (saw_subject_alt_name ||
                (san_rc = parse_subject_alt_name(cert, contents,
                                                 contents_len)) < 0)
                return -1;
            saw_subject_alt_name = 1;
            if (san_critical && critical_flag)
                *san_critical = 1;
            /* RFC 5280 §4.2 / Go processExtensions: a critical SAN that
             * yielded no supported names is unhandled. */
            if (san_rc > 0 && critical_flag)
                cert->has_unhandled_critical_extension = 1;
        } else if (oid_equals(oid, oid_len, OID_KEY_USAGE,
                              sizeof(OID_KEY_USAGE))) {
            if (saw_key_usage ||
                parse_key_usage(cert, contents, contents_len) != 0)
                return -1;
            saw_key_usage = 1;
        } else if (oid_equals(oid, oid_len, OID_EXT_KEY_USAGE,
                              sizeof(OID_EXT_KEY_USAGE))) {
            if (saw_ext_key_usage ||
                parse_ext_key_usage(cert, contents,
                                    contents_len) != 0)
                return -1;
            saw_ext_key_usage = 1;
        } else if (oid_equals(oid, oid_len, OID_NAME_CONSTRAINTS,
                              sizeof(OID_NAME_CONSTRAINTS))) {
            neverc_x509_name_constraints_t constraints;
            memset(&constraints, 0, sizeof(constraints));
            int constraint_rc =
                parse_name_constraints(&constraints, contents, contents_len);
            neverc_x509_name_constraints_clear(&constraints);
            if (saw_name_constraints || constraint_rc != 0)
                return -1;
            saw_name_constraints = 1;
        } else {
            handled = 0;
        }
        if (!handled && critical_flag)
            cert->has_unhandled_critical_extension = 1;
    }
    /* RFC 5280 4.2.1.10: nameConstraints MUST be used only in a CA
     * certificate. A critical leaf constraint is otherwise "handled"
     * and never applied to a subsequent certificate. */
    if (saw_name_constraints && !cert->is_ca)
        return -1;
    return 0;
}

static int skip_expected_tlv(asn1_reader_t *reader, uint8_t expected_tag) {
    uint8_t tag;
    const uint8_t *value;
    size_t value_len;
    return asn1_read_tlv(reader, &tag, &value, &value_len) == 0 &&
                   tag == expected_tag
               ? 0
               : -1;
}

/* Locate the extension without retaining any pointer outside cert->raw. The
 * certificate parser already validates full DER; this strict walk also makes
 * the public presence accessor fail closed for caller-constructed objects. */
static int find_name_constraints_extension(
    const neverc_x509_cert_t *cert, const uint8_t **contents_out,
    size_t *contents_len_out) {
    if (contents_out) *contents_out = NULL;
    if (contents_len_out) *contents_len_out = 0;
    if (!cert || !cert->raw || cert->raw_len == 0)
        return 0;

    asn1_reader_t root = {cert->raw, cert->raw_len, 0};
    asn1_reader_t certificate;
    asn1_reader_t tbs;
    if (asn1_enter_sequence(&root, &certificate) != 0 ||
        root.pos != root.len || asn1_enter_sequence(&certificate, &tbs) != 0)
        return -1;

    if (tbs.pos < tbs.len && tbs.data[tbs.pos] == ASN1_TAG_CONTEXT_0) {
        if (skip_expected_tlv(&tbs, ASN1_TAG_CONTEXT_0) != 0)
            return -1;
    }
    if (skip_expected_tlv(&tbs, ASN1_TAG_INTEGER) != 0 ||
        skip_expected_tlv(&tbs, ASN1_TAG_SEQUENCE) != 0 ||
        skip_expected_tlv(&tbs, ASN1_TAG_SEQUENCE) != 0 ||
        skip_expected_tlv(&tbs, ASN1_TAG_SEQUENCE) != 0 ||
        skip_expected_tlv(&tbs, ASN1_TAG_SEQUENCE) != 0 ||
        skip_expected_tlv(&tbs, ASN1_TAG_SEQUENCE) != 0)
        return -1;

    int found = 0;
    int saw_issuer_unique_id = 0;
    int saw_subject_unique_id = 0;
    int saw_extensions = 0;
    while (tbs.pos < tbs.len) {
        uint8_t tag;
        const uint8_t *value;
        size_t value_len;
        if (asn1_read_tlv(&tbs, &tag, &value, &value_len) != 0)
            return -1;
        if (tag == ASN1_TAG_CONTEXT_1_IMPLICIT) {
            if (saw_issuer_unique_id || saw_subject_unique_id ||
                saw_extensions || !valid_implicit_bit_string(value, value_len))
                return -1;
            saw_issuer_unique_id = 1;
            continue;
        }
        if (tag == ASN1_TAG_CONTEXT_2_IMPLICIT) {
            if (saw_subject_unique_id || saw_extensions ||
                !valid_implicit_bit_string(value, value_len))
                return -1;
            saw_subject_unique_id = 1;
            continue;
        }
        if (tag != ASN1_TAG_CONTEXT_3 || saw_extensions)
            return -1;
        saw_extensions = 1;

        asn1_reader_t wrapper = {value, value_len, 0};
        asn1_reader_t extensions;
        if (asn1_enter_sequence(&wrapper, &extensions) != 0 ||
            wrapper.pos != wrapper.len || extensions.len == 0)
            return -1;
        while (extensions.pos < extensions.len) {
            asn1_reader_t extension;
            if (asn1_enter_sequence(&extensions, &extension) != 0)
                return -1;
            const uint8_t *oid;
            size_t oid_len;
            if (asn1_read_tlv(&extension, &tag, &oid, &oid_len) != 0 ||
                tag != ASN1_TAG_OID)
                return -1;
            if (extension.pos < extension.len &&
                extension.data[extension.pos] == ASN1_TAG_BOOLEAN) {
                const uint8_t *critical;
                size_t critical_len;
                if (asn1_read_tlv(&extension, &tag, &critical,
                                  &critical_len) != 0 ||
                    critical_len != 1 ||
                    (critical[0] != 0x00 && critical[0] != 0xff))
                    return -1;
            }
            const uint8_t *contents;
            size_t contents_len;
            if (asn1_read_tlv(&extension, &tag, &contents,
                              &contents_len) != 0 ||
                tag != ASN1_TAG_OCTET_STRING ||
                extension.pos != extension.len)
                return -1;
            if (oid_equals(oid, oid_len, OID_NAME_CONSTRAINTS,
                           sizeof(OID_NAME_CONSTRAINTS))) {
                if (found)
                    return -1;
                found = 1;
                if (contents_out) *contents_out = contents;
                if (contents_len_out) *contents_len_out = contents_len;
            }
        }
    }
    int signature_algorithm = 0;
    uint8_t signature_tag;
    const uint8_t *signature;
    size_t signature_len;
    if (parse_signature_algorithm(&certificate, &signature_algorithm,
                                  NULL, NULL) != 0 ||
        asn1_read_tlv(&certificate, &signature_tag, &signature,
                      &signature_len) != 0 ||
        signature_tag != ASN1_TAG_BIT_STRING || signature_len < 2 ||
        signature[0] != 0 || certificate.pos != certificate.len)
        return -1;
    return found;
}

int neverc_x509_extract_name_constraints(
    const neverc_x509_cert_t *cert,
    neverc_x509_name_constraints_t *constraints) {
    if (!constraints) return -1;
    memset(constraints, 0, sizeof(*constraints));
    const uint8_t *contents;
    size_t contents_len;
    int found = find_name_constraints_extension(
        cert, &contents, &contents_len);
    if (found < 0)
        return -1;
    if (found == 0)
        return 0;
    if (parse_name_constraints(constraints, contents, contents_len) != 0) {
        neverc_x509_name_constraints_clear(constraints);
        return -1;
    }
    return 0;
}

int neverc_x509_has_name_constraints(const neverc_x509_cert_t *cert) {
    neverc_x509_name_constraints_t constraints;
    if (neverc_x509_extract_name_constraints(cert, &constraints) != 0)
        return -1;
    int present = constraints.present;
    neverc_x509_name_constraints_clear(&constraints);
    return present;
}

/* ===== Name Parsing ===== */

static int parse_name(asn1_reader_t *r, neverc_x509_name_t *name) {
    memset(name, 0, sizeof(*name));
    while (r->pos < r->len) {
        asn1_reader_t set_r;
        uint8_t tag;
        const uint8_t *val;
        size_t vlen;

        if (asn1_read_tlv(r, &tag, &val, &vlen) < 0 ||
            tag != ASN1_TAG_SET || vlen == 0)
            return -1;
        set_r.data = val;
        set_r.len = vlen;
        set_r.pos = 0;

        while (set_r.pos < set_r.len) {
            asn1_reader_t seq_r;
            if (asn1_enter_sequence(&set_r, &seq_r) < 0)
                return -1;

            uint8_t oid_tag;
            const uint8_t *oid_val;
            size_t oid_len;
            if (asn1_read_tlv(&seq_r, &oid_tag, &oid_val,
                              &oid_len) < 0 ||
                oid_tag != ASN1_TAG_OID || oid_len == 0)
                return -1;

            uint8_t str_tag;
            const uint8_t *str_val;
            size_t str_len;
            if (asn1_read_tlv(&seq_r, &str_tag, &str_val,
                              &str_len) < 0 ||
                seq_r.pos != seq_r.len)
                return -1;
            (void)str_tag;

            if (oid_equals(oid_val, oid_len, OID_CN, sizeof(OID_CN))) {
                if (copy_string_value(name->common_name,
                                      sizeof(name->common_name),
                                      str_val, str_len) != 0)
                    return -1;
            } else if (oid_equals(oid_val, oid_len, OID_C,
                                  sizeof(OID_C))) {
                if (copy_string_value(name->country,
                                      sizeof(name->country),
                                      str_val, str_len) != 0)
                    return -1;
            } else if (oid_equals(oid_val, oid_len, OID_O,
                                  sizeof(OID_O))) {
                if (copy_string_value(name->organization,
                                      sizeof(name->organization),
                                      str_val, str_len) != 0)
                    return -1;
            } else if (oid_equals(oid_val, oid_len, OID_OU,
                                  sizeof(OID_OU))) {
                if (copy_string_value(name->organizational_unit,
                                      sizeof(name->organizational_unit),
                                      str_val, str_len) != 0)
                    return -1;
            } else if (oid_equals(oid_val, oid_len, OID_L,
                                  sizeof(OID_L))) {
                if (copy_string_value(name->locality,
                                      sizeof(name->locality),
                                      str_val, str_len) != 0)
                    return -1;
            } else if (oid_equals(oid_val, oid_len, OID_ST,
                                  sizeof(OID_ST))) {
                if (copy_string_value(name->province,
                                      sizeof(name->province),
                                      str_val, str_len) != 0)
                    return -1;
            } else if (oid_equals(oid_val, oid_len, OID_SN,
                                  sizeof(OID_SN))) {
                if (copy_string_value(name->serial_number_str,
                                      sizeof(name->serial_number_str),
                                      str_val, str_len) != 0)
                    return -1;
            }
        }
    }
    return 0;
}

/* ===== Time Parsing ===== */

static int digit2(const uint8_t *s) {
    return (s[0] - '0') * 10 + (s[1] - '0');
}

static int valid_time_digits(const uint8_t *data, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        if (data[i] < '0' || data[i] > '9')
            return 0;
    }
    return 1;
}

static int valid_calendar_time(const neverc_x509_time_t *time) {
    static const uint8_t days_per_month[] = {
        31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31
    };
    if (time->month < 1 || time->month > 12 ||
        time->hour > 23 || time->minute > 59 || time->second > 59)
        return 0;
    unsigned days = days_per_month[time->month - 1];
    int leap = (time->year % 4 == 0 && time->year % 100 != 0) ||
               time->year % 400 == 0;
    if (time->month == 2 && leap) ++days;
    return time->day >= 1 && time->day <= days;
}

static int parse_utctime(const uint8_t *data, size_t len, neverc_x509_time_t *t) {
    if (len != 13 || data[12] != 'Z' ||
        !valid_time_digits(data, 12))
        return -1;
    int yy = digit2(data);
    t->year   = (uint16_t)((yy >= 50) ? 1900 + yy : 2000 + yy);
    t->month  = (uint8_t)digit2(data + 2);
    t->day    = (uint8_t)digit2(data + 4);
    t->hour   = (uint8_t)digit2(data + 6);
    t->minute = (uint8_t)digit2(data + 8);
    t->second = (uint8_t)digit2(data + 10);
    return valid_calendar_time(t) ? 0 : -1;
}

static int parse_generaltime(const uint8_t *data, size_t len, neverc_x509_time_t *t) {
    if (len != 15 || data[14] != 'Z' ||
        !valid_time_digits(data, 14))
        return -1;
    t->year   = (uint16_t)(digit2(data) * 100 + digit2(data + 2));
    t->month  = (uint8_t)digit2(data + 4);
    t->day    = (uint8_t)digit2(data + 6);
    t->hour   = (uint8_t)digit2(data + 8);
    t->minute = (uint8_t)digit2(data + 10);
    t->second = (uint8_t)digit2(data + 12);
    return valid_calendar_time(t) ? 0 : -1;
}

/* ===== Public API ===== */

int neverc_x509_parse_certificate(neverc_x509_cert_t *cert,
                                    const uint8_t *der, size_t len) {
    if (!cert) return -1;
    memset(cert, 0, sizeof(*cert));
    cert->max_path_len = -1;
    if (!der || len == 0) return -1;
    cert->raw = der;
    cert->raw_len = len;

    asn1_reader_t root = {der, len, 0};
    asn1_reader_t cert_seq;
    if (asn1_enter_sequence(&root, &cert_seq) < 0 ||
        root.pos != root.len)
        return -1;

    /* TBSCertificate */
    asn1_reader_t tbs;
    size_t tbs_start = cert_seq.pos;
    if (asn1_enter_sequence(&cert_seq, &tbs) < 0) return -1;
    cert->raw_tbs = cert_seq.data + tbs_start;
    cert->raw_tbs_len = cert_seq.pos - tbs_start;

    /* Version (optional, context [0]) */
    cert->version = 0;
    if (tbs.pos < tbs.len && tbs.data[tbs.pos] == ASN1_TAG_CONTEXT_0) {
        uint8_t tag;
        const uint8_t *val;
        size_t vlen;
        if (asn1_read_tlv(&tbs, &tag, &val, &vlen) != 0 ||
            tag != ASN1_TAG_CONTEXT_0)
            return -1;
        asn1_reader_t version = {val, vlen, 0};
        uint8_t integer_tag;
        const uint8_t *integer;
        size_t integer_len;
        if (asn1_read_tlv(&version, &integer_tag, &integer,
                          &integer_len) != 0 ||
            integer_tag != ASN1_TAG_INTEGER || integer_len != 1 ||
            integer[0] > 2 || version.pos != version.len)
            return -1;
        cert->version = integer[0];
    }

    /* SerialNumber. RFC 5280 §4.1.2.2: applications MUST handle serial
     * values up to 20 octets. A 20-octet magnitude with the high bit set
     * is a 21-byte DER INTEGER (leading 0x00). Reject negatives,
     * non-minimal encodings, and magnitudes longer than 20 octets. */
    {
        uint8_t tag;
        const uint8_t *val;
        size_t vlen;
        if (asn1_read_tlv(&tbs, &tag, &val, &vlen) < 0 ||
            tag != ASN1_TAG_INTEGER || vlen == 0 ||
            (val[0] & 0x80) != 0)
            return -1;
        if (vlen > 1 && val[0] == 0) {
            if ((val[1] & 0x80) == 0)
                return -1;
            val++;
            vlen--;
        }
        if (vlen > sizeof(cert->serial))
            return -1;
        cert->serial_len = (int)vlen;
        memcpy(cert->serial, val, vlen);
    }

    /* Signature algorithm in TBS */
    const uint8_t *tbs_signature_algorithm;
    size_t tbs_signature_algorithm_len;
    if (parse_signature_algorithm(
            &tbs, &cert->sig_algorithm, &tbs_signature_algorithm,
            &tbs_signature_algorithm_len) != 0)
        return -1;

    /* Issuer */
    {
        uint8_t tag;
        const uint8_t *val;
        size_t vlen;
        size_t name_start = tbs.pos;
        if (asn1_read_tlv(&tbs, &tag, &val, &vlen) < 0 ||
            tag != ASN1_TAG_SEQUENCE)
            return -1;
        cert->raw_issuer = tbs.data + name_start;
        cert->raw_issuer_len = tbs.pos - name_start;
        if (vlen == 0)
            return -1;
        asn1_reader_t name_r = {val, vlen, 0};
        if (parse_name(&name_r, &cert->issuer) != 0)
            return -1;
    }

    /* Validity */
    {
        asn1_reader_t val_seq;
        if (asn1_enter_sequence(&tbs, &val_seq) < 0) return -1;
        uint8_t tag;
        const uint8_t *val;
        size_t vlen;
        if (asn1_read_tlv(&val_seq, &tag, &val, &vlen) < 0 ||
            !((tag == ASN1_TAG_UTCTIME &&
               parse_utctime(val, vlen, &cert->not_before) == 0) ||
              (tag == ASN1_TAG_GENERALTIME &&
               parse_generaltime(val, vlen, &cert->not_before) == 0)))
            return -1;
        if (asn1_read_tlv(&val_seq, &tag, &val, &vlen) < 0 ||
            !((tag == ASN1_TAG_UTCTIME &&
               parse_utctime(val, vlen, &cert->not_after) == 0) ||
              (tag == ASN1_TAG_GENERALTIME &&
               parse_generaltime(val, vlen, &cert->not_after) == 0)) ||
            val_seq.pos != val_seq.len)
            return -1;
        if (neverc_x509_time_compare(&cert->not_before, &cert->not_after) > 0)
            return -1;
    }

    /* Subject */
    int subject_empty = 0;
    {
        uint8_t tag;
        const uint8_t *val;
        size_t vlen;
        size_t name_start = tbs.pos;
        if (asn1_read_tlv(&tbs, &tag, &val, &vlen) < 0 ||
            tag != ASN1_TAG_SEQUENCE)
            return -1;
        cert->raw_subject = tbs.data + name_start;
        cert->raw_subject_len = tbs.pos - name_start;
        subject_empty = (vlen == 0);
        asn1_reader_t name_r = {val, vlen, 0};
        if (parse_name(&name_r, &cert->subject) != 0)
            return -1;
    }

    /* SubjectPublicKeyInfo */
    {
        asn1_reader_t spki;
        if (asn1_enter_sequence(&tbs, &spki) < 0) return -1;

        /* Algorithm */
        if (parse_public_key_algorithm(&spki, cert) != 0)
            return -1;

        /* Public key (BIT STRING) */
        uint8_t tag;
        const uint8_t *val;
        size_t vlen;
        if (asn1_read_tlv(&spki, &tag, &val, &vlen) != 0 ||
            tag != ASN1_TAG_BIT_STRING || vlen <= 1 || val[0] != 0 ||
            spki.pos != spki.len)
            return -1;
        cert->public_key_len = vlen - 1;
        cert->public_key = (uint8_t *)malloc(cert->public_key_len);
        if (!cert->public_key)
            return -1;
        memcpy(cert->public_key, val + 1, cert->public_key_len);
    }

    /* Extensions (optional, context [3]) */
    int saw_issuer_unique_id = 0;
    int saw_subject_unique_id = 0;
    int saw_extensions = 0;
    int san_critical = 0;
    while (tbs.pos < tbs.len) {
        uint8_t next_tag = tbs.data[tbs.pos];
        if (next_tag == ASN1_TAG_CONTEXT_1_IMPLICIT ||
            next_tag == ASN1_TAG_CONTEXT_2_IMPLICIT) {
            int is_issuer = next_tag == ASN1_TAG_CONTEXT_1_IMPLICIT;
            if (cert->version == 0 || saw_extensions ||
                (is_issuer &&
                 (saw_issuer_unique_id || saw_subject_unique_id)) ||
                (!is_issuer && saw_subject_unique_id)) {
                neverc_x509_cert_free(cert);
                return -1;
            }
            uint8_t tag;
            const uint8_t *val;
            size_t vlen;
            if (asn1_read_tlv(&tbs, &tag, &val, &vlen) < 0 ||
                tag != next_tag || !valid_implicit_bit_string(val, vlen)) {
                neverc_x509_cert_free(cert);
                return -1;
            }
            if (is_issuer)
                saw_issuer_unique_id = 1;
            else
                saw_subject_unique_id = 1;
        } else if (next_tag == ASN1_TAG_CONTEXT_3) {
            if (cert->version != 2 || saw_extensions) {
                neverc_x509_cert_free(cert);
                return -1;
            }
            uint8_t tag;
            const uint8_t *val;
            size_t vlen;
            if (asn1_read_tlv(&tbs, &tag, &val, &vlen) < 0 ||
                tag != ASN1_TAG_CONTEXT_3 ||
                parse_extensions(cert, val, vlen, &san_critical) != 0) {
                neverc_x509_cert_free(cert);
                return -1;
            }
            saw_extensions = 1;
        } else {
            neverc_x509_cert_free(cert);
            return -1;
        }
    }

    /* RFC 5280 4.1.2.6: an empty subject requires a critical
     * subjectAltName. CA certificates MUST have a populated subject. */
    if (subject_empty && (cert->is_ca || !san_critical)) {
        neverc_x509_cert_free(cert);
        return -1;
    }

    int outer_signature_algorithm = 0;
    const uint8_t *outer_signature_algorithm_der;
    size_t outer_signature_algorithm_der_len;
    if (parse_signature_algorithm(
            &cert_seq, &outer_signature_algorithm,
            &outer_signature_algorithm_der,
            &outer_signature_algorithm_der_len) != 0 ||
        outer_signature_algorithm != cert->sig_algorithm ||
        outer_signature_algorithm_der_len !=
            tbs_signature_algorithm_len ||
        memcmp(outer_signature_algorithm_der, tbs_signature_algorithm,
               tbs_signature_algorithm_len) != 0) {
        neverc_x509_cert_free(cert);
        return -1;
    }
    uint8_t signature_tag;
    const uint8_t *signature;
    size_t signature_len;
    if (asn1_read_tlv(&cert_seq, &signature_tag, &signature,
                      &signature_len) < 0 ||
        signature_tag != ASN1_TAG_BIT_STRING ||
        signature_len < 2 || signature[0] != 0 ||
        cert_seq.pos != cert_seq.len) {
        neverc_x509_cert_free(cert);
        return -1;
    }
    cert->signature = signature + 1;
    cert->signature_len = signature_len - 1;

    return 0;
}

void neverc_x509_cert_free(neverc_x509_cert_t *cert) {
    if (!cert) return;
    free(cert->public_key);
    cert->public_key = NULL;
    cert->public_key_len = 0;
    for (size_t i = 0; i < cert->dns_name_count; ++i)
        free(cert->dns_names[i]);
    free(cert->dns_names);
    cert->dns_names = NULL;
    cert->dns_name_count = 0;
    free(cert->ip_addresses);
    cert->ip_addresses = NULL;
    cert->ip_address_count = 0;
    cert->raw_tbs = NULL;
    cert->raw_tbs_len = 0;
    cert->signature = NULL;
    cert->signature_len = 0;
    cert->raw_issuer = NULL;
    cert->raw_issuer_len = 0;
    cert->raw_subject = NULL;
    cert->raw_subject_len = 0;
}

static int ascii_lower(int ch) {
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

static int ascii_equal(const char *a, const char *b, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (ascii_lower((unsigned char)a[i]) !=
            ascii_lower((unsigned char)b[i]))
            return 0;
    }
    return 1;
}

static int parse_ipv4_literal(const char *text, size_t len,
                              uint8_t out[4]) {
    size_t pos = 0;
    for (int part = 0; part < 4; ++part) {
        if (pos >= len) return -1;
        size_t start = pos;
        unsigned value = 0;
        while (pos < len && text[pos] >= '0' && text[pos] <= '9') {
            if (value > 25 ||
                (value == 25 && (unsigned)(text[pos] - '0') > 5))
                return -1;
            value = value * 10 + (unsigned)(text[pos] - '0');
            ++pos;
        }
        if (pos == start || pos - start > 3 ||
            (pos - start > 1 && text[start] == '0'))
            return -1;
        out[part] = (uint8_t)value;
        if (part == 3)
            return pos == len ? 0 : -1;
        if (pos >= len || text[pos] != '.')
            return -1;
        ++pos;
    }
    return -1;
}

static int hex_value(int ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    ch = ascii_lower(ch);
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    return -1;
}

static int parse_ipv6_literal(const char *text, size_t len,
                              uint8_t out[16]) {
    uint16_t words[8] = {0};
    int word_count = 0;
    int compress_at = -1;
    size_t pos = 0;

    if (len == 0) return -1;
    if (text[0] == ':') {
        if (len < 2 || text[1] != ':') return -1;
        compress_at = 0;
        pos = 2;
    }

    while (pos < len) {
        if (word_count >= 8) return -1;
        size_t start = pos;
        unsigned value = 0;
        int digits = 0;
        int digit;
        while (pos < len && (digit = hex_value((unsigned char)text[pos])) >= 0 &&
               digits < 4) {
            value = (value << 4) | (unsigned)digit;
            ++digits;
            ++pos;
        }

        if (pos < len && text[pos] == '.') {
            uint8_t ipv4[4];
            if (word_count > 6 ||
                parse_ipv4_literal(text + start, len - start, ipv4) != 0)
                return -1;
            words[word_count++] =
                (uint16_t)(((uint16_t)ipv4[0] << 8) | ipv4[1]);
            words[word_count++] =
                (uint16_t)(((uint16_t)ipv4[2] << 8) | ipv4[3]);
            break;
        }

        if (digits == 0 ||
            (pos < len && hex_value((unsigned char)text[pos]) >= 0))
            return -1;
        words[word_count++] = (uint16_t)value;
        if (pos == len) break;
        if (text[pos] != ':') return -1;
        ++pos;
        if (pos == len) return -1;
        if (text[pos] == ':') {
            if (compress_at >= 0) return -1;
            compress_at = word_count;
            ++pos;
            if (pos == len) break;
        }
    }

    if (compress_at >= 0) {
        int zero_words = 8 - word_count;
        if (zero_words < 1) return -1;
        for (int i = word_count - 1; i >= compress_at; --i)
            words[i + zero_words] = words[i];
        for (int i = 0; i < zero_words; ++i)
            words[compress_at + i] = 0;
    } else if (word_count != 8) {
        return -1;
    }

    for (int i = 0; i < 8; ++i) {
        out[i * 2] = (uint8_t)(words[i] >> 8);
        out[i * 2 + 1] = (uint8_t)words[i];
    }
    return 0;
}

static int parse_ip_literal(const char *text, size_t len,
                            uint8_t out[16], size_t *out_len) {
    if (parse_ipv4_literal(text, len, out) == 0) {
        *out_len = 4;
        return 0;
    }
    if (parse_ipv6_literal(text, len, out) == 0) {
        *out_len = 16;
        return 0;
    }
    /* Go net.ParseIP accepts a zone on IPv6 (`::1%lo0`). */
    if (memchr(text, ':', len)) {
        const char *pct = (const char *)memchr(text, '%', len);
        if (pct && pct > text &&
            parse_ipv6_literal(text, (size_t)(pct - text), out) == 0) {
            *out_len = 16;
            return 0;
        }
    }
    return -1;
}

static int valid_dns_labels(const char *name, size_t len) {
    if (len == 0 || len > X509_MAX_DNS_NAME_LEN) return 0;
    size_t label_start = 0;
    for (size_t i = 0; i <= len; ++i) {
        if (i == len || name[i] == '.') {
            size_t label_len = i - label_start;
            if (label_len == 0 || label_len > 63 ||
                name[label_start] == '-' || name[i - 1] == '-')
                return 0;
            label_start = i + 1;
            continue;
        }
        int ch = (unsigned char)name[i];
        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= 'A' && ch <= 'Z') ||
              (ch >= '0' && ch <= '9') || ch == '-'))
            return 0;
    }
    return 1;
}

static int dns_pattern_matches(const char *pattern, const char *hostname) {
    size_t pattern_len = strlen(pattern);
    size_t hostname_len = strlen(hostname);
    if (pattern_len > 0 && pattern[pattern_len - 1] == '.')
        --pattern_len;
    if (hostname_len > 0 && hostname[hostname_len - 1] == '.')
        --hostname_len;
    if (pattern_len == 0 || hostname_len == 0)
        return 0;

    int wildcard = pattern_len > 2 && pattern[0] == '*' &&
                   pattern[1] == '.';
    const char *validated_pattern = wildcard ? pattern + 2 : pattern;
    size_t validated_len = wildcard ? pattern_len - 2 : pattern_len;
    if (!valid_dns_labels(validated_pattern, validated_len) ||
        !valid_dns_labels(hostname, hostname_len))
        return 0;
    if (wildcard) {
        if (validated_len == 0 ||
            !memchr(validated_pattern, '.', validated_len))
            return 0;
    }
    if (!wildcard)
        return pattern_len == hostname_len &&
               ascii_equal(pattern, hostname, pattern_len);

    const char *suffix = pattern + 1;
    size_t suffix_len = pattern_len - 1;
    if (hostname_len <= suffix_len ||
        !ascii_equal(hostname + hostname_len - suffix_len,
                     suffix, suffix_len))
        return 0;
    size_t wildcard_len = hostname_len - suffix_len;
    for (size_t i = 0; i < wildcard_len; ++i) {
        if (hostname[i] == '.')
            return 0;
    }
    return wildcard_len > 0;
}

int neverc_x509_verify_hostname(const neverc_x509_cert_t *cert,
                                  const char *hostname) {
    if (!cert || !hostname || hostname[0] == '\0') return -1;

    const char *identity = hostname;
    size_t identity_len = strlen(hostname);
    int bracketed = identity_len >= 2 && hostname[0] == '[' &&
                    hostname[identity_len - 1] == ']';
    if (bracketed) {
        ++identity;
        identity_len -= 2;
    }

    uint8_t address[16];
    size_t address_len = 0;
    if (parse_ip_literal(identity, identity_len,
                         address, &address_len) == 0) {
        for (size_t i = 0; i < cert->ip_address_count; ++i) {
            if (cert->ip_addresses[i].len == address_len &&
                memcmp(cert->ip_addresses[i].bytes, address,
                       address_len) == 0)
                return 0;
        }
        return -1;
    }
    if (bracketed) return -1;

    for (size_t i = 0; i < cert->dns_name_count; ++i) {
        if (dns_pattern_matches(cert->dns_names[i], hostname))
            return 0;
    }
    return -1;
}

const char *neverc_x509_sig_algorithm_string(int algo) {
    switch (algo) {
    case NEVERC_X509_SIG_SHA1_RSA:     return "SHA1-RSA";
    case NEVERC_X509_SIG_SHA256_RSA:   return "SHA256-RSA";
    case NEVERC_X509_SIG_SHA384_RSA:   return "SHA384-RSA";
    case NEVERC_X509_SIG_SHA512_RSA:   return "SHA512-RSA";
    case NEVERC_X509_SIG_ECDSA_SHA256: return "ECDSA-SHA256";
    case NEVERC_X509_SIG_ECDSA_SHA384: return "ECDSA-SHA384";
    case NEVERC_X509_SIG_ED25519:      return "Ed25519";
    case NEVERC_X509_SIG_RSA_PSS_SHA256: return "RSA-PSS-SHA256";
    case NEVERC_X509_SIG_RSA_PSS_SHA384: return "RSA-PSS-SHA384";
    case NEVERC_X509_SIG_RSA_PSS_SHA512: return "RSA-PSS-SHA512";
    default: return "Unknown";
    }
}

const char *neverc_x509_key_algorithm_string(int algo) {
    switch (algo) {
    case NEVERC_X509_KEY_RSA:     return "RSA";
    case NEVERC_X509_KEY_ECDSA:   return "ECDSA";
    case NEVERC_X509_KEY_ED25519: return "Ed25519";
    default: return "Unknown";
    }
}

static int append_name_component(char *buf, size_t buf_size,
                                 size_t *written, const char *label,
                                 const char *value) {
    if (!value[0]) return 0;
    if (*written >= buf_size) return -1;
    size_t remaining = buf_size - *written;
    int n = snprintf(buf + *written, remaining, "%s%s=%s",
                     *written ? ", " : "", label, value);
    if (n < 0 || (size_t)n >= remaining)
        return -1;
    *written += (size_t)n;
    return 0;
}

int neverc_x509_format_name(const neverc_x509_name_t *name,
                              char *buf, size_t buf_size) {
    if (!name || !buf || buf_size == 0) return -1;
    size_t written = 0;
    buf[0] = '\0';
    if (append_name_component(buf, buf_size, &written,
                              "C", name->country) != 0 ||
        append_name_component(buf, buf_size, &written,
                              "ST", name->province) != 0 ||
        append_name_component(buf, buf_size, &written,
                              "L", name->locality) != 0 ||
        append_name_component(buf, buf_size, &written,
                              "O", name->organization) != 0 ||
        append_name_component(buf, buf_size, &written,
                              "OU", name->organizational_unit) != 0 ||
        append_name_component(buf, buf_size, &written,
                              "CN", name->common_name) != 0)
        return -1;
    return (int)written;
}

int neverc_x509_time_compare(const neverc_x509_time_t *a,
                               const neverc_x509_time_t *b) {
    if (!a || !b) return 0;
    if (a->year != b->year) return a->year < b->year ? -1 : 1;
    if (a->month != b->month) return a->month < b->month ? -1 : 1;
    if (a->day != b->day) return a->day < b->day ? -1 : 1;
    if (a->hour != b->hour) return a->hour < b->hour ? -1 : 1;
    if (a->minute != b->minute) return a->minute < b->minute ? -1 : 1;
    if (a->second != b->second) return a->second < b->second ? -1 : 1;
    return 0;
}

int neverc_x509_is_valid_at(const neverc_x509_cert_t *cert,
                              const neverc_x509_time_t *moment) {
    if (!cert || !moment) return 0;
    return neverc_x509_time_compare(moment, &cert->not_before) >= 0 &&
           neverc_x509_time_compare(moment, &cert->not_after) <= 0;
}
