#ifndef NEVERC_MIME_H
#define NEVERC_MIME_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

const char *neverc_mime_type_by_extension(const char *ext);
const char *neverc_mime_extension_by_type(const char *mime_type);

int neverc_mime_parse_media_type(const char *v,
                                 char *media_type, size_t mt_cap,
                                 char *params_keys[], char *params_vals[],
                                 int max_params, int *nparams);

int neverc_mime_format_media_type(const char *media_type,
                                  const char *param_keys[],
                                  const char *param_vals[],
                                  int nparams,
                                  char *out, size_t out_cap);

int neverc_mime_qp_decode(const char *src, size_t src_len,
                           char *dst, size_t dst_cap, size_t *out_len);
int neverc_mime_qp_encode(const char *src, size_t src_len,
                           char *dst, size_t dst_cap, size_t *out_len);

#ifdef __cplusplus
}
#endif

#endif
