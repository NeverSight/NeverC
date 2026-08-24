#ifndef NEVERC_PEM_H
#define NEVERC_PEM_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * PEM encoding/decoding (RFC 1421).
 *
 * Encode: writes "-----BEGIN <type>-----\n<base64>\n-----END <type>-----\n"
 * Decode: extracts type string and raw binary data from the next valid PEM
 * block, skipping malformed armor the same way as Go encoding/pem.Decode.
 * Encode accepts at most 200 printable ASCII type characters and rejects ':'
 * because a colon-bearing type is parsed as an encapsulated header in an
 * empty block and therefore cannot reliably round-trip.
 *
 * Unlike Go's version, this C API uses caller-provided buffers rather than
 * allocating memory. Encode returns the payload length (no trailing NUL in
 * the count), or -1 on error. Decode returns 0 on success or -1 if no valid
 * block is found / a caller buffer is too small; a failed Decode does not
 * write type_buf or out_buf.
 */

int neverc_pem_encode(char *out, size_t out_cap,
                      const char *type_str,
                      const uint8_t *data, size_t data_len);

int neverc_pem_decode(const char *pem_data, size_t pem_len,
                      char *type_buf, size_t type_cap,
                      uint8_t *out_buf, size_t out_cap,
                      size_t *bytes_written,
                      size_t *rest_offset);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/encoding.h>
#endif

#endif /* NEVERC_PEM_H */
