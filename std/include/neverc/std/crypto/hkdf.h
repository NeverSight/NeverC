#ifndef NEVERC_HKDF_H
#define NEVERC_HKDF_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* NULL input pointers are accepted only for zero-length spans. Extract and
 * expand return -1 for invalid spans or an RFC 5869 output-length violation. */
int neverc_hkdf_sha256(uint8_t *okm, size_t okm_len,
                       const uint8_t *ikm, size_t ikm_len,
                       const uint8_t *salt, size_t salt_len,
                       const uint8_t *info, size_t info_len);

int neverc_hkdf_extract_sha256(uint8_t prk[32],
                               const uint8_t *salt, size_t salt_len,
                               const uint8_t *ikm, size_t ikm_len);

int neverc_hkdf_expand_sha256(uint8_t *okm, size_t okm_len,
                              const uint8_t prk[32],
                              const uint8_t *info, size_t info_len);

int neverc_hkdf_sha512(uint8_t *okm, size_t okm_len,
                       const uint8_t *ikm, size_t ikm_len,
                       const uint8_t *salt, size_t salt_len,
                       const uint8_t *info, size_t info_len);

int neverc_hkdf_extract_sha512(uint8_t prk[64],
                               const uint8_t *salt, size_t salt_len,
                               const uint8_t *ikm, size_t ikm_len);

int neverc_hkdf_expand_sha512(uint8_t *okm, size_t okm_len,
                              const uint8_t prk[64],
                              const uint8_t *info, size_t info_len);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/crypto.h>
#endif

#endif /* NEVERC_HKDF_H */
