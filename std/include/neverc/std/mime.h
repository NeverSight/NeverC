#ifndef NEVERC_MIME_H
#define NEVERC_MIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *neverc_mime_type_by_extension(const char *ext);
const char *neverc_mime_extension_by_type(const char *mime_type);

/* Parses type/subtype and at most max_params parameters. Parameter names and
 * the media type are lower-cased; quoted values are unescaped. A backslash in
 * a quoted value is an escape only before a tspecial (Go/MSIE), so Windows
 * paths such as `C:\dev\file.txt` are preserved. RFC 2231
 * `name*=charset''value` and `name*0` / `name*1` continuations are decoded
 * into `name` (utf-8 / us-ascii). A trailing semicolon is ignored. Parameter
 * values that 2047-decode to CTL or invalid UTF-8 are rejected. The caller
 * owns every returned key/value and must free them. On failure, returns -1,
 * frees all partial results, writes an empty media type when mt_cap is
 * nonzero, and sets *nparams to zero. */
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

/* Quoted-printable helpers do not append a NUL byte. Encode inserts RFC 2045
 * soft line breaks so no output line exceeds 76 characters. They return -1
 * and set out_len to zero for invalid arguments or insufficient space. */
int neverc_mime_qp_decode(const char *src, size_t src_len,
                           char *dst, size_t dst_cap, size_t *out_len);
int neverc_mime_qp_encode(const char *src, size_t src_len,
                           char *dst, size_t dst_cap, size_t *out_len);

/* Decode RFC 2047 encoded-words in an unstructured header. Adjacent
 * encoded-words have intervening WSP removed. Q-encoding maps '_' to
 * space. utf-8, us-ascii, and iso-8859-1 are supported. Decoded CTL
 * (including CR/LF, except TAB) is rejected so an encoded-word cannot
 * inject headers. A utf-8 encoded-word must be well-formed UTF-8
 * (overlong CR/LF and surrogate halves are rejected). A well-formed
 * word that does not fit the decode buffer is rejected rather than
 * copied through as a literal. Returns 0, or -1 with *out_len zero. */
int neverc_mime_decode_header(const char *src, size_t src_len,
                              char *out, size_t out_cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#include "mime/quotedprintable.h"
#include "mime/multipart.h"

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
