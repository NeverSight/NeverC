#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

static int allocation_call;
static int fail_on_call;

static void *threadpool_test_calloc(size_t count, size_t size) {
    allocation_call++;
    if (allocation_call == fail_on_call)
        return NULL;
    return calloc(count, size);
}

#define NC_THREADPOOL_CALLOC threadpool_test_calloc
#include "_net_thread.h"

int main(void) {
    for (fail_on_call = 1; fail_on_call <= 3; fail_on_call++) {
        allocation_call = 0;
        nc_threadpool_t *pool = nc_threadpool_create(2);
        if (pool != NULL) {
            nc_threadpool_destroy(pool);
            return 1;
        }
    }

    fail_on_call = 0;
    allocation_call = 0;
    nc_threadpool_t *pool = nc_threadpool_create(1);
    if (!pool) return 2;
    if (nc_threadpool_submit(pool, NULL, NULL) != -1) {
        nc_threadpool_destroy(pool);
        return 3;
    }
    nc_threadpool_destroy(pool);

    puts("passed");
    return 0;
}
