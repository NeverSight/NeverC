/*
 * X.509 certificate parser.
 * Parses DER-encoded certificates (ASN.1 structure).
 * Self-contained implementation without external ASN.1 library dependency.
 */
#include "neverc/std/crypto/x509.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* ===== Minimal ASN.1 DER Reader ===== */

typedef struct {
    const uint8_t *data;
    size_t         len;
    size_t         pos;
} asn1_reader_t;

#define ASN1_TAG_INTEGER       0x02
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
        if (nbytes > 4 || r->pos + nbytes > r->len) return -1;
        *length = 0;
        for (int i = 0; i < nbytes; i++)
            *length = (*length << 8) | r->data[r->pos++];
    }
    return 0;
}

static int asn1_read_tlv(asn1_reader_t *r, uint8_t *tag,
                          const uint8_t **value, size_t *vlen) {
    if (asn1_read_tag(r, tag) < 0) return -1;
    if (asn1_read_length(r, vlen) < 0) return -1;
    if (r->pos + *vlen > r->len) return -1;
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

static void copy_string_value(char *dst, size_t dstsz,
                                const uint8_t *src, size_t srclen) {
    if (srclen >= dstsz) srclen = dstsz - 1;
    memcpy(dst, src, srclen);
    dst[srclen] = '\0';
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

/* rsaEncryption: 1.2.840.113549.1.1.1 */
static const uint8_t OID_RSA[] = {0x2A, 0x86, 0x48, 0x86, 0xF7, 0x0D, 0x01, 0x01, 0x01};
/* ecPublicKey: 1.2.840.10045.2.1 */
static const uint8_t OID_EC[] = {0x2A, 0x86, 0x48, 0xCE, 0x3D, 0x02, 0x01};

/* basicConstraints: 2.5.29.19 */
static const uint8_t OID_BASIC_CONSTRAINTS[] = {0x55, 0x1D, 0x13};

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

static int identify_key_algorithm(const uint8_t *oid, size_t len) {
    if (oid_equals(oid, len, OID_RSA, sizeof(OID_RSA)))
        return NEVERC_X509_KEY_RSA;
    if (oid_equals(oid, len, OID_EC, sizeof(OID_EC)))
        return NEVERC_X509_KEY_ECDSA;
    if (oid_equals(oid, len, OID_ED25519, sizeof(OID_ED25519)))
        return NEVERC_X509_KEY_ED25519;
    return 0;
}

/* ===== Name Parsing ===== */

static int parse_name(asn1_reader_t *r, neverc_x509_name_t *name) {
    memset(name, 0, sizeof(*name));
    while (r->pos < r->len) {
        asn1_reader_t set_r;
        uint8_t tag;
        const uint8_t *val;
        size_t vlen;

        if (asn1_read_tlv(r, &tag, &val, &vlen) < 0) return -1;
        if (tag != ASN1_TAG_SET) continue;
        set_r.data = val; set_r.len = vlen; set_r.pos = 0;

        asn1_reader_t seq_r;
        if (asn1_enter_sequence(&set_r, &seq_r) < 0) continue;

        uint8_t oid_tag;
        const uint8_t *oid_val;
        size_t oid_len;
        if (asn1_read_tlv(&seq_r, &oid_tag, &oid_val, &oid_len) < 0) continue;
        if (oid_tag != ASN1_TAG_OID) continue;

        uint8_t str_tag;
        const uint8_t *str_val;
        size_t str_len;
        if (asn1_read_tlv(&seq_r, &str_tag, &str_val, &str_len) < 0) continue;

        if (oid_equals(oid_val, oid_len, OID_CN, sizeof(OID_CN)))
            copy_string_value(name->common_name, sizeof(name->common_name), str_val, str_len);
        else if (oid_equals(oid_val, oid_len, OID_C, sizeof(OID_C)))
            copy_string_value(name->country, sizeof(name->country), str_val, str_len);
        else if (oid_equals(oid_val, oid_len, OID_O, sizeof(OID_O)))
            copy_string_value(name->organization, sizeof(name->organization), str_val, str_len);
        else if (oid_equals(oid_val, oid_len, OID_OU, sizeof(OID_OU)))
            copy_string_value(name->organizational_unit, sizeof(name->organizational_unit), str_val, str_len);
        else if (oid_equals(oid_val, oid_len, OID_L, sizeof(OID_L)))
            copy_string_value(name->locality, sizeof(name->locality), str_val, str_len);
        else if (oid_equals(oid_val, oid_len, OID_ST, sizeof(OID_ST)))
            copy_string_value(name->province, sizeof(name->province), str_val, str_len);
        else if (oid_equals(oid_val, oid_len, OID_SN, sizeof(OID_SN)))
            copy_string_value(name->serial_number_str, sizeof(name->serial_number_str), str_val, str_len);
    }
    return 0;
}

/* ===== Time Parsing ===== */

static int digit2(const uint8_t *s) {
    return (s[0] - '0') * 10 + (s[1] - '0');
}

static int parse_utctime(const uint8_t *data, size_t len, neverc_x509_time_t *t) {
    if (len < 13) return -1;
    int yy = digit2(data);
    t->year   = (uint16_t)((yy >= 50) ? 1900 + yy : 2000 + yy);
    t->month  = (uint8_t)digit2(data + 2);
    t->day    = (uint8_t)digit2(data + 4);
    t->hour   = (uint8_t)digit2(data + 6);
    t->minute = (uint8_t)digit2(data + 8);
    t->second = (uint8_t)digit2(data + 10);
    return 0;
}

static int parse_generaltime(const uint8_t *data, size_t len, neverc_x509_time_t *t) {
    if (len < 15) return -1;
    t->year   = (uint16_t)(digit2(data) * 100 + digit2(data + 2));
    t->month  = (uint8_t)digit2(data + 4);
    t->day    = (uint8_t)digit2(data + 6);
    t->hour   = (uint8_t)digit2(data + 8);
    t->minute = (uint8_t)digit2(data + 10);
    t->second = (uint8_t)digit2(data + 12);
    return 0;
}

/* ===== Public API ===== */

int neverc_x509_parse_certificate(neverc_x509_cert_t *cert,
                                    const uint8_t *der, size_t len) {
    memset(cert, 0, sizeof(*cert));
    cert->raw = der;
    cert->raw_len = len;

    asn1_reader_t root = {der, len, 0};
    asn1_reader_t cert_seq;
    if (asn1_enter_sequence(&root, &cert_seq) < 0) return -1;

    /* TBSCertificate */
    asn1_reader_t tbs;
    if (asn1_enter_sequence(&cert_seq, &tbs) < 0) return -1;

    /* Version (optional, context [0]) */
    cert->version = 0;
    if (tbs.pos < tbs.len && tbs.data[tbs.pos] == ASN1_TAG_CONTEXT_0) {
        uint8_t tag;
        const uint8_t *val;
        size_t vlen;
        if (asn1_read_tlv(&tbs, &tag, &val, &vlen) == 0 && vlen > 0) {
            asn1_reader_t vr = {val, vlen, 0};
            uint8_t itag;
            const uint8_t *ival;
            size_t ilen;
            if (asn1_read_tlv(&vr, &itag, &ival, &ilen) == 0 && ilen > 0)
                cert->version = (int)ival[ilen - 1];
        }
    }

    /* SerialNumber */
    {
        uint8_t tag;
        const uint8_t *val;
        size_t vlen;
        if (asn1_read_tlv(&tbs, &tag, &val, &vlen) < 0) return -1;
        if (tag == ASN1_TAG_INTEGER) {
            cert->serial_len = (int)(vlen > 20 ? 20 : vlen);
            memcpy(cert->serial, val, (size_t)cert->serial_len);
        }
    }

    /* Signature algorithm in TBS */
    {
        asn1_reader_t sig_seq;
        if (asn1_enter_sequence(&tbs, &sig_seq) < 0) return -1;
        uint8_t tag;
        const uint8_t *val;
        size_t vlen;
        if (asn1_read_tlv(&sig_seq, &tag, &val, &vlen) == 0 && tag == ASN1_TAG_OID)
            cert->sig_algorithm = identify_sig_algorithm(val, vlen);
    }

    /* Issuer */
    {
        uint8_t tag;
        const uint8_t *val;
        size_t vlen;
        if (asn1_read_tlv(&tbs, &tag, &val, &vlen) < 0) return -1;
        if (tag == ASN1_TAG_SEQUENCE) {
            asn1_reader_t name_r = {val, vlen, 0};
            parse_name(&name_r, &cert->issuer);
        }
    }

    /* Validity */
    {
        asn1_reader_t val_seq;
        if (asn1_enter_sequence(&tbs, &val_seq) < 0) return -1;
        uint8_t tag;
        const uint8_t *val;
        size_t vlen;
        if (asn1_read_tlv(&val_seq, &tag, &val, &vlen) == 0) {
            if (tag == ASN1_TAG_UTCTIME) parse_utctime(val, vlen, &cert->not_before);
            else if (tag == ASN1_TAG_GENERALTIME) parse_generaltime(val, vlen, &cert->not_before);
        }
        if (asn1_read_tlv(&val_seq, &tag, &val, &vlen) == 0) {
            if (tag == ASN1_TAG_UTCTIME) parse_utctime(val, vlen, &cert->not_after);
            else if (tag == ASN1_TAG_GENERALTIME) parse_generaltime(val, vlen, &cert->not_after);
        }
    }

    /* Subject */
    {
        uint8_t tag;
        const uint8_t *val;
        size_t vlen;
        if (asn1_read_tlv(&tbs, &tag, &val, &vlen) < 0) return -1;
        if (tag == ASN1_TAG_SEQUENCE) {
            asn1_reader_t name_r = {val, vlen, 0};
            parse_name(&name_r, &cert->subject);
        }
    }

    /* SubjectPublicKeyInfo */
    {
        asn1_reader_t spki;
        if (asn1_enter_sequence(&tbs, &spki) < 0) return -1;

        /* Algorithm */
        asn1_reader_t alg_seq;
        if (asn1_enter_sequence(&spki, &alg_seq) == 0) {
            uint8_t tag;
            const uint8_t *val;
            size_t vlen;
            if (asn1_read_tlv(&alg_seq, &tag, &val, &vlen) == 0 && tag == ASN1_TAG_OID)
                cert->key_algorithm = identify_key_algorithm(val, vlen);
        }

        /* Public key (BIT STRING) */
        uint8_t tag;
        const uint8_t *val;
        size_t vlen;
        if (asn1_read_tlv(&spki, &tag, &val, &vlen) == 0 && tag == ASN1_TAG_BIT_STRING) {
            if (vlen > 1) {
                cert->public_key_len = vlen - 1;
                cert->public_key = (uint8_t *)malloc(cert->public_key_len);
                if (cert->public_key)
                    memcpy(cert->public_key, val + 1, cert->public_key_len);
            }
        }
    }

    /* Extensions (optional, context [3]) */
    while (tbs.pos < tbs.len) {
        if (tbs.data[tbs.pos] == ASN1_TAG_CONTEXT_3) {
            uint8_t tag;
            const uint8_t *val;
            size_t vlen;
            if (asn1_read_tlv(&tbs, &tag, &val, &vlen) == 0) {
                asn1_reader_t ext_seq_r = {val, vlen, 0};
                asn1_reader_t exts;
                if (asn1_enter_sequence(&ext_seq_r, &exts) == 0) {
                    while (exts.pos < exts.len) {
                        asn1_reader_t ext;
                        if (asn1_enter_sequence(&exts, &ext) < 0) break;
                        uint8_t oid_tag;
                        const uint8_t *oid_val;
                        size_t oid_len;
                        if (asn1_read_tlv(&ext, &oid_tag, &oid_val, &oid_len) < 0) continue;
                        if (oid_tag != ASN1_TAG_OID) continue;

                        if (oid_equals(oid_val, oid_len, OID_BASIC_CONSTRAINTS,
                                       sizeof(OID_BASIC_CONSTRAINTS))) {
                            /* Skip optional BOOLEAN (critical flag) */
                            while (ext.pos < ext.len) {
                                uint8_t t2;
                                const uint8_t *v2;
                                size_t l2;
                                size_t save = ext.pos;
                                if (asn1_read_tlv(&ext, &t2, &v2, &l2) < 0) break;
                                if (t2 == ASN1_TAG_OCTET_STRING) {
                                    asn1_reader_t bc_r = {v2, l2, 0};
                                    asn1_reader_t bc;
                                    if (asn1_enter_sequence(&bc_r, &bc) == 0) {
                                        if (bc.pos < bc.len) {
                                            uint8_t bt;
                                            const uint8_t *bv;
                                            size_t bl;
                                            if (asn1_read_tlv(&bc, &bt, &bv, &bl) == 0) {
                                                if (bt == 0x01 && bl > 0 && bv[0])
                                                    cert->is_ca = 1;
                                            }
                                        }
                                    }
                                    break;
                                }
                                (void)save;
                            }
                        }
                    }
                }
            }
        } else {
            uint8_t tag;
            const uint8_t *val;
            size_t vlen;
            if (asn1_read_tlv(&tbs, &tag, &val, &vlen) < 0) break;
        }
    }

    return 0;
}

void neverc_x509_cert_free(neverc_x509_cert_t *cert) {
    free(cert->public_key);
    cert->public_key = NULL;
    cert->public_key_len = 0;
}

int neverc_x509_is_self_signed(const neverc_x509_cert_t *cert) {
    return strcmp(cert->issuer.common_name, cert->subject.common_name) == 0 &&
           strcmp(cert->issuer.organization, cert->subject.organization) == 0 &&
           strcmp(cert->issuer.country, cert->subject.country) == 0;
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

int neverc_x509_format_name(const neverc_x509_name_t *name,
                              char *buf, size_t buf_size) {
    int written = 0;
    buf[0] = '\0';
    if (name->country[0]) {
        int n = snprintf(buf + written, buf_size - (size_t)written,
                         "C=%s", name->country);
        if (n < 0) return -1;
        written += n;
    }
    if (name->province[0]) {
        int n = snprintf(buf + written, buf_size - (size_t)written,
                         "%sST=%s", written ? ", " : "", name->province);
        if (n < 0) return -1;
        written += n;
    }
    if (name->locality[0]) {
        int n = snprintf(buf + written, buf_size - (size_t)written,
                         "%sL=%s", written ? ", " : "", name->locality);
        if (n < 0) return -1;
        written += n;
    }
    if (name->organization[0]) {
        int n = snprintf(buf + written, buf_size - (size_t)written,
                         "%sO=%s", written ? ", " : "", name->organization);
        if (n < 0) return -1;
        written += n;
    }
    if (name->organizational_unit[0]) {
        int n = snprintf(buf + written, buf_size - (size_t)written,
                         "%sOU=%s", written ? ", " : "", name->organizational_unit);
        if (n < 0) return -1;
        written += n;
    }
    if (name->common_name[0]) {
        int n = snprintf(buf + written, buf_size - (size_t)written,
                         "%sCN=%s", written ? ", " : "", name->common_name);
        if (n < 0) return -1;
        written += n;
    }
    return written;
}

int neverc_x509_time_compare(const neverc_x509_time_t *a,
                               const neverc_x509_time_t *b) {
    if (a->year != b->year) return a->year < b->year ? -1 : 1;
    if (a->month != b->month) return a->month < b->month ? -1 : 1;
    if (a->day != b->day) return a->day < b->day ? -1 : 1;
    if (a->hour != b->hour) return a->hour < b->hour ? -1 : 1;
    if (a->minute != b->minute) return a->minute < b->minute ? -1 : 1;
    if (a->second != b->second) return a->second < b->second ? -1 : 1;
    return 0;
}
