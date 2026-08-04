#include <stdio.h>
#include <stdlib.h>

static int fail_key_allocation;
static int fail_bucket_allocation;

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
#include "../../../std/src/sync/sync.c"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",               \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static int range_calls;

static int count_range_call(const char *key, void *value, void *user) {
    (void)key;
    (void)value;
    (void)user;
    range_calls++;
    return 1;
}

int main(void) {
    neverc_sync_map_t *map = neverc_sync_map_new();
    CHECK(map != NULL);

    int original = 1;
    int replacement = 2;
    neverc_sync_map_store(map, "existing", &original);

    /* Fill the initial 16-bucket table to its 0.75 load threshold. */
    for (int i = 0; i < 11; i++) {
        char key[32];
        snprintf(key, sizeof(key), "fill-%d", i);
        neverc_sync_map_store(map, key, &original);
    }

    fail_bucket_allocation = 1;

    range_calls = 0;
    neverc_sync_map_range(map, count_range_call, NULL);
    CHECK(range_calls == 0);

    /* Existing keys do not require growth and must remain usable when a
     * speculative grow would fail. */
    neverc_sync_map_store(map, "existing", &replacement);
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
    neverc_sync_map_store(map, "grow-store-oom", &original);
    CHECK(neverc_sync_map_load(map, "grow-store-oom", &ok) == NULL);
    CHECK(ok == 0);

    loaded = 1;
    CHECK(neverc_sync_map_load_or_store(
              map, "grow-load-or-store-oom", &original, &loaded) == NULL);
    CHECK(loaded == 0);

    loaded = 1;
    CHECK(neverc_sync_map_swap(
              map, "grow-swap-oom", &original, &loaded) == NULL);
    CHECK(loaded == 0);

    fail_bucket_allocation = 0;
    fail_key_allocation = 1;

    range_calls = 0;
    neverc_sync_map_range(map, count_range_call, NULL);
    CHECK(range_calls == 0);

    /* Replacing an existing value does not allocate a key. */
    neverc_sync_map_store(map, "existing", &replacement);
    CHECK(neverc_sync_map_load(map, "existing", &ok) == &replacement);
    CHECK(ok == 1);

    /* Failed key copies must leave the map unchanged instead of dereferencing
     * NULL or publishing a partially initialized entry. */
    neverc_sync_map_store(map, "store-oom", &original);
    CHECK(neverc_sync_map_load(map, "store-oom", &ok) == NULL);
    CHECK(ok == 0);

    loaded = 1;
    CHECK(neverc_sync_map_load_or_store(
              map, "load-or-store-oom", &original, &loaded) == NULL);
    CHECK(loaded == 0);
    CHECK(neverc_sync_map_load(map, "load-or-store-oom", &ok) == NULL);
    CHECK(ok == 0);

    loaded = 1;
    CHECK(neverc_sync_map_swap(
              map, "swap-oom", &original, &loaded) == NULL);
    CHECK(loaded == 0);
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
