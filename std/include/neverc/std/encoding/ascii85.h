#ifndef NEVERC_ASCII85_H
#define NEVERC_ASCII85_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    size_t ndst;
    size_t nsrc;
    int    error;
} neverc_ascii85_result_t;

/* Length and encode return -1 when the result cannot fit in int. */
int    neverc_ascii85_max_encoded_len(int n);
int    neverc_ascii85_encode(unsigned char *dst, const unsigned char *src, size_t src_len);
neverc_ascii85_result_t neverc_ascii85_decode(unsigned char *dst, size_t dst_len,
                                               const unsigned char *src, size_t src_len,
                                               int flush);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/encoding.h>
#endif

#endif /* NEVERC_ASCII85_H */
