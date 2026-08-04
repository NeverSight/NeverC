#ifndef NEVERC_MIME_H
#define NEVERC_MIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *neverc_mime_type_by_extension(const char *ext);
const char *neverc_mime_extension_by_type(const char *mime_type);

/* Parses type/subtype and at most max_params parameters. Parameter names and
 * the media type are lower-cased; quoted values are unescaped. The caller owns
 * every returned key/value and must free them. On failure, returns -1, frees
 * all partial results, writes an empty media type when mt_cap is nonzero, and
 * sets *nparams to zero. */
int neverc_mime_parse_media_type(const char *v,
                                 char *media_type, size_t mt_cap,
                                 char *params_keys[], char *params_vals[],
                                 int max_params, int *nparams);

/* Formats a validated type/subtype and parameters. Names are lower-cased and
 * values are quoted/escaped as needed. Returns the byte length, or -1 with an
 * empty output on invalid input or insufficient capacity. */
int neverc_mime_format_media_type(const char *media_type,
                                  const char *param_keys[],
                                  const char *param_vals[],
                                  int nparams,
                                  char *out, size_t out_cap);

/* Quoted-printable helpers do not append a NUL byte. They return -1 and set
 * out_len to zero for invalid arguments or insufficient destination space. */
int neverc_mime_qp_decode(const char *src, size_t src_len,
                           char *dst, size_t dst_cap, size_t *out_len);
int neverc_mime_qp_encode(const char *src, size_t src_len,
                           char *dst, size_t dst_cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
struct __neverc_std_quotedprintable_t { char __tag; };
struct __neverc_std_multipart_t { char __tag; };

struct __neverc_std_mime_t {
    char __tag;
    struct __neverc_std_quotedprintable_t quotedprintable;
    struct __neverc_std_multipart_t multipart;
};
extern struct __neverc_std_mime_t __neverc_mod_mime;
extern struct __neverc_std_mime_t mime;
#endif

#endif
