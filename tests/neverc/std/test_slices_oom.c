#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *failing_malloc(size_t size) {
    (void)size;
    return NULL;
}

static void *failing_realloc(void *ptr, size_t size) {
    (void)ptr;
    (void)size;
    return NULL;
}

#define malloc failing_malloc
#define realloc failing_realloc
#include "../../../std/src/slices/slices.c"
#undef realloc
#undef malloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
    int insert_values[8] = {1, 2, 3, 4};
    int insert_original[] = {1, 2, 3, 4};
    size_t len = neverc_slices_insert(insert_values, 4, sizeof(int), 1,
                                      &insert_values[3], 1);
    CHECK(len == 4);
    CHECK(memcmp(insert_values, insert_original,
                 sizeof(insert_original)) == 0);

    int replace_values[10] = {1, 2, 3, 4, 5};
    int replace_original[] = {1, 2, 3, 4, 5};
    len = neverc_slices_replace(replace_values, 5, sizeof(int), 1, 2,
                                &replace_values[3], 2);
    CHECK(len == 5);
    CHECK(memcmp(replace_values, replace_original,
                 sizeof(replace_original)) == 0);

    int grow_values[4] = {1, 2, 3, 4};
    size_t grow_cap = 0;
    CHECK(neverc_slices_grow(grow_values, 4, 4, 8, sizeof(int),
                             &grow_cap) == NULL);
    CHECK(grow_cap == 0);
    CHECK(grow_values[0] == 1);
    puts("passed");
    return 0;
}
