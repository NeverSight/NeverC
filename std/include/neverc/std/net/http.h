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
#include "neverc/std/context.h"
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int (*neverc_http_request_body_read_func_t)(
    void *stream, neverc_context_t *context,
    void *output, size_t output_capacity);
typedef void (*neverc_http_request_body_cancel_func_t)(
    void *stream, uint32_t error_code);

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

    /* Path parameters from pattern matching (Go 1.22+ style).
     * E.g. pattern "GET /users/{id}" matches "/users/42" → id="42" */
    const char *path_params;  /* name\0value\0name\0value\0\0 */
    int         nparams;

    /* Valid only for the duration of the handler call. It carries the server
     * handler deadline when one is configured. */
    neverc_context_t *context;

    /* Non-NULL only for protocol-native streaming handlers. The pointed-to
     * transport object is opaque outside its protocol adapter. */
    void *protocol_stream;

    /* Protocol-neutral streaming request body. These fields are populated for
     * routes registered with mux_handle_stream_context. Use
     * neverc_http_request_body_read rather than invoking the callback. */
    void *body_stream;
    neverc_http_request_body_read_func_t body_stream_read;
    neverc_http_request_body_cancel_func_t body_stream_cancel;
} neverc_http_request_t;

/* Read a streaming request body. Returns bytes read, 0 at end of body, and -1
 * on malformed framing, transport failure, cancellation, or deadline. */
int neverc_http_request_body_read(neverc_http_request_t *request,
                                  void *output, size_t output_capacity);

/* Abort a streaming request body. error_code is protocol-specific; zero asks
 * the transport to use its ordinary cancellation code. */
void neverc_http_request_body_cancel(neverc_http_request_t *request,
                                     uint32_t error_code);

/* --- Response Writer --- */
typedef struct neverc_http_response_writer neverc_http_response_writer_t;

/* Set response status code (default 200). Values outside 100..999 become 500. */
void neverc_http_set_status(neverc_http_response_writer_t *w, int code);

/* Set a response header. */
void neverc_http_set_header(neverc_http_response_writer_t *w,
                             const char *name, const char *value);

/* Append a response header without replacing existing values. Connection,
 * Content-Length, and Transfer-Encoding remain writer-managed. Returns 0 on
 * success and -1 for invalid input, allocation/capacity failure, or a writer
 * whose headers have already been sent. */
int neverc_http_add_header(neverc_http_response_writer_t *w,
                            const char *name, const char *value);

/* Set representation-length metadata. HTTP/1 emits it for ordinary responses
 * and HEAD, and for 304 when explicitly set; 1xx and 204 always omit it.
 * Returns 0 on success and -1 once headers are sent or chunking is enabled. */
int neverc_http_set_content_length(neverc_http_response_writer_t *w,
                                    size_t content_length);

/* Discard an unsent buffered response and restore clean response defaults
 * while preserving transport, request, timeout, and server configuration.
 * Returns -1 if output may already have reached the client. */
int neverc_http_reset_response(neverc_http_response_writer_t *w);

/* Set a response trailer. HTTP/1 sends trailers for chunked responses;
 * HTTP/2 transports send a trailing HEADERS block. */
void neverc_http_set_trailer(neverc_http_response_writer_t *w,
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
typedef void (*neverc_http_handler_context_func_t)(
    neverc_http_request_t *req, neverc_http_response_writer_t *w,
    void *context);

typedef void (*neverc_http_access_log_func_t)(
    const char *method, const char *path,
    int status, double duration_ms, size_t body_size);

/* --- Mux (Router) --- */
typedef struct neverc_http_mux neverc_http_mux_t;

/* Create a new ServeMux (router). */
neverc_http_mux_t *neverc_http_new_mux(void);

/* Register a handler for a pattern.
 * Supports Go 1.22+ style patterns:
 *   "/users/{id}"         — captures path parameter "id"
 *   "GET /api/items"      — method-specific; also serves HEAD
 *                           unless a HEAD route exists (Go 1.22)
 *   "GET /files/{path...}" — wildcard captures rest of path
 *   "/posts/{$}"          — exact trailing slash (not /posts/123)
 *   "/static/"            — prefix match (trailing /)
 *   "/exact"              — exact match (no trailing /) */
void neverc_http_mux_handle(neverc_http_mux_t *mux, const char *pattern,
                             neverc_http_handler_func_t handler);

/* Register an instance-bound handler. context is borrowed and must outlive
 * the mux route. Returns 0 on success and -1 on invalid input, capacity, or
 * allocation failure. */
int neverc_http_mux_handle_context(
    neverc_http_mux_t *mux, const char *pattern,
    neverc_http_handler_context_func_t handler, void *context);

/* As above, but permits an HTTP/2 transport to dispatch after HEADERS and
 * feed request DATA incrementally through request.protocol_stream. */
int neverc_http_mux_handle_stream_context(
    neverc_http_mux_t *mux, const char *pattern,
    neverc_http_handler_context_func_t handler, void *context);

/* Free a mux. */
void neverc_http_mux_free(neverc_http_mux_t *mux);

/* --- Default Mux (like Go http.DefaultServeMux) --- */

/* Register on the default mux (like Go http.HandleFunc). */
void neverc_http_handle_func(const char *pattern,
                              neverc_http_handler_func_t handler);

/* --- Path Parameters (Go 1.22+ style) --- */

/* Get a path parameter by name from the request.
 * E.g. if pattern is "/users/{id}" and path is "/users/42", returns "42".
 * Returns NULL if not found. */
const char *neverc_http_path_value(const neverc_http_request_t *req,
                                    const char *name);

/* --- Server --- */

typedef struct neverc_http_server neverc_http_server_t;

/* Per-server configuration. A zero workers value selects the CPU count; a
 * zero max_connections value means unlimited. Use the default constructor
 * rather than zero-initializing this structure. */
typedef struct {
    int workers;
    int max_requests_per_connection;
    int read_timeout_ms;
    int read_header_timeout_ms;
    int write_timeout_ms;
    int idle_timeout_ms;
    int max_connections;
    int max_header_size;
    int max_body_size;
    int shutdown_timeout_ms;
    int handler_timeout_ms;
    int gzip_enabled;
    int gzip_level;
    size_t gzip_min_size;
    int access_log_enabled;
    neverc_http_access_log_func_t access_log;
    const char *alt_svc; /* optional per-server Alt-Svc response value */
} neverc_http_server_config_t;

/* Return a complete server configuration populated with safe defaults. */
neverc_http_server_config_t neverc_http_server_config_default(void);

/* Create a server with an independent configuration and connection limiter.
 * The mux remains owned by the caller and must outlive the server. */
neverc_http_server_t *neverc_http_server_new(
    neverc_http_mux_t *mux, const neverc_http_server_config_t *config);

/* Release an idle server. Calling this while server_listen_and_serve is active
 * is ignored; shut down the server and wait for serve to return first. */
void neverc_http_server_free(neverc_http_server_t *server);

/* Serve on addr using this server instance. Blocks until shutdown. */
int neverc_http_server_listen_and_serve(neverc_http_server_t *server,
                                        const char *addr);

/* Serve verified TLS 1.3 using the same server configuration, parser and
 * response writer semantics. Blocks until shutdown. */
int neverc_http_server_listen_and_serve_tls(
    neverc_http_server_t *server, const char *addr,
    const char *cert_file, const char *key_file);

/* Gracefully stop one server instance. Safe to call from another thread. */
void neverc_http_server_shutdown(neverc_http_server_t *server);

/* Query state belonging to one server instance. */
int neverc_http_server_active_connections(neverc_http_server_t *server);
int neverc_http_server_bound_port(neverc_http_server_t *server);

/* Start serving HTTP on addr with the given mux (NULL = default mux).
 * Uses a thread pool for concurrent connection handling with keep-alive.
 * Blocks until server is stopped. Returns 0 on normal shutdown, -1 on error. */
int neverc_http_listen_and_serve(const char *addr, neverc_http_mux_t *mux);

/* HTTPS entry point using the verified TLS 1.3 transport. */
int neverc_http_listen_and_serve_tls(const char *addr, neverc_http_mux_t *mux,
                                      const char *cert_file,
                                      const char *key_file);

/* Stop a running server (call from another thread or signal handler). */
void neverc_http_shutdown(void);

/* --- Server Configuration (call before listen_and_serve) --- */

/* Set number of worker threads (default = CPU cores). */
void neverc_http_set_workers(int n);

/* Set max requests per keep-alive connection (default 1000). */
void neverc_http_set_max_requests(int n);

/* Set read timeout in milliseconds (default 60000). */
void neverc_http_set_read_timeout(int ms);

/* Set timeout for receiving request headers (default 10000). */
void neverc_http_set_read_header_timeout(int ms);

/* Set response write timeout (default 60000). */
void neverc_http_set_write_timeout(int ms);

/* Set keep-alive idle timeout in milliseconds (default 60000). */
void neverc_http_set_idle_timeout(int ms);

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

/* --- Memory Writer (for testing, httptest) --- */

/* Create a response writer that buffers output in memory instead of a socket.
 * Use neverc_http_memory_writer_result to retrieve the buffered response. */
neverc_http_response_writer_t *neverc_http_memory_writer_new(void);

/* Get the buffered response from a memory writer. Caller must free *out_data.
 * Returns the status code used. */
int neverc_http_memory_writer_result(neverc_http_response_writer_t *w,
                                      char **out_data, size_t *out_len);

/* Free a memory writer. */
void neverc_http_memory_writer_free(neverc_http_response_writer_t *w);

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
    char       *trailers;       /* raw chunk trailers or NULL (caller frees) */
    const char *error;          /* error message or NULL on success */
} neverc_http_response_t;

typedef struct neverc_http_client neverc_http_client_t;

typedef struct {
    int max_redirects;
    int timeout_ms;
    int max_idle_per_host;
    /* Applied independently to the main header block and to the combined
     * chunk-extension/trailer metadata. */
    size_t max_response_header_size;
    size_t max_response_body_size;
    /* Per-client TLS identity and trust. Paths are copied by client_new. */
    const char *root_cert_file;
    const char *client_cert_file;
    const char *client_key_file;
    /* Explicit verification opt-out. Defaults to false. */
    int insecure_skip_verify;
} neverc_http_client_config_t;

/* Create an independent HTTP client and connection pool. */
neverc_http_client_config_t neverc_http_client_config_default(void);
neverc_http_client_t *neverc_http_client_new(
    const neverc_http_client_config_t *config);

/* Close all idle connections and release the client. The caller must ensure
 * no requests are active. */
void neverc_http_client_free(neverc_http_client_t *client);

/* Perform a request through one client instance. */
neverc_http_response_t *neverc_http_client_do(
    neverc_http_client_t *client, const char *method, const char *url,
    const char *content_type, const void *body, size_t body_len);

/* As above, with caller cancellation/deadline propagation. The client's own
 * timeout remains an absolute upper bound for the complete redirect chain. */
neverc_http_response_t *neverc_http_client_do_context(
    neverc_http_client_t *client, neverc_context_t *context,
    const char *method, const char *url, const char *content_type,
    const void *body, size_t body_len);

/* Streaming request/response callbacks. A source returns bytes produced, 0 at
 * EOF, or -1 on failure. A sink returns 0 after consuming the complete chunk
 * or -1 to abort the request. Callbacks run synchronously and therefore apply
 * natural backpressure to the network connection. */
typedef int (*neverc_http_body_source_func_t)(
    void *context, void *buffer, size_t capacity);
typedef int (*neverc_http_body_sink_func_t)(
    void *context, const void *data, size_t length);

/* Stream an HTTP/1.1 request and response without buffering either body.
 * content_length >= 0 sends an exact Content-Length and requires the source to
 * produce exactly that many bytes. content_length == -1 uses chunked transfer
 * encoding. The returned response owns headers/trailers as usual, has body ==
 * NULL, and records the delivered byte count in body_len. Streaming requests
 * are not automatically redirected or retried because their source may not be
 * replayable. */
neverc_http_response_t *neverc_http_client_do_stream_context(
    neverc_http_client_t *client, neverc_context_t *context,
    const char *method, const char *url, const char *content_type,
    int64_t content_length, neverc_http_body_source_func_t source,
    void *source_context, neverc_http_body_sink_func_t sink,
    void *sink_context);

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

/* ======================================================================
 * Cookies — like Go http.Cookie / http.SetCookie / r.Cookie
 * ====================================================================== */

typedef struct {
    const char *name;
    const char *value;
    const char *path;
    const char *domain;
    int         max_age;   /* seconds; 0 = session, <0 = delete */
    int         secure;
    int         http_only;
    /* SameSite: 0=default, 1=Lax, 2=Strict, 3=None */
    int         same_site;
} neverc_http_cookie_t;

/* Set a cookie on the response (like Go http.SetCookie). */
void neverc_http_set_cookie(neverc_http_response_writer_t *w,
                              const neverc_http_cookie_t *cookie);

/* Get a cookie value from the request by name (like Go r.Cookie).
 * Returns the value or NULL if not found. Writes to buf. */
const char *neverc_http_get_cookie(const neverc_http_request_t *req,
                                     const char *name,
                                     char *buf, size_t buflen);

/* ======================================================================
 * Response Compression — automatic gzip when Accept-Encoding allows it
 * ====================================================================== */

/* Enable automatic gzip compression for responses > min_size bytes
 * when the client sends Accept-Encoding: gzip.
 * Call before listen_and_serve. level: 1-9 (default 6).
 * min_size: minimum response size to compress (default 256). */
void neverc_http_enable_gzip(int level, size_t min_size);

/* Disable automatic gzip compression. */
void neverc_http_disable_gzip(void);

/* ======================================================================
 * Access Logging Middleware
 * ====================================================================== */

/* Enable access logging with a custom callback.
 * If func is NULL, logs to stdout in Apache Combined format. */
void neverc_http_enable_access_log(neverc_http_access_log_func_t func);

/* ======================================================================
 * Server-Sent Events (SSE) — like Go's http.Flusher for event streams
 * ====================================================================== */

/* Begin an SSE stream. Sets appropriate headers (Content-Type: text/event-stream,
 * Cache-Control: no-cache, Connection: keep-alive) and sends them.
 * After calling this, use neverc_http_sse_event to send events.
 * Returns 0 on success, -1 on error. */
int neverc_http_sse_begin(neverc_http_response_writer_t *w);

/* Send an SSE event. Fields:
 * - event: event type (optional, NULL for default "message")
 * - data: event data (required)
 * - id: event ID (optional, NULL to omit)
 * Returns 0 on success, -1 on error. */
int neverc_http_sse_event(neverc_http_response_writer_t *w,
                            const char *event, const char *data,
                            const char *id);

/* Send an SSE retry directive (milliseconds). */
int neverc_http_sse_retry(neverc_http_response_writer_t *w, int ms);

/* End the SSE stream and close the connection. */
void neverc_http_sse_end(neverc_http_response_writer_t *w);

/* ======================================================================
 * Multipart Form Parsing — like Go r.FormFile / r.MultipartReader
 * ====================================================================== */

typedef struct {
    const char *name;          /* form field name */
    const char *filename;      /* original filename (NULL if not a file) */
    const char *content_type;  /* MIME type (NULL if not specified) */
    const char *data;          /* field/file data */
    size_t      data_len;      /* length of data */
} neverc_http_multipart_part_t;

typedef struct neverc_http_multipart neverc_http_multipart_t;

/* Parse a multipart/form-data body. boundary is extracted from Content-Type.
 * Returns NULL on error. Caller must call neverc_http_multipart_free(). */
neverc_http_multipart_t *neverc_http_multipart_parse(
    const char *content_type, const char *body, size_t body_len);

/* Get the number of parts. */
int neverc_http_multipart_count(const neverc_http_multipart_t *mp);

/* Get part by index. Returns NULL if out of bounds. */
const neverc_http_multipart_part_t *neverc_http_multipart_get(
    const neverc_http_multipart_t *mp, int index);

/* Get part by field name. Returns NULL if not found. */
const neverc_http_multipart_part_t *neverc_http_multipart_field(
    const neverc_http_multipart_t *mp, const char *name);

/* Free a multipart result. */
void neverc_http_multipart_free(neverc_http_multipart_t *mp);

/* ======================================================================
 * Content Type Detection — like Go http.DetectContentType
 *
 * Implements the WHATWG MIME Sniffing Standard algorithm.
 * Considers at most the first 512 bytes.
 * Always returns a valid MIME type string.
 * ====================================================================== */

/* Detect the content type of data by examining magic bytes.
 * Returns a static string — caller must NOT free it. */
const char *neverc_http_detect_content_type(const void *data, size_t len);

/* ======================================================================
 * Handler Wrappers — like Go http.StripPrefix, http.TimeoutHandler
 * ====================================================================== */

/* Create a handler that strips prefix from the URL path before passing
 * to the inner handler. If the path doesn't have the prefix, returns 404.
 * (like Go http.StripPrefix) */
void neverc_http_strip_prefix(neverc_http_mux_t *mux, const char *prefix,
                                const char *pattern,
                                neverc_http_handler_func_t handler);

/* Send a 404 Not Found response (like Go http.NotFound). */
void neverc_http_not_found(neverc_http_request_t *req,
                             neverc_http_response_writer_t *w);

/* Serve a single file with proper Content-Type, Content-Length, and
 * Last-Modified headers. Supports Range requests and If-Modified-Since.
 * (like Go http.ServeFile) */
void neverc_http_serve_file(neverc_http_response_writer_t *w,
                              neverc_http_request_t *req,
                              const char *filepath);

/* ======================================================================
 * Header Utilities — like Go http.CanonicalHeaderKey
 * ====================================================================== */

/* Convert header name to canonical form: first letter and letters
 * following '-' are uppercased, rest lowercased.
 * E.g. "accept-encoding" → "Accept-Encoding".
 * Writes result to buf. Returns buf. */
char *neverc_http_canonical_header_key(const char *key, char *buf,
                                         size_t buflen);

/* Get a response header value by name from the raw response headers string.
 * Returns NULL if not found. Result is written to buf. */
const char *neverc_http_response_header(const neverc_http_response_t *resp,
                                          const char *name,
                                          char *buf, size_t buflen);

/* ======================================================================
 * CORS Middleware — Cross-Origin Resource Sharing
 *
 * Essential for web API development. Handles preflight OPTIONS requests
 * automatically and adds proper Access-Control-* headers.
 * ====================================================================== */

typedef struct {
    const char *allowed_origins;   /* "*" or "https://example.com" */
    const char *allowed_methods;   /* "GET, POST, PUT, DELETE" */
    const char *allowed_headers;   /* "Content-Type, Authorization" */
    const char *exposed_headers;   /* headers visible to client JS */
    int         allow_credentials; /* 1 to send Access-Control-Allow-Credentials */
    int         max_age;           /* preflight cache seconds (default 86400) */
} neverc_http_cors_config_t;

/* Enable CORS on a mux with the given configuration.
 * If config is NULL, uses permissive defaults (origin=*, all methods).
 * Automatically handles OPTIONS preflight requests. */
void neverc_http_enable_cors(neverc_http_mux_t *mux,
                               const neverc_http_cors_config_t *config);

/* Apply CORS headers to a single response (for manual use). */
void neverc_http_cors_headers(neverc_http_response_writer_t *w,
                                const neverc_http_cors_config_t *config,
                                const char *origin);

/* ======================================================================
 * JSON Request Helpers — convenient body parsing
 * ====================================================================== */

/* Read a string value from a JSON request body by key.
 * Simple top-level key extraction (no nested objects).
 * Returns the value or NULL if not found. Writes to buf. */
const char *neverc_http_json_get(const neverc_http_request_t *req,
                                   const char *key, char *buf, size_t buflen);

/* Write a JSON error response with status code and message. */
int neverc_http_json_error(neverc_http_response_writer_t *w,
                             int code, const char *message);

/* ======================================================================
 * Rate Limiter — token bucket for per-handler or global rate limiting
 * ====================================================================== */

typedef struct neverc_http_rate_limiter neverc_http_rate_limiter_t;

/* Create a rate limiter: rate requests per second, burst is max tokens.
 * E.g. rate=100, burst=200 allows 100 req/s sustained with 200 burst. */
neverc_http_rate_limiter_t *neverc_http_rate_limiter_new(double rate, int burst);

/* Free a rate limiter. */
void neverc_http_rate_limiter_free(neverc_http_rate_limiter_t *rl);

/* Check if a request is allowed (consumes 1 token).
 * Returns 1 if allowed, 0 if rate limited. */
int neverc_http_rate_limiter_allow(neverc_http_rate_limiter_t *rl);

/* Enable global rate limiting for all requests.
 * Requests exceeding the rate get 429 Too Many Requests.
 * Call before listen_and_serve. */
void neverc_http_set_rate_limit(double rate, int burst);

/* ======================================================================
 * MaxBytesReader — like Go http.MaxBytesReader
 * ====================================================================== */

/* Limit the request body size for a specific handler.
 * If body exceeds max_bytes, the server responds with 413. */
void neverc_http_set_max_bytes(neverc_http_response_writer_t *w, int64_t max_bytes);

/* ======================================================================
 * Timeout Handler — like Go http.TimeoutHandler
 * ====================================================================== */

/* Set per-request handler timeout in milliseconds.
 * If handler doesn't complete in time, responds with 503.
 * Call before listen_and_serve. 0 = no timeout. */
void neverc_http_set_handler_timeout(int ms);

/* ======================================================================
 * ListenAndServe on addr ":0" — automatic port selection
 * Get the actual port the server is listening on.
 * ====================================================================== */

/* Get the port the server bound to (useful when addr is ":0"). */
int neverc_http_server_port(void);

/* ======================================================================
 * Server-Sent Events (SSE) — like Go's flusher-based streaming
 *
 * SSE enables real-time server-to-client push over HTTP. The client opens
 * a long-lived connection and receives events as they are sent.
 *
 * Usage:
 *   void my_handler(neverc_http_request_t *req, neverc_http_response_writer_t *w) {
 *       neverc_sse_t *sse = neverc_sse_start(w);
 *       neverc_sse_send(sse, "message", "Hello, World!", NULL);
 *       neverc_sse_send_id(sse, "update", "data here", "evt-1");
 *       neverc_sse_close(sse);
 *   }
 * ====================================================================== */

typedef struct neverc_sse neverc_sse_t;

/* Start an SSE stream. Sets Content-Type: text/event-stream, disables buffering,
 * and sends initial headers. The response writer is consumed by the SSE stream. */
neverc_sse_t *neverc_sse_start(neverc_http_response_writer_t *w);

/* Send an event. event_type can be NULL (defaults to "message").
 * id can be NULL (no id field). Returns 0 on success, -1 if client disconnected. */
int neverc_sse_send(neverc_sse_t *sse, const char *event_type,
                     const char *data, const char *id);

/* Send an event with explicit id. */
int neverc_sse_send_id(neverc_sse_t *sse, const char *event_type,
                        const char *data, const char *id);

/* Send a retry directive (tells client reconnect interval in ms). */
int neverc_sse_retry(neverc_sse_t *sse, int retry_ms);

/* Send a comment (line starting with ':'). Useful as keep-alive ping. */
int neverc_sse_comment(neverc_sse_t *sse, const char *text);

/* Close the SSE stream. Frees resources. */
void neverc_sse_close(neverc_sse_t *sse);

#ifdef __cplusplus
}
#endif

#endif /* NEVERC_NET_HTTP_H */
