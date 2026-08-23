/*
 * X.509 certificate signature verification.
 *
 * This file intentionally keeps cryptographic dependencies separate from the
 * DER parser so parse-only callers do not pull the public-key implementations.
 */
#include "neverc/std/crypto/x509.h"
#include "neverc/std/crypto/ecdsa.h"
#include "neverc/std/crypto/ed25519.h"
#include "neverc/std/crypto/elliptic.h"
#include "neverc/std/crypto/rsa.h"
#include "neverc/std/crypto/sha256.h"
#include "neverc/std/crypto/sha384.h"
#include "neverc/std/crypto/sha512.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>

#define X509_ASN1_INTEGER  0x02
#define X509_ASN1_SEQUENCE 0x30
#define X509_RSA_MAX_BYTES 512U
#define X509_MAX_CHAIN_DEPTH 16U

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
} x509_der_reader_t;

static int x509_der_read_length(x509_der_reader_t *reader, size_t *length) {
    if (!reader || !length || reader->pos >= reader->len)
        return -1;

    uint8_t first = reader->data[reader->pos++];
    if (first < 0x80) {
        *length = first;
        return 0;
    }

    unsigned count = first & 0x7f;
    if (count == 0 || count > 4 ||
        (size_t)count > reader->len - reader->pos ||
        reader->data[reader->pos] == 0)
        return -1;

    size_t result = 0;
    for (unsigned i = 0; i < count; ++i) {
        if (result > (SIZE_MAX >> 8))
            return -1;
        result = (result << 8) | reader->data[reader->pos++];
    }
    if (result < 0x80)
        return -1;
    *length = result;
    return 0;
}

static int x509_der_read_tlv(x509_der_reader_t *reader, uint8_t *tag,
                             const uint8_t **value, size_t *value_len) {
    if (!reader || !tag || !value || !value_len ||
        reader->pos >= reader->len)
        return -1;

    *tag = reader->data[reader->pos++];
    if (x509_der_read_length(reader, value_len) != 0 ||
        *value_len > reader->len - reader->pos)
        return -1;
    *value = reader->data + reader->pos;
    reader->pos += *value_len;
    return 0;
}

static int x509_der_positive_integer(x509_der_reader_t *reader,
                                     const uint8_t **value,
                                     size_t *value_len) {
    uint8_t tag;
    if (x509_der_read_tlv(reader, &tag, value, value_len) != 0 ||
        tag != X509_ASN1_INTEGER || *value_len == 0)
        return -1;

    if (((*value)[0] & 0x80) != 0)
        return -1;
    if ((*value)[0] == 0) {
        if (*value_len == 1 || (((*value)[1] & 0x80) == 0))
            return -1;
        ++*value;
        --*value_len;
    }
    return 0;
}

static int x509_bigint_from_bytes(neverc_bigint_t *value,
                                  const uint8_t *bytes, size_t len) {
    if (!value || !bytes || len == 0 || len > X509_RSA_MAX_BYTES)
        return -1;
    if (len > (SIZE_MAX - 1) / 2)
        return -1;

    char *hex = (char *)malloc(len * 2 + 1);
    if (!hex)
        return -1;
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < len; ++i) {
        hex[i * 2] = digits[bytes[i] >> 4];
        hex[i * 2 + 1] = digits[bytes[i] & 0x0f];
    }
    hex[len * 2] = '\0';
    int result = neverc_bigint_set_string(value, hex, 16);
    free(hex);
    return result;
}

static int x509_parse_rsa_public_key(const neverc_x509_cert_t *parent,
                                     neverc_rsa_public_key_t *public_key) {
    if (!parent || !public_key || !parent->public_key ||
        parent->public_key_len == 0)
        return -1;

    x509_der_reader_t root = {
        parent->public_key, parent->public_key_len, 0
    };
    uint8_t tag;
    const uint8_t *sequence_value;
    size_t sequence_len;
    if (x509_der_read_tlv(&root, &tag, &sequence_value, &sequence_len) != 0 ||
        tag != X509_ASN1_SEQUENCE || root.pos != root.len)
        return -1;

    x509_der_reader_t sequence = {sequence_value, sequence_len, 0};
    const uint8_t *modulus;
    const uint8_t *exponent;
    size_t modulus_len;
    size_t exponent_len;
    if (x509_der_positive_integer(&sequence, &modulus, &modulus_len) != 0 ||
        x509_der_positive_integer(&sequence, &exponent, &exponent_len) != 0 ||
        sequence.pos != sequence.len || modulus_len > X509_RSA_MAX_BYTES ||
        exponent_len > sizeof(uint32_t))
        return -1;

    if (x509_bigint_from_bytes(&public_key->n, modulus, modulus_len) != 0 ||
        x509_bigint_from_bytes(&public_key->e, exponent, exponent_len) != 0 ||
        neverc_bigint_bit_len(&public_key->n) < 512 ||
        neverc_bigint_bit(&public_key->n, 0) == 0 ||
        neverc_bigint_uint64(&public_key->e) < 3 ||
        neverc_bigint_bit(&public_key->e, 0) == 0)
        return -1;
    return 0;
}

static int x509_check_rsa_pkcs1v15(const neverc_x509_cert_t *cert,
                                   const neverc_x509_cert_t *parent) {
    uint8_t digest[NEVERC_SHA512_DIGEST_SIZE];
    size_t digest_len;
    if (cert->sig_algorithm == NEVERC_X509_SIG_SHA256_RSA) {
        neverc_sha256_sum(cert->raw_tbs, cert->raw_tbs_len, digest);
        digest_len = NEVERC_SHA256_DIGEST_SIZE;
    } else if (cert->sig_algorithm == NEVERC_X509_SIG_SHA384_RSA) {
        neverc_sha384_sum(cert->raw_tbs, cert->raw_tbs_len, digest);
        digest_len = NEVERC_SHA384_DIGEST_SIZE;
    } else if (cert->sig_algorithm == NEVERC_X509_SIG_SHA512_RSA) {
        neverc_sha512_sum(cert->raw_tbs, cert->raw_tbs_len, digest);
        digest_len = NEVERC_SHA512_DIGEST_SIZE;
    } else {
        return -1;
    }

    neverc_rsa_public_key_t public_key;
    neverc_rsa_public_key_init(&public_key);
    int result = x509_parse_rsa_public_key(parent, &public_key);
    if (result == 0) {
        neverc_bigint_t signature_value;
        neverc_bigint_init(&signature_value);
        result = x509_bigint_from_bytes(
            &signature_value, cert->signature, cert->signature_len);
        if (result == 0 &&
            neverc_bigint_cmp(&signature_value, &public_key.n) >= 0)
            result = -1;
        neverc_bigint_free(&signature_value);
    }
    if (result == 0) {
        if (cert->sig_algorithm == NEVERC_X509_SIG_SHA256_RSA) {
            result = neverc_rsa_verify_pkcs1v15_sha256(
                &public_key, digest, digest_len,
                cert->signature, cert->signature_len);
        } else if (cert->sig_algorithm == NEVERC_X509_SIG_SHA384_RSA) {
            result = neverc_rsa_verify_pkcs1v15_sha384(
                &public_key, digest, digest_len,
                cert->signature, cert->signature_len);
        } else {
            result = neverc_rsa_verify_pkcs1v15_sha512(
                &public_key, digest, digest_len,
                cert->signature, cert->signature_len);
        }
    }
    neverc_rsa_public_key_free(&public_key);
    return result;
}

static int x509_check_rsa_pss(const neverc_x509_cert_t *cert,
                              const neverc_x509_cert_t *parent) {
    uint8_t digest[NEVERC_SHA512_DIGEST_SIZE];
    size_t digest_len;
    if (cert->sig_algorithm == NEVERC_X509_SIG_RSA_PSS_SHA256) {
        neverc_sha256_sum(cert->raw_tbs, cert->raw_tbs_len, digest);
        digest_len = NEVERC_SHA256_DIGEST_SIZE;
    } else if (cert->sig_algorithm == NEVERC_X509_SIG_RSA_PSS_SHA384) {
        neverc_sha384_sum(cert->raw_tbs, cert->raw_tbs_len, digest);
        digest_len = NEVERC_SHA384_DIGEST_SIZE;
    } else if (cert->sig_algorithm == NEVERC_X509_SIG_RSA_PSS_SHA512) {
        neverc_sha512_sum(cert->raw_tbs, cert->raw_tbs_len, digest);
        digest_len = NEVERC_SHA512_DIGEST_SIZE;
    } else {
        return -1;
    }

    neverc_rsa_public_key_t public_key;
    neverc_rsa_public_key_init(&public_key);
    int result = x509_parse_rsa_public_key(parent, &public_key);
    if (result == 0) {
        if (cert->sig_algorithm == NEVERC_X509_SIG_RSA_PSS_SHA256)
            result = neverc_rsa_verify_pss_sha256(
                &public_key, digest, digest_len,
                cert->signature, cert->signature_len);
        else if (cert->sig_algorithm == NEVERC_X509_SIG_RSA_PSS_SHA384)
            result = neverc_rsa_verify_pss_sha384(
                &public_key, digest, digest_len,
                cert->signature, cert->signature_len);
        else
            result = neverc_rsa_verify_pss_sha512(
                &public_key, digest, digest_len,
                cert->signature, cert->signature_len);
    }
    neverc_rsa_public_key_free(&public_key);
    return result;
}

static int x509_parse_ecdsa_signature(
    const neverc_x509_cert_t *cert, neverc_ecdsa_signature_t *signature,
    size_t scalar_bytes) {
    x509_der_reader_t root = {cert->signature, cert->signature_len, 0};
    uint8_t tag;
    const uint8_t *sequence_value;
    size_t sequence_len;
    if (x509_der_read_tlv(&root, &tag, &sequence_value, &sequence_len) != 0 ||
        tag != X509_ASN1_SEQUENCE || root.pos != root.len)
        return -1;

    x509_der_reader_t sequence = {sequence_value, sequence_len, 0};
    const uint8_t *r;
    const uint8_t *s;
    size_t r_len;
    size_t s_len;
    if (x509_der_positive_integer(&sequence, &r, &r_len) != 0 ||
        x509_der_positive_integer(&sequence, &s, &s_len) != 0 ||
        sequence.pos != sequence.len || r_len > scalar_bytes ||
        s_len > scalar_bytes ||
        x509_bigint_from_bytes(&signature->r, r, r_len) != 0 ||
        x509_bigint_from_bytes(&signature->s, s, s_len) != 0)
        return -1;
    return 0;
}

static int x509_check_ecdsa(const neverc_x509_cert_t *cert,
                            const neverc_x509_cert_t *parent) {
    const neverc_elliptic_curve_t *curve = NULL;
    if (parent->public_key_curve == NEVERC_X509_CURVE_P256)
        curve = neverc_elliptic_p256();
    else if (parent->public_key_curve == NEVERC_X509_CURVE_P384)
        curve = neverc_elliptic_p384();
    if (!curve)
        return -1;

    uint8_t digest[NEVERC_SHA384_DIGEST_SIZE];
    size_t digest_len;
    if (cert->sig_algorithm == NEVERC_X509_SIG_ECDSA_SHA256) {
        neverc_sha256_sum(cert->raw_tbs, cert->raw_tbs_len, digest);
        digest_len = NEVERC_SHA256_DIGEST_SIZE;
    } else if (cert->sig_algorithm == NEVERC_X509_SIG_ECDSA_SHA384) {
        neverc_sha384_sum(cert->raw_tbs, cert->raw_tbs_len, digest);
        digest_len = NEVERC_SHA384_DIGEST_SIZE;
    } else {
        return -1;
    }

    neverc_ecdsa_public_key_t public_key;
    neverc_ecdsa_public_key_init(&public_key);
    public_key.curve = curve;
    int result = neverc_elliptic_unmarshal(
        curve, &public_key.pub, parent->public_key,
        parent->public_key_len);
    if (result == 0 &&
        neverc_bigint_is_zero(&public_key.pub.x) &&
        neverc_bigint_is_zero(&public_key.pub.y))
        result = -1;

    neverc_ecdsa_signature_t signature;
    neverc_ecdsa_signature_init(&signature);
    if (result == 0)
        result = x509_parse_ecdsa_signature(
            cert, &signature, (size_t)(curve->bit_size + 7) / 8);
    if (result == 0)
        result = neverc_ecdsa_verify(
            &public_key, digest, digest_len, &signature);

    neverc_ecdsa_signature_free(&signature);
    neverc_ecdsa_public_key_free(&public_key);
    return result;
}

static int x509_check_signature(const neverc_x509_cert_t *cert,
                                const neverc_x509_cert_t *parent) {
    if (!cert || !parent || !cert->raw_tbs || cert->raw_tbs_len == 0 ||
        !cert->signature || cert->signature_len == 0)
        return -1;

    if ((cert->sig_algorithm == NEVERC_X509_SIG_SHA256_RSA ||
         cert->sig_algorithm == NEVERC_X509_SIG_SHA384_RSA ||
         cert->sig_algorithm == NEVERC_X509_SIG_SHA512_RSA) &&
        parent->key_algorithm == NEVERC_X509_KEY_RSA)
        return x509_check_rsa_pkcs1v15(cert, parent);
    if ((cert->sig_algorithm == NEVERC_X509_SIG_RSA_PSS_SHA256 ||
         cert->sig_algorithm == NEVERC_X509_SIG_RSA_PSS_SHA384 ||
         cert->sig_algorithm == NEVERC_X509_SIG_RSA_PSS_SHA512) &&
        parent->key_algorithm == NEVERC_X509_KEY_RSA)
        return x509_check_rsa_pss(cert, parent);
    if ((cert->sig_algorithm == NEVERC_X509_SIG_ECDSA_SHA256 ||
         cert->sig_algorithm == NEVERC_X509_SIG_ECDSA_SHA384) &&
        parent->key_algorithm == NEVERC_X509_KEY_ECDSA)
        return x509_check_ecdsa(cert, parent);
    if (cert->sig_algorithm == NEVERC_X509_SIG_ED25519 &&
        parent->key_algorithm == NEVERC_X509_KEY_ED25519 &&
        parent->public_key_len == NEVERC_ED25519_PUBLIC_KEY_SIZE &&
        cert->signature_len == NEVERC_ED25519_SIGNATURE_SIZE)
        return neverc_ed25519_verify(
            parent->public_key, cert->raw_tbs, cert->raw_tbs_len,
            cert->signature);
    return -1;
}

int neverc_x509_check_signature_from(const neverc_x509_cert_t *cert,
                                     const neverc_x509_cert_t *parent) {
    if (!cert || !parent)
        return -1;

    if ((parent->version == 2 && !parent->basic_constraints_valid) ||
        (parent->basic_constraints_valid && !parent->is_ca) ||
        (parent->key_usage_present &&
         (parent->key_usage & NEVERC_X509_KEY_USAGE_CERT_SIGN) == 0))
        return -1;

    return x509_check_signature(cert, parent);
}

int neverc_x509_verify_signature(const neverc_x509_cert_t *certificate,
                                 int signature_algorithm,
                                 const uint8_t *signed_data,
                                 size_t signed_data_len,
                                 const uint8_t *signature,
                                 size_t signature_len) {
    if (!certificate || !signed_data || signed_data_len == 0 ||
        !signature || signature_len == 0)
        return -1;

    neverc_x509_cert_t signed_object;
    memset(&signed_object, 0, sizeof(signed_object));
    signed_object.sig_algorithm = signature_algorithm;
    signed_object.raw_tbs = signed_data;
    signed_object.raw_tbs_len = signed_data_len;
    signed_object.signature = signature;
    signed_object.signature_len = signature_len;
    return x509_check_signature(&signed_object, certificate);
}

static int x509_names_equal(const uint8_t *left, size_t left_len,
                            const uint8_t *right, size_t right_len) {
    return left && right && left_len == right_len &&
           memcmp(left, right, left_len) == 0;
}

static int x509_allows_ext_key_usage(const neverc_x509_cert_t *cert,
                                     uint32_t required) {
    if (required == 0 || !cert->ext_key_usage_present)
        return 1;
    return (cert->ext_key_usage & NEVERC_X509_EXT_KEY_USAGE_ANY) != 0 ||
           (cert->ext_key_usage & required) == required;
}

static int x509_ascii_lower(int ch) {
    return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
}

static int x509_ascii_equal(const char *a, const char *b, size_t len) {
    for (size_t i = 0; i < len; ++i) {
        if (x509_ascii_lower((unsigned char)a[i]) !=
            x509_ascii_lower((unsigned char)b[i]))
            return 0;
    }
    return 1;
}

static size_t x509_dns_stripped_len(const char *name) {
    size_t len = strlen(name);
    if (len > 0 && name[len - 1] == '.')
        --len;
    return len;
}

/* RFC 5280 4.2.1.10 plus the common leading-dot subdomain-only form. */
static int x509_dns_constraint_matches(const char *constraint,
                                       const char *name) {
    if (!constraint || !name)
        return 0;
    size_t constraint_len = x509_dns_stripped_len(constraint);
    size_t name_len = x509_dns_stripped_len(name);
    /* An empty constraint (or ".") must not match every name. */
    if (constraint_len == 0)
        return 0;

    int subdomain_only = constraint[0] == '.';
    if (subdomain_only) {
        ++constraint;
        --constraint_len;
        if (constraint_len == 0)
            return 0;
    }
    if (name_len < constraint_len)
        return 0;
    if (name_len == constraint_len)
        return !subdomain_only &&
               x509_ascii_equal(constraint, name, constraint_len);
    if (name[name_len - constraint_len - 1] != '.')
        return 0;
    return x509_ascii_equal(
        name + name_len - constraint_len, constraint, constraint_len);
}

static int x509_ip_is_v4_mapped(const uint8_t *ip, size_t len) {
    static const uint8_t prefix[12] = {
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0xff, 0xff
    };
    return len == 16 && ip && memcmp(ip, prefix, 12) == 0;
}

static int x509_ip_constraint_matches(
    const neverc_x509_ip_network_t *network,
    const neverc_x509_ip_address_t *address) {
    const uint8_t *addr;
    uint8_t addr_len;
    uint8_t i;
    if (!network || !address)
        return 0;
    addr = address->bytes;
    addr_len = address->len;
    if (x509_ip_is_v4_mapped(addr, addr_len)) {
        addr += 12;
        addr_len = 4;
    }
    if (network->len != addr_len)
        return 0;
    for (i = 0; i < addr_len; ++i) {
        if ((addr[i] & network->mask[i]) !=
            (network->bytes[i] & network->mask[i]))
            return 0;
    }
    return 1;
}

#define X509_DNS_PROBE_MAX 256

/* `*.suffix` SAN/CN identities assert every single-label child of suffix.
 * Constraints must be applied to that set, not the literal "*.suffix" string. */
static int x509_wildcard_dns_suffix(const char *name, const char **suffix,
                                    size_t *suffix_len) {
    if (!name || !suffix || !suffix_len)
        return 0;
    size_t len = x509_dns_stripped_len(name);
    if (len < 5 || name[0] != '*' || name[1] != '.')
        return 0;
    *suffix = name + 2;
    *suffix_len = len - 2;
    if (*suffix_len == 0 || *suffix_len > 253)
        return 0;
    int saw_dot = 0;
    for (size_t i = 0; i < *suffix_len; ++i) {
        if ((*suffix)[i] == '.')
            saw_dot = 1;
    }
    return saw_dot;
}

static int x509_wildcard_probes(const char *suffix, size_t suffix_len,
                                char *probe0, char *probe1) {
    if (!suffix || suffix_len == 0 || suffix_len > X509_DNS_PROBE_MAX - 3)
        return 0;
    probe0[0] = '0';
    probe0[1] = '.';
    memcpy(probe0 + 2, suffix, suffix_len);
    probe0[2 + suffix_len] = '\0';
    probe1[0] = '1';
    probe1[1] = '.';
    memcpy(probe1 + 2, suffix, suffix_len);
    probe1[2 + suffix_len] = '\0';
    return 1;
}

static int x509_wildcard_covers_hostname(const char *suffix, size_t suffix_len,
                                         const char *hostname) {
    if (!suffix || !hostname || suffix_len == 0)
        return 0;
    size_t name_len = x509_dns_stripped_len(hostname);
    if (name_len <= suffix_len + 1)
        return 0;
    if (hostname[name_len - suffix_len - 1] != '.')
        return 0;
    if (!x509_ascii_equal(hostname + name_len - suffix_len, suffix,
                          suffix_len))
        return 0;
    size_t label_len = name_len - suffix_len - 1;
    for (size_t i = 0; i < label_len; ++i) {
        if (hostname[i] == '.')
            return 0;
    }
    return 1;
}

static int x509_dns_name_permitted(const char *name, char *const *permitted,
                                   size_t permitted_count) {
    if (permitted_count == 0)
        return 1;
    if (!name)
        return 0;
    const char *suffix;
    size_t suffix_len;
    if (x509_wildcard_dns_suffix(name, &suffix, &suffix_len)) {
        char probe0[X509_DNS_PROBE_MAX];
        char probe1[X509_DNS_PROBE_MAX];
        if (!x509_wildcard_probes(suffix, suffix_len, probe0, probe1))
            return 0;
        for (size_t j = 0; j < permitted_count; ++j) {
            if (x509_dns_constraint_matches(permitted[j], probe0) &&
                x509_dns_constraint_matches(permitted[j], probe1))
                return 1;
        }
        return 0;
    }
    for (size_t j = 0; j < permitted_count; ++j) {
        if (x509_dns_constraint_matches(permitted[j], name))
            return 1;
    }
    return 0;
}

static int x509_dns_name_excluded(const char *name, char *const *excluded,
                                  size_t excluded_count) {
    if (!name || excluded_count == 0)
        return 0;
    const char *suffix;
    size_t suffix_len;
    int wildcard = x509_wildcard_dns_suffix(name, &suffix, &suffix_len);
    char probe0[X509_DNS_PROBE_MAX];
    char probe1[X509_DNS_PROBE_MAX];
    if (wildcard &&
        !x509_wildcard_probes(suffix, suffix_len, probe0, probe1))
        return 1;
    for (size_t j = 0; j < excluded_count; ++j) {
        if (!excluded[j])
            continue;
        if (wildcard) {
            if (x509_dns_constraint_matches(excluded[j], probe0) ||
                x509_dns_constraint_matches(excluded[j], probe1))
                return 1;
            if (excluded[j][0] != '.' &&
                x509_wildcard_covers_hostname(
                    suffix, suffix_len, excluded[j]))
                return 1;
        } else if (x509_dns_constraint_matches(excluded[j], name)) {
            return 1;
        }
    }
    return 0;
}

static int x509_dns_names_permitted(char *const *names, size_t name_count,
                                    char *const *permitted,
                                    size_t permitted_count) {
    if (permitted_count == 0)
        return 1;
    for (size_t i = 0; i < name_count; ++i) {
        if (!x509_dns_name_permitted(names[i], permitted, permitted_count))
            return 0;
    }
    return 1;
}

static int x509_dns_names_excluded(char *const *names, size_t name_count,
                                   char *const *excluded,
                                   size_t excluded_count) {
    for (size_t i = 0; i < name_count; ++i) {
        if (x509_dns_name_excluded(names[i], excluded, excluded_count))
            return 1;
    }
    return 0;
}

static int x509_ip_addresses_permitted(
    const neverc_x509_ip_address_t *addresses, size_t address_count,
    const neverc_x509_ip_network_t *permitted, size_t permitted_count) {
    if (permitted_count == 0)
        return 1;
    for (size_t i = 0; i < address_count; ++i) {
        int matched = 0;
        for (size_t j = 0; j < permitted_count; ++j) {
            if (x509_ip_constraint_matches(&permitted[j], &addresses[i])) {
                matched = 1;
                break;
            }
        }
        if (!matched)
            return 0;
    }
    return 1;
}

static int x509_ip_addresses_excluded(
    const neverc_x509_ip_address_t *addresses, size_t address_count,
    const neverc_x509_ip_network_t *excluded, size_t excluded_count) {
    for (size_t i = 0; i < address_count; ++i) {
        for (size_t j = 0; j < excluded_count; ++j) {
            if (x509_ip_constraint_matches(&excluded[j], &addresses[i]))
                return 1;
        }
    }
    return 0;
}

/* CN is not a dNSName, but a SAN-less certificate still presents CN as its
 * DNS identity. Skipping it lets CN-only leaves (and client certs verified
 * with hostname=NULL) silently evade permitted/excluded DNS constraints. */
static int x509_cn_looks_like_dns(const char *cn) {
    if (!cn || !cn[0])
        return 0;
    size_t len = x509_dns_stripped_len(cn);
    if (len == 0 || len > 253)
        return 0;

    size_t start = 0;
    if (len >= 5 && cn[0] == '*' && cn[1] == '.')
        start = 2;

    int saw_dot = 0;
    int all_numeric = 1;
    int dots = 0;
    size_t label_start = start;
    for (size_t i = start; i <= len; ++i) {
        if (i == len || cn[i] == '.') {
            size_t label_len = i - label_start;
            if (label_len == 0 || label_len > 63 ||
                cn[label_start] == '-' || cn[i - 1] == '-')
                return 0;
            if (i < len) {
                saw_dot = 1;
                ++dots;
            }
            label_start = i + 1;
            continue;
        }
        int ch = (unsigned char)cn[i];
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
        if (!((ch >= 'a' && ch <= 'z') ||
              (ch >= '0' && ch <= '9') || ch == '-'))
            return 0;
        if (ch < '0' || ch > '9')
            all_numeric = 0;
    }
    if (!saw_dot)
        return start == 0 && len == 9 &&
               x509_ascii_equal(cn, "localhost", 9);
    /* IPv4 literals are not dNSName identities. */
    if (start == 0 && all_numeric && dots == 3)
        return 0;
    return 1;
}

static int x509_parse_ipv4_literal(const char *text, size_t len,
                                   uint8_t out[4]) {
    size_t pos = 0;
    for (int part = 0; part < 4; ++part) {
        if (pos >= len)
            return 0;
        size_t start = pos;
        unsigned value = 0;
        while (pos < len && text[pos] >= '0' && text[pos] <= '9') {
            if (value > 25 ||
                (value == 25 && (unsigned)(text[pos] - '0') > 5))
                return 0;
            value = value * 10 + (unsigned)(text[pos] - '0');
            ++pos;
        }
        if (pos == start || pos - start > 3 ||
            (pos - start > 1 && text[start] == '0'))
            return 0;
        out[part] = (uint8_t)value;
        if (part == 3)
            return pos == len;
        if (pos >= len || text[pos] != '.')
            return 0;
        ++pos;
    }
    return 0;
}

static int x509_cn_looks_like_ipv4(const char *cn) {
    if (!cn || !cn[0])
        return 0;
    size_t len = x509_dns_stripped_len(cn);
    int dots = 0;
    if (len == 0)
        return 0;
    for (size_t i = 0; i < len; ++i) {
        if (cn[i] == '.')
            ++dots;
        else if (cn[i] < '0' || cn[i] > '9')
            return 0;
    }
    return dots == 3;
}

static int x509_cn_looks_like_ipv6(const char *cn) {
    if (!cn || !cn[0])
        return 0;
    size_t len = x509_dns_stripped_len(cn);
    int colons = 0;
    if (len == 0)
        return 0;
    for (size_t i = 0; i < len; ++i) {
        int ch = x509_ascii_lower((unsigned char)cn[i]);
        if (ch == ':')
            ++colons;
        else if (!((ch >= '0' && ch <= '9') ||
                   (ch >= 'a' && ch <= 'f') || ch == '.'))
            return 0;
    }
    return colons >= 2;
}

static int x509_cn_parse_ipv4(const char *cn, neverc_x509_ip_address_t *out) {
    if (!cn || !out)
        return 0;
    memset(out, 0, sizeof(*out));
    if (!x509_parse_ipv4_literal(cn, x509_dns_stripped_len(cn), out->bytes))
        return 0;
    out->len = 4;
    return 1;
}

static int x509_cert_satisfies_name_constraints(
    const neverc_x509_cert_t *subject, const neverc_x509_cert_t *issuer) {
    if (!subject || !issuer)
        return 0;
    if (!issuer->name_constraints_present)
        return 1;
    if (x509_dns_names_excluded(subject->dns_names, subject->dns_name_count,
                                issuer->excluded_dns_names,
                                issuer->excluded_dns_name_count) ||
        x509_ip_addresses_excluded(subject->ip_addresses,
                                   subject->ip_address_count,
                                   issuer->excluded_ip_networks,
                                   issuer->excluded_ip_network_count))
        return 0;
    if (!x509_dns_names_permitted(subject->dns_names,
                                  subject->dns_name_count,
                                  issuer->permitted_dns_names,
                                  issuer->permitted_dns_name_count) ||
        !x509_ip_addresses_permitted(subject->ip_addresses,
                                     subject->ip_address_count,
                                     issuer->permitted_ip_networks,
                                     issuer->permitted_ip_network_count))
        return 0;

    if (subject->dns_name_count == 0 &&
        x509_cn_looks_like_dns(subject->subject.common_name)) {
        char *cn_name = (char *)subject->subject.common_name;
        if (x509_dns_names_excluded(&cn_name, 1,
                                    issuer->excluded_dns_names,
                                    issuer->excluded_dns_name_count) ||
            !x509_dns_names_permitted(&cn_name, 1,
                                      issuer->permitted_dns_names,
                                      issuer->permitted_dns_name_count))
            return 0;
    }

    /* Same CN-as-identity hole for iPAddress constraints: a SAN-less
     * IPv4 CN would otherwise evade permitted/excluded IP trees. */
    if (subject->ip_address_count == 0 &&
        subject->subject.common_name[0]) {
        neverc_x509_ip_address_t cn_ip;
        if (x509_cn_parse_ipv4(subject->subject.common_name, &cn_ip)) {
            if (x509_ip_addresses_excluded(
                    &cn_ip, 1, issuer->excluded_ip_networks,
                    issuer->excluded_ip_network_count) ||
                !x509_ip_addresses_permitted(
                    &cn_ip, 1, issuer->permitted_ip_networks,
                    issuer->permitted_ip_network_count))
                return 0;
        } else if ((x509_cn_looks_like_ipv4(subject->subject.common_name) ||
                    x509_cn_looks_like_ipv6(subject->subject.common_name)) &&
                   (issuer->permitted_ip_network_count > 0 ||
                    issuer->excluded_ip_network_count > 0)) {
            return 0;
        }
    }
    return 1;
}

int neverc_x509_verify_chain(const neverc_x509_cert_t *const *chain,
                             size_t chain_len,
                             const neverc_x509_time_t *moment,
                             const char *hostname,
                             uint32_t required_ext_key_usage) {
    if (!chain || !moment || chain_len == 0 ||
        chain_len > X509_MAX_CHAIN_DEPTH)
        return -1;

    for (size_t i = 0; i < chain_len; ++i) {
        const neverc_x509_cert_t *cert = chain[i];
        if (!cert || cert->has_unhandled_critical_extension ||
            !neverc_x509_is_valid_at(cert, moment) ||
            !x509_allows_ext_key_usage(
                cert, required_ext_key_usage))
            return -1;
    }

    const neverc_x509_cert_t *leaf = chain[0];
    if (hostname && neverc_x509_verify_hostname(leaf, hostname) != 0)
        return -1;
    if (leaf->key_usage_present &&
        (required_ext_key_usage &
         (NEVERC_X509_EXT_KEY_USAGE_SERVER_AUTH |
          NEVERC_X509_EXT_KEY_USAGE_CLIENT_AUTH |
          NEVERC_X509_EXT_KEY_USAGE_CODE_SIGNING |
          NEVERC_X509_EXT_KEY_USAGE_TIME_STAMPING |
          NEVERC_X509_EXT_KEY_USAGE_OCSP_SIGNING)) != 0 &&
        (leaf->key_usage &
         NEVERC_X509_KEY_USAGE_DIGITAL_SIGNATURE) == 0)
        return -1;

    size_t ca_certificates_below = 0;
    for (size_t i = 0; i + 1 < chain_len; ++i) {
        const neverc_x509_cert_t *child = chain[i];
        const neverc_x509_cert_t *parent = chain[i + 1];
        if (!x509_names_equal(
                child->raw_issuer, child->raw_issuer_len,
                parent->raw_subject, parent->raw_subject_len))
            return -1;
        if (i + 1 < chain_len - 1 &&
            (parent->version != 2 ||
             !parent->basic_constraints_valid || !parent->is_ca))
            return -1;
        if (neverc_x509_check_signature_from(child, parent) != 0)
            return -1;

        for (size_t j = 0; j <= i; ++j) {
            if (!x509_cert_satisfies_name_constraints(chain[j], parent))
                return -1;
        }

        if (i > 0 && child->is_ca &&
            !x509_names_equal(
                child->raw_issuer, child->raw_issuer_len,
                child->raw_subject, child->raw_subject_len))
            ++ca_certificates_below;
        if (parent->max_path_len >= 0 &&
            ca_certificates_below > (size_t)parent->max_path_len)
            return -1;
    }
    return 0;
}

int neverc_x509_is_self_signed(const neverc_x509_cert_t *cert) {
    if (!cert || !cert->raw_issuer || !cert->raw_subject ||
        cert->raw_issuer_len != cert->raw_subject_len ||
        memcmp(cert->raw_issuer, cert->raw_subject,
               cert->raw_issuer_len) != 0)
        return 0;
    return x509_check_signature(cert, cert) == 0;
}
