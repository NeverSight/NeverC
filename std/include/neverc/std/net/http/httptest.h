#ifndef NEVERC_NET_HTTP_HTTPTEST_H
#define NEVERC_NET_HTTP_HTTPTEST_H

/*
 * NeverC net/http/httptest — Test server (mirrors Go httptest.NewServer).
 *
 * Starts a temporary HTTP/HTTPS server on a random port for testing.
 *
 * Go-style API:
 *   neverc_httptest_server_t *ts = neverc_httptest_new_server(handler);
 *   const char *url = neverc_httptest_url(ts);
 *   // make requests to url...
 *   neverc_httptest_close(ts);
 */

#include "neverc/std/net/http.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct neverc_httptest_server neverc_httptest_server_t;

/* Create a new test server with the given handler.
 * Starts listening on 127.0.0.1 with a random available port. */
neverc_httptest_server_t *neverc_httptest_new_server(
    neverc_http_handler_func_t handler);

/* Get the server's URL (e.g., "http://127.0.0.1:12345"). */
const char *neverc_httptest_url(neverc_httptest_server_t *ts);

/* Get the server's listener address (e.g., "127.0.0.1:12345"). */
const char *neverc_httptest_addr(neverc_httptest_server_t *ts);

/* Close the test server, join the accept thread, and free resources.
 * Safe to call with NULL. */
void neverc_httptest_close(neverc_httptest_server_t *ts);

/* --- Response Recorder (like Go httptest.ResponseRecorder) --- */

typedef struct {
    int         status_code;
    char       *body;
    size_t      body_len;
    char       *header_names[64];
    char       *header_values[64];
    int         nheaders;
} neverc_httptest_recorder_t;

/* Create a new response recorder. */
neverc_httptest_recorder_t *neverc_httptest_new_recorder(void);

/* Get the recorder as a response writer (pass to handler). */
neverc_http_response_writer_t *neverc_httptest_recorder_writer(
    neverc_httptest_recorder_t *rec);

/* Snapshot status, body, headers, and writer-managed Content-Length
 * from the recorder writer. Header lookups flush automatically. */
void neverc_httptest_recorder_flush(neverc_httptest_recorder_t *rec);

/* Get a response header value from the recorder. */
const char *neverc_httptest_recorder_header(
    neverc_httptest_recorder_t *rec, const char *name);

/* Free a recorder. */
void neverc_httptest_recorder_free(neverc_httptest_recorder_t *rec);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/net.h>
#endif

#endif /* NEVERC_NET_HTTP_HTTPTEST_H */
