#include "neverc/std/crypto/subtle.h"
#include <limits.h>

/*
 * Constant-time cryptographic operations.
 * Ported from Go crypto/subtle — all operations avoid data-dependent branches
 * and memory access patterns to prevent timing side-channels.
 */

int neverc_subtle_constant_time_compare(const uint8_t *x, const uint8_t *y, size_t len) {
    uint8_t v = 0;
    for (size_t i = 0; i < len; i++)
        v |= x[i] ^ y[i];
    return neverc_subtle_constant_time_byte_eq(v, 0);
}

int neverc_subtle_constant_time_select(int v, int x, int y) {
    unsigned int mask = 0U - (unsigned int)v;
    return (int)((mask & (unsigned int)x) |
                 (~mask & (unsigned int)y));
}

int neverc_subtle_constant_time_byte_eq(uint8_t x, uint8_t y) {
    return (int)(((uint32_t)(x ^ y) - 1) >> 31);
}

int neverc_subtle_constant_time_eq(int32_t x, int32_t y) {
    /* Go approach: uint64 subtraction avoids signed overflow UB that the old
       -(int32_t)d pattern triggers when d == 0x80000000. */
    return (int)(((uint64_t)((uint32_t)(x ^ y)) - 1) >> 63);
}

void neverc_subtle_constant_time_copy(int v, uint8_t *x, const uint8_t *y, size_t len) {
    uint8_t mask = (uint8_t)(~((uint8_t)v - 1));
    for (size_t i = 0; i < len; i++)
        x[i] = x[i] ^ (mask & (x[i] ^ y[i]));
}

int neverc_subtle_constant_time_less_or_eq(int x, int y) {
    const unsigned int sign_shift =
        (unsigned int)(sizeof(unsigned int) * CHAR_BIT - 1);
    unsigned int ux = (unsigned int)x;
    unsigned int uy = (unsigned int)y;
    unsigned int signs_differ = (ux ^ uy) >> sign_shift;
    unsigned int same_sign_le = (ux - uy - 1U) >> sign_shift;
    return (int)((signs_differ & (ux >> sign_shift)) |
                 ((signs_differ ^ 1U) & same_sign_le));
}
