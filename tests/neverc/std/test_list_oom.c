#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static size_t allocation_count;
static size_t fail_at;

static void *controlled_malloc(size_t size) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at ? NULL : malloc(size);
}

#define malloc controlled_malloc
#include "../../../std/src/container/list/list.c"
#undef malloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void reset_allocator(size_t failure) {
    allocation_count = 0;
    fail_at = failure;
}

int main(void) {
    reset_allocator(1);
    CHECK(neverc_list_new() == NULL);

    neverc_list_t list = {0};
    reset_allocator(1);
    CHECK(neverc_list_push_front(&list, (void *)(intptr_t)1) == NULL);
    CHECK(neverc_list_len(&list) == 0);
    CHECK(neverc_list_front(&list) == NULL);

    reset_allocator(0);
    neverc_list_element_t *mark =
        neverc_list_push_back(&list, (void *)(intptr_t)2);
    CHECK(mark != NULL);
    CHECK(neverc_list_len(&list) == 1);

    reset_allocator(1);
    CHECK(neverc_list_push_back(&list, (void *)(intptr_t)3) == NULL);
    reset_allocator(1);
    CHECK(neverc_list_insert_before(
              &list, (void *)(intptr_t)1, mark) == NULL);
    reset_allocator(1);
    CHECK(neverc_list_insert_after(
              &list, (void *)(intptr_t)3, mark) == NULL);
    CHECK(neverc_list_len(&list) == 1);
    CHECK(neverc_list_front(&list) == mark);
    CHECK(neverc_list_back(&list) == mark);
    CHECK(neverc_list_element_next(mark) == NULL);
    CHECK(neverc_list_element_prev(mark) == NULL);

    neverc_list_t other = {0};
    reset_allocator(0);
    CHECK(neverc_list_push_back(&other, (void *)(intptr_t)10) != NULL);
    CHECK(neverc_list_push_back(&other, (void *)(intptr_t)11) != NULL);
    CHECK(neverc_list_len(&other) == 2);

    reset_allocator(1);
    CHECK(neverc_list_push_back_list(&list, &other) == -1);
    CHECK(neverc_list_len(&list) == 1);
    CHECK(neverc_list_front(&list) == mark);
    CHECK(neverc_list_back(&list) == mark);

    reset_allocator(2);
    CHECK(neverc_list_push_back_list(&list, &other) == -1);
    CHECK(neverc_list_len(&list) == 1);
    CHECK(neverc_list_front(&list) == mark);
    CHECK(neverc_list_back(&list) == mark);

    reset_allocator(1);
    CHECK(neverc_list_push_front_list(&list, &other) == -1);
    CHECK(neverc_list_len(&list) == 1);
    CHECK(neverc_list_front(&list) == mark);
    CHECK(neverc_list_back(&list) == mark);

    reset_allocator(2);
    CHECK(neverc_list_push_front_list(&list, &other) == -1);
    CHECK(neverc_list_len(&list) == 1);
    CHECK(neverc_list_front(&list) == mark);
    CHECK(neverc_list_back(&list) == mark);

    fail_at = 0;
    neverc_list_free(&other);
    neverc_list_free(&list);
    puts("passed");
    return 0;
}
