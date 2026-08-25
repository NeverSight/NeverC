#ifndef NEVERC_NET_MAIL_H
#define NEVERC_NET_MAIL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char name[256];
    char address[256];
} neverc_mail_address_t;

typedef struct {
    char key[128];
    char value[1024];
} neverc_mail_header_t;

#define NEVERC_MAIL_MAX_HEADERS     64
#define NEVERC_MAIL_MAX_ADDRESSES   32

typedef struct {
    neverc_mail_header_t  headers[NEVERC_MAIL_MAX_HEADERS];
    int                   header_count;
    const char           *body;
    size_t                body_len;
} neverc_mail_message_t;

/* Parse a single email address like "Name <addr>" or "addr". Returns 0 on
 * success. RFC 2047 encoded-words that decode to CTL are rejected. */
int neverc_mail_parse_address(const char *s, neverc_mail_address_t *out);

/* Parse a comma-separated address list. Returns count, or -1 on error. */
int neverc_mail_parse_address_list(const char *s,
                                   neverc_mail_address_t *out, int max_out);

/* Format an address to "Name <addr>" or just "addr". Returns length.
 * Rejects names/addresses whose 2047 decoding contains CTL. */
int neverc_mail_format_address(const neverc_mail_address_t *addr, char *buf, size_t cap);

/* Parse an RFC 5322 message (headers + body). Returns 0 on success. */
int neverc_mail_parse_message(const char *data, size_t len, neverc_mail_message_t *out);

/* Get a header value by key (case-insensitive). Returns NULL if not found. */
const char *neverc_mail_header_get(const neverc_mail_message_t *msg, const char *key);

/* Parse an RFC 5322 date into *timestamp. Returns 0 on success and -1 on
 * error.  This form can represent the valid Unix timestamp -1 without
 * confusing it with the error result. */
int neverc_mail_parse_date_ex(const char *s, long long *timestamp);

/* Compatibility API. Returns the Unix timestamp, or -1 on error. A valid
 * timestamp of -1 is therefore ambiguous; new code should use parse_date_ex. */
long long neverc_mail_parse_date(const char *s);

#ifdef __cplusplus
}
#endif


/* ===== Std Module Dot-Syntax Support ===== */

#ifdef __neverc__
#include <neverc/std/net.h>
#endif


#endif
