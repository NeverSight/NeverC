#ifndef NEVERC_PBKDF2_H
#define NEVERC_PBKDF2_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

int neverc_pbkdf2_sha256(uint8_t *dk, size_t dk_len,
                         const uint8_t *password, size_t password_len,
                         const uint8_t *salt, size_t salt_len,
                         int iterations);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_PBKDF2_H */
