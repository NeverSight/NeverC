#include "neverc/std/hash/maphash.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t seed;
    uint64_t state;
    uint8_t buf[128];
    int n;
} v3389_maphash_t;

_Static_assert(NEVERC_MAPHASH_BUF_SIZE == 128,
               "v3389.1.4 maphash buffer size changed");

#define ABI_FIELD_EQ(field)                                                \
    _Static_assert(offsetof(neverc_maphash_t, field) ==                    \
                       offsetof(v3389_maphash_t, field),                   \
                   "v3389.1.4 maphash field offset changed")

_Static_assert(sizeof(neverc_maphash_t) == sizeof(v3389_maphash_t),
               "v3389.1.4 maphash size changed");
_Static_assert(_Alignof(neverc_maphash_t) == _Alignof(v3389_maphash_t),
               "v3389.1.4 maphash alignment changed");
ABI_FIELD_EQ(seed);
ABI_FIELD_EQ(state);
ABI_FIELD_EQ(buf);
ABI_FIELD_EQ(n);

#undef ABI_FIELD_EQ

typedef struct {
    uint8_t before[32];
    neverc_maphash_t hash;
    uint8_t after[32];
} guarded_hash_t;

static int canary_ok(const uint8_t *bytes, size_t length) {
    for (size_t i = 0; i < length; i++)
        if (bytes[i] != 0xa5U) return 0;
    return 1;
}

int main(void) {
    guarded_hash_t guarded;
    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.before, 0xa5, sizeof(guarded.before));
    memset(guarded.after, 0xa5, sizeof(guarded.after));

    neverc_maphash_init(&guarded.hash, UINT64_C(42));
    uint64_t empty = neverc_maphash_sum64(&guarded.hash);
    if (empty != neverc_maphash_bytes(UINT64_C(42), NULL, 0)) return 1;

    uint8_t input[129];
    memset(input, 0x3c, sizeof(input));
    if (neverc_maphash_write(&guarded.hash, input, 1) != 1) return 1;
    if (neverc_maphash_write(&guarded.hash, input + 1, 127) != 127) return 1;
    if (neverc_maphash_sum64(&guarded.hash) !=
        neverc_maphash_bytes(UINT64_C(42), input, 128)) return 1;
    if (neverc_maphash_write_byte(&guarded.hash, input[128]) != 1) return 1;
    if (neverc_maphash_sum64(&guarded.hash) !=
        neverc_maphash_bytes(UINT64_C(42), input, sizeof(input))) return 1;

    neverc_maphash_reset(&guarded.hash);
    if (neverc_maphash_sum64(&guarded.hash) != empty) return 1;

    guarded.hash.n = -2;
    if (neverc_maphash_write(&guarded.hash, input, 1) != 0) return 1;
    if (neverc_maphash_write_byte(&guarded.hash, input[0]) != 0) return 1;
    if (neverc_maphash_sum64(&guarded.hash) != 0) return 1;
    guarded.hash.n = 129;
    if (neverc_maphash_write(&guarded.hash, input, 1) != 0) return 1;
    if (neverc_maphash_write_byte(&guarded.hash, input[0]) != 0) return 1;
    if (neverc_maphash_sum64(&guarded.hash) != 0) return 1;

    if (!canary_ok(guarded.before, sizeof(guarded.before)) ||
        !canary_ok(guarded.after, sizeof(guarded.after))) return 1;

    puts("passed");
    return 0;
}
