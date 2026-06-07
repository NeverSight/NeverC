#ifndef NEVERC_MIME_MULTIPART_H
#define NEVERC_MIME_MULTIPART_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NEVERC_MULTIPART_MAX_HEADERS  32
#define NEVERC_MULTIPART_MAX_PARTS    64

typedef struct {
    char    key[128];
    char    value[512];
} neverc_multipart_header_t;

typedef struct {
    neverc_multipart_header_t headers[NEVERC_MULTIPART_MAX_HEADERS];
    int                       header_count;
    const unsigned char      *body;
    size_t                    body_len;
} neverc_multipart_part_t;

typedef struct {
    neverc_multipart_part_t   parts[NEVERC_MULTIPART_MAX_PARTS];
    int                       part_count;
} neverc_multipart_reader_t;

/* Parse multipart data with given boundary.
 * Returns 0 on success, -1 on error. */
int neverc_multipart_parse(const unsigned char *data, size_t data_len,
                           const char *boundary,
                           neverc_multipart_reader_t *out);

/* Get a header value from a part (case-insensitive). Returns NULL if not found. */
const char *neverc_multipart_part_header(const neverc_multipart_part_t *part,
                                         const char *key);

/* Generate a random boundary string. Returns length written. */
int neverc_multipart_generate_boundary(char *buf, size_t cap);

/* Write multipart body.
 * Returns bytes written, or -1 on error. */
int neverc_multipart_write(const neverc_multipart_part_t *parts, int count,
                           const char *boundary,
                           unsigned char *out, size_t out_cap);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/mime.h>
#endif


#endif
