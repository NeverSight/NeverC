#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *fail_realloc(void *ptr, size_t size) {
    (void)ptr;
    (void)size;
    return NULL;
}

#ifndef _WIN32
#include <pthread.h>

static int (*real_pthread_mutex_lock)(pthread_mutex_t *) = pthread_mutex_lock;
static int (*real_pthread_mutex_unlock)(pthread_mutex_t *) = pthread_mutex_unlock;

static int hook_lock(pthread_mutex_t *m);
static int hook_unlock(pthread_mutex_t *m);

#include "../../../std/src/net/_net_platform.h"
#undef nc_mutex_lock
#undef nc_mutex_unlock
#define nc_mutex_lock(m) hook_lock(m)
#define nc_mutex_unlock(m) hook_unlock(m)
#endif

#define NC_H2_REALLOC fail_realloc
#include "../../../std/src/net/http/http2/http2_server.c"

static int checks;

#define CHECK(condition)                                                     \
    do {                                                                     \
        checks++;                                                            \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",               \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#ifndef _WIN32
static h2_conn_t *hook_conn;
static h2_stream_t *hook_stream;
static int hook_mode;
static int hook_armed;
static int hook_gap_closed;

static int hook_lock(pthread_mutex_t *m) {
    int rc = real_pthread_mutex_lock(m);
    if (rc != 0) return rc;
    if (hook_mode == 1 && hook_armed && hook_conn &&
        m == &hook_conn->state_lock) {
        hook_stream->state = H2_STREAM_CLOSED;
        hook_armed = 0;
    }
    return rc;
}

static int hook_unlock(pthread_mutex_t *m) {
    if (hook_mode == 2 && hook_armed && hook_conn &&
        m == &hook_conn->state_lock &&
        hook_stream->state == H2_STREAM_OPEN) {
        hook_stream->state = H2_STREAM_CLOSED;
        hook_gap_closed = 1;
        hook_armed = 0;
    }
    return real_pthread_mutex_unlock(m);
}

static const uint8_t trailer_block[] = {
    0x00, 11, 'g', 'r', 'p', 'c', '-', 's', 't', 'a', 't', 'u', 's', 1, '0'
};

static int setup_trailer(h2_conn_t *conn, h2_stream_t *stream) {
    memset(conn, 0, sizeof(*conn));
    memset(stream, 0, sizeof(*stream));
    nc_mutex_init(&conn->state_lock);
    conn->hpack_dec = neverc_hpack_decoder_create(4096);
    if (!conn->hpack_dec) return -1;
    conn->local_settings.max_header_list_size = 65536;
    stream->conn = conn;
    stream->headers_complete = 1;
    stream->streaming_request = 1;
    stream->content_length = -1;
    stream->state = H2_STREAM_OPEN;
    return 0;
}

static void teardown_trailer(h2_conn_t *conn) {
    neverc_hpack_decoder_destroy(conn->hpack_dec);
    nc_mutex_destroy(&conn->state_lock);
}

/* A close that lands inside the state_lock hold, before the stream is
 * observed, must be the state the trailer decision uses. */
static int test_trailer_close_on_lock_entry(void) {
    h2_conn_t conn;
    h2_stream_t stream;
    CHECK(setup_trailer(&conn, &stream) == 0);
    hook_conn = &conn;
    hook_stream = &stream;
    hook_mode = 1;
    hook_armed = 1;
    hook_gap_closed = 0;
    int rc = h2_process_trailer_block(&conn, &stream, trailer_block,
                                      sizeof(trailer_block), 1);
    CHECK(rc == -2);
    CHECK(stream.state == H2_STREAM_CLOSED);
    CHECK(stream.remote_ended == 0);
    hook_conn = NULL;
    hook_mode = 0;
    teardown_trailer(&conn);
    return 0;
}

/* Releasing the lock while the stream is still open lets a closer publish
 * CLOSED in that gap. A later store must not overwrite it. */
static int test_trailer_close_in_unlock_gap(void) {
    h2_conn_t conn;
    h2_stream_t stream;
    CHECK(setup_trailer(&conn, &stream) == 0);
    hook_conn = &conn;
    hook_stream = &stream;
    hook_mode = 2;
    hook_armed = 1;
    hook_gap_closed = 0;
    int rc = h2_process_trailer_block(&conn, &stream, trailer_block,
                                      sizeof(trailer_block), 1);
    if (hook_gap_closed) {
        CHECK(rc == -2);
        CHECK(stream.state == H2_STREAM_CLOSED);
        CHECK(stream.remote_ended == 0);
    } else {
        CHECK(rc == 0);
        CHECK(stream.state == H2_STREAM_HALF_CLOSED_REMOTE);
        CHECK(stream.remote_ended == 1);
    }
    hook_conn = NULL;
    hook_mode = 0;
    teardown_trailer(&conn);
    return 0;
}

static int test_trailer_already_closed(void) {
    h2_conn_t conn;
    h2_stream_t stream;
    CHECK(setup_trailer(&conn, &stream) == 0);
    stream.state = H2_STREAM_CLOSED;
    hook_mode = 0;
    int rc = h2_process_trailer_block(&conn, &stream, trailer_block,
                                      sizeof(trailer_block), 1);
    CHECK(rc == -2);
    CHECK(stream.state == H2_STREAM_CLOSED);
    CHECK(stream.remote_ended == 0);
    teardown_trailer(&conn);
    return 0;
}

static int test_trailer_open_publishes_half_closed(void) {
    h2_conn_t conn;
    h2_stream_t stream;
    CHECK(setup_trailer(&conn, &stream) == 0);
    hook_mode = 0;
    int rc = h2_process_trailer_block(&conn, &stream, trailer_block,
                                      sizeof(trailer_block), 1);
    CHECK(rc == 0);
    CHECK(stream.state == H2_STREAM_HALF_CLOSED_REMOTE);
    CHECK(stream.remote_ended == 1);
    teardown_trailer(&conn);
    return 0;
}

static int test_headers_close_on_lock_entry(void) {
    h2_conn_t conn;
    h2_stream_t stream;
    CHECK(setup_trailer(&conn, &stream) == 0);
    hook_conn = &conn;
    hook_stream = &stream;
    hook_mode = 1;
    hook_armed = 1;
    int reject = h2_headers_stream_is_closed(&conn, &stream);
    CHECK(reject == 1);
    CHECK(stream.state == H2_STREAM_CLOSED);
    hook_conn = NULL;
    hook_mode = 0;
    teardown_trailer(&conn);
    return 0;
}

static int test_headers_open_is_not_rejected(void) {
    h2_conn_t conn;
    h2_stream_t stream;
    CHECK(setup_trailer(&conn, &stream) == 0);
    hook_mode = 0;
    int reject = h2_headers_stream_is_closed(&conn, &stream);
    CHECK(reject == 0);
    CHECK(stream.state == H2_STREAM_OPEN);
    stream.reset = 1;
    CHECK(h2_headers_stream_is_closed(&conn, &stream) == 1);
    teardown_trailer(&conn);
    return 0;
}
#endif

static int test_buffer_oom(void) {
    uint8_t *buffer = (uint8_t *)malloc(8);
    CHECK(buffer != NULL);
    memset(buffer, 0x5a, 8);

    uint8_t *original = buffer;
    size_t length = 8;
    size_t capacity = 8;
    const uint8_t extra = 0xa5;

    CHECK(h2_buffer_append(&buffer, &length, &capacity, &extra, 1) == -1);
    CHECK(buffer == original);
    CHECK(length == 8);
    CHECK(capacity == 8);
    for (size_t i = 0; i < length; i++)
        CHECK(buffer[i] == 0x5a);

    free(buffer);
    return 0;
}

int main(void) {
#ifndef _WIN32
    if (test_trailer_close_on_lock_entry() != 0) return 1;
    if (test_trailer_close_in_unlock_gap() != 0) return 1;
    if (test_trailer_already_closed() != 0) return 1;
    if (test_trailer_open_publishes_half_closed() != 0) return 1;
    if (test_headers_close_on_lock_entry() != 0) return 1;
    if (test_headers_open_is_not_rejected() != 0) return 1;
#endif
    if (test_buffer_oom() != 0) return 1;
    printf("http2-oom: %d checks, 0 failed\n", checks);
    puts("passed");
    return 0;
}
