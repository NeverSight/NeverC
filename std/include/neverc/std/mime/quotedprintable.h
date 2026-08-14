#ifndef NEVERC_MIME_QUOTEDPRINTABLE_H
#define NEVERC_MIME_QUOTEDPRINTABLE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Decode quoted-printable data.
 * Returns bytes written to out, or -1 on error.
 * Soft line breaks (=\r\n, =\n, =\r, and a trailing '=') are stripped. */
int neverc_qp_decode(const char *src, size_t src_len,
                     unsigned char *out, size_t out_cap);

/* Encode data as quoted-printable.
 * Returns bytes written to out, or -1 on error.
 * Lines are wrapped at max_line (76 if max_line < 0, 0 = no wrap). */
int neverc_qp_encode(const unsigned char *src, size_t src_len,
                     char *out, size_t out_cap, int max_line);

/* Calculate maximum encoded length. */
size_t neverc_qp_max_encoded_len(size_t src_len);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/mime.h>
#endif


#endif
