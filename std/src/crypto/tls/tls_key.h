#ifndef NEVERC_STD_CRYPTO_TLS_KEY_H
#define NEVERC_STD_CRYPTO_TLS_KEY_H

#include <stddef.h>
#include <stdint.h>

#define NCI_TLS_KEY_ECDSA_P256 2

int nci_tls_validate_certificate_key_pair(
    const uint8_t *certificate_der,
    size_t certificate_der_len,
    const uint8_t *private_key_der,
    size_t private_key_der_len,
    int key_type);

int nci_tls_sign_ecdsa_p256_sha256(
    const uint8_t *private_key_der,
    size_t private_key_der_len,
    const uint8_t *digest,
    size_t digest_len,
    uint8_t *signature,
    size_t signature_capacity,
    size_t *signature_len);

#endif
