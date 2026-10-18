#ifndef NEVERC_GCM_CT_FIELD_H
#define NEVERC_GCM_CT_FIELD_H

#include "neverc/std/_platform.h"
#include <stdint.h>

static inline uint64_t gcm_field_load64(const uint8_t p[8]) {
    uint64_t word = 0;
    for (unsigned i = 0; i < 8; i++)
        word = (word << 8) | p[i];
    return word;
}

static inline void gcm_field_store64(uint8_t p[8], uint64_t word) {
    for (unsigned i = 0; i < 8; i++)
        p[i] = (uint8_t)(word >> (56 - 8 * i));
}

/* SP 800-38D, section 6.3, Algorithm 1:
 * https://doi.org/10.6028/NIST.SP.800-38D
 * The leftmost bit represents the constant coefficient, so multiplication
 * by u shifts right and reduces with 0xe1 in the most significant byte.
 * Two big-endian words implement the same 128-bit shift; all 128 iterations
 * use fixed addresses and unsigned masks, with no secret-indexed table.
 * Byte loads/stores impose no alignment or native-endian requirement.
 * Output may overlap either input: all reads finish before the first store. */
static inline void gcm_field_mul(uint8_t out[16], const uint8_t h[16],
                                 const uint8_t x[16]) {
    uint64_t v[2] = {gcm_field_load64(h), gcm_field_load64(h + 8)};
    uint64_t z[2] = {0, 0};
    for (unsigned word = 0; word < 2; word++) {
        uint64_t bits = gcm_field_load64(x + 8 * word);
        for (unsigned bit = 0; bit < 64; bit++) {
            uint64_t selected = UINT64_C(0) - (bits >> 63);
            z[0] ^= v[0] & selected;
            z[1] ^= v[1] & selected;

            uint64_t reduce = UINT64_C(0) - (v[1] & 1);
            v[1] = (v[1] >> 1) | (v[0] << 63);
            v[0] = (v[0] >> 1) ^ (UINT64_C(0xe100000000000000) & reduce);
            bits <<= 1;
        }
    }
    gcm_field_store64(out, z[0]);
    gcm_field_store64(out + 8, z[1]);
    neverc_platform_secure_zero(z, sizeof(z));
    neverc_platform_secure_zero(v, sizeof(v));
}

#endif /* NEVERC_GCM_CT_FIELD_H */
