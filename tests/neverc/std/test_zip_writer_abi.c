#include "neverc/std/archive/zip.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    uint32_t *offsets;
    neverc_zip_file_header_t *entries;
    int nentries;
    int entries_cap;
} v3389_zip_writer_t;

_Static_assert(sizeof(neverc_zip_writer_t) == sizeof(v3389_zip_writer_t),
               "v3389 zip writer size ABI");
_Static_assert(_Alignof(neverc_zip_writer_t) ==
                   _Alignof(v3389_zip_writer_t),
               "v3389 zip writer alignment ABI");
#define ZIP_FIELD_ABI(field)                                               \
    _Static_assert(offsetof(neverc_zip_writer_t, field) ==                 \
                       offsetof(v3389_zip_writer_t, field),                \
                   "v3389 zip writer field ABI")
ZIP_FIELD_ABI(data);
ZIP_FIELD_ABI(len);
ZIP_FIELD_ABI(cap);
ZIP_FIELD_ABI(offsets);
ZIP_FIELD_ABI(entries);
ZIP_FIELD_ABI(nentries);
ZIP_FIELD_ABI(entries_cap);

enum { CANARY_SIZE = 32 };

typedef struct {
    union {
        neverc_zip_writer_t current;
        v3389_zip_writer_t legacy;
    } writer;
    uint8_t canary[CANARY_SIZE];
} guarded_zip_writer_t;

static int canary_ok(const uint8_t canary[CANARY_SIZE]) {
    for (size_t i = 0; i < CANARY_SIZE; i++)
        if (canary[i] != 0xa5) return 0;
    return 1;
}

int main(void) {
    guarded_zip_writer_t guarded;
    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.canary, 0xa5, sizeof(guarded.canary));

    neverc_zip_writer_init(&guarded.writer.current);
    uint8_t payload[8192];
    memset(payload, 0x5a, sizeof(payload));
    int status = neverc_zip_writer_add(
        &guarded.writer.current, "large.bin", payload, sizeof(payload));
    if (status == 0)
        status = neverc_zip_writer_close(&guarded.writer.current);
    size_t closed_length = guarded.writer.current.len;
    if (status == 0 &&
        neverc_zip_writer_close(&guarded.writer.current) != 0)
        status = -1;
    if (status == 0 && guarded.writer.current.len != closed_length)
        status = -1;
    if (status == 0 && neverc_zip_writer_add(
            &guarded.writer.current, "late", payload, 1) != -1)
        status = -1;
    if (!canary_ok(guarded.canary)) status = -1;

    neverc_zip_writer_free(&guarded.writer.current);
    if (!canary_ok(guarded.canary)) status = -1;
    if (status != 0) {
        fputs("released zip writer ABI canary failed\n", stderr);
        return 1;
    }
    puts("passed");
    return 0;
}
