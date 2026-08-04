#include <stdio.h>
#include <stdlib.h>

static void *failing_malloc(size_t size) {
    (void)size;
    return NULL;
}

static void *failing_calloc(size_t count, size_t size) {
    (void)count;
    (void)size;
    return NULL;
}

static void *failing_realloc(void *ptr, size_t size) {
    (void)ptr;
    (void)size;
    return NULL;
}

#define malloc failing_malloc
#define calloc failing_calloc
#define realloc failing_realloc
#include "../../../std/src/container/vector/vector.c"
#undef malloc
#undef calloc
#undef realloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

typedef struct {
    int value;
    unsigned char padding[300];
} large_value_t;

typedef struct {
    int key;
    int order;
    unsigned char padding[300];
} stable_value_t;

static bool is_even(const void *element) {
    return ((const large_value_t *)element)->value % 2 == 0;
}

static int compare_values(const void *left, const void *right) {
    int a = ((const large_value_t *)left)->value;
    int b = ((const large_value_t *)right)->value;
    return (a > b) - (a < b);
}

static int compare_stable_values(const void *left, const void *right) {
    int a = ((const stable_value_t *)left)->key;
    int b = ((const stable_value_t *)right)->key;
    return (a > b) - (a < b);
}

int main(void) {
    large_value_t values[4] = {{2, {0}}, {1, {0}}, {4, {0}}, {3, {0}}};
    neverc_vector_t vector = {
        values, 4, 4, sizeof(values[0])
    };

    CHECK(neverc_vector_stable_partition(&vector, is_even) == 2);
    CHECK(values[0].value == 2);
    CHECK(values[1].value == 4);
    CHECK(values[2].value == 1);
    CHECK(values[3].value == 3);

    large_value_t merge_values[4] = {
        {1, {0}}, {3, {0}}, {2, {0}}, {4, {0}}
    };
    neverc_vector_t merge_vector = {
        merge_values, 4, 4, sizeof(merge_values[0])
    };
    neverc_vector_inplace_merge(&merge_vector, 2, compare_values);
    CHECK(merge_values[0].value == 1);
    CHECK(merge_values[1].value == 2);
    CHECK(merge_values[2].value == 3);
    CHECK(merge_values[3].value == 4);

    large_value_t rotate_values[4] = {
        {1, {0}}, {2, {0}}, {3, {0}}, {4, {0}}
    };
    neverc_vector_t rotate_vector = {
        rotate_values, 4, 4, sizeof(rotate_values[0])
    };
    neverc_vector_rotate(&rotate_vector, 2);
    CHECK(rotate_values[0].value == 3);
    CHECK(rotate_values[1].value == 4);
    CHECK(rotate_values[2].value == 1);
    CHECK(rotate_values[3].value == 2);

    int reference_values[4] = {1, 2, 3, 4};
    neverc_vector_t reference_vector = {
        reference_values, 4, 4, sizeof(reference_values[0])
    };
    neverc_vector_shuffle(&reference_vector, 12345);
    CHECK(reference_values[0] != 1 || reference_values[1] != 2 ||
          reference_values[2] != 3 || reference_values[3] != 4);

    large_value_t shuffle_values[4] = {
        {1, {0}}, {2, {0}}, {3, {0}}, {4, {0}}
    };
    neverc_vector_t shuffle_vector = {
        shuffle_values, 4, 4, sizeof(shuffle_values[0])
    };
    neverc_vector_shuffle(&shuffle_vector, 12345);
    for (size_t i = 0; i < 4; i++)
        CHECK(shuffle_values[i].value == reference_values[i]);

    large_value_t partition_values[4] = {
        {1, {0}}, {2, {0}}, {3, {0}}, {4, {0}}
    };
    neverc_vector_t partition_vector = {
        partition_values, 4, 4, sizeof(partition_values[0])
    };
    size_t partition = neverc_vector_partition(&partition_vector, is_even);
    CHECK(partition == 2);
    CHECK(is_even(&partition_values[0]));
    CHECK(is_even(&partition_values[1]));
    CHECK(!is_even(&partition_values[2]));
    CHECK(!is_even(&partition_values[3]));

    large_value_t reverse_values[3] = {
        {1, {0}}, {2, {0}}, {3, {0}}
    };
    neverc_vector_t reverse_vector = {
        reverse_values, 3, 3, sizeof(reverse_values[0])
    };
    neverc_vector_reverse(&reverse_vector);
    CHECK(reverse_values[0].value == 3);
    CHECK(reverse_values[1].value == 2);
    CHECK(reverse_values[2].value == 1);

    large_value_t swap_values[2] = {{1, {0}}, {2, {0}}};
    neverc_vector_t swap_vector = {
        swap_values, 2, 2, sizeof(swap_values[0])
    };
    neverc_vector_swap_elements(&swap_vector, 0, 1);
    CHECK(swap_values[0].value == 2);
    CHECK(swap_values[1].value == 1);

    large_value_t sort_values[4] = {
        {4, {0}}, {1, {0}}, {3, {0}}, {2, {0}}
    };
    neverc_vector_t sort_vector = {
        sort_values, 4, 4, sizeof(sort_values[0])
    };
    neverc_vector_sort(&sort_vector, compare_values);
    for (size_t i = 0; i < 4; i++)
        CHECK(sort_values[i].value == (int)i + 1);

    large_value_t nth_values[4] = {
        {4, {0}}, {3, {0}}, {2, {0}}, {1, {0}}
    };
    neverc_vector_t nth_vector = {
        nth_values, 4, 4, sizeof(nth_values[0])
    };
    neverc_vector_nth_element(&nth_vector, 2, compare_values);
    CHECK(nth_values[2].value == 3);
    CHECK(nth_values[0].value <= nth_values[2].value);
    CHECK(nth_values[1].value <= nth_values[2].value);
    CHECK(nth_values[3].value >= nth_values[2].value);

    large_value_t partial_values[5] = {
        {5, {0}}, {4, {0}}, {3, {0}}, {2, {0}}, {1, {0}}
    };
    neverc_vector_t partial_vector = {
        partial_values, 5, 5, sizeof(partial_values[0])
    };
    neverc_vector_partial_sort(&partial_vector, 3, compare_values);
    CHECK(partial_values[0].value == 1);
    CHECK(partial_values[1].value == 2);
    CHECK(partial_values[2].value == 3);

    stable_value_t stable_values[4] = {
        {2, 0, {0}}, {1, 1, {0}}, {2, 2, {0}}, {1, 3, {0}}
    };
    neverc_vector_t stable_vector = {
        stable_values, 4, 4, sizeof(stable_values[0])
    };
    neverc_vector_stable_sort(&stable_vector, compare_stable_values);
    CHECK(stable_values[0].key == 1 && stable_values[0].order == 1);
    CHECK(stable_values[1].key == 1 && stable_values[1].order == 3);
    CHECK(stable_values[2].key == 2 && stable_values[2].order == 0);
    CHECK(stable_values[3].key == 2 && stable_values[3].order == 2);

    stable_value_t large_stable_values[40];
    for (size_t i = 0; i < 40; i++) {
        large_stable_values[i].key = (int)((i * 17) % 5);
        large_stable_values[i].order = (int)i;
    }
    neverc_vector_t large_stable_vector = {
        large_stable_values, 40, 40, sizeof(large_stable_values[0])
    };
    neverc_vector_stable_sort(&large_stable_vector,
                              compare_stable_values);
    for (size_t i = 1; i < 40; i++) {
        CHECK(large_stable_values[i - 1].key <= large_stable_values[i].key);
        if (large_stable_values[i - 1].key == large_stable_values[i].key)
            CHECK(large_stable_values[i - 1].order <
                  large_stable_values[i].order);
    }

    for (size_t count = 0; count <= 64; count++) {
        large_value_t fuzz_values[64];
        stable_value_t stable_fuzz_values[64];
        for (size_t i = 0; i < count; i++) {
            int key = (int)((i * 37 + count * 13) % 23);
            fuzz_values[i].value = key;
            stable_fuzz_values[i].key = key % 7;
            stable_fuzz_values[i].order = (int)i;
        }
        neverc_vector_t fuzz_vector = {
            fuzz_values, count, count, sizeof(fuzz_values[0])
        };
        neverc_vector_t stable_fuzz_vector = {
            stable_fuzz_values, count, count,
            sizeof(stable_fuzz_values[0])
        };
        neverc_vector_sort(&fuzz_vector, compare_values);
        neverc_vector_stable_sort(&stable_fuzz_vector,
                                  compare_stable_values);
        for (size_t i = 1; i < count; i++) {
            CHECK(fuzz_values[i - 1].value <= fuzz_values[i].value);
            CHECK(stable_fuzz_values[i - 1].key <=
                  stable_fuzz_values[i].key);
            if (stable_fuzz_values[i - 1].key == stable_fuzz_values[i].key)
                CHECK(stable_fuzz_values[i - 1].order <
                      stable_fuzz_values[i].order);
        }
    }

    puts("passed");
    return 0;
}
