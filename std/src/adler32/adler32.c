#include "neverc/adler32.h"

/*
 * Adler-32 checksum — mirrors Go hash/adler32 package.
 *
 * Adler-32 = (s2 << 16) | s1
 * where s1 = sum of all bytes + 1 (mod 65521)
 *       s2 = sum of all s1 values (mod 65521)
 *
 * 65521 is the largest prime < 2^16.
 * Process in blocks of NMAX to defer the modulo operation.
 */

#define MOD_ADLER 65521U
#define NMAX 5552U

uint32_t neverc_adler32_update(uint32_t adler, const uint8_t *data, size_t len) {
    uint32_t s1 = adler & 0xFFFF;
    uint32_t s2 = (adler >> 16) & 0xFFFF;

    while (len > 0) {
        size_t block = len;
        if (block > NMAX) block = NMAX;
        len -= block;

        for (size_t i = 0; i < block; i++) {
            s1 += data[i];
            s2 += s1;
        }
        data += block;

        s1 %= MOD_ADLER;
        s2 %= MOD_ADLER;
    }

    return (s2 << 16) | s1;
}

uint32_t neverc_adler32_checksum(const uint8_t *data, size_t len) {
    return neverc_adler32_update(NEVERC_ADLER32_INIT, data, len);
}
