#include "neverc/std/thread.h"
#include "../../../std/src/net/rpc/_rpc_client_epoch.h"

#include <stdio.h>

typedef struct {
    nc_rpc_client_epoch_t *epoch;
    neverc_thread_channel_t *completed;
    int marker;
} epoch_wait_task_t;

typedef struct {
    nc_rpc_client_epoch_t *epoch;
    neverc_thread_channel_t *pinned;
    neverc_thread_channel_t *proceed;
    neverc_thread_channel_t *completed;
    neverc_thread_channel_t *lease_queue;
    uint64_t generation;
    int pin_result;
    int send_result;
    int marker;
} epoch_send_task_t;

static void epoch_wait_for_idle(void *context) {
    epoch_wait_task_t *task = (epoch_wait_task_t *)context;
    nc_rpc_client_epoch_wait_idle(task->epoch);
    (void)neverc_thread_channel_send(task->completed, &task->marker);
}

static void epoch_send_after_release(void *context) {
    epoch_send_task_t *task = (epoch_send_task_t *)context;
    nc_rpc_client_epoch_lease_t lease;
    task->pin_result =
        nc_rpc_client_epoch_pin(task->epoch, 1U, &lease);
    if (task->pin_result == 0) {
        task->lease_queue = lease.send_queue;
        task->generation = lease.generation;
    }
    (void)neverc_thread_channel_send(task->pinned, &task->marker);
    void *value = NULL;
    (void)neverc_thread_channel_receive(task->proceed, &value);
    task->send_result = task->pin_result == 0
        ? neverc_thread_channel_send(lease.send_queue, &task->marker)
        : NEVERC_THREAD_INVALID;
    nc_rpc_client_epoch_unpin(&lease);
    (void)neverc_thread_channel_send(task->completed, &task->marker);
}

static int epoch_wait_until_blocked(nc_rpc_client_epoch_t *epoch) {
    nc_mutex_lock(&epoch->lock);
    while (epoch->leases != 0 && epoch->waiters == 0)
        (void)nc_cond_wait(&epoch->idle, &epoch->lock);
    int blocked = epoch->waiters != 0;
    nc_mutex_unlock(&epoch->lock);
    return blocked;
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
    neverc_thread_channel_t *first =
        neverc_thread_channel_create(1U);
    neverc_thread_channel_t *second =
        neverc_thread_channel_create(1U);
    neverc_thread_channel_t *pinned =
        neverc_thread_channel_create(1U);
    neverc_thread_channel_t *proceed =
        neverc_thread_channel_create(1U);
    neverc_thread_channel_t *send_completed =
        neverc_thread_channel_create(1U);
    neverc_thread_channel_t *retire_completed =
        neverc_thread_channel_create(1U);
    neverc_thread_executor_t *executor =
        neverc_thread_executor_create(2U, 2U);
    CHECK(first != NULL);
    CHECK(second != NULL);
    CHECK(pinned != NULL);
    CHECK(proceed != NULL);
    CHECK(send_completed != NULL);
    CHECK(retire_completed != NULL);
    CHECK(executor != NULL);
    if (!first || !second || !pinned || !proceed ||
        !send_completed || !retire_completed || !executor) {
        neverc_thread_executor_free(executor);
        neverc_thread_channel_free(retire_completed);
        neverc_thread_channel_free(send_completed);
        neverc_thread_channel_free(proceed);
        neverc_thread_channel_free(pinned);
        neverc_thread_channel_free(second);
        neverc_thread_channel_free(first);
        return 1;
    }

    nc_rpc_client_epoch_t epoch;
    nc_rpc_client_epoch_init(&epoch);
    CHECK(nc_rpc_client_epoch_prepare(&epoch, first, 1U) == 0);
    CHECK(nc_rpc_client_epoch_is_active(&epoch));

    epoch_send_task_t send_task = {
        &epoch, pinned, proceed, send_completed, NULL, 0,
        NEVERC_THREAD_INVALID, NEVERC_THREAD_INVALID, 1};
    int send_submitted = neverc_thread_executor_submit(
        executor, epoch_send_after_release, &send_task);
    CHECK(send_submitted == NEVERC_THREAD_OK);
    void *value = NULL;
    if (send_submitted == NEVERC_THREAD_OK)
        CHECK(neverc_thread_channel_receive(pinned, &value) ==
              NEVERC_THREAD_OK);
    CHECK(value == &send_task.marker);
    CHECK(send_task.pin_result == 0);
    CHECK(send_task.lease_queue == first);
    CHECK(send_task.generation == 1U);

    CHECK(nc_rpc_client_epoch_stop(&epoch) == first);
    CHECK(!nc_rpc_client_epoch_is_active(&epoch));
    CHECK(neverc_thread_channel_close(first) == NEVERC_THREAD_OK);
    neverc_thread_channel_t *retired = NULL;
    if (send_task.pin_result == 0) {
        CHECK(nc_rpc_client_epoch_clear(&epoch, &retired) == -1);
        CHECK(retired == NULL);
    }

    nc_rpc_client_epoch_lease_t rejected;
    CHECK(nc_rpc_client_epoch_pin(&epoch, 1U, &rejected) == -1);

    epoch_wait_task_t wait_task = {&epoch, retire_completed, 2};
    int wait_submitted = neverc_thread_executor_submit(
        executor, epoch_wait_for_idle, &wait_task);
    CHECK(wait_submitted == NEVERC_THREAD_OK);
    if (wait_submitted == NEVERC_THREAD_OK &&
        send_task.pin_result == 0) {
        CHECK(epoch_wait_until_blocked(&epoch));
        CHECK(neverc_thread_channel_try_receive(retire_completed, &value) ==
              NEVERC_THREAD_WOULD_BLOCK);
    }
    if (send_submitted == NEVERC_THREAD_OK) {
        CHECK(neverc_thread_channel_send(proceed, &send_task.marker) ==
              NEVERC_THREAD_OK);
        CHECK(neverc_thread_channel_receive(send_completed, &value) ==
              NEVERC_THREAD_OK);
        CHECK(value == &send_task.marker);
        CHECK(send_task.send_result == NEVERC_THREAD_CLOSED);
    }
    if (wait_submitted == NEVERC_THREAD_OK) {
        CHECK(neverc_thread_channel_receive(retire_completed, &value) ==
              NEVERC_THREAD_OK);
        CHECK(value == &wait_task.marker);
    }
    CHECK(neverc_thread_executor_shutdown(executor) == NEVERC_THREAD_OK);

    CHECK(nc_rpc_client_epoch_clear(&epoch, &retired) == 0);
    CHECK(retired == first);
    CHECK(nc_rpc_client_epoch_prepare(&epoch, second, 2U) == 0);
    CHECK(nc_rpc_client_epoch_pin(&epoch, 1U, &rejected) == -1);
    nc_rpc_client_epoch_lease_t lease;
    CHECK(nc_rpc_client_epoch_pin(&epoch, 2U, &lease) == 0);
    CHECK(lease.send_queue == second);
    CHECK(lease.generation == 2U);
    nc_rpc_client_epoch_unpin(&lease);
    CHECK(nc_rpc_client_epoch_stop(&epoch) == second);
    CHECK(nc_rpc_client_epoch_clear(&epoch, &retired) == 0);
    CHECK(retired == second);

    CHECK(nc_rpc_client_epoch_destroy(&epoch) == 0);
    neverc_thread_executor_free(executor);
    neverc_thread_channel_free(retire_completed);
    neverc_thread_channel_free(send_completed);
    neverc_thread_channel_free(proceed);
    neverc_thread_channel_free(pinned);
    neverc_thread_channel_free(second);
    neverc_thread_channel_free(first);
    printf("rpc client epoch: %d checks, %d failed\n",
           tests_run, tests_failed);
    if (tests_failed == 0) puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
