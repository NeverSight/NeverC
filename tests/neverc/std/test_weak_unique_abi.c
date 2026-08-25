#include "neverc/std/unique.h"
#include "neverc/std/weak.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    void *ptr;
    void *_ctrl;
} v3389_weak_strong_t;

typedef struct {
    const void *ptr;
} v3389_unique_handle_t;

#define ABI_TYPE_EQ(current, legacy)                                      \
    _Static_assert(sizeof(current) == sizeof(legacy), "v3389 size ABI");  \
    _Static_assert(_Alignof(current) == _Alignof(legacy),                 \
                   "v3389 alignment ABI")
#define ABI_FIELD_EQ(current, legacy, field)                              \
    _Static_assert(offsetof(current, field) == offsetof(legacy, field),   \
                   "v3389 field offset ABI")

ABI_TYPE_EQ(neverc_weak_strong_t, v3389_weak_strong_t);
ABI_FIELD_EQ(neverc_weak_strong_t, v3389_weak_strong_t, ptr);
ABI_FIELD_EQ(neverc_weak_strong_t, v3389_weak_strong_t, _ctrl);
ABI_TYPE_EQ(neverc_unique_handle_t, v3389_unique_handle_t);
ABI_FIELD_EQ(neverc_unique_handle_t, v3389_unique_handle_t, ptr);

static int payload_frees;

static void tracked_free(void *p) {
    payload_frees++;
    free(p);
}

static int weak_abi_and_generation(void) {
    int *first = (int *)malloc(sizeof(*first));
    int *second = (int *)malloc(sizeof(*second));
    if (!first || !second) {
        free(first);
        free(second);
        return -1;
    }
    *first = 1;
    *second = 2;

    neverc_weak_strong_t live =
        neverc_weak_new_with_free(first, tracked_free);
    if (!live.ptr || !live._ctrl) {
        free(first);
        free(second);
        return -1;
    }
    neverc_weak_strong_t stale = live;
    neverc_weak_strong_release(&live);
    if (payload_frees != 1 || live.ptr || live._ctrl)
        return -1;

    neverc_weak_strong_t replacement =
        neverc_weak_new_with_free(second, tracked_free);
    if (!replacement.ptr || !replacement._ctrl) {
        free(second);
        return -1;
    }

    /* The implementation may recycle its control-block allocation, but a
     * released v3389-sized value must never release the replacement life. */
    neverc_weak_strong_release(&stale);
    if (payload_frees != 1 || neverc_weak_strong_count(replacement) != 1 ||
        *(int *)replacement.ptr != 2)
        return -1;

    neverc_weak_strong_release(&replacement);
    return payload_frees == 2 ? 0 : -1;
}

static int unique_abi_and_generation(void) {
    neverc_unique_destroy();
    neverc_unique_init();

    neverc_unique_handle_t old = neverc_unique_make_string("released-abi");
    const char *old_value = neverc_unique_string_value(old);
    if (!neverc_unique_handle_valid(old) || !old_value ||
        strcmp(old_value, "released-abi") != 0)
        return -1;

    neverc_unique_destroy();
    neverc_unique_init();

    neverc_unique_handle_t replacement =
        neverc_unique_make_string("released-abi");
    const char *replacement_value = neverc_unique_string_value(replacement);
    if (!neverc_unique_handle_valid(replacement) ||
        neverc_unique_handle_valid(old) ||
        neverc_unique_handle_equal(old, replacement) ||
        neverc_unique_string_value(old) != NULL ||
        !replacement_value ||
        strcmp(replacement_value, "released-abi") != 0)
        return -1;

    /* A public one-pointer value is forgeable; accessors must validate the
     * opaque token without dereferencing it. */
    uint64_t attacker_data = UINT64_C(0x1122334455667788);
    neverc_unique_handle_t forged = {&attacker_data};
    size_t len = 99;
    if (neverc_unique_handle_valid(forged) ||
        neverc_unique_string_value(forged) != NULL ||
        neverc_unique_int64_value(forged) != 0 ||
        neverc_unique_bytes_value(forged, &len) != NULL || len != 0)
        return -1;

    neverc_unique_destroy();
    return 0;
}

int main(void) {
    if (weak_abi_and_generation() != 0 ||
        unique_abi_and_generation() != 0) {
        fputs("released weak/unique ABI regression failed\n", stderr);
        return 1;
    }
    puts("passed");
    return 0;
}
