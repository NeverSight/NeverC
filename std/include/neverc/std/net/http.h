#ifndef NEVERC_NET_HTTP_H
#define NEVERC_NET_HTTP_H

/*
 * NeverC net/http — HTTP server (mirrors Go net/http.ListenAndServe).
 *
 * Go-style API:
 *   neverc_http_handle_func("/", my_handler);
 *   neverc_http_listen_and_serve(":8080", NULL);
 *
 * Handler function receives (request, response_writer) and writes the response.
 * Cross-platform: POSIX + WinSock.
 */

#include "neverc/std/net/tcp.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* --- Request --- */
typedef struct {
    const char *method;       /* "GET", "POST", etc. */
    const char *path;         /* "/path/to/resource" */
    const char *query;        /* "key=val&key2=val2" or NULL */
    const char *http_version; /* "HTTP/1.1" */
    const char *host;         /* Host header value */
    const char *content_type; /* Content-Type header or NULL */
    const char *body;         /* request body (POST data) or NULL */
    size_t      body_len;     /* length of body */

    /* Raw headers: name\0value\0name\0value\0\0 */
    const char *raw_headers;
    int         nheaders;
} neverc_http_request_t;

/* --- Response Writer --- */
typedef struct neverc_http_response_writer neverc_http_response_writer_t;

/* Set response status code (default 200). */
void neverc_http_set_status(neverc_http_response_writer_t *w, int code);

/* Set a response header. */
void neverc_http_set_header(neverc_http_response_writer_t *w,
                             const char *name, const char *value);

/* Write response body. Can be called multiple times. */
int neverc_http_write(neverc_http_response_writer_t *w,
                       const void *data, size_t len);

/* Write a string response body. */
int neverc_http_write_string(neverc_http_response_writer_t *w,
                              const char *s);

/* Convenience: write formatted string. */
int neverc_http_writef(neverc_http_response_writer_t *w,
                        const char *fmt, ...);

/* Enable chunked transfer encoding for streaming responses.
 * Must be called BEFORE any neverc_http_write.
 * After enabling, each neverc_http_write sends a chunk.
 * Call neverc_http_end_chunked to send the final 0-length chunk. */
void neverc_http_enable_chunked(neverc_http_response_writer_t *w);

/* Flush current buffered data as a chunk (only in chunked mode).
 * Returns 0 on success, -1 on error. */
int neverc_http_flush_chunk(neverc_http_response_writer_t *w);

/* End chunked transfer (sends terminating 0\r\n\r\n). */
int neverc_http_end_chunked(neverc_http_response_writer_t *w);

/* --- Handler --- */
typedef void (*neverc_http_handler_func_t)(neverc_http_request_t *req,
                                            neverc_http_response_writer_t *w);

/* --- Mux (Router) --- */
typedef struct neverc_http_mux neverc_http_mux_t;

/* Create a new ServeMux (router). */
neverc_http_mux_t *neverc_http_new_mux(void);

/* Register a handler for a pattern. */
void neverc_http_mux_handle(neverc_http_mux_t *mux, const char *pattern,
                             neverc_http_handler_func_t handler);

/* Free a mux. */
void neverc_http_mux_free(neverc_http_mux_t *mux);

/* --- Default Mux (like Go http.DefaultServeMux) --- */

/* Register on the default mux (like Go http.HandleFunc). */
void neverc_http_handle_func(const char *pattern,
                              neverc_http_handler_func_t handler);

/* --- Server --- */

/* Start serving HTTP on addr with the given mux (NULL = default mux).
 * Uses a thread pool for concurrent connection handling with keep-alive.
 * Blocks until server is stopped. Returns 0 on normal shutdown, -1 on error. */
int neverc_http_listen_and_serve(const char *addr, neverc_http_mux_t *mux);

/* Stop a running server (call from another thread or signal handler). */
void neverc_http_shutdown(void);

/* --- Server Configuration (call before listen_and_serve) --- */

/* Set number of worker threads (default = CPU cores). */
void neverc_http_set_workers(int n);

/* Set max requests per keep-alive connection (default 1000). */
void neverc_http_set_max_requests(int n);

/* Set read timeout in milliseconds (default 60000). */
void neverc_http_set_read_timeout(int ms);

/* Set max concurrent connections (default 0 = unlimited). */
void neverc_http_set_max_connections(int n);

/* Set max request header size in bytes (default 1MB). */
void neverc_http_set_max_header_size(int bytes);

/* Set max request body size in bytes (default 10MB). */
void neverc_http_set_max_body_size(int bytes);

/* Set graceful shutdown timeout in ms (default 5000). */
void neverc_http_set_shutdown_timeout(int ms);

/* Get current active connection count. */
int neverc_http_active_connections(void);

/* --- Middleware --- */

/* Middleware function: wraps a handler, returns the original or modified
 * handler. Chain multiple middlewares by composing them.
 * Example: logged_handler = neverc_http_use(my_handler, logger_middleware); */
typedef neverc_http_handler_func_t (*neverc_http_middleware_t)(
    neverc_http_handler_func_t next);

/* Apply a middleware to a handler, returning a new handler. */
neverc_http_handler_func_t neverc_http_use(neverc_http_handler_func_t handler,
                                            neverc_http_middleware_t mw);

/* --- Static File Serving --- */

/* Serve files from a directory. Pattern should end with '/'.
 * Example: neverc_http_serve_dir(mux, "/static/", "./public"); */
void neverc_http_serve_dir(neverc_http_mux_t *mux, const char *pattern,
                            const char *dir_path);

/* --- Helpers --- */

/* Get a query parameter value from the query string. Returns NULL if not found. */
const char *neverc_http_query_get(const char *query, const char *key,
                                   char *buf, size_t buflen);

/* Get a request header value. Returns NULL if not found. */
const char *neverc_http_request_header(const neverc_http_request_t *req,
                                        const char *name);

/* Hijack the underlying TCP connection for protocol upgrade (e.g. WebSocket).
 * After success the writer must not be used for normal HTTP responses.
 * Caller takes ownership of the returned TCP connection. */
neverc_tcp_conn_t *neverc_http_hijack(neverc_http_response_writer_t *w);

/* Common status text. */
const char *neverc_http_status_text(int code);

/* --- Go-style convenience APIs --- */

/* Send an HTTP redirect response (like Go http.Redirect). */
void neverc_http_redirect(neverc_http_response_writer_t *w,
                            const char *url, int code);

/* Send an error response with status code and message (like Go http.Error). */
void neverc_http_error(neverc_http_response_writer_t *w,
                         const char *message, int code);

/* Parse URL-encoded form body (application/x-www-form-urlencoded) and
 * retrieve a value by key (like Go r.FormValue). Returns NULL if not found.
 * `body` is the raw POST body. Result is written to buf. */
const char *neverc_http_form_value(const char *body, size_t body_len,
                                     const char *key, char *buf, size_t buflen);

/* Write a JSON string response with proper Content-Type. */
int neverc_http_write_json(neverc_http_response_writer_t *w,
                             const char *json);

/* ======================================================================
 * HTTP Client — like Go http.Get / http.Post
 * ====================================================================== */

typedef struct {
    int         status_code;    /* 200, 404, etc. */
    char       *body;           /* response body (caller must free) */
    size_t      body_len;
    char       *headers;        /* raw response headers (caller must free) */
    const char *error;          /* error message or NULL on success */
} neverc_http_response_t;

/* HTTP GET request. Caller must call neverc_http_response_free(). */
neverc_http_response_t *neverc_http_get(const char *url);

/* HTTP POST request. Caller must call neverc_http_response_free(). */
neverc_http_response_t *neverc_http_post(const char *url,
                                          const char *content_type,
                                          const void *body, size_t body_len);

/* HTTP HEAD request. */
neverc_http_response_t *neverc_http_head(const char *url);

/* HTTP PUT request. */
neverc_http_response_t *neverc_http_put(const char *url,
                                         const char *content_type,
                                         const void *body, size_t body_len);

/* HTTP DELETE request. */
neverc_http_response_t *neverc_http_delete(const char *url);

/* HTTP PATCH request. */
neverc_http_response_t *neverc_http_patch(const char *url,
                                           const char *content_type,
                                           const void *body, size_t body_len);

/* Generic HTTP request with any method. */
neverc_http_response_t *neverc_http_do(const char *method, const char *url,
                                        const char *content_type,
                                        const void *body, size_t body_len);

/* Free a response. */
void neverc_http_response_free(neverc_http_response_t *resp);

/* Set max redirects for client requests (default 10, 0 = no redirects). */
void neverc_http_client_set_max_redirects(int n);

/* Set client request timeout in milliseconds (default 30000). */
void neverc_http_client_set_timeout(int ms);

/* Enable/disable connection pooling for client requests (default: enabled).
 * When enabled, idle TCP connections are reused across requests to the same
 * host:port, similar to Go's http.Transport.
 * max_idle_per_host: max idle connections per host (default 2, 0 = disable). */
void neverc_http_client_set_pool(int max_idle_per_host);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_NET_HTTP_H */
