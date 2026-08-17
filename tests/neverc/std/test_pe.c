/*
 * PE parser test suite.
 * Tests with synthesized minimal PE32+ (64-bit) binary data.
 */
#include "neverc/std/debug/pe.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define CHECK(name, cond) do { \
    tests_run++; \
    if (cond) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: %s\n", name); } \
} while(0)

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v>>8); }
static void put32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

/*
 * Build a minimal PE32+ binary:
 *   DOS header (64 bytes, e_lfanew at offset 60 -> 64)
 *   PE signature (4 bytes) at offset 64
 *   COFF header (20 bytes) at offset 68
 *   Optional header (PE32+, 240 bytes) at offset 88
 *   Section table (.text, 40 bytes) at offset 328
 *   .text data at offset 512
 */
static uint8_t *build_minimal_pe64(size_t *out_len) {
    size_t total = 1024;
    uint8_t *buf = (uint8_t *)calloc(total, 1);
    if (!buf) return NULL;

    /* DOS header */
    buf[0] = 'M'; buf[1] = 'Z';
    put32(buf + 60, 64);  /* e_lfanew */

    /* PE signature */
    buf[64] = 'P'; buf[65] = 'E'; buf[66] = 0; buf[67] = 0;

    /* COFF header at offset 68 */
    uint8_t *coff = buf + 68;
    put16(coff + 0, 0x8664);   /* Machine: AMD64 */
    put16(coff + 2, 1);        /* NumberOfSections: 1 */
    put32(coff + 4, 0x5F3B1234); /* TimeDateStamp */
    put32(coff + 8, 0);        /* PointerToSymbolTable */
    put32(coff + 12, 0);       /* NumberOfSymbols */
    put16(coff + 16, 240);     /* SizeOfOptionalHeader (PE32+) */
    put16(coff + 18, 0x0022);  /* Characteristics: EXEC | LARGE_ADDRESS_AWARE */

    /* Optional header (PE32+) at offset 88 */
    uint8_t *opt = buf + 88;
    put16(opt + 0, 0x20b);     /* Magic: PE32+ */
    /* ImageBase at offset 24 (8 bytes for PE32+) */
    put32(opt + 24, 0x00400000);
    put32(opt + 28, 0x00000000);
    /* SectionAlignment */
    put32(opt + 32, 0x1000);
    /* FileAlignment */
    put32(opt + 36, 0x200);
    /* SizeOfImage */
    put32(opt + 56, 0x3000);
    /* SizeOfHeaders */
    put32(opt + 60, 0x200);
    /* Subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI (3) */
    put16(opt + 68, 3);
    /* NumberOfRvaAndSizes */
    put32(opt + 108, 16);

    /* Section table at offset 328 */
    uint8_t *sec = buf + 328;
    memcpy(sec, ".text\0\0\0", 8);         /* Name */
    put32(sec + 8, 16);                    /* VirtualSize */
    put32(sec + 12, 0x1000);               /* VirtualAddress */
    put32(sec + 16, 16);                   /* SizeOfRawData */
    put32(sec + 20, 512);                  /* PointerToRawData */
    put32(sec + 36, 0x60000020);           /* Characteristics: CODE|EXECUTE|READ */

    /* .text data at offset 512 */
    uint8_t code[] = {0x48, 0x89, 0xE5,    /* mov rbp, rsp */
                      0x48, 0x83, 0xEC, 0x20, /* sub rsp, 0x20 */
                      0x31, 0xC0,            /* xor eax, eax */
                      0xC3,                  /* ret */
                      0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    memcpy(buf + 512, code, sizeof(code));

    *out_len = total;
    return buf;
}

static uint8_t *build_pe64_with_imports(size_t *out_len) {
    uint8_t *buf = build_minimal_pe64(out_len);
    if (!buf) return NULL;

    uint8_t *coff = buf + 68;
    uint8_t *opt = buf + 88;
    put16(coff + 2, 2);
    put32(opt + 112 + 8, 0x2000);
    put32(opt + 112 + 12, 40);

    uint8_t *sec = buf + 368;
    memcpy(sec, ".idata\0\0", 8);
    put32(sec + 8, 256);
    put32(sec + 12, 0x2000);
    put32(sec + 16, 256);
    put32(sec + 20, 768);
    put32(sec + 36, NEVERC_IMAGE_SCN_CNT_INITIALIZED_DATA |
                              NEVERC_IMAGE_SCN_MEM_READ);

    uint8_t *descriptor = buf + 768;
    put32(descriptor, 0x2030);
    put32(descriptor + 12, 0x2040);
    put32(descriptor + 16, 0x2030);
    put64(buf + 816, 0x2060);
    put64(buf + 824, 0);
    memcpy(buf + 832, "kernel32.dll", 13);
    put16(buf + 864, 7);
    memcpy(buf + 866, "ExitProcess", 12);
    return buf;
}

static void test_pe64_parse(void) {
    size_t len;
    uint8_t *data = build_minimal_pe64(&len);
    CHECK("build_pe64_not_null", data != NULL);
    if (!data) return;

    CHECK("is_valid", neverc_pe_is_valid(data, len));

    neverc_pe_file_t f;
    int rc = neverc_pe_open(&f, data, len);
    CHECK("open_success", rc == 0);

    CHECK("machine_amd64", f.file_header.machine == 0x8664);
    CHECK("is_64bit", f.is_64bit == 1);
    CHECK("section_count_1", f.section_count == 1);
    CHECK("optional_magic", f.optional_header.magic == 0x20b);
    CHECK("image_base", f.optional_header.image_base == 0x00400000);

    const neverc_pe_section_t *text = neverc_pe_section(&f, ".text");
    CHECK("text_found", text != NULL);
    if (text) {
        CHECK("text_va", text->virtual_address == 0x1000);
        CHECK("text_rawsize", text->size_of_raw_data == 16);
        CHECK("text_is_code", (text->characteristics & NEVERC_IMAGE_SCN_CNT_CODE) != 0);

        uint8_t *sec_data = NULL;
        size_t sec_len = 0;
        rc = neverc_pe_section_data(&f, text, &sec_data, &sec_len);
        CHECK("text_data_read", rc == 0 && sec_data != NULL);
        if (sec_data) {
            CHECK("text_data_mov_rbp", sec_data[0] == 0x48 && sec_data[1] == 0x89);
            CHECK("text_data_ret", sec_data[9] == 0xC3);
            free(sec_data);
        }
    }

    const neverc_pe_section_t *missing = neverc_pe_section(&f, ".data");
    CHECK("data_not_found", missing == NULL);

    CHECK("machine_string", strcmp(neverc_pe_machine_string(0x8664), "x86-64") == 0);
    CHECK("machine_string_arm64", strcmp(neverc_pe_machine_string(0xaa64), "ARM64") == 0);

    neverc_pe_close(&f);
    free(data);
}

static void test_imports(void) {
    size_t len = 0;
    uint8_t *data = build_pe64_with_imports(&len);
    CHECK("build imports PE", data != NULL);
    if (!data) return;

    neverc_pe_file_t f;
    CHECK("open imports PE", neverc_pe_open(&f, data, len) == 0);
    char *names[2] = {NULL, NULL};
    int count = neverc_pe_imported_libraries(&f, names, 2);
    CHECK("one imported library", count == 1);
    CHECK("imported library name",
          count == 1 && strcmp(names[0], "kernel32.dll") == 0);
    names[0] = NULL;
    count = neverc_pe_imported_symbols(&f, names, 2);
    CHECK("one imported symbol", count == 1);
    CHECK("imported symbol name",
          count == 1 && strcmp(names[0], "ExitProcess") == 0);
    neverc_pe_close(&f);

    put32(data + 88 + 112 + 12, 20);
    CHECK("open missing import terminator", neverc_pe_open(&f, data, len) == 0);
    CHECK("reject missing import terminator",
          neverc_pe_imported_libraries(&f, names, 2) == -1);
    CHECK("reject symbols without import terminator",
          neverc_pe_imported_symbols(&f, names, 2) == -1);
    neverc_pe_close(&f);
    free(data);
}

static void test_symbols(void) {
    size_t len = 0;
    uint8_t *data = build_minimal_pe64(&len);
    CHECK("build symbols PE", data != NULL);
    if (!data) return;

    put32(data + 68 + 8, 600);
    put32(data + 68 + 12, 1);
    memcpy(data + 600, "main\0\0\0\0", 8);
    put32(data + 608, 0x1234);
    put16(data + 612, 1);
    data[616] = NEVERC_IMAGE_SYM_CLASS_EXTERNAL;
    put32(data + 618, 4);

    neverc_pe_file_t f;
    CHECK("open symbols PE", neverc_pe_open(&f, data, len) == 0);
    neverc_pe_symbol_t *symbols = NULL;
    int count = 0;
    CHECK("read inline symbol", neverc_pe_symbols(&f, &symbols, &count) == 0);
    CHECK("inline symbol count and name",
          count == 1 && symbols != NULL &&
              strcmp(symbols[0].name, "main") == 0);
    free(symbols);

    data[617] = 1;
    symbols = (neverc_pe_symbol_t *)1;
    count = 99;
    CHECK("reject auxiliary record overrun",
          neverc_pe_symbols(&f, &symbols, &count) == -1);
    CHECK("symbol failure clears outputs", symbols == NULL && count == 0);
    data[617] = 0;

    memset(data + 600, 0, 4);
    put32(data + 604, 4);
    CHECK("reject string offset at table end",
          neverc_pe_symbols(&f, &symbols, &count) == -1);
    neverc_pe_close(&f);
    free(data);
}

static void test_long_section_names(void) {
    size_t len = 0;
    uint8_t *data = build_minimal_pe64(&len);
    CHECK("build long-name PE", data != NULL);
    if (!data) return;

    memcpy(data + 328, "/4\0\0\0\0\0\0", 8);
    put32(data + 68 + 8, 600);
    put32(data + 68 + 12, 0);
    put32(data + 600, 16);
    memcpy(data + 604, ".debug_info", 12);

    neverc_pe_file_t f;
    CHECK("open long-name PE", neverc_pe_open(&f, data, len) == 0);
    const neverc_pe_section_t *debug = neverc_pe_section(&f, ".debug_info");
    CHECK("resolved /4 to .debug_info", debug != NULL);
    CHECK("short name still absent", neverc_pe_section(&f, "/4") == NULL);
    neverc_pe_close(&f);

    put32(data + 68 + 8, 0);
    CHECK("reject long name without string table",
          neverc_pe_open(&f, data, len) == -1);
    put32(data + 68 + 8, 600);
    memcpy(data + 328, "/99\0\0\0\0\0", 8);
    CHECK("reject long name past string table",
          neverc_pe_open(&f, data, len) == -1);
    free(data);
}

static uint8_t *build_minimal_pe32(size_t *out_len) {
    size_t total = 1024;
    uint8_t *buf = (uint8_t *)calloc(total, 1);
    if (!buf) return NULL;

    buf[0] = 'M'; buf[1] = 'Z';
    put32(buf + 60, 64);
    buf[64] = 'P'; buf[65] = 'E';

    uint8_t *coff = buf + 68;
    put16(coff + 0, 0x14c);
    put16(coff + 2, 1);
    put16(coff + 16, 224);
    put16(coff + 18, 0x0102);

    uint8_t *opt = buf + 88;
    put16(opt + 0, NEVERC_PE32_MAGIC);
    put32(opt + 28, 0x00400000);
    put32(opt + 32, 0x1000);
    put32(opt + 36, 0x200);
    put32(opt + 56, 0x2000);
    put32(opt + 60, 0x200);
    put16(opt + 68, 3);
    put32(opt + 92, 16);

    uint8_t *sec = buf + 312;
    memcpy(sec, ".text\0\0\0", 8);
    put32(sec + 8, 16);
    put32(sec + 12, 0x1000);
    put32(sec + 16, 16);
    put32(sec + 20, 512);
    put32(sec + 36, 0x60000020);
    buf[512] = 0xC3;

    *out_len = total;
    return buf;
}

static void test_pe32_and_bounds(void) {
    size_t len = 0;
    uint8_t *data = build_minimal_pe32(&len);
    CHECK("build PE32 fixture", data != NULL);
    if (!data) return;

    neverc_pe_file_t f;
    CHECK("open PE32", neverc_pe_open(&f, data, len) == 0);
    CHECK("PE32 is 32-bit", f.is_64bit == 0);
    CHECK("PE32 image base", f.optional_header.image_base == 0x00400000);
    CHECK("PE32 .text", neverc_pe_section(&f, ".text") != NULL);
    neverc_pe_close(&f);
    free(data);

    data = build_minimal_pe64(&len);
    CHECK("build PE32+ bound fixture", data != NULL);
    if (!data) return;
    put32(data + 88 + 60, (uint32_t)(len + 1));
    CHECK("reject SizeOfHeaders past EOF", neverc_pe_open(&f, data, len) == -1);

    uint8_t wrap[64];
    memset(wrap, 0, sizeof(wrap));
    wrap[0] = 'M'; wrap[1] = 'Z';
    put32(wrap + 60, 0xFFFFFFFEu);
    CHECK("reject e_lfanew near UINT32_MAX",
          !neverc_pe_is_valid(wrap, sizeof(wrap)));
    free(data);
}

static void test_pe_invalid(void) {
    uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03};
    CHECK("invalid_too_short", !neverc_pe_is_valid(garbage, sizeof(garbage)));

    uint8_t not_pe[] = {'M', 'Z', 0, 0};
    CHECK("invalid_no_pe_sig", !neverc_pe_is_valid(not_pe, sizeof(not_pe)));

    neverc_pe_file_t f;
    CHECK("open_fails_garbage", neverc_pe_open(&f, garbage, sizeof(garbage)) < 0);
    CHECK("null data invalid", !neverc_pe_is_valid(NULL, 64));
    CHECK("null output rejected",
          neverc_pe_open(NULL, garbage, sizeof(garbage)) == -1);

    size_t len = 0;
    uint8_t *data = build_minimal_pe64(&len);
    CHECK("build malformed fixtures", data != NULL);
    if (!data) return;

    put16(data + 88, 0x999);
    CHECK("reject unknown optional-header magic",
          neverc_pe_open(&f, data, len) == -1);
    put16(data + 88, NEVERC_PE32P_MAGIC);

    put32(data + 88 + 108, 17);
    CHECK("reject truncated data-directory array",
          neverc_pe_open(&f, data, len) == -1);
    put32(data + 88 + 108, 16);

    put32(data + 328 + 20, (uint32_t)(len - 8));
    CHECK("reject section outside file", neverc_pe_open(&f, data, len) == -1);
    put32(data + 328 + 20, 512);

    CHECK("open valid fixture for argument checks",
          neverc_pe_open(&f, data, len) == 0);
    uint8_t *section_data = (uint8_t *)1;
    size_t section_len = 99;
    CHECK("reject null section", neverc_pe_section_data(
              &f, NULL, &section_data, &section_len) == -1);
    CHECK("null section clears outputs",
          section_data == NULL && section_len == 0);
    CHECK("reject null imported-library output",
          neverc_pe_imported_libraries(&f, NULL, 1) == -1);
    CHECK("reject null imported-symbol output",
          neverc_pe_imported_symbols(&f, NULL, 1) == -1);
    neverc_pe_close(&f);
    free(data);
}

int main(void) {
    printf("=== NeverC debug/pe Tests ===\n\n");

    test_pe64_parse();
    test_imports();
    test_symbols();
    test_long_section_names();
    test_pe32_and_bounds();
    test_pe_invalid();

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf("\n");
    return tests_failed > 0 ? 1 : 0;
}
