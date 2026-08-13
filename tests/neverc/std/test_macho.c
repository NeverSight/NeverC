/*
 * Mach-O parser test suite.
 * Tests with synthesized minimal Mach-O 64-bit binary data.
 */
#include "neverc/std/debug/macho.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define CHECK(name, cond) do { \
    tests_run++; \
    if (cond) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: %s\n", name); } \
} while(0)

static void put32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p+4, (uint32_t)(v>>32));
}
static void put32be(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)(v>>24); p[1]=(uint8_t)(v>>16);
    p[2]=(uint8_t)(v>>8); p[3]=(uint8_t)v;
}

/*
 * Build a minimal Mach-O 64-bit binary:
 *   Header (32 bytes)
 *   LC_SEGMENT_64 with one __TEXT,__text section
 */
static uint8_t *build_minimal_macho64(size_t *out_len) {
    size_t total = 1024;
    uint8_t *buf = (uint8_t *)calloc(total, 1);
    if (!buf) return NULL;

    /* Mach-O header (32 bytes for 64-bit) */
    put32(buf + 0, 0xFEEDFACF);  /* magic: MH_MAGIC_64 */
    put32(buf + 4, 0x0100000C);  /* cputype: CPU_TYPE_ARM64 */
    put32(buf + 8, 0x00000000);  /* cpusubtype */
    put32(buf + 12, 2);          /* filetype: MH_EXECUTE */
    put32(buf + 16, 1);          /* ncmds: 1 */
    /* sizeofcmds: LC_SEGMENT_64 = 72 + 80*1 = 152 */
    put32(buf + 20, 152);
    put32(buf + 24, 0);          /* flags */

    /* LC_SEGMENT_64 at offset 32 */
    uint8_t *lc = buf + 32;
    put32(lc + 0, 0x19);        /* cmd: LC_SEGMENT_64 */
    put32(lc + 4, 152);         /* cmdsize: 72 + 80 */
    memcpy(lc + 8, "__TEXT\0\0\0\0\0\0\0\0\0\0\0", 16); /* segname */
    put64(lc + 24, 0x100000000ULL); /* vmaddr */
    put64(lc + 32, 0x1000);        /* vmsize */
    put64(lc + 40, 0);             /* fileoff */
    put64(lc + 48, total);         /* filesize */
    put32(lc + 56, 7);             /* maxprot: rwx */
    put32(lc + 60, 5);             /* initprot: rx */
    put32(lc + 64, 1);             /* nsects */
    put32(lc + 68, 0);             /* flags */

    /* Section within LC_SEGMENT_64: offset 32 + 72 = 104 */
    uint8_t *sec = lc + 72;
    memcpy(sec + 0, "__text\0\0\0\0\0\0\0\0\0\0", 16);  /* sectname */
    memcpy(sec + 16, "__TEXT\0\0\0\0\0\0\0\0\0\0", 16);  /* segname */
    put64(sec + 32, 0x100000000ULL + 512); /* addr */
    put64(sec + 40, 4);            /* size */
    put32(sec + 48, 512);          /* offset */
    put32(sec + 52, 2);            /* align */
    put32(sec + 56, 0);            /* reloff */
    put32(sec + 60, 0);            /* nreloc */
    put32(sec + 64, 0x80000400);   /* flags: S_ATTR_PURE_INSTRUCTIONS|S_REGULAR */

    /* Section data at offset 512 */
    buf[512] = 0xC0; buf[513] = 0x03; buf[514] = 0x5F; buf[515] = 0xD6; /* ret (ARM64) */

    *out_len = total;
    return buf;
}

static uint8_t *build_macho64_with_metadata(size_t *out_len) {
    uint8_t *buf = build_minimal_macho64(out_len);
    if (!buf) return NULL;

    put32(buf + 16, 3);
    put32(buf + 20, 224);

    uint8_t *symtab = buf + 184;
    put32(symtab, NEVERC_LC_SYMTAB);
    put32(symtab + 4, 24);
    put32(symtab + 8, 600);
    put32(symtab + 12, 1);
    put32(symtab + 16, 616);
    put32(symtab + 20, 7);

    uint8_t *dylib = buf + 208;
    put32(dylib, NEVERC_LC_LOAD_DYLIB);
    put32(dylib + 4, 48);
    put32(dylib + 8, 24);
    put32(dylib + 12, 2);
    put32(dylib + 16, 0x10000);
    put32(dylib + 20, 0x10000);
    memcpy(dylib + 24, "libSystem.B.dylib", 18);

    put32(buf + 600, 1);
    buf[604] = NEVERC_N_SECT | NEVERC_N_EXT;
    buf[605] = 1;
    put64(buf + 608, 0x100000200ULL);
    memcpy(buf + 616, "\0_main\0", 7);
    return buf;
}

static void test_macho64_parse(void) {
    size_t len;
    uint8_t *data = build_minimal_macho64(&len);
    CHECK("build_macho64_not_null", data != NULL);
    if (!data) return;

    CHECK("is_valid", neverc_macho_is_valid(data, len));

    neverc_macho_file_t f;
    int rc = neverc_macho_open(&f, data, len);
    CHECK("open_success", rc == 0);

    CHECK("is_64bit", f.is_64bit == 1);
    CHECK("cpu_arm64", f.header.cpu == NEVERC_CPU_TYPE_ARM64);
    CHECK("type_execute", f.header.type == NEVERC_MH_EXECUTE);
    CHECK("segment_count_1", f.segment_count == 1);
    CHECK("section_count_1", f.section_count == 1);

    const neverc_macho_segment_t *text_seg = neverc_macho_segment(&f, "__TEXT");
    CHECK("text_segment_found", text_seg != NULL);
    if (text_seg) {
        CHECK("text_seg_addr", text_seg->addr == 0x100000000ULL);
        CHECK("text_seg_nsects", text_seg->nsects == 1);
    }

    const neverc_macho_section_t *text_sec = neverc_macho_section(&f, "__text");
    CHECK("text_section_found", text_sec != NULL);
    if (text_sec) {
        CHECK("text_sec_size_4", text_sec->size == 4);
        CHECK("text_sec_segname", strcmp(text_sec->segname, "__TEXT") == 0);

        uint8_t *sec_data = NULL;
        size_t sec_len = 0;
        rc = neverc_macho_section_data(&f, text_sec, &sec_data, &sec_len);
        CHECK("text_sec_data_read", rc == 0 && sec_data != NULL);
        if (sec_data) {
            CHECK("text_sec_data_ret", sec_data[0] == 0xC0);
            free(sec_data);
        }
    }

    const neverc_macho_segment_t *missing_seg = neverc_macho_segment(&f, "__DATA");
    CHECK("data_seg_not_found", missing_seg == NULL);

    CHECK("cpu_string_arm64", strcmp(neverc_macho_cpu_string(NEVERC_CPU_TYPE_ARM64), "ARM64") == 0);
    CHECK("type_string_exec", strcmp(neverc_macho_type_string(NEVERC_MH_EXECUTE), "Exec") == 0);
    CHECK("type_string_dylib", strcmp(neverc_macho_type_string(NEVERC_MH_DYLIB), "Dylib") == 0);

    neverc_macho_close(&f);
    free(data);
}

static void test_metadata(void) {
    size_t len = 0;
    uint8_t *data = build_macho64_with_metadata(&len);
    CHECK("build metadata Mach-O", data != NULL);
    if (!data) return;

    neverc_macho_file_t f;
    CHECK("open metadata Mach-O", neverc_macho_open(&f, data, len) == 0);
    CHECK("one dylib", f.dylib_count == 1);
    CHECK("dylib name",
          f.dylib_count == 1 &&
              strcmp(f.dylibs[0].name, "libSystem.B.dylib") == 0);

    neverc_macho_symbol_t *symbols = NULL;
    int count = 0;
    CHECK("read symbols", neverc_macho_symbols(&f, &symbols, &count) == 0);
    CHECK("symbol name and value",
          count == 1 && symbols != NULL &&
              strcmp(symbols[0].name, "_main") == 0 &&
              symbols[0].value == 0x100000200ULL);
    free(symbols);

    put32(data + 600, 7);
    symbols = (neverc_macho_symbol_t *)1;
    count = 99;
    CHECK("reject symbol string offset at end",
          neverc_macho_symbols(&f, &symbols, &count) == -1);
    CHECK("symbol error clears outputs", symbols == NULL && count == 0);
    put32(data + 600, 1);
    data[622] = 'x';
    CHECK("reject unterminated symbol name",
          neverc_macho_symbols(&f, &symbols, &count) == -1);
    data[622] = '\0';
    neverc_macho_close(&f);

    memset(data + 208 + 24, 'x', 24);
    CHECK("reject unterminated dylib name",
          neverc_macho_open(&f, data, len) == -1);
    free(data);
}

static void test_header_variants(void) {
    uint8_t macho32[28] = {0};
    put32(macho32, NEVERC_MH_MAGIC);
    put32(macho32 + 4, NEVERC_CPU_TYPE_X86);
    put32(macho32 + 12, NEVERC_MH_OBJECT);
    neverc_macho_file_t f;
    CHECK("open thin 32-bit header",
          neverc_macho_open(&f, macho32, sizeof(macho32)) == 0);
    CHECK("identify 32-bit header", !f.is_64bit && !f.is_swap);
    neverc_macho_close(&f);

    uint8_t macho64be[32] = {0};
    put32be(macho64be, NEVERC_MH_MAGIC_64);
    put32be(macho64be + 4, NEVERC_CPU_TYPE_ARM64);
    put32be(macho64be + 12, NEVERC_MH_OBJECT);
    CHECK("open thin big-endian header",
          neverc_macho_open(&f, macho64be, sizeof(macho64be)) == 0);
    CHECK("identify swapped 64-bit header",
          f.is_64bit && f.is_swap &&
              f.header.cpu == NEVERC_CPU_TYPE_ARM64);
    neverc_macho_close(&f);
}

static void test_macho_invalid(void) {
    uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03};
    CHECK("invalid_too_short", !neverc_macho_is_valid(garbage, sizeof(garbage)));

    neverc_macho_file_t f;
    CHECK("open_fails_garbage", neverc_macho_open(&f, garbage, sizeof(garbage)) < 0);
    CHECK("null data invalid", !neverc_macho_is_valid(NULL, 4));
    CHECK("null output rejected",
          neverc_macho_open(NULL, garbage, sizeof(garbage)) == -1);

    size_t len = 0;
    uint8_t *data = build_minimal_macho64(&len);
    CHECK("build malformed Mach-O fixtures", data != NULL);
    if (!data) return;

    put32(data + 20, 144);
    CHECK("reject command beyond declared region",
          neverc_macho_open(&f, data, len) == -1);
    put32(data + 20, 152);

    put32(data + 16, 2);
    CHECK("reject missing declared command",
          neverc_macho_open(&f, data, len) == -1);
    put32(data + 16, 1);

    put32(data + 32 + 64, 2);
    CHECK("reject truncated section array",
          neverc_macho_open(&f, data, len) == -1);
    put32(data + 32 + 64, 1);

    put32(data + 32 + 72 + 48, (uint32_t)(len - 2));
    CHECK("reject section outside file",
          neverc_macho_open(&f, data, len) == -1);
    put32(data + 32 + 72 + 48, 512);

    put32(data + 32 + 72 + 60, 1);
    put32(data + 32 + 72 + 56, (uint32_t)(len - 4));
    CHECK("reject relocation table outside file",
          neverc_macho_open(&f, data, len) == -1);
    put32(data + 32 + 72 + 60, 0);
    put32(data + 32 + 72 + 56, 0);

    CHECK("open fixture for argument checks",
          neverc_macho_open(&f, data, len) == 0);
    uint8_t *section_data = (uint8_t *)1;
    size_t section_len = 99;
    CHECK("reject null section", neverc_macho_section_data(
              &f, NULL, &section_data, &section_len) == -1);
    CHECK("null section clears outputs",
          section_data == NULL && section_len == 0);
    int symbol_count = 0;
    CHECK("reject null symbol output",
          neverc_macho_symbols(&f, NULL, &symbol_count) == -1);
    neverc_macho_close(&f);

    put64(data + 32 + 72 + 40, UINT64_C(0x100000000));
    put32(data + 32 + 72 + 64, 1);
    CHECK("accept zero-fill section without file bytes",
          neverc_macho_open(&f, data, len) == 0);
    const neverc_macho_section_t *zero =
        neverc_macho_section(&f, "__text");
    section_data = (uint8_t *)1;
    section_len = 99;
    CHECK("zero-fill section has no copied data",
          zero != NULL &&
              neverc_macho_section_data(
                  &f, zero, &section_data, &section_len) == 0 &&
              section_data == NULL && section_len == 0);
    neverc_macho_close(&f);
    free(data);
}

#ifdef __APPLE__
static void test_macho_self_binary(void) {
    /* On macOS, parse our own binary */
    extern const char *_dyld_get_image_name(uint32_t);
    const char *self = _dyld_get_image_name(0);
    if (!self) return;

    neverc_macho_file_t f;
    int rc = neverc_macho_open_file(&f, self);
    /* May fail on universal binaries (FAT), that's OK */
    if (rc == 0) {
        CHECK("self_is_64bit", f.is_64bit == 1);
        CHECK("self_has_sections", f.section_count > 0);
        CHECK("self_has_text", neverc_macho_section(&f, "__text") != NULL);
        neverc_macho_close(&f);
    }
}
#endif

int main(void) {
    printf("=== NeverC debug/macho Tests ===\n\n");

    test_macho64_parse();
    test_metadata();
    test_header_variants();
    test_macho_invalid();
#ifdef __APPLE__
    test_macho_self_binary();
#endif

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf("\n");
    return tests_failed > 0 ? 1 : 0;
}
