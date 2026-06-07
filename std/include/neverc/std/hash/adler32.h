#ifndef NEVERC_ADLER32_H
#define NEVERC_ADLER32_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Adler-32 initial value */
#define NEVERC_ADLER32_INIT 1U

/* Update Adler-32 checksum with new data. */
uint32_t neverc_adler32_update(uint32_t adler, const uint8_t *data, size_t len);

/* Compute Adler-32 checksum of data[0..len). */
uint32_t neverc_adler32_checksum(const uint8_t *data, size_t len);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/hash.h>
#endif

#endif /* NEVERC_ADLER32_H */
