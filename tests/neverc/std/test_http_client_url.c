#include "neverc/std/net/http.h"
#include "neverc/std/net/tcp.h"

#include <stdio.h>
#include <string.h>

static unsigned controlled_dial_calls;
static char controlled_dial_address[280];

static neverc_net_result_t controlled_tcp_dial_context(
    const char *address, neverc_context_t *context,
    neverc_tcp_conn_t **connection_out);

/* Exercise the public HTTP API while replacing only the external dial
 * boundary.  Including tcp.h before this macro keeps its public declaration
 * intact; the macro rewrites the call inside http_client.c, not the header. */
#define neverc_tcp_dial_context controlled_tcp_dial_context
#include "../../../std/src/net/http/http_client.c"
#undef neverc_tcp_dial_context

static neverc_net_result_t controlled_tcp_dial_context(
    const char *address, neverc_context_t *context,
    neverc_tcp_conn_t **connection_out) {
    (void)context;
    controlled_dial_calls++;
    snprintf(controlled_dial_address, sizeof(controlled_dial_address), "%s",
             address ? address : "");
    if (connection_out) *connection_out = NULL;
    return (neverc_net_result_t){NEVERC_NET_RESOLVE, 0, "dial", 0U};
}

static void reset_dial_probe(void) {
    controlled_dial_calls = 0;
    controlled_dial_address[0] = '\0';
}

static int test_valid_url_reaches_controlled_dial(const char *label,
                                                  const char *url,
                                                  const char *dial_address) {
    reset_dial_probe();
    neverc_http_response_t *response = neverc_http_get(url);
    int passed = response != NULL && controlled_dial_calls == 1 &&
        strcmp(controlled_dial_address, dial_address) == 0;
    printf("[%s] error=%s dial_calls=%u dial_address=%s\n", label,
           response && response->error ? response->error : "<null>",
           controlled_dial_calls,
           controlled_dial_address[0] ? controlled_dial_address : "<empty>");
    neverc_http_response_free(response);
    if (!passed) fprintf(stderr, "[%s] failed\n", label);
    return passed ? 0 : 1;
}

static int test_invalid_url_is_rejected_before_dial(const char *label,
                                                     const char *url) {
    reset_dial_probe();
    neverc_http_response_t *response = neverc_http_get(url);
    int passed = response != NULL && response->error != NULL &&
        strcmp(response->error, "invalid url") == 0 &&
        controlled_dial_calls == 0 && controlled_dial_address[0] == '\0';
    printf("[%s] error=%s dial_calls=%u dial_address=%s\n", label,
           response && response->error ? response->error : "<null>",
           controlled_dial_calls,
           controlled_dial_address[0] ? controlled_dial_address : "<empty>");
    neverc_http_response_free(response);
    if (!passed) fprintf(stderr, "[%s] failed\n", label);
    return passed ? 0 : 1;
}

int main(void) {
    int failed = 0;
    failed |= test_valid_url_reaches_controlled_dial(
        "valid-http", "http://xn--bcher-kva.de/", "xn--bcher-kva.de:80");
    failed |= test_invalid_url_is_rejected_before_dial(
        "invalid-lower-http", "http://xn--example-.com/");
    failed |= test_invalid_url_is_rejected_before_dial(
        "invalid-upper-http", "http://XN--EXAMPLE-.COM/");
    failed |= test_invalid_url_is_rejected_before_dial(
        "invalid-lower-https", "https://xn--example-.com/");
    failed |= test_valid_url_reaches_controlled_dial(
        "valid-ipv6-http", "http://[2001:db8::1]/", "[2001:db8::1]:80");
    failed |= test_invalid_url_is_rejected_before_dial(
        "invalid-ipv6-http", "http://[2001:::1]/");
    if (failed) return 1;
    puts("passed");
    return 0;
}
