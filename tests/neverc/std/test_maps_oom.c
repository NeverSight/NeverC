#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t allocation_count;
static size_t fail_at;

static int allocation_fails(void) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at;
}

static void *controlled_malloc(size_t size) {
    return allocation_fails() ? NULL : malloc(size);
}

static void *controlled_calloc(size_t count, size_t size) {
    return allocation_fails() ? NULL : calloc(count, size);
}

#define malloc controlled_malloc
#define calloc controlled_calloc
#include "../../../std/src/maps/maps.c"
#undef malloc
#undef calloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",             \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void reset_allocator(size_t failure) {
    allocation_count = 0;
    fail_at = failure;
}

static size_t callback_count;

static void count_entry(const char *key, void *value, void *user_data) {
    (void)key;
    (void)value;
    (void)user_data;
    callback_count++;
}

static int count_and_delete(const char *key, void *value) {
    (void)key;
    (void)value;
    callback_count++;
    return 1;
}

int main(void) {
    for (size_t failure = 1; failure <= 3; failure++) {
        reset_allocator(failure);
        CHECK(neverc_maps_new() == NULL);
    }

    reset_allocator(0);
    neverc_map_t *m = neverc_maps_new();
    CHECK(m != NULL);
    static int values[32];

    reset_allocator(1);
    CHECK(neverc_maps_set(m, "missing", &values[0]) == -1);
    CHECK(neverc_maps_len(m) == 0);
    CHECK(!neverc_maps_has(m, "missing"));

    reset_allocator(0);
    CHECK(neverc_maps_set(m, "present", &values[0]) == 0);

    size_t count = 99;
    reset_allocator(1);
    CHECK(neverc_maps_keys(m, &count) == NULL);
    CHECK(count == 0);
    reset_allocator(1);
    count = 99;
    CHECK(neverc_maps_values(m, &count) == NULL);
    CHECK(count == 0);

    for (size_t failure = 1; failure <= 2; failure++) {
        callback_count = 0;
        reset_allocator(failure);
        neverc_maps_foreach(m, count_entry, NULL);
        CHECK(callback_count == 0);
        CHECK(neverc_maps_has(m, "present"));

        callback_count = 0;
        reset_allocator(failure);
        neverc_maps_delete_func(m, count_and_delete);
        CHECK(callback_count == 0);
        CHECK(neverc_maps_has(m, "present"));
    }

    reset_allocator(0);
    CHECK(neverc_maps_set(m, "second", &values[1]) == 0);
    for (size_t failure = 1; failure <= 5; failure++) {
        reset_allocator(failure);
        CHECK(neverc_maps_clone(m) == NULL);
        CHECK(neverc_maps_len(m) == 2);
        CHECK(neverc_maps_has(m, "present"));
        CHECK(neverc_maps_has(m, "second"));
    }

    reset_allocator(0);
    for (int i = 2; i < 14; i++) {
        char key[24];
        snprintf(key, sizeof(key), "key_%d", i);
        CHECK(neverc_maps_set(m, key, &values[i]) == 0);
    }
    CHECK(neverc_maps_len(m) == 14);
    for (size_t failure = 1; failure <= 3; failure++) {
        reset_allocator(failure);
        CHECK(neverc_maps_set(m, "growth", &values[14]) == -1);
        CHECK(neverc_maps_len(m) == 14);
        CHECK(!neverc_maps_has(m, "growth"));
        CHECK(neverc_maps_has(m, "present"));
    }

    reset_allocator(0);
    neverc_map_t *copy_dst = neverc_maps_new();
    CHECK(copy_dst != NULL);
    reset_allocator(1);
    CHECK(neverc_maps_copy(copy_dst, m) == -1);
    CHECK(neverc_maps_len(copy_dst) == 0);
    CHECK(!neverc_maps_has(copy_dst, "present"));
    CHECK(neverc_maps_len(m) == 14);

    reset_allocator(0);
    neverc_maps_free(copy_dst);
    neverc_maps_free(m);
    puts("passed");
    return 0;
}
