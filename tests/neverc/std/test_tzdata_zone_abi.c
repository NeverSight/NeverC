#include "neverc/std/time_tzdata.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    const char *name;
    const char *abbrev;
    const char *abbrev_dst;
    int utc_offset;
    int dst_offset;
    int has_dst;
} v3389_tzdata_zone_t;

_Static_assert(sizeof(neverc_tzdata_zone_t) == sizeof(v3389_tzdata_zone_t),
               "v3389 tzdata zone size");
_Static_assert(_Alignof(neverc_tzdata_zone_t) ==
                   _Alignof(v3389_tzdata_zone_t),
               "v3389 tzdata zone alignment");

#define ABI_FIELD(field)                                                     \
    _Static_assert(offsetof(neverc_tzdata_zone_t, field) ==                  \
                       offsetof(v3389_tzdata_zone_t, field),                 \
                   "v3389 tzdata zone field offset")
ABI_FIELD(name);
ABI_FIELD(abbrev);
ABI_FIELD(abbrev_dst);
ABI_FIELD(utc_offset);
ABI_FIELD(dst_offset);
ABI_FIELD(has_dst);
#undef ABI_FIELD

static int bytes_are(const uint8_t *bytes, size_t size, uint8_t value) {
    for (size_t i = 0; i < size; i++)
        if (bytes[i] != value) return 0;
    return 1;
}

int main(void) {
    struct {
        uint8_t before[32];
        union {
            neverc_tzdata_zone_t current;
            v3389_tzdata_zone_t released;
        } zone;
        uint8_t after[32];
    } guarded;

    memset(&guarded, 0xa5, sizeof(guarded));
    guarded.zone.released.name = "Custom/Released";
    guarded.zone.released.abbrev = "STD";
    guarded.zone.released.abbrev_dst = "DST";
    guarded.zone.released.utc_offset = 3600;
    guarded.zone.released.dst_offset = 7200;
    guarded.zone.released.has_dst = 1;

    /* Bytes that were tail padding in v3389 remain 0xa5. They must not be
     * interpreted as a hidden hemisphere field by the current library. */
    if (neverc_tzdata_dst_hemisphere(&guarded.zone.current) != 0 ||
        neverc_tzdata_offset_for_month(&guarded.zone.current, 1) != 3600 ||
        neverc_tzdata_offset_for_month(&guarded.zone.current, 7) != 7200 ||
        !bytes_are(guarded.before, sizeof(guarded.before), 0xa5) ||
        !bytes_are(guarded.after, sizeof(guarded.after), 0xa5)) {
        fputs("released tzdata zone ABI regression failed\n", stderr);
        return 1;
    }

    /* Use caller-owned name storage so this contract cannot pass through
     * compiler string pooling or pointer identity by accident. */
    char sydney_name[] = "Australia/Sydney";
    guarded.zone.released.name = sydney_name;
    if (neverc_tzdata_dst_hemisphere(&guarded.zone.current) != 2 ||
        neverc_tzdata_offset_for_month(&guarded.zone.current, 1) != 7200 ||
        neverc_tzdata_offset_for_month(&guarded.zone.current, 7) != 3600) {
        fputs("tzdata private hemisphere lookup failed\n", stderr);
        return 1;
    }

    char new_york_name[] = "America/New_York";
    guarded.zone.released.name = new_york_name;
    if (neverc_tzdata_dst_hemisphere(&guarded.zone.current) != 1 ||
        neverc_tzdata_offset_for_month(&guarded.zone.current, 1) != 3600 ||
        neverc_tzdata_offset_for_month(&guarded.zone.current, 7) != 7200) {
        fputs("tzdata private northern lookup failed\n", stderr);
        return 1;
    }

    const neverc_tzdata_zone_t *builtin =
        neverc_tzdata_lookup("Australia/Sydney");
    if (!builtin) {
        fputs("tzdata builtin Sydney lookup failed\n", stderr);
        return 1;
    }
    neverc_tzdata_zone_t copied = *builtin;
    if (neverc_tzdata_dst_hemisphere(&copied) != 2 ||
        neverc_tzdata_offset_for_month(&copied, 1) != copied.dst_offset ||
        neverc_tzdata_offset_for_month(&copied, 7) != copied.utc_offset) {
        fputs("tzdata copied zone metadata lookup failed\n", stderr);
        return 1;
    }

    guarded.zone.released.name = NULL;
    if (neverc_tzdata_dst_hemisphere(&guarded.zone.current) != 0 ||
        !bytes_are(guarded.before, sizeof(guarded.before), 0xa5) ||
        !bytes_are(guarded.after, sizeof(guarded.after), 0xa5)) {
        fputs("tzdata unnamed zone metadata lookup failed\n", stderr);
        return 1;
    }

    puts("passed");
    return 0;
}
