#include "neverc/std/context.h"
#include "neverc/std/thread.h"

#include <stdint.h>
#include <stdio.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",               \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef struct {
    neverc_thread_channel_t *output;
    intptr_t value;
} emit_task_arg_t;

static void emit_task(void *opaque) {
    emit_task_arg_t *arg = (emit_task_arg_t *)opaque;
    (void)neverc_thread_channel_send(arg->output, (void *)arg->value);
}

typedef struct {
    neverc_thread_channel_t *started;
    neverc_thread_channel_t *release;
} blocking_task_arg_t;

static void blocking_task(void *opaque) {
    blocking_task_arg_t *arg = (blocking_task_arg_t *)opaque;
    void *ignored = NULL;
    (void)neverc_thread_channel_send(arg->started, arg);
    (void)neverc_thread_channel_receive(arg->release, &ignored);
}

static void noop_task(void *opaque) {
    (void)opaque;
}

static void sleep_ms(unsigned milliseconds) {
#if defined(_WIN32)
    Sleep(milliseconds);
#else
    usleep(milliseconds * 1000);
#endif
}

static int receive_with_timeout(neverc_thread_channel_t *channel,
                                void **value_out) {
    neverc_context_t *background = neverc_context_background();
    if (!background)
        return NEVERC_THREAD_NOMEM;
    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *ctx =
        neverc_context_with_timeout_handle(background, 2000, &cancel);
    if (!ctx) {
        neverc_context_free(background);
        return NEVERC_THREAD_NOMEM;
    }

    int result =
        neverc_thread_channel_receive_context(channel, ctx, value_out);
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(ctx);
    neverc_context_free(background);
    return result;
}

typedef struct {
    neverc_thread_channel_t *target;
    neverc_thread_channel_t *started;
    neverc_thread_channel_t *completed;
    neverc_context_t *ctx;
    void *value;
    int send;
    int result;
} channel_wait_task_arg_t;

static void channel_wait_task(void *opaque) {
    channel_wait_task_arg_t *arg = (channel_wait_task_arg_t *)opaque;
    void *value = NULL;
    (void)neverc_thread_channel_send(arg->started, arg);
    if (arg->send) {
        arg->result = neverc_thread_channel_send_context(
            arg->target, arg->ctx, arg->value);
    } else {
        arg->result = neverc_thread_channel_receive_context(
            arg->target, arg->ctx, &value);
    }
    (void)neverc_thread_channel_send(arg->completed, arg);
}

typedef struct {
    neverc_thread_executor_t *target;
    neverc_thread_channel_t *started;
    neverc_thread_channel_t *completed;
    neverc_context_t *ctx;
    int result;
} submit_wait_task_arg_t;

static void submit_wait_task(void *opaque) {
    submit_wait_task_arg_t *arg = (submit_wait_task_arg_t *)opaque;
    (void)neverc_thread_channel_send(arg->started, arg);
    arg->result = neverc_thread_executor_submit_context(
        arg->target, arg->ctx, noop_task, NULL);
    (void)neverc_thread_channel_send(arg->completed, arg);
}

typedef struct {
    neverc_thread_executor_t *target;
    neverc_thread_channel_t *completed;
    int wait_result;
    int shutdown_result;
} executor_self_task_arg_t;

static void executor_self_task(void *opaque) {
    executor_self_task_arg_t *arg = (executor_self_task_arg_t *)opaque;
    arg->wait_result = neverc_thread_executor_wait(arg->target);
    arg->shutdown_result = neverc_thread_executor_shutdown(arg->target);
    (void)neverc_thread_channel_send(arg->completed, arg);
}

typedef struct {
    neverc_thread_executor_t *target;
    neverc_thread_channel_t *completed;
    int result;
} shutdown_task_arg_t;

static void shutdown_task(void *opaque) {
    shutdown_task_arg_t *arg = (shutdown_task_arg_t *)opaque;
    arg->result = neverc_thread_executor_shutdown(arg->target);
    (void)neverc_thread_channel_send(arg->completed, arg);
}

static int test_channel_fifo_and_close(void) {
    neverc_thread_channel_t *channel = neverc_thread_channel_create(2);
    CHECK(channel != NULL);
    CHECK(neverc_thread_channel_capacity(channel) == 2);
    CHECK(neverc_thread_channel_length(channel) == 0);

    void *value = NULL;
    CHECK(neverc_thread_channel_try_receive(channel, &value) ==
          NEVERC_THREAD_WOULD_BLOCK);
    CHECK(neverc_thread_channel_try_send(channel, (void *)(intptr_t)11) ==
          NEVERC_THREAD_OK);
    CHECK(neverc_thread_channel_try_send(channel, (void *)(intptr_t)22) ==
          NEVERC_THREAD_OK);
    CHECK(neverc_thread_channel_try_send(channel, (void *)(intptr_t)33) ==
          NEVERC_THREAD_WOULD_BLOCK);
    CHECK(neverc_thread_channel_length(channel) == 2);

    CHECK(neverc_thread_channel_close(channel) == NEVERC_THREAD_OK);
    CHECK(neverc_thread_channel_close(channel) == NEVERC_THREAD_OK);
    CHECK(neverc_thread_channel_send(channel, (void *)(intptr_t)44) ==
          NEVERC_THREAD_CLOSED);
    CHECK(neverc_thread_channel_receive(channel, &value) == NEVERC_THREAD_OK);
    CHECK((intptr_t)value == 11);
    CHECK(neverc_thread_channel_receive(channel, &value) == NEVERC_THREAD_OK);
    CHECK((intptr_t)value == 22);
    CHECK(neverc_thread_channel_receive(channel, &value) ==
          NEVERC_THREAD_CLOSED);

    neverc_thread_channel_free(channel);
    return 0;
}

static int test_channel_wait_cancel_and_close(void) {
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(2, 4);
    neverc_thread_channel_t *target = neverc_thread_channel_create(1);
    neverc_thread_channel_t *full = neverc_thread_channel_create(1);
    neverc_thread_channel_t *started = neverc_thread_channel_create(3);
    neverc_thread_channel_t *completed = neverc_thread_channel_create(3);
    neverc_context_t *background = neverc_context_background();
    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *ctx =
        neverc_context_with_cancel_handle(background, &cancel);
    CHECK(executor != NULL);
    CHECK(target != NULL);
    CHECK(full != NULL);
    CHECK(started != NULL);
    CHECK(completed != NULL);
    CHECK(background != NULL);
    CHECK(ctx != NULL);
    CHECK(cancel != NULL);

    channel_wait_task_arg_t cancelled_receiver = {
        target, started, completed, ctx, NULL, 0, NEVERC_THREAD_SYSTEM};
    CHECK(neverc_thread_executor_submit(
              executor, channel_wait_task, &cancelled_receiver) ==
          NEVERC_THREAD_OK);
    void *value = NULL;
    CHECK(receive_with_timeout(started, &value) == NEVERC_THREAD_OK);
    CHECK(value == &cancelled_receiver);
    sleep_ms(30);
    neverc_context_cancel_handle_cancel(cancel);
    CHECK(receive_with_timeout(completed, &value) == NEVERC_THREAD_OK);
    CHECK(value == &cancelled_receiver);
    CHECK(cancelled_receiver.result == NEVERC_THREAD_CANCELLED);

    channel_wait_task_arg_t closed_receiver = {
        target, started, completed, NULL, NULL, 0, NEVERC_THREAD_SYSTEM};
    CHECK(neverc_thread_executor_submit(
              executor, channel_wait_task, &closed_receiver) ==
          NEVERC_THREAD_OK);
    CHECK(receive_with_timeout(started, &value) == NEVERC_THREAD_OK);
    CHECK(value == &closed_receiver);
    sleep_ms(30);
    CHECK(neverc_thread_channel_close(target) == NEVERC_THREAD_OK);
    CHECK(receive_with_timeout(completed, &value) == NEVERC_THREAD_OK);
    CHECK(value == &closed_receiver);
    CHECK(closed_receiver.result == NEVERC_THREAD_CLOSED);

    CHECK(neverc_thread_channel_send(full, &full) == NEVERC_THREAD_OK);
    channel_wait_task_arg_t closed_sender = {
        full, started, completed, NULL, &closed_sender, 1,
        NEVERC_THREAD_SYSTEM};
    CHECK(neverc_thread_executor_submit(
              executor, channel_wait_task, &closed_sender) ==
          NEVERC_THREAD_OK);
    CHECK(receive_with_timeout(started, &value) == NEVERC_THREAD_OK);
    CHECK(value == &closed_sender);
    sleep_ms(30);
    CHECK(neverc_thread_channel_close(full) == NEVERC_THREAD_OK);
    CHECK(receive_with_timeout(completed, &value) == NEVERC_THREAD_OK);
    CHECK(value == &closed_sender);
    CHECK(closed_sender.result == NEVERC_THREAD_CLOSED);

    CHECK(neverc_thread_executor_wait(executor) == NEVERC_THREAD_OK);
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(ctx);
    neverc_context_free(background);
    neverc_thread_channel_free(completed);
    neverc_thread_channel_free(started);
    neverc_thread_channel_free(full);
    neverc_thread_channel_free(target);
    neverc_thread_executor_free(executor);
    return 0;
}

static int test_channel_context_cancel(void) {
    neverc_context_t *background = neverc_context_background();
    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *ctx =
        neverc_context_with_cancel_handle(background, &cancel);
    neverc_thread_channel_t *channel = neverc_thread_channel_create(1);
    CHECK(background != NULL);
    CHECK(ctx != NULL);
    CHECK(cancel != NULL);
    CHECK(channel != NULL);

    CHECK(neverc_thread_channel_send(channel, (void *)(intptr_t)1) ==
          NEVERC_THREAD_OK);
    neverc_context_cancel_handle_cancel(cancel);
    CHECK(neverc_thread_channel_send_context(
              channel, ctx, (void *)(intptr_t)2) ==
          NEVERC_THREAD_CANCELLED);

    void *value = NULL;
    CHECK(neverc_thread_channel_receive(channel, &value) == NEVERC_THREAD_OK);
    CHECK((intptr_t)value == 1);
    CHECK(neverc_thread_channel_receive_context(channel, ctx, &value) ==
          NEVERC_THREAD_CANCELLED);

    neverc_thread_channel_free(channel);
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(ctx);
    neverc_context_free(background);
    return 0;
}

static int test_executor_runs_tasks(void) {
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(2, 8);
    neverc_thread_channel_t *output = neverc_thread_channel_create(8);
    emit_task_arg_t args[8];
    CHECK(executor != NULL);
    CHECK(output != NULL);

    for (intptr_t i = 0; i < 8; ++i) {
        args[i].output = output;
        args[i].value = i + 1;
        CHECK(neverc_thread_executor_submit(executor, emit_task, &args[i]) ==
              NEVERC_THREAD_OK);
    }

    intptr_t sum = 0;
    for (int i = 0; i < 8; ++i) {
        void *value = NULL;
        CHECK(neverc_thread_channel_receive(output, &value) ==
              NEVERC_THREAD_OK);
        sum += (intptr_t)value;
    }
    CHECK(sum == 36);
    CHECK(neverc_thread_executor_wait(executor) == NEVERC_THREAD_OK);
    CHECK(neverc_thread_executor_pending(executor) == 0);
    CHECK(neverc_thread_executor_active(executor) == 0);
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);
    CHECK(neverc_thread_executor_try_submit(executor, noop_task, NULL) ==
          NEVERC_THREAD_CLOSED);

    neverc_thread_channel_free(output);
    neverc_thread_executor_free(executor);
    return 0;
}

static int test_executor_backpressure(void) {
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(1, 1);
    neverc_thread_channel_t *started = neverc_thread_channel_create(1);
    neverc_thread_channel_t *release = neverc_thread_channel_create(1);
    blocking_task_arg_t task_arg = {started, release};
    CHECK(executor != NULL);
    CHECK(started != NULL);
    CHECK(release != NULL);

    CHECK(neverc_thread_executor_submit(executor, blocking_task, &task_arg) ==
          NEVERC_THREAD_OK);
    void *value = NULL;
    CHECK(neverc_thread_channel_receive(started, &value) == NEVERC_THREAD_OK);
    CHECK(neverc_thread_executor_submit(executor, noop_task, NULL) ==
          NEVERC_THREAD_OK);
    CHECK(neverc_thread_executor_try_submit(executor, noop_task, NULL) ==
          NEVERC_THREAD_WOULD_BLOCK);
    CHECK(neverc_thread_channel_send(release, executor) == NEVERC_THREAD_OK);
    CHECK(neverc_thread_executor_wait(executor) == NEVERC_THREAD_OK);

    neverc_thread_channel_free(started);
    neverc_thread_channel_free(release);
    neverc_thread_executor_free(executor);
    return 0;
}

static int test_executor_context_waits(void) {
    neverc_thread_executor_t *target =
        neverc_thread_executor_create(1, 1);
    neverc_thread_executor_t *control =
        neverc_thread_executor_create(1, 1);
    neverc_thread_channel_t *blocker_started =
        neverc_thread_channel_create(1);
    neverc_thread_channel_t *release = neverc_thread_channel_create(1);
    neverc_thread_channel_t *submit_started =
        neverc_thread_channel_create(1);
    neverc_thread_channel_t *submit_completed =
        neverc_thread_channel_create(1);
    blocking_task_arg_t blocker = {blocker_started, release};
    neverc_context_t *background = neverc_context_background();
    neverc_context_cancel_handle_t *cancel = NULL;
    neverc_context_t *ctx =
        neverc_context_with_cancel_handle(background, &cancel);
    CHECK(target != NULL);
    CHECK(control != NULL);
    CHECK(blocker_started != NULL);
    CHECK(release != NULL);
    CHECK(submit_started != NULL);
    CHECK(submit_completed != NULL);
    CHECK(background != NULL);
    CHECK(ctx != NULL);
    CHECK(cancel != NULL);

    CHECK(neverc_thread_executor_submit(target, blocking_task, &blocker) ==
          NEVERC_THREAD_OK);
    void *value = NULL;
    CHECK(receive_with_timeout(blocker_started, &value) ==
          NEVERC_THREAD_OK);
    CHECK(neverc_thread_executor_submit(target, noop_task, NULL) ==
          NEVERC_THREAD_OK);

    submit_wait_task_arg_t submit = {
        target, submit_started, submit_completed, ctx, NEVERC_THREAD_SYSTEM};
    CHECK(neverc_thread_executor_submit(
              control, submit_wait_task, &submit) == NEVERC_THREAD_OK);
    CHECK(receive_with_timeout(submit_started, &value) == NEVERC_THREAD_OK);
    CHECK(value == &submit);
    sleep_ms(30);
    neverc_context_cancel_handle_cancel(cancel);
    CHECK(receive_with_timeout(submit_completed, &value) ==
          NEVERC_THREAD_OK);
    CHECK(value == &submit);
    CHECK(submit.result == NEVERC_THREAD_CANCELLED);

    neverc_context_t *wait_background = neverc_context_background();
    neverc_context_cancel_handle_t *wait_cancel = NULL;
    neverc_context_t *wait_ctx =
        neverc_context_with_timeout_handle(
            wait_background, 30, &wait_cancel);
    CHECK(wait_background != NULL);
    CHECK(wait_ctx != NULL);
    CHECK(wait_cancel != NULL);
    CHECK(neverc_thread_executor_wait_context(target, wait_ctx) ==
          NEVERC_THREAD_CANCELLED);

    CHECK(neverc_thread_channel_send(release, target) == NEVERC_THREAD_OK);
    CHECK(neverc_thread_executor_wait(target) == NEVERC_THREAD_OK);
    CHECK(neverc_thread_executor_wait(control) == NEVERC_THREAD_OK);

    neverc_context_cancel_handle_free(wait_cancel);
    neverc_context_free(wait_ctx);
    neverc_context_free(wait_background);
    neverc_context_cancel_handle_free(cancel);
    neverc_context_free(ctx);
    neverc_context_free(background);
    neverc_thread_channel_free(submit_completed);
    neverc_thread_channel_free(submit_started);
    neverc_thread_channel_free(release);
    neverc_thread_channel_free(blocker_started);
    neverc_thread_executor_free(control);
    neverc_thread_executor_free(target);
    return 0;
}

static int test_executor_reentrancy_and_concurrent_shutdown(void) {
    neverc_thread_executor_t *target =
        neverc_thread_executor_create(1, 2);
    neverc_thread_executor_t *control =
        neverc_thread_executor_create(2, 2);
    neverc_thread_channel_t *completed = neverc_thread_channel_create(3);
    CHECK(target != NULL);
    CHECK(control != NULL);
    CHECK(completed != NULL);

    executor_self_task_arg_t self = {
        target, completed, NEVERC_THREAD_SYSTEM, NEVERC_THREAD_SYSTEM};
    CHECK(neverc_thread_executor_submit(target, executor_self_task, &self) ==
          NEVERC_THREAD_OK);
    void *value = NULL;
    CHECK(receive_with_timeout(completed, &value) == NEVERC_THREAD_OK);
    CHECK(value == &self);
    CHECK(self.wait_result == NEVERC_THREAD_INVALID);
    CHECK(self.shutdown_result == NEVERC_THREAD_INVALID);

    shutdown_task_arg_t shutdowns[2] = {
        {target, completed, NEVERC_THREAD_SYSTEM},
        {target, completed, NEVERC_THREAD_SYSTEM},
    };
    CHECK(neverc_thread_executor_submit(
              control, shutdown_task, &shutdowns[0]) == NEVERC_THREAD_OK);
    CHECK(neverc_thread_executor_submit(
              control, shutdown_task, &shutdowns[1]) == NEVERC_THREAD_OK);
    CHECK(receive_with_timeout(completed, &value) == NEVERC_THREAD_OK);
    CHECK(receive_with_timeout(completed, &value) == NEVERC_THREAD_OK);
    CHECK(shutdowns[0].result == NEVERC_THREAD_OK);
    CHECK(shutdowns[1].result == NEVERC_THREAD_OK);
    CHECK(neverc_thread_executor_wait(control) == NEVERC_THREAD_OK);
    CHECK(neverc_thread_executor_shutdown(target) == NEVERC_THREAD_OK);

    neverc_thread_channel_free(completed);
    neverc_thread_executor_free(control);
    neverc_thread_executor_free(target);
    return 0;
}

int main(void) {
    CHECK(neverc_thread_channel_create(0) == NULL);
    CHECK(neverc_thread_channel_create(SIZE_MAX) == NULL);
    CHECK(neverc_thread_executor_create(0, 1) == NULL);
    CHECK(neverc_thread_executor_create(1, 0) == NULL);
    CHECK(neverc_thread_executor_create(SIZE_MAX, 1) == NULL);
    CHECK(test_channel_fifo_and_close() == 0);
    CHECK(test_channel_wait_cancel_and_close() == 0);
    CHECK(test_channel_context_cancel() == 0);
    CHECK(test_executor_runs_tasks() == 0);
    CHECK(test_executor_backpressure() == 0);
    CHECK(test_executor_context_waits() == 0);
    CHECK(test_executor_reentrancy_and_concurrent_shutdown() == 0);
    puts("passed");
    return 0;
}
