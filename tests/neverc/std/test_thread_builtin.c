#include "neverc/std/thread.h"

#include <stdint.h>
#include <stdio.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",               \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void set_value(void *opaque) {
    *(int *)opaque = 42;
}

int main(void) {
    neverc_thread_channel_t *channel = thread.channel_create(1);
    CHECK(channel != NULL);
    CHECK(thread.channel_try_send(channel, (void *)(intptr_t)42) ==
          NEVERC_THREAD_OK);

    void *value = NULL;
    CHECK(thread.channel_receive(channel, &value) == NEVERC_THREAD_OK);
    CHECK((intptr_t)value == 42);
    CHECK(thread.channel_close(channel) == NEVERC_THREAD_OK);
    CHECK(thread.channel_receive(channel, &value) == NEVERC_THREAD_CLOSED);
    thread.channel_free(channel);

    neverc_thread_executor_t *executor = thread.executor_create(1, 1);
    int task_value = 0;
    CHECK(executor != NULL);
    CHECK(thread.executor_submit(executor, set_value, &task_value) ==
          NEVERC_THREAD_OK);
    CHECK(thread.executor_wait(executor) == NEVERC_THREAD_OK);
    CHECK(task_value == 42);
    CHECK(thread.executor_shutdown(executor) == NEVERC_THREAD_OK);
    thread.executor_free(executor);

    puts("passed");
    return 0;
}
