#include "tls_key.h"

#include "neverc/std/_platform.h"
#include "neverc/std/crypto/ecdsa.h"
#include "neverc/std/crypto/elliptic.h"
#include "neverc/std/crypto/x509.h"

#include <stdint.h>
#include <string.h>

#define DER_INTEGER       0x02
#define DER_OCTET_STRING  0x04
#define DER_OBJECT_ID     0x06
#define DER_BIT_STRING    0x03
#define DER_SEQUENCE      0x30
#define DER_CONTEXT_0     0xa0
#define DER_CONTEXT_1     0xa1

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
} tls_der_reader_t;

static const uint8_t k_oid_prime256v1[] = {
    0x2a, 0x86, 0x48, 0xce, 0x3d, 0x03, 0x01, 0x07
};

static int tls_der_read_length(tls_der_reader_t *reader, size_t *length) {
    if (!reader || !length || reader->pos >= reader->len)
        return -1;

    uint8_t first = reader->data[reader->pos++];
    if (first < 0x80) {
        *length = first;
        return 0;
    }

    size_t count = first & 0x7f;
    if (count == 0 || count > sizeof(size_t) ||
        count > reader->len - reader->pos ||
        reader->data[reader->pos] == 0)
        return -1;

    size_t result = 0;
    for (size_t i = 0; i < count; ++i) {
        if (result > (SIZE_MAX >> 8))
            return -1;
        result = (result << 8) | reader->data[reader->pos++];
    }
    if (result < 0x80 || result > reader->len - reader->pos)
        return -1;
    *length = result;
    return 0;
}

static int tls_der_read_tlv(tls_der_reader_t *reader, uint8_t *tag,
                            const uint8_t **value, size_t *value_len) {
    if (!reader || !tag || !value || !value_len ||
        reader->pos >= reader->len)
        return -1;

    *tag = reader->data[reader->pos++];
    if (tls_der_read_length(reader, value_len) != 0 ||
        *value_len > reader->len - reader->pos)
        return -1;
    *value = reader->data + reader->pos;
    reader->pos += *value_len;
    return 0;
}

static int tls_der_enter_sequence(tls_der_reader_t *reader,
                                  tls_der_reader_t *sequence) {
    uint8_t tag;
    const uint8_t *value;
    size_t value_len;
    if (tls_der_read_tlv(reader, &tag, &value, &value_len) != 0 ||
        tag != DER_SEQUENCE)
        return -1;
    sequence->data = value;
    sequence->len = value_len;
    sequence->pos = 0;
    return 0;
}

static int tls_bigint_from_bytes(neverc_bigint_t *value,
                                 const uint8_t *bytes, size_t len) {
    if (!value || !bytes || len == 0 || len > 32)
        return -1;

    char hex[65];
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        hex[i * 2] = digits[bytes[i] >> 4];
        hex[i * 2 + 1] = digits[bytes[i] & 0x0f];
    }
    hex[len * 2] = '\0';
    return neverc_bigint_set_string(value, hex, 16);
}

static int tls_parse_p256_private_key(
    const uint8_t *der, size_t der_len,
    neverc_ecdsa_private_key_t *private_key) {
    if (!der || der_len == 0 || !private_key)
        return -1;

    tls_der_reader_t root = {der, der_len, 0};
    tls_der_reader_t sequence;
    if (tls_der_enter_sequence(&root, &sequence) != 0 ||
        root.pos != root.len)
        return -1;

    uint8_t tag;
    const uint8_t *value;
    size_t value_len;
    if (tls_der_read_tlv(&sequence, &tag, &value, &value_len) != 0 ||
        tag != DER_INTEGER || value_len != 1 || value[0] != 1)
        return -1;
    if (tls_der_read_tlv(&sequence, &tag, &value, &value_len) != 0 ||
        tag != DER_OCTET_STRING || value_len != 32 ||
        tls_bigint_from_bytes(&private_key->d, value, value_len) != 0)
        return -1;

    const uint8_t *encoded_public_key = NULL;
    size_t encoded_public_key_len = 0;
    int saw_curve = 0;
    int saw_public_key = 0;
    while (sequence.pos < sequence.len) {
        if (tls_der_read_tlv(
                &sequence, &tag, &value, &value_len) != 0)
            return -1;
        if (tag == DER_CONTEXT_0 && !saw_curve && !saw_public_key) {
            tls_der_reader_t parameters = {value, value_len, 0};
            const uint8_t *oid;
            size_t oid_len;
            if (tls_der_read_tlv(
                    &parameters, &tag, &oid, &oid_len) != 0 ||
                tag != DER_OBJECT_ID ||
                oid_len != sizeof(k_oid_prime256v1) ||
                memcmp(oid, k_oid_prime256v1, oid_len) != 0 ||
                parameters.pos != parameters.len)
                return -1;
            saw_curve = 1;
        } else if (tag == DER_CONTEXT_1 && !saw_public_key) {
            tls_der_reader_t public_key = {value, value_len, 0};
            const uint8_t *bits;
            size_t bits_len;
            if (tls_der_read_tlv(
                    &public_key, &tag, &bits, &bits_len) != 0 ||
                tag != DER_BIT_STRING || bits_len != 66 ||
                bits[0] != 0 || public_key.pos != public_key.len)
                return -1;
            encoded_public_key = bits + 1;
            encoded_public_key_len = bits_len - 1;
            saw_public_key = 1;
        } else {
            return -1;
        }
    }
    if (!saw_curve)
        return -1;

    const neverc_elliptic_curve_t *curve = neverc_elliptic_p256();
    if (!curve || neverc_bigint_sign(&private_key->d) <= 0 ||
        neverc_bigint_cmp(&private_key->d, &curve->n) >= 0)
        return -1;

    private_key->pub.curve = curve;
    neverc_elliptic_scalar_base_mult(
        curve, &private_key->pub.pub, &private_key->d);
    if (!neverc_elliptic_is_on_curve(curve, &private_key->pub.pub))
        return -1;

    if (encoded_public_key) {
        uint8_t derived_public_key[65];
        size_t derived_public_key_len = 0;
        if (neverc_elliptic_marshal(
                curve, &private_key->pub.pub,
                derived_public_key, sizeof(derived_public_key),
                &derived_public_key_len) != 0 ||
            derived_public_key_len != encoded_public_key_len ||
            memcmp(derived_public_key, encoded_public_key,
                   encoded_public_key_len) != 0)
            return -1;
    }
    return 0;
}

static int tls_der_encode_positive_bigint(
    const neverc_bigint_t *value, uint8_t out[35], size_t *out_len) {
    if (!value || !out || !out_len || neverc_bigint_sign(value) <= 0)
        return -1;

    int bit_len = neverc_bigint_bit_len(value);
    size_t value_len = (size_t)(bit_len + 7) / 8;
    if (value_len == 0 || value_len > 32)
        return -1;

    uint8_t bytes[32];
    for (size_t i = 0; i < value_len; ++i) {
        size_t byte_index = value_len - 1 - i;
        uint8_t byte = 0;
        for (unsigned bit = 0; bit < 8; ++bit) {
            if (neverc_bigint_bit(value,
                                  (unsigned)(byte_index * 8 + bit)))
                byte |= (uint8_t)(1u << bit);
        }
        bytes[i] = byte;
    }

    size_t prefix_len = (bytes[0] & 0x80) != 0 ? 1 : 0;
    out[0] = DER_INTEGER;
    out[1] = (uint8_t)(value_len + prefix_len);
    if (prefix_len)
        out[2] = 0;
    memcpy(out + 2 + prefix_len, bytes, value_len);
    *out_len = 2 + prefix_len + value_len;
    neverc_platform_secure_zero(bytes, sizeof(bytes));
    return 0;
}

int nci_tls_sign_ecdsa_p256_sha256(
    const uint8_t *private_key_der,
    size_t private_key_der_len,
    const uint8_t *digest,
    size_t digest_len,
    uint8_t *signature,
    size_t signature_capacity,
    size_t *signature_len) {
    if (!private_key_der || private_key_der_len == 0 ||
        !digest || digest_len != 32 || !signature || !signature_len)
        return -1;

    neverc_ecdsa_private_key_t private_key;
    neverc_ecdsa_private_key_init(&private_key);
    int result = tls_parse_p256_private_key(
        private_key_der, private_key_der_len, &private_key);

    neverc_ecdsa_signature_t ecdsa_signature;
    neverc_ecdsa_signature_init(&ecdsa_signature);
    if (result == 0)
        result = neverc_ecdsa_sign(
            &private_key, digest, digest_len, &ecdsa_signature);

    if (result == 0) {
        neverc_bigint_t half_order;
        neverc_bigint_t normalized_s;
        neverc_bigint_init(&half_order);
        neverc_bigint_init(&normalized_s);
        neverc_bigint_rsh(
            &half_order, &private_key.pub.curve->n, 1);
        if (neverc_bigint_cmp(
                &ecdsa_signature.s, &half_order) > 0) {
            neverc_bigint_sub(
                &normalized_s, &private_key.pub.curve->n,
                &ecdsa_signature.s);
            neverc_bigint_set(&ecdsa_signature.s, &normalized_s);
        }
        neverc_bigint_free(&normalized_s);
        neverc_bigint_free(&half_order);
    }

    uint8_t r_der[35];
    uint8_t s_der[35];
    size_t r_der_len = 0;
    size_t s_der_len = 0;
    if (result == 0)
        result = tls_der_encode_positive_bigint(
            &ecdsa_signature.r, r_der, &r_der_len);
    if (result == 0)
        result = tls_der_encode_positive_bigint(
            &ecdsa_signature.s, s_der, &s_der_len);
    if (result == 0) {
        size_t payload_len = r_der_len + s_der_len;
        if (payload_len > 127 ||
            signature_capacity < payload_len + 2) {
            result = -1;
        } else {
            signature[0] = DER_SEQUENCE;
            signature[1] = (uint8_t)payload_len;
            memcpy(signature + 2, r_der, r_der_len);
            memcpy(signature + 2 + r_der_len, s_der, s_der_len);
            *signature_len = payload_len + 2;
        }
    }

    neverc_platform_secure_zero(r_der, sizeof(r_der));
    neverc_platform_secure_zero(s_der, sizeof(s_der));
    neverc_ecdsa_signature_free(&ecdsa_signature);
    neverc_ecdsa_private_key_free(&private_key);
    return result;
}

int nci_tls_validate_certificate_key_pair(
    const uint8_t *certificate_der,
    size_t certificate_der_len,
    const uint8_t *private_key_der,
    size_t private_key_der_len,
    int key_type) {
    if (!certificate_der || certificate_der_len == 0 ||
        !private_key_der || private_key_der_len == 0 ||
        key_type != NCI_TLS_KEY_ECDSA_P256)
        return -1;

    neverc_x509_cert_t certificate;
    int result = neverc_x509_parse_certificate(
        &certificate, certificate_der, certificate_der_len);
    if (result != 0) {
        neverc_x509_cert_free(&certificate);
        return -1;
    }
    if (certificate.key_algorithm != NEVERC_X509_KEY_ECDSA ||
        certificate.public_key_curve != NEVERC_X509_CURVE_P256 ||
        !certificate.public_key || certificate.public_key_len != 65 ||
        (certificate.key_usage_present &&
         (certificate.key_usage &
          NEVERC_X509_KEY_USAGE_DIGITAL_SIGNATURE) == 0)) {
        neverc_x509_cert_free(&certificate);
        return -1;
    }

    neverc_ecdsa_private_key_t private_key;
    neverc_ecdsa_private_key_init(&private_key);
    result = tls_parse_p256_private_key(
        private_key_der, private_key_der_len, &private_key);
    if (result == 0) {
        uint8_t derived_public_key[65];
        size_t derived_public_key_len = 0;
        result = neverc_elliptic_marshal(
            private_key.pub.curve, &private_key.pub.pub,
            derived_public_key, sizeof(derived_public_key),
            &derived_public_key_len);
        if (result == 0 &&
            (derived_public_key_len != certificate.public_key_len ||
             memcmp(derived_public_key, certificate.public_key,
                    derived_public_key_len) != 0))
            result = -1;
    }

    neverc_ecdsa_private_key_free(&private_key);
    neverc_x509_cert_free(&certificate);
    return result;
}
