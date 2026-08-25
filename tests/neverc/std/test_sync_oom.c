#include <stdio.h>
#include <stdlib.h>
#if !defined(_WIN32)
#include <errno.h>
#include <pthread.h>
#endif

static int fail_key_allocation;
static int fail_bucket_allocation;
static int fail_sync_init;
static int fail_sync_lock_remaining;
#if !defined(_WIN32)
static int fail_mutexattr_init;
static int fail_mutexattr_settype;
static int fail_mutex_init_remaining;

static int controlled_mutexattr_init(pthread_mutexattr_t *attr) {
    return fail_mutexattr_init ? EAGAIN : pthread_mutexattr_init(attr);
}

static int controlled_mutexattr_settype(pthread_mutexattr_t *attr, int type) {
    return fail_mutexattr_settype ? EINVAL :
           pthread_mutexattr_settype(attr, type);
}

static int controlled_mutex_init(pthread_mutex_t *mu,
                                 const pthread_mutexattr_t *attr) {
    if (fail_mutex_init_remaining > 0) {
        fail_mutex_init_remaining--;
        return EAGAIN;
    }
    return pthread_mutex_init(mu, attr);
}
#endif

static int sync_init_should_fail(void) {
    return fail_sync_init;
}

static int sync_lock_should_fail(void) {
    if (fail_sync_lock_remaining > 0) {
        fail_sync_lock_remaining--;
        return 1;
    }
    return 0;
}

static void *controlled_malloc(size_t size) {
    if (fail_key_allocation)
        return NULL;
    return malloc(size);
}

static void *controlled_calloc(size_t count, size_t size) {
    if (fail_bucket_allocation)
        return NULL;
    return calloc(count, size);
}

#define NC_SYNC_MALLOC controlled_malloc
#define NC_SYNC_CALLOC controlled_calloc
#define NEVERC_SYNC_INIT_SHOULD_FAIL() sync_init_should_fail()
#define NEVERC_SYNC_LOCK_SHOULD_FAIL() sync_lock_should_fail()
#if !defined(_WIN32)
#define NCI_SYNC_MUTEXATTR_INIT controlled_mutexattr_init
#define NCI_SYNC_MUTEXATTR_SETTYPE controlled_mutexattr_settype
#define NCI_SYNC_MUTEX_INIT controlled_mutex_init
#endif
#include "../../../std/src/sync/sync.c"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",               \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int once_runs;

static void oom_once_func(void) {
    once_runs++;
}

static int range_calls;

static int count_range_call(const char *key, void *value, void *user) {
    (void)key;
    (void)value;
    (void)user;
    range_calls++;
    return 1;
}

int main(void) {
    fail_sync_init = 1;
    CHECK(neverc_sync_map_new() == NULL);
    CHECK(neverc_sync_pool_new(NULL) == NULL);
    neverc_mutex_t failed_mu;
    CHECK(neverc_mutex_init(&failed_mu) == -1);
    neverc_rwmutex_t failed_rw;
    CHECK(neverc_rwmutex_init(&failed_rw) == -1);
    neverc_waitgroup_t failed_wg;
    CHECK(neverc_waitgroup_init(&failed_wg) == -1);
    neverc_once_t failed_once;
    CHECK(neverc_once_init(&failed_once) == -1);
    neverc_cond_t failed_cond;
    neverc_mutex_t cond_mu;
    fail_sync_init = 0;
    CHECK(neverc_mutex_init(&cond_mu) == 0);
    fail_sync_init = 1;
    CHECK(neverc_cond_init(&failed_cond, &cond_mu) == -1);
    fail_sync_init = 0;
    neverc_mutex_destroy(&cond_mu);

#if !defined(_WIN32)
    /* The documented wrong-owner no-op requires ERRORCHECK semantics. Never
     * silently fall back to a default mutex if any attribute step fails. */
    neverc_mutex_t attr_mu;
    fail_mutexattr_init = 1;
    CHECK(neverc_mutex_init(&attr_mu) == -1);
    fail_mutexattr_init = 0;
    fail_mutexattr_settype = 1;
    CHECK(neverc_mutex_init(&attr_mu) == -1);
    fail_mutexattr_settype = 0;
    fail_mutex_init_remaining = 1;
    CHECK(neverc_mutex_init(&attr_mu) == -1);
    CHECK(fail_mutex_init_remaining == 0);
    CHECK(neverc_mutex_init(&attr_mu) == 0);
    neverc_mutex_destroy(&attr_mu);
#endif

    neverc_once_t once;
    CHECK(neverc_once_init(&once) == 0);
    once_runs = 0;
    fail_sync_lock_remaining = 2;
    neverc_once_do(&once, oom_once_func);
    CHECK(once_runs == 1);
    CHECK(once.done == 1);
    neverc_once_do(&once, oom_once_func);
    CHECK(once_runs == 1);
    neverc_once_destroy(&once);

    neverc_waitgroup_t wg;
    CHECK(neverc_waitgroup_init(&wg) == 0);
    fail_sync_lock_remaining = 2;
    neverc_waitgroup_wait(&wg);
    neverc_waitgroup_destroy(&wg);

    /* A failed lock must not drop Add: Wait would then return while the
     * matching Done never ran (fail-open). */
    CHECK(neverc_waitgroup_init(&wg) == 0);
    fail_sync_lock_remaining = 3;
    neverc_waitgroup_add(&wg, 1);
    CHECK(wg.counter == 1);
    fail_sync_lock_remaining = 2;
    CHECK(neverc_waitgroup_add_checked(&wg, 1) == 0);
    CHECK(wg.counter == 2);
    fail_sync_lock_remaining = 0;
    CHECK(neverc_waitgroup_done_checked(&wg) == 0);
    CHECK(neverc_waitgroup_done_checked(&wg) == 0);
    neverc_waitgroup_wait(&wg);
    neverc_waitgroup_destroy(&wg);

    neverc_sync_map_t *map = neverc_sync_map_new();
    CHECK(map != NULL);

    int original = 1;
    int replacement = 2;
    CHECK(neverc_sync_map_store(map, "existing", &original) == 0);

    /* Fill the initial 16-bucket table to its 0.75 load threshold. */
    for (int i = 0; i < 11; i++) {
        char key[32];
        snprintf(key, sizeof(key), "fill-%d", i);
        CHECK(neverc_sync_map_store(map, key, &original) == 0);
    }

    fail_bucket_allocation = 1;

    range_calls = 0;
    neverc_sync_map_range(map, count_range_call, NULL);
    CHECK(range_calls == 0);

    /* Existing keys do not require growth and must remain usable when a
     * speculative grow would fail. */
    CHECK(neverc_sync_map_store(map, "existing", &replacement) == 0);
    int ok = 0;
    CHECK(neverc_sync_map_load(map, "existing", &ok) == &replacement);
    CHECK(ok == 1);

    int loaded = 0;
    CHECK(neverc_sync_map_load_or_store(
              map, "existing", &original, &loaded) == &replacement);
    CHECK(loaded == 1);

    loaded = 0;
    CHECK(neverc_sync_map_swap(
              map, "existing", &original, &loaded) == &replacement);
    CHECK(loaded == 1);

    /* New keys still fail atomically if the required bucket grow fails. */
    CHECK(neverc_sync_map_store(map, "grow-store-oom", &original) == -1);
    CHECK(neverc_sync_map_load(map, "grow-store-oom", &ok) == NULL);
    CHECK(ok == 0);

    loaded = 1;
    CHECK(neverc_sync_map_load_or_store(
              map, "grow-load-or-store-oom", &original, &loaded) == NULL);
    CHECK(loaded == -1);

    loaded = 1;
    CHECK(neverc_sync_map_swap(
              map, "grow-swap-oom", &original, &loaded) == NULL);
    CHECK(loaded == -1);

    fail_bucket_allocation = 0;
    fail_key_allocation = 1;

    range_calls = 0;
    neverc_sync_map_range(map, count_range_call, NULL);
    CHECK(range_calls == 0);

    /* Replacing an existing value does not allocate a key. */
    CHECK(neverc_sync_map_store(map, "existing", &replacement) == 0);
    CHECK(neverc_sync_map_load(map, "existing", &ok) == &replacement);
    CHECK(ok == 1);

    /* Failed key copies must leave the map unchanged instead of dereferencing
     * NULL or publishing a partially initialized entry. */
    CHECK(neverc_sync_map_store(map, "store-oom", &original) == -1);
    CHECK(neverc_sync_map_load(map, "store-oom", &ok) == NULL);
    CHECK(ok == 0);

    loaded = 1;
    CHECK(neverc_sync_map_load_or_store(
              map, "load-or-store-oom", &original, &loaded) == NULL);
    CHECK(loaded == -1);
    CHECK(neverc_sync_map_load(map, "load-or-store-oom", &ok) == NULL);
    CHECK(ok == 0);

    loaded = 1;
    CHECK(neverc_sync_map_swap(
              map, "swap-oom", &original, &loaded) == NULL);
    CHECK(loaded == -1);
    CHECK(neverc_sync_map_load(map, "swap-oom", &ok) == NULL);
    CHECK(ok == 0);

    fail_key_allocation = 0;
    range_calls = 0;
    neverc_sync_map_range(map, count_range_call, NULL);
    CHECK(range_calls == 12);

    neverc_sync_map_free(map);
    puts("passed");
    return 0;
}
