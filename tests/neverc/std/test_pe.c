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

static void test_pe_invalid(void) {
    uint8_t garbage[] = {0x00, 0x01, 0x02, 0x03};
    CHECK("invalid_too_short", !neverc_pe_is_valid(garbage, sizeof(garbage)));

    uint8_t not_pe[] = {'M', 'Z', 0, 0};
    CHECK("invalid_no_pe_sig", !neverc_pe_is_valid(not_pe, sizeof(not_pe)));

    neverc_pe_file_t f;
    CHECK("open_fails_garbage", neverc_pe_open(&f, garbage, sizeof(garbage)) < 0);
}

int main(void) {
    printf("=== NeverC debug/pe Tests ===\n\n");

    test_pe64_parse();
    test_pe_invalid();

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf("\n");
    return tests_failed > 0 ? 1 : 0;
}
