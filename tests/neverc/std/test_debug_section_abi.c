#include "neverc/std/debug/pe.h"
#include "neverc/std/debug/plan9obj.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[9];
    uint32_t virtual_size;
    uint32_t virtual_address;
    uint32_t size_of_raw_data;
    uint32_t pointer_to_raw_data;
    uint32_t pointer_to_relocations;
    uint32_t number_of_relocations;
    uint32_t characteristics;
} v3389_pe_section_t;

typedef struct {
    char *name;
    uint32_t size;
    uint32_t offset;
} v3389_plan9_section_t;

#define ABI_FIELD(current, legacy, field)                                  \
    _Static_assert(offsetof(current, field) == offsetof(legacy, field),     \
                   #current "." #field " v3389 offset changed")

_Static_assert(sizeof(neverc_pe_section_t) == sizeof(v3389_pe_section_t),
               "neverc_pe_section_t v3389 size changed");
_Static_assert(sizeof(neverc_pe_section_t) == 40,
               "neverc_pe_section_t must remain 40 bytes");
_Static_assert(_Alignof(neverc_pe_section_t) ==
                   _Alignof(v3389_pe_section_t),
               "neverc_pe_section_t v3389 alignment changed");
ABI_FIELD(neverc_pe_section_t, v3389_pe_section_t, name);
ABI_FIELD(neverc_pe_section_t, v3389_pe_section_t, virtual_size);
ABI_FIELD(neverc_pe_section_t, v3389_pe_section_t, virtual_address);
ABI_FIELD(neverc_pe_section_t, v3389_pe_section_t, size_of_raw_data);
ABI_FIELD(neverc_pe_section_t, v3389_pe_section_t, pointer_to_raw_data);
ABI_FIELD(neverc_pe_section_t, v3389_pe_section_t, pointer_to_relocations);
ABI_FIELD(neverc_pe_section_t, v3389_pe_section_t, number_of_relocations);
ABI_FIELD(neverc_pe_section_t, v3389_pe_section_t, characteristics);

_Static_assert(sizeof(neverc_plan9_section_t) ==
                   sizeof(v3389_plan9_section_t),
               "neverc_plan9_section_t v3389 size changed");
_Static_assert(_Alignof(neverc_plan9_section_t) ==
                   _Alignof(v3389_plan9_section_t),
               "neverc_plan9_section_t v3389 alignment changed");
#if UINTPTR_MAX == UINT64_MAX
_Static_assert(sizeof(neverc_plan9_section_t) == 16,
               "64-bit neverc_plan9_section_t must remain 16 bytes");
#endif
ABI_FIELD(neverc_plan9_section_t, v3389_plan9_section_t, name);
ABI_FIELD(neverc_plan9_section_t, v3389_plan9_section_t, size);
ABI_FIELD(neverc_plan9_section_t, v3389_plan9_section_t, offset);

#undef ABI_FIELD

static int failures;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n", __LINE__,    \
                    #condition);                                             \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static int canary_ok(const uint8_t *canary, size_t size) {
    for (size_t i = 0; i < size; i++)
        if (canary[i] != 0xa5) return 0;
    return 1;
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

static void put32be(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void build_long_name_pe(uint8_t image[196], const char *long_name) {
    memset(image, 0, 196);
    image[0] = 'M';
    image[1] = 'Z';
    put32le(image + 60, 64);
    memcpy(image + 64, "PE\0\0", 4);
    uint8_t *coff = image + 68;
    put16le(coff, NEVERC_IMAGE_FILE_MACHINE_AMD64);
    put16le(coff + 2, 1);
    put32le(coff + 8, 128); /* string table follows zero symbols */
    put32le(coff + 12, 0);
    memcpy(image + 88, "/4\0\0\0\0\0\0", 8);
    put32le(image + 88 + 8, 4);
    put32le(image + 88 + 16, 4);
    put32le(image + 88 + 20, 192);
    size_t name_length = strlen(long_name);
    put32le(image + 128, (uint32_t)(4U + name_length + 1U));
    memcpy(image + 132, long_name, name_length + 1U);
    memcpy(image + 192, "PE!!", 4);
}

static void test_pe_section_abi_and_long_name(void) {
    static const char long_name[] = ".debug_really_long_section";
    uint8_t image[196];
    build_long_name_pe(image, long_name);
    struct {
        neverc_pe_file_t value;
        uint8_t canary[32];
    } guarded;
    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.canary, 0xa5, sizeof(guarded.canary));

    CHECK(neverc_pe_open(&guarded.value, image, sizeof(image)) == 0);
    CHECK(guarded.value.section_count == 1);
    const neverc_pe_section_t *section =
        neverc_pe_section(&guarded.value, long_name);
    CHECK(section != NULL);
    CHECK(section != NULL && strcmp(section->name, "/4") == 0);
    const char *full_name = section == NULL
                                ? NULL
                                : neverc_pe_section_name(&guarded.value,
                                                         section);
    CHECK(full_name != NULL && strcmp(full_name, long_name) == 0);
    CHECK(neverc_pe_section(&guarded.value, "/4") == NULL);
    if (section) {
        neverc_pe_section_t copied = *section;
        CHECK(neverc_pe_section_name(&guarded.value, &copied) == NULL);
        uint8_t *data = NULL;
        size_t data_length = 0;
        CHECK(neverc_pe_section_data(
                  &guarded.value, section, &data, &data_length) == 0);
        CHECK(data_length == 4 && data != NULL &&
              memcmp(data, "PE!!", 4) == 0);
        free(data);
    }
    CHECK(canary_ok(guarded.canary, sizeof(guarded.canary)));
    neverc_pe_close(&guarded.value);
    CHECK(canary_ok(guarded.canary, sizeof(guarded.canary)));
}

static void build_plan9(uint8_t image[36]) {
    memset(image, 0, 36);
    put32be(image, NEVERC_PLAN9_MAGIC386);
    put32be(image + 4, 4);
    memcpy(image + 32, "P9!!", 4);
}

static void test_plan9_section_abi_and_offset64(void) {
    uint8_t image[36];
    build_plan9(image);
    struct {
        neverc_plan9_file_t value;
        uint8_t canary[32];
    } guarded;
    memset(&guarded, 0, sizeof(guarded));
    memset(guarded.canary, 0xa5, sizeof(guarded.canary));

    CHECK(neverc_plan9_parse(&guarded.value, image, sizeof(image)) == 0);
    neverc_plan9_section_t *text =
        neverc_plan9_section(&guarded.value, "text");
    CHECK(text != NULL && text->offset == 32);
    uint64_t offset = 0;
    CHECK(text != NULL && neverc_plan9_section_offset64(
              &guarded.value, text, &offset) == 0 && offset == 32);
    if (text) {
        text->offset = 0; /* section_data must use the private 64-bit value. */
        uint8_t data[4] = {0};
        CHECK(neverc_plan9_section_data(
                  &guarded.value, text, data, sizeof(data)) == 0);
        CHECK(memcmp(data, "P9!!", 4) == 0);

        neverc_plan9_section_t copied = *text;
        offset = 99;
        CHECK(neverc_plan9_section_offset64(
                  &guarded.value, &copied, &offset) == -1 && offset == 0);
        CHECK(neverc_plan9_section_data(
                  &guarded.value, &copied, data, sizeof(data)) == -1);
    }
    CHECK(canary_ok(guarded.canary, sizeof(guarded.canary)));
    neverc_plan9_close(&guarded.value);
    CHECK(canary_ok(guarded.canary, sizeof(guarded.canary)));
}

int main(void) {
    test_pe_section_abi_and_long_name();
    test_plan9_section_abi_and_offset64();
    if (failures == 0) puts("passed");
    return failures == 0 ? 0 : 1;
}
