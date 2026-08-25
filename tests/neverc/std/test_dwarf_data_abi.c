#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail_state_allocations;

static void *dwarf_test_calloc(size_t count, size_t size) {
    if (fail_state_allocations > 0) {
        fail_state_allocations--;
        return NULL;
    }
    return calloc(count, size);
}

#define NEVERC_DWARF_CALLOC dwarf_test_calloc
#include "../../../std/src/debug/dwarf/dwarf.c"

typedef struct {
    const uint8_t *debug_info;
    size_t debug_info_len;
    const uint8_t *debug_abbrev;
    size_t debug_abbrev_len;
    const uint8_t *debug_str;
    size_t debug_str_len;
    const uint8_t *debug_line_str;
    size_t debug_line_str_len;
    neverc_dwarf_abbrev_t *abbrevs;
    int abbrev_count;
} v3389_dwarf_data_t;

#define ABI_FIELD(field)                                                    \
    _Static_assert(offsetof(neverc_dwarf_data_t, field) ==                  \
                       offsetof(v3389_dwarf_data_t, field),                 \
                   "neverc_dwarf_data_t." #field " v3389 offset changed")

_Static_assert(sizeof(neverc_dwarf_data_t) == sizeof(v3389_dwarf_data_t),
               "neverc_dwarf_data_t v3389 size changed");
_Static_assert(_Alignof(neverc_dwarf_data_t) ==
                   _Alignof(v3389_dwarf_data_t),
               "neverc_dwarf_data_t v3389 alignment changed");
ABI_FIELD(debug_info);
ABI_FIELD(debug_info_len);
ABI_FIELD(debug_abbrev);
ABI_FIELD(debug_abbrev_len);
ABI_FIELD(debug_str);
ABI_FIELD(debug_str_len);
ABI_FIELD(debug_line_str);
ABI_FIELD(debug_line_str_len);
ABI_FIELD(abbrevs);
ABI_FIELD(abbrev_count);

#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(neverc_dwarf_data_t) == 80,
               "64-bit DWARF data ABI must remain 80 bytes");
#elif UINTPTR_MAX == UINT32_MAX
_Static_assert(sizeof(neverc_dwarf_data_t) == 40,
               "32-bit DWARF data ABI must remain 40 bytes");
#endif

#undef ABI_FIELD

static int failures;

#define CHECK(condition)                                                    \
    do {                                                                    \
        if (!(condition)) {                                                 \
            fprintf(stderr, "check failed at line %d: %s\n",             \
                    __LINE__, #condition);                                  \
            failures++;                                                     \
        }                                                                   \
    } while (0)

typedef struct {
    neverc_dwarf_data_t data;
    uint8_t canary[32];
} guarded_dwarf_data_t;

static int canary_ok(const uint8_t *canary, size_t size) {
    for (size_t i = 0; i < size; i++)
        if (canary[i] != 0xa5U) return 0;
    return 1;
}

static size_t registered_state_count(void) {
    size_t count = 0;
    dwarf_state_lock();
    for (size_t i = 0; i < DWARF_STATE_BUCKET_COUNT; i++) {
        for (dwarf_data_state_t *state = dwarf_state_buckets[i]; state;
             state = state->next)
            count++;
    }
    dwarf_state_unlock();
    return count;
}

static int ignore_entry(const neverc_dwarf_entry_t *entry, void *user) {
    (void)entry;
    (void)user;
    return 0;
}

static void put16le(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32le(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
    p[2] = (uint8_t)(value >> 16);
    p[3] = (uint8_t)(value >> 24);
}

static void put16be_test(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)(value >> 8);
    p[1] = (uint8_t)value;
}

static void put32be_test(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void put64be_test(uint8_t *p, uint64_t value) {
    put32be_test(p, (uint32_t)(value >> 32));
    put32be_test(p + 4, (uint32_t)value);
}

static void build_v4_header(uint8_t info[11], int big_endian) {
    memset(info, 0, 11);
    if (big_endian) {
        put32be_test(info, 7);
        put16be_test(info + 4, 4);
    } else {
        put32le(info, 7);
        put16le(info + 4, 4);
    }
    info[10] = 8;
}

static void test_sidecar_oom_is_transactional(void) {
    uint8_t info[11];
    build_v4_header(info, 0);
    guarded_dwarf_data_t guarded;
    memset(&guarded, 0xcc, sizeof(guarded));
    memset(guarded.canary, 0xa5, sizeof(guarded.canary));

    fail_state_allocations = 1;
    CHECK(neverc_dwarf_init(&guarded.data, info, sizeof(info),
                            NULL, 0, NULL, 0) == -1);
    CHECK(fail_state_allocations == 0);
    v3389_dwarf_data_t zero;
    memset(&zero, 0, sizeof(zero));
    CHECK(memcmp(&guarded.data, &zero, sizeof(zero)) == 0);
    int big_endian = 7;
    CHECK(neverc_dwarf_get_big_endian(
              &guarded.data, &big_endian) == -1 && big_endian == 0);
    neverc_dwarf_comp_unit_header_t header;
    CHECK(neverc_dwarf_parse_comp_unit(&guarded.data, 0, &header) == -1);
    neverc_dwarf_free(&guarded.data);
    CHECK(canary_ok(guarded.canary, sizeof(guarded.canary)));
}

static void test_default_big_endian_and_same_address_rebuild(void) {
    static const uint8_t abbrev[] = {
        1, NEVERC_DW_TAG_compile_unit, 0,
        NEVERC_DW_AT_name, NEVERC_DW_FORM_strp, 0, 0,
        0
    };
    uint8_t little[11];
    uint8_t big[11];
    build_v4_header(little, 0);
    build_v4_header(big, 1);
    guarded_dwarf_data_t guarded;
    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.canary, 0xa5, sizeof(guarded.canary));
    neverc_dwarf_comp_unit_header_t header;
    int big_endian = 9;

    CHECK(neverc_dwarf_init(&guarded.data, little, sizeof(little),
                            abbrev, sizeof(abbrev), NULL, 0) == 0);
    CHECK(neverc_dwarf_get_big_endian(
              &guarded.data, &big_endian) == 0 && big_endian == 0);
    CHECK(neverc_dwarf_parse_comp_unit(
              &guarded.data, 0, &header) == 0 && header.version == 4);
    CHECK(neverc_dwarf_set_big_endian(&guarded.data, 2) == -1);
    CHECK(neverc_dwarf_get_big_endian(
              &guarded.data, &big_endian) == 0 && big_endian == 0);
    CHECK(neverc_dwarf_set_big_endian(&guarded.data, 1) == 0);
    CHECK(neverc_dwarf_parse_comp_unit(&guarded.data, 0, &header) == -1);

    /* Populate both levels of the library-owned abbreviation cache. Leak
     * sanitizers verify that same-address init releases these allocations. */
    CHECK(parse_abbrevs(&guarded.data, 0) == 0);
    CHECK(guarded.data.abbrevs != NULL &&
          guarded.data.abbrev_count == 1 &&
          guarded.data.abbrevs[0].attrs != NULL);

    /* Reinitializing the same address must release the cache and discard the
     * previous endian state. */
    CHECK(neverc_dwarf_init(&guarded.data, little, sizeof(little),
                            NULL, 0, NULL, 0) == 0);
    CHECK(guarded.data.abbrevs == NULL && guarded.data.abbrev_count == 0);
    CHECK(neverc_dwarf_get_big_endian(
              &guarded.data, &big_endian) == 0 && big_endian == 0);
    CHECK(neverc_dwarf_parse_comp_unit(&guarded.data, 0, &header) == 0);
    neverc_dwarf_free(&guarded.data);

    big_endian = 9;
    CHECK(neverc_dwarf_get_big_endian(
              &guarded.data, &big_endian) == -1 && big_endian == 0);
    CHECK(neverc_dwarf_parse_comp_unit(&guarded.data, 0, &header) == -1);
    CHECK(neverc_dwarf_init(&guarded.data, big, sizeof(big),
                            NULL, 0, NULL, 0) == 0);
    CHECK(neverc_dwarf_parse_comp_unit(&guarded.data, 0, &header) == -1);
    CHECK(neverc_dwarf_set_big_endian(&guarded.data, 1) == 0);
    CHECK(neverc_dwarf_get_big_endian(
              &guarded.data, &big_endian) == 0 && big_endian == 1);
    CHECK(neverc_dwarf_parse_comp_unit(
              &guarded.data, 0, &header) == 0 && header.version == 4);
    neverc_dwarf_free(&guarded.data);
    CHECK(canary_ok(guarded.canary, sizeof(guarded.canary)));
}

static void test_reinit_oom_releases_abbrev_cache(void) {
    static const uint8_t abbrev[] = {
        1, NEVERC_DW_TAG_compile_unit, 0,
        NEVERC_DW_AT_name, NEVERC_DW_FORM_strp, 0, 0,
        0
    };
    uint8_t little[11];
    build_v4_header(little, 0);
    guarded_dwarf_data_t guarded;
    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.canary, 0xa5, sizeof(guarded.canary));

    CHECK(neverc_dwarf_init(&guarded.data, little, sizeof(little),
                            abbrev, sizeof(abbrev), NULL, 0) == 0);
    CHECK(parse_abbrevs(&guarded.data, 0) == 0);
    CHECK(guarded.data.abbrevs != NULL &&
          guarded.data.abbrev_count == 1 &&
          guarded.data.abbrevs[0].attrs != NULL);

    fail_state_allocations = 1;
    CHECK(neverc_dwarf_init(&guarded.data, little, sizeof(little),
                            NULL, 0, NULL, 0) == -1);
    CHECK(fail_state_allocations == 0);
    v3389_dwarf_data_t zero;
    memset(&zero, 0, sizeof(zero));
    CHECK(memcmp(&guarded.data, &zero, sizeof(zero)) == 0);
    CHECK(registered_state_count() == 0);
    CHECK(canary_ok(guarded.canary, sizeof(guarded.canary)));
}

static void test_big_endian_dwarf64_v5(void) {
    uint8_t info[24];
    memset(info, 0, sizeof(info));
    put32be_test(info, UINT32_MAX);
    put64be_test(info + 4, UINT64_C(12));
    put16be_test(info + 12, 5);
    info[14] = NEVERC_DW_UT_compile;
    info[15] = 8;
    put64be_test(info + 16, UINT64_C(0x0102030405060708));

    neverc_dwarf_data_t data;
    neverc_dwarf_comp_unit_header_ex_t header;
    CHECK(neverc_dwarf_init(&data, info, sizeof(info),
                            NULL, 0, NULL, 0) == 0);
    CHECK(neverc_dwarf_set_big_endian(&data, 1) == 0);
    CHECK(neverc_dwarf_parse_comp_unit_ex(&data, 0, &header) == 0);
    CHECK(header.is_64bit == 1);
    CHECK(header.unit_length == UINT64_C(12));
    CHECK(header.version == 5);
    CHECK(header.unit_type == NEVERC_DW_UT_compile);
    CHECK(header.address_size == 8);
    CHECK(header.abbrev_offset == UINT64_C(0x0102030405060708));
    CHECK(header.header_size == sizeof(info));
    neverc_dwarf_free(&data);
}

static void test_foreign_object_fails_closed(void) {
    uint8_t little[11];
    build_v4_header(little, 0);
    guarded_dwarf_data_t guarded;
    memset(&guarded, 0, sizeof(guarded));
    guarded.data.debug_info = little;
    guarded.data.debug_info_len = sizeof(little);
    guarded.data.debug_str = (const uint8_t *)"foreign";
    guarded.data.debug_str_len = 8;
    memset(guarded.canary, 0xa5, sizeof(guarded.canary));
    neverc_dwarf_data_t before = guarded.data;
    neverc_dwarf_comp_unit_header_t header;
    int big_endian = 9;

    CHECK(neverc_dwarf_get_big_endian(
              &guarded.data, &big_endian) == -1 && big_endian == 0);
    CHECK(neverc_dwarf_get_big_endian(&guarded.data, NULL) == -1);
    CHECK(neverc_dwarf_set_big_endian(&guarded.data, 1) == -1);
    CHECK(neverc_dwarf_parse_comp_unit(&guarded.data, 0, &header) == -1);
    CHECK(neverc_dwarf_walk_entries(
              &guarded.data, ignore_entry, NULL) == -1);
    CHECK(neverc_dwarf_get_string(&guarded.data, 0) == NULL);
    neverc_dwarf_free(&guarded.data);
    CHECK(memcmp(&guarded.data, &before, sizeof(before)) == 0);
    CHECK(canary_ok(guarded.canary, sizeof(guarded.canary)));
}

int main(void) {
    CHECK(registered_state_count() == 0);
    test_sidecar_oom_is_transactional();
    test_default_big_endian_and_same_address_rebuild();
    test_reinit_oom_releases_abbrev_cache();
    test_big_endian_dwarf64_v5();
    test_foreign_object_fails_closed();
    CHECK(registered_state_count() == 0);
    if (failures == 0) puts("passed");
    return failures == 0 ? 0 : 1;
}
