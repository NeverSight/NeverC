#ifndef NEVERC_NET_HTTP_HTTPUTIL_H
#define NEVERC_NET_HTTP_HTTPUTIL_H

/*
 * NeverC net/http/httputil — HTTP utilities (mirrors Go net/http/httputil).
 *
 * Provides:
 *   - Reverse proxy handler
 *   - Request/response dumping (debugging)
 *
 * Cross-platform: POSIX + WinSock.
 */

#include "neverc/std/net/http.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================
 * Reverse Proxy
 * ====================================================================== */

typedef struct neverc_httputil_reverse_proxy neverc_httputil_reverse_proxy_t;

/* The proxy fully buffers request and backend response bodies. The response
 * limit leaves ample room below HTTP's 16 MiB pending-output ceiling for the
 * serialized response headers and writer bookkeeping. */
#define NEVERC_HTTPUTIL_PROXY_HEADER_LIMIT (64U * 1024U)
#define NEVERC_HTTPUTIL_PROXY_BODY_LIMIT (8U * 1024U * 1024U)
/* One hard budget covers backend dial, verified TLS handshake, request write,
 * and response read; an earlier inbound request deadline still wins. */
#define NEVERC_HTTPUTIL_PROXY_BACKEND_TIMEOUT_MS 30000

/* Optional rewrite callback: modify the outbound request before proxying.
 * The `out_req` can be modified in-place. `in_req` is the original request
 * (read-only). Return 0 to continue proxying, non-zero to abort. */
typedef int (*neverc_httputil_rewrite_func_t)(
    const neverc_http_request_t *in_req,
    neverc_http_request_t       *out_req,
    void                        *user_data);

/* Optional error handler: called for request construction, transport,
 * response parsing, and downstream write failures. */
typedef void (*neverc_httputil_error_handler_t)(
    neverc_http_response_writer_t *w,
    const neverc_http_request_t   *req,
    const char                    *error_msg,
    void                          *user_data);

/* Create a reverse proxy that forwards requests to a single target URL.
 * target_url: e.g. "http://127.0.0.1:8081",
 * "http://backend.local:3000", or "https://[2001:db8::1]/base".
 * Userinfo, fragments, target queries, and unbracketed IPv6 are rejected.
 * Returns NULL on error. */
neverc_httputil_reverse_proxy_t *neverc_httputil_new_single_host_reverse_proxy(
    const char *target_url);

/* Set optional request rewrite callback. */
void neverc_httputil_proxy_set_rewrite(neverc_httputil_reverse_proxy_t *rp,
                                        neverc_httputil_rewrite_func_t func,
                                        void *user_data);

/* Set optional error handler. */
void neverc_httputil_proxy_set_error_handler(
    neverc_httputil_reverse_proxy_t *rp,
    neverc_httputil_error_handler_t handler,
    void *user_data);

/* Remove untrusted Forwarded/X-Forwarded-* / X-Real-IP (and similar
 * client-IP spoof headers) and add the trusted X-Forwarded metadata
 * available to the proxy (default: enabled). */
void neverc_httputil_proxy_set_forwarded_headers(
    neverc_httputil_reverse_proxy_t *rp, int enable);

/* Set the trusted inbound protocol used for X-Forwarded-Proto. The default is
 * conservatively "http"; accepted values are "http" and "https".
 * Returns 0 on success and -1 on invalid input. */
int neverc_httputil_proxy_set_forwarded_proto(
    neverc_httputil_reverse_proxy_t *rp, const char *proto);

/* Register an instance-bound proxy route using mux context dispatch. The
 * proxy is borrowed and must outlive the mux route. Returns 0 on success. */
int neverc_httputil_proxy_register(
    neverc_http_mux_t *mux, const char *pattern,
    neverc_httputil_reverse_proxy_t *rp);

/* Get a legacy handler function for use with neverc_http_mux_handle.
 * Each proxy receives a stable, process-lifetime handler slot. A handler kept
 * after its proxy is freed returns 502 and is never rebound to another proxy.
 * In-flight legacy handler calls retain the proxy until they complete.
 * Returns NULL if rp is NULL or the finite legacy slot pool is exhausted.
 * Usage: neverc_http_mux_handle(mux, "/api/",
 *            neverc_httputil_proxy_handler(rp)); */
neverc_http_handler_func_t neverc_httputil_proxy_handler(
    neverc_httputil_reverse_proxy_t *rp);

/* Free a reverse proxy. */
void neverc_httputil_proxy_free(neverc_httputil_reverse_proxy_t *rp);

/* ======================================================================
 * Request / Response Dumping (debugging)
 * ====================================================================== */

/* Dump an HTTP request to a string (for debugging).
 * If body is non-zero, the request body is included and Content-Length is
 * synthesized when the raw headers do not already carry one. Host is taken
 * from req->host when set, otherwise from raw headers.
 * Caller must free the returned string. Returns NULL on error. */
char *neverc_httputil_dump_request(const neverc_http_request_t *req,
                                    int include_body);

/* Dump a raw HTTP request string from method + path + headers + body.
 * Caller must free the returned string. */
char *neverc_httputil_dump_request_out(const char *method,
                                        const char *url,
                                        const char *headers,
                                        const char *body,
                                        size_t body_len);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_NET_HTTP_HTTPUTIL_H */
