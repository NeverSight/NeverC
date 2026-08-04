#include "neverc/std/weak.h"

#include <stdio.h>

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

int main(void) {
    int value = 42;
    neverc_weak_strong_t strong = neverc_weak_new(&value, sizeof(value));
    CHECK(strong.ptr != NULL);

    neverc_weak_ref_t *weak = neverc_weak_make(strong);
    CHECK(weak != NULL);
    neverc_weak_ref_t *retained = neverc_weak_ref_retain(weak);
    CHECK(retained == weak);
    CHECK(neverc_weak_ref_count(retained) == 3);

    neverc_weak_ref_release(weak);
    CHECK(neverc_weak_value(retained) != NULL);
    CHECK(*(int *)neverc_weak_value(retained) == value);
    CHECK(neverc_weak_ref_count(retained) == 2);

    neverc_weak_ref_release(retained);
    neverc_weak_strong_release(&strong);
    puts("passed");
    return 0;
}
