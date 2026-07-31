#include "neverc/std/crypto/tls.h"

#include <string.h>

#define TLS13_TRANSCRIPT_SHA256_SIZE 32U
#define TLS13_TRANSCRIPT_SHA384_SIZE 48U

int neverc_tls_verify_certificate_verify(
    const neverc_x509_cert_t *certificate,
    uint16_t signature_scheme,
    int from_server,
    const uint8_t *transcript_hash,
    size_t transcript_hash_len,
    const uint8_t *signature,
    size_t signature_len) {
    static const char server_context[] =
        "TLS 1.3, server CertificateVerify";
    static const char client_context[] =
        "TLS 1.3, client CertificateVerify";
    if (!certificate || !transcript_hash ||
        (transcript_hash_len != TLS13_TRANSCRIPT_SHA256_SIZE &&
         transcript_hash_len != TLS13_TRANSCRIPT_SHA384_SIZE) ||
        !signature || signature_len == 0 ||
        (from_server != 0 && from_server != 1))
        return -1;

    int signature_algorithm;
    if (signature_scheme ==
            NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA256 &&
        certificate->key_algorithm == NEVERC_X509_KEY_RSA) {
        signature_algorithm = NEVERC_X509_SIG_RSA_PSS_SHA256;
    } else if (signature_scheme ==
                   NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA384 &&
               certificate->key_algorithm == NEVERC_X509_KEY_RSA) {
        signature_algorithm = NEVERC_X509_SIG_RSA_PSS_SHA384;
    } else if (signature_scheme ==
                   NEVERC_TLS_SIGNATURE_RSA_PSS_RSAE_SHA512 &&
               certificate->key_algorithm == NEVERC_X509_KEY_RSA) {
        signature_algorithm = NEVERC_X509_SIG_RSA_PSS_SHA512;
    } else if (signature_scheme ==
                   NEVERC_TLS_SIGNATURE_ECDSA_SECP256R1_SHA256 &&
               certificate->key_algorithm == NEVERC_X509_KEY_ECDSA &&
               certificate->public_key_curve ==
                   NEVERC_X509_CURVE_P256) {
        signature_algorithm = NEVERC_X509_SIG_ECDSA_SHA256;
    } else if (signature_scheme == NEVERC_TLS_SIGNATURE_ED25519 &&
               certificate->key_algorithm == NEVERC_X509_KEY_ED25519) {
        signature_algorithm = NEVERC_X509_SIG_ED25519;
    } else {
        return -1;
    }

    const char *context = from_server ?
                          server_context : client_context;
    size_t context_len = strlen(context);
    uint8_t signed_content[
        64 + sizeof(server_context) - 1 + 1 +
        TLS13_TRANSCRIPT_SHA384_SIZE];
    memset(signed_content, 0x20, 64);
    memcpy(signed_content + 64, context, context_len);
    signed_content[64 + context_len] = 0;
    memcpy(signed_content + 64 + context_len + 1,
           transcript_hash, transcript_hash_len);
    size_t signed_content_len =
        64 + context_len + 1 + transcript_hash_len;

    return neverc_x509_verify_signature(
        certificate, signature_algorithm,
        signed_content, signed_content_len,
        signature, signature_len);
}
