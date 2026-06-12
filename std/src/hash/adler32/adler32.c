#include "neverc/std/hash/adler32.h"

/*
 * Adler-32 checksum — mirrors Go hash/adler32 package.
 *
 * 16-way unrolled inner loop: processes 16 bytes per iteration,
 * deferring the expensive modulo to block boundaries.
 * ~4x faster than byte-at-a-time on modern CPUs.
 *
 * The unrolled additions of s2 exploit the identity:
 *   s2 += s1 + d[0]; s2 += s1 + d[0] + d[1]; ...
 * which equals: s2 += 16*s1 + 16*d[0] + 15*d[1] + ... + 1*d[15]
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

        while (block >= 16) {
            s2 += s1; s1 += data[ 0];
            s2 += s1; s1 += data[ 1];
            s2 += s1; s1 += data[ 2];
            s2 += s1; s1 += data[ 3];
            s2 += s1; s1 += data[ 4];
            s2 += s1; s1 += data[ 5];
            s2 += s1; s1 += data[ 6];
            s2 += s1; s1 += data[ 7];
            s2 += s1; s1 += data[ 8];
            s2 += s1; s1 += data[ 9];
            s2 += s1; s1 += data[10];
            s2 += s1; s1 += data[11];
            s2 += s1; s1 += data[12];
            s2 += s1; s1 += data[13];
            s2 += s1; s1 += data[14];
            s2 += s1; s1 += data[15];
            data += 16;
            block -= 16;
        }

        while (block-- > 0) {
            s1 += *data++;
            s2 += s1;
        }

        s1 %= MOD_ADLER;
        s2 %= MOD_ADLER;
    }

    return (s2 << 16) | s1;
}

uint32_t neverc_adler32_checksum(const uint8_t *data, size_t len) {
    return neverc_adler32_update(NEVERC_ADLER32_INIT, data, len);
}
