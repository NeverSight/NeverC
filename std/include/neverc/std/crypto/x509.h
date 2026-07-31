#ifndef NEVERC_CRYPTO_X509_H
#define NEVERC_CRYPTO_X509_H

/*
 * X.509 certificate parser, trust-store loader, and chain verifier.
 * API modeled after Go's crypto/x509 package.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== Constants ===== */

/* Key algorithms */
#define NEVERC_X509_KEY_RSA   1
#define NEVERC_X509_KEY_ECDSA 2
#define NEVERC_X509_KEY_ED25519 3

/* Named elliptic curves used by SubjectPublicKeyInfo. */
#define NEVERC_X509_CURVE_NONE 0
#define NEVERC_X509_CURVE_P256 1
#define NEVERC_X509_CURVE_P384 2

/* Signature algorithms */
#define NEVERC_X509_SIG_SHA1_RSA     1
#define NEVERC_X509_SIG_SHA256_RSA   2
#define NEVERC_X509_SIG_SHA384_RSA   3
#define NEVERC_X509_SIG_SHA512_RSA   4
#define NEVERC_X509_SIG_ECDSA_SHA256 5
#define NEVERC_X509_SIG_ECDSA_SHA384 6
#define NEVERC_X509_SIG_ED25519      7
#define NEVERC_X509_SIG_RSA_PSS_SHA256 8
#define NEVERC_X509_SIG_RSA_PSS_SHA384 9
#define NEVERC_X509_SIG_RSA_PSS_SHA512 10

/* KeyUsage bits (RFC 5280 section 4.2.1.3). */
#define NEVERC_X509_KEY_USAGE_DIGITAL_SIGNATURE  (1u << 0)
#define NEVERC_X509_KEY_USAGE_CONTENT_COMMITMENT (1u << 1)
#define NEVERC_X509_KEY_USAGE_KEY_ENCIPHERMENT   (1u << 2)
#define NEVERC_X509_KEY_USAGE_DATA_ENCIPHERMENT  (1u << 3)
#define NEVERC_X509_KEY_USAGE_KEY_AGREEMENT      (1u << 4)
#define NEVERC_X509_KEY_USAGE_CERT_SIGN          (1u << 5)
#define NEVERC_X509_KEY_USAGE_CRL_SIGN           (1u << 6)
#define NEVERC_X509_KEY_USAGE_ENCIPHER_ONLY      (1u << 7)
#define NEVERC_X509_KEY_USAGE_DECIPHER_ONLY      (1u << 8)

/* Common ExtendedKeyUsage purposes. */
#define NEVERC_X509_EXT_KEY_USAGE_SERVER_AUTH (1u << 0)
#define NEVERC_X509_EXT_KEY_USAGE_CLIENT_AUTH (1u << 1)
#define NEVERC_X509_EXT_KEY_USAGE_CODE_SIGNING (1u << 2)
#define NEVERC_X509_EXT_KEY_USAGE_EMAIL_PROTECTION (1u << 3)
#define NEVERC_X509_EXT_KEY_USAGE_TIME_STAMPING (1u << 4)
#define NEVERC_X509_EXT_KEY_USAGE_OCSP_SIGNING (1u << 5)
#define NEVERC_X509_EXT_KEY_USAGE_ANY (1u << 31)

/* ===== Types ===== */

typedef struct {
    char country[64];
    char organization[256];
    char organizational_unit[256];
    char common_name[256];
    char locality[128];
    char province[128];
    char serial_number_str[64];
} neverc_x509_name_t;

typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
} neverc_x509_time_t;

typedef struct {
    uint8_t bytes[16];
    uint8_t len; /* 4 for IPv4, 16 for IPv6 */
} neverc_x509_ip_address_t;

typedef struct {
    /* Version (0 = v1, 1 = v2, 2 = v3) */
    int version;

    /* Serial number (up to 20 bytes per X.509) */
    uint8_t serial[20];
    int     serial_len;

    /* Signature algorithm */
    int sig_algorithm;

    /* Issuer and Subject distinguished names */
    neverc_x509_name_t issuer;
    neverc_x509_name_t subject;

    /* Validity period */
    neverc_x509_time_t not_before;
    neverc_x509_time_t not_after;

    /* Public key algorithm */
    int key_algorithm;
    int public_key_curve;

    /* Raw public key bytes */
    uint8_t *public_key;
    size_t   public_key_len;

    /* Signed certificate data and signature (views into raw DER). */
    const uint8_t *raw_tbs;
    size_t         raw_tbs_len;
    const uint8_t *signature;
    size_t         signature_len;

    /* Canonical DER names (views into raw DER). */
    const uint8_t *raw_issuer;
    size_t         raw_issuer_len;
    const uint8_t *raw_subject;
    size_t         raw_subject_len;

    /* Is this a CA certificate? */
    int is_ca;
    int basic_constraints_valid;
    int max_path_len; /* -1 when omitted */

    uint16_t key_usage;
    int      key_usage_present;
    uint32_t ext_key_usage;
    int      ext_key_usage_present;
    int      has_unhandled_critical_extension;

    /* Subject Alternative Name identities. */
    char                      **dns_names;
    size_t                      dns_name_count;
    neverc_x509_ip_address_t   *ip_addresses;
    size_t                      ip_address_count;

    /* Raw DER bytes (not owned by this struct) */
    const uint8_t *raw;
    size_t         raw_len;
} neverc_x509_cert_t;

typedef struct neverc_x509_cert_pool neverc_x509_cert_pool_t;

/* ===== Functions ===== */

/* Parse a DER-encoded X.509 certificate. Returns 0 on success. */
int neverc_x509_parse_certificate(neverc_x509_cert_t *cert,
                                    const uint8_t *der, size_t len);

/* Free all fields allocated by neverc_x509_parse_certificate. */
void neverc_x509_cert_free(neverc_x509_cert_t *cert);

/* Check that issuer == subject and the certificate signature verifies. */
int neverc_x509_is_self_signed(const neverc_x509_cert_t *cert);

/* Verify cert's signature with parent and enforce parent CA/key-usage
 * constraints. SHA-1 certificate signatures are rejected.
 * Returns 0 on success and -1 on failure or unsupported algorithms. */
int neverc_x509_check_signature_from(const neverc_x509_cert_t *cert,
                                      const neverc_x509_cert_t *parent);

/* Verify a signature over arbitrary data with certificate's public key.
 * The signature_algorithm is one of NEVERC_X509_SIG_*.
 * Returns 0 on success and -1 on failure or unsupported algorithms. */
int neverc_x509_verify_signature(const neverc_x509_cert_t *certificate,
                                  int signature_algorithm,
                                  const uint8_t *signed_data,
                                  size_t signed_data_len,
                                  const uint8_t *signature,
                                  size_t signature_len);

/* Verify an ordered chain from leaf (chain[0]) to a caller-trusted anchor.
 * All certificates must be valid at moment; hostname and required EKU are
 * optional when NULL/zero. The chain is limited to 16 certificates.
 * Returns 0 on success and -1 on validation failure. */
int neverc_x509_verify_chain(const neverc_x509_cert_t *const *chain,
                              size_t chain_len,
                              const neverc_x509_time_t *moment,
                              const char *hostname,
                              uint32_t required_ext_key_usage);

/* Create/free a certificate pool. Certificates added as DER are copied and
 * owned by the pool. Adding the same DER certificate more than once is
 * idempotent. */
neverc_x509_cert_pool_t *neverc_x509_cert_pool_new(void);
void neverc_x509_cert_pool_free(neverc_x509_cert_pool_t *pool);
int neverc_x509_cert_pool_add_der(neverc_x509_cert_pool_t *pool,
                                  const uint8_t *der, size_t der_len);
/* Append valid CERTIFICATE blocks from a PEM bundle. Non-certificate and
 * malformed certificate blocks are skipped. Returns the number of new
 * certificates added, or -1 for invalid arguments/allocation failure. */
int neverc_x509_cert_pool_add_pem(neverc_x509_cert_pool_t *pool,
                                  const char *pem, size_t pem_len);
size_t neverc_x509_cert_pool_count(
    const neverc_x509_cert_pool_t *pool);

/* Return a newly allocated pool populated from the platform trust store.
 * Non-empty SSL_CERT_FILE and SSL_CERT_DIR select explicit trust sources.
 * On Unix, each variable replaces its corresponding default source list.
 * The caller owns the returned pool. Returns NULL if no usable roots can be
 * loaded. */
neverc_x509_cert_pool_t *neverc_x509_system_cert_pool(void);

/* Build and verify a path from leaf through intermediates to an exact
 * certificate in roots. Candidate paths are capped at 16 certificates and
 * 100 signature checks. A root is trusted by membership and need not be
 * self-signed. Returns 0 on success and -1 on failure. */
int neverc_x509_verify_with_pools(
    const neverc_x509_cert_t *leaf,
    const neverc_x509_cert_pool_t *intermediates,
    const neverc_x509_cert_pool_t *roots,
    const neverc_x509_time_t *moment,
    const char *hostname,
    uint32_t required_ext_key_usage);

/* Verify a DNS name or IP literal against Subject Alternative Name.
 * Common Name is deliberately not used as a fallback.
 * Returns 0 for a match and -1 for mismatch or invalid input. */
int neverc_x509_verify_hostname(const neverc_x509_cert_t *cert,
                                  const char *hostname);

/* Return 1 when moment is within the inclusive certificate validity range. */
int neverc_x509_is_valid_at(const neverc_x509_cert_t *cert,
                              const neverc_x509_time_t *moment);

/* Get signature algorithm name string. */
const char *neverc_x509_sig_algorithm_string(int algo);

/* Get key algorithm name string. */
const char *neverc_x509_key_algorithm_string(int algo);

/* Format distinguished name to string.
   Returns bytes written (excluding NUL), or -1 on error. */
int neverc_x509_format_name(const neverc_x509_name_t *name,
                              char *buf, size_t buf_size);

/* Compare two X.509 times. Returns <0, 0, >0. */
int neverc_x509_time_compare(const neverc_x509_time_t *a,
                               const neverc_x509_time_t *b);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_CRYPTO_X509_H */
