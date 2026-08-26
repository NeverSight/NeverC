#include "neverc/std/context.h"
#include "neverc/std/thread.h"
#include "../../../std/src/net/rpc/_rpc_server_queue.h"

#include <stdio.h>

typedef struct {
    int context_calls;
    int try_calls;
    int blocking_calls;
    neverc_thread_channel_t *last_queue;
    neverc_context_t *last_context;
    void *last_value;
} queue_probe_t;

static queue_probe_t *active_probe;

static int probe_send_context(neverc_thread_channel_t *queue,
                              neverc_context_t *context, void *value) {
    queue_probe_t *probe = active_probe;
    if (!probe) return NEVERC_THREAD_INVALID;
    probe->context_calls++;
    probe->last_queue = queue;
    probe->last_context = context;
    probe->last_value = value;
    return NEVERC_THREAD_CANCELLED;
}

static int probe_try_send(neverc_thread_channel_t *queue, void *value) {
    queue_probe_t *probe = active_probe;
    if (!probe) return NEVERC_THREAD_INVALID;
    probe->try_calls++;
    probe->last_queue = queue;
    probe->last_value = value;
    return NEVERC_THREAD_WOULD_BLOCK;
}

static int probe_blocking_send(neverc_thread_channel_t *queue, void *value) {
    queue_probe_t *probe = active_probe;
    if (!probe) return NEVERC_THREAD_INVALID;
    probe->blocking_calls++;
    probe->last_queue = queue;
    probe->last_value = value;
    return NEVERC_THREAD_CLOSED;
}

static int tests_run;
static int tests_failed;

#define CHECK(condition)                                                     \
    do {                                                                     \
        tests_run++;                                                         \
        if (!(condition)) {                                                  \
            tests_failed++;                                                  \
            printf("  FAIL %s:%d: %s\n", __func__, __LINE__, #condition);  \
        }                                                                    \
    } while (0)

int main(void) {
    queue_probe_t probe = {0};
    char value_storage = 0;
    neverc_thread_channel_t *queue =
        neverc_thread_channel_create(1U);
    neverc_context_t *context = neverc_context_background();
    void *value = &value_storage;
    const nc_rpc_server_queue_ops_t ops = {
        probe_send_context, probe_try_send, probe_blocking_send};
    CHECK(queue != NULL);
    CHECK(context != NULL);
    if (!queue || !context) {
        neverc_context_free(context);
        neverc_thread_channel_free(queue);
        return 1;
    }
    active_probe = &probe;

    CHECK(nc_rpc_server_queue_send_with_ops(
              queue, context, NC_RPC_SERVER_QUEUE_STREAM, value, &ops) ==
          NEVERC_THREAD_CANCELLED);
    CHECK(probe.context_calls == 1);
    CHECK(probe.try_calls == 0);
    CHECK(probe.blocking_calls == 0);
    CHECK(probe.last_queue == queue);
    CHECK(probe.last_context == context);
    CHECK(probe.last_value == value);

    probe.last_value = NULL;
    CHECK(nc_rpc_server_queue_send_with_ops(
              queue, NULL, NC_RPC_SERVER_QUEUE_CONTROL, value, &ops) ==
          NEVERC_THREAD_WOULD_BLOCK);
    CHECK(probe.context_calls == 1);
    CHECK(probe.try_calls == 1);
    CHECK(probe.blocking_calls == 0);
    CHECK(probe.last_queue == queue);
    CHECK(probe.last_value == value);

    probe.last_value = NULL;
    CHECK(nc_rpc_server_queue_send_with_ops(
              queue, NULL, NC_RPC_SERVER_QUEUE_TERMINAL, value, &ops) ==
          NEVERC_THREAD_CLOSED);
    CHECK(probe.context_calls == 1);
    CHECK(probe.try_calls == 1);
    CHECK(probe.blocking_calls == 1);
    CHECK(probe.last_queue == queue);
    CHECK(probe.last_value == value);

    CHECK(nc_rpc_server_queue_send_with_ops(
              queue, NULL, NC_RPC_SERVER_QUEUE_STREAM, value, &ops) ==
          NEVERC_THREAD_INVALID);
    CHECK(probe.context_calls == 1);
    CHECK(nc_rpc_server_queue_send_with_ops(
              queue, context, (nc_rpc_server_queue_mode_t)99, value, &ops) ==
          NEVERC_THREAD_INVALID);
    CHECK(probe.context_calls == 1);
    CHECK(probe.try_calls == 1);
    CHECK(probe.blocking_calls == 1);

    printf("rpc server queue: %d checks, %d failed\n",
           tests_run, tests_failed);
    active_probe = NULL;
    neverc_context_free(context);
    neverc_thread_channel_free(queue);
    if (tests_failed == 0) puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
