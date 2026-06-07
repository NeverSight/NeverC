#ifndef NEVERC_CRYPTO_X509_H
#define NEVERC_CRYPTO_X509_H

/*
 * X.509 certificate parser (DER/PEM).
 * Parses certificate fields, validates basic structure.
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

/* Signature algorithms */
#define NEVERC_X509_SIG_SHA1_RSA     1
#define NEVERC_X509_SIG_SHA256_RSA   2
#define NEVERC_X509_SIG_SHA384_RSA   3
#define NEVERC_X509_SIG_SHA512_RSA   4
#define NEVERC_X509_SIG_ECDSA_SHA256 5
#define NEVERC_X509_SIG_ECDSA_SHA384 6
#define NEVERC_X509_SIG_ED25519      7

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

    /* Raw public key bytes */
    uint8_t *public_key;
    size_t   public_key_len;

    /* Is this a CA certificate? */
    int is_ca;

    /* Raw DER bytes (not owned by this struct) */
    const uint8_t *raw;
    size_t         raw_len;
} neverc_x509_cert_t;

/* ===== Functions ===== */

/* Parse a DER-encoded X.509 certificate. Returns 0 on success. */
int neverc_x509_parse_certificate(neverc_x509_cert_t *cert,
                                    const uint8_t *der, size_t len);

/* Free allocated fields (public_key). */
void neverc_x509_cert_free(neverc_x509_cert_t *cert);

/* Check if the certificate is self-signed (issuer == subject). */
int neverc_x509_is_self_signed(const neverc_x509_cert_t *cert);

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
