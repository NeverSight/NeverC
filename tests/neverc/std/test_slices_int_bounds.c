#include <stdint.h>
#include <stdio.h>

/* Include the implementation so the byte-span predicate can be exercised
 * without constructing an impossible object or handing a tiny allocation a
 * deliberately false length. */
#include "../../../std/src/slices/slices.c"

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
    size_t max_int_len = SIZE_MAX / sizeof(int);
    size_t ilp32_max_int_len = (size_t)UINT32_MAX / sizeof(int);

    CHECK(sizeof(int) > 1);
    CHECK(slices_int_span_valid(max_int_len));
    CHECK(!slices_int_span_valid(max_int_len + 1));

    /* Simulate the ILP32 size_t limit even when this test runs on LP64. */
    CHECK(slices_int_span_valid_for_limit(ilp32_max_int_len,
                                          (size_t)UINT32_MAX));
    CHECK(!slices_int_span_valid_for_limit(ilp32_max_int_len + 1,
                                           (size_t)UINT32_MAX));
    if (sizeof(int) == 4)
        CHECK(ilp32_max_int_len + 1 <= (size_t)INT_MAX);

    puts("passed");
    return 0;
}
