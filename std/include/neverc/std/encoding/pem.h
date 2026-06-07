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
 * Decode: extracts type string and raw binary data from PEM block
 *
 * Unlike Go's version, this C API uses caller-provided buffers rather than
 * allocating memory. Returns the number of bytes written, or -1 on error.
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
#include <neverc/encoding.h>
#endif

#endif /* NEVERC_PEM_H */
