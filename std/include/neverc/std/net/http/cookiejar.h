#ifndef NEVERC_NET_HTTP_COOKIEJAR_H
#define NEVERC_NET_HTTP_COOKIEJAR_H

/*
 * NeverC net/http/cookiejar — RFC 6265 in-memory cookie jar.
 *
 * Go-style API:
 *   jar = neverc_cookiejar_new(NULL);
 *   neverc_cookiejar_set_cookies(jar, "https://example.com/path", cookies, n);
 *   n = neverc_cookiejar_cookies(jar, "https://example.com/path", out, max);
 *   neverc_cookiejar_free(jar);
 *
 * Thread-safe. Handles domain/path matching, expiration, Secure flag.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neverc_cookiejar neverc_cookiejar_t;

typedef struct {
    const char *name;
    const char *value;
    const char *domain;
    const char *path;
    int64_t     expires;    /* unix timestamp; 0 = session cookie */
    int         secure;     /* only send over HTTPS */
    int         http_only;  /* not accessible via JS */
} neverc_cookiejar_entry_t;

/* Create a new cookie jar. */
neverc_cookiejar_t *neverc_cookiejar_new(void);

/* Free a cookie jar and all stored cookies. */
void neverc_cookiejar_free(neverc_cookiejar_t *jar);

/* Store cookies for a URL (like Go jar.SetCookies).
 * Parses domain/path from the URL and applies RFC 6265 rules. */
void neverc_cookiejar_set_cookies(neverc_cookiejar_t *jar,
                                   const char *url,
                                   const neverc_cookiejar_entry_t *cookies,
                                   int count);

/* Retrieve matching cookies for a URL (like Go jar.Cookies).
 * Returns the number of cookies written to out (up to max_out).
 * Expired cookies are automatically pruned. String pointers in out are
 * borrowed and remain valid only until the jar is next mutated or freed. */
int neverc_cookiejar_cookies(neverc_cookiejar_t *jar,
                              const char *url,
                              neverc_cookiejar_entry_t *out,
                              int max_out);

/* Set a single cookie from a Set-Cookie header value.
 * E.g. "name=value; Path=/; Domain=.example.com; Secure; HttpOnly" */
void neverc_cookiejar_set_cookie_header(neverc_cookiejar_t *jar,
                                         const char *url,
                                         const char *set_cookie_header);

/* Build a Cookie header string for the given URL.
 * Returns a malloc'd string like "name1=val1; name2=val2" or NULL.
 * Caller must free the result. */
char *neverc_cookiejar_cookie_header(neverc_cookiejar_t *jar,
                                      const char *url);

/* Remove all cookies for a domain. */
void neverc_cookiejar_clear_domain(neverc_cookiejar_t *jar,
                                     const char *domain);

/* Remove all cookies. */
void neverc_cookiejar_clear_all(neverc_cookiejar_t *jar);

/* Get total number of stored cookies. */
int neverc_cookiejar_count(neverc_cookiejar_t *jar);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_NET_HTTP_COOKIEJAR_H */
