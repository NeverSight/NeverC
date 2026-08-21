/*
 * Plan 9 a.out parser test suite.
 * Tests with synthesized minimal Plan 9 a.out binary data.
 */
#include "neverc/std/debug/plan9obj.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define CHECK(name, cond) do { \
    tests_run++; \
    if (cond) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: %s\n", name); } \
} while(0)

static void put32be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8);  p[3] = (uint8_t)v;
}
static void put64be(uint8_t *p, uint64_t v) {
    put32be(p, (uint32_t)(v >> 32));
    put32be(p + 4, (uint32_t)v);
}

/*
 * Build a minimal Plan 9 386 a.out binary:
 *   Header: 32 bytes (8 x uint32 big-endian)
 *   text section: 16 bytes
 *   data section: 8 bytes
 *   syms section: variable (value(4) + type(1) + name + NUL per symbol)
 *   spsz/pcsz: 0 bytes each
 */
static uint8_t *build_plan9_386(size_t *out_len) {
    /* symbol: 4-byte value + 1-byte type + "main\0" */
    uint32_t sym_entry_len = 4 + 1 + 5; /* 10 bytes */
    size_t total = 32 + 16 + 8 + sym_entry_len;
    uint8_t *buf = (uint8_t *)calloc(total, 1);
    if (!buf) return NULL;

    put32be(buf + 0,  NEVERC_PLAN9_MAGIC386);  /* magic */
    put32be(buf + 4,  16);                      /* text size */
    put32be(buf + 8,  8);                       /* data size */
    put32be(buf + 12, 0);                       /* bss */
    put32be(buf + 16, sym_entry_len);           /* syms size */
    put32be(buf + 20, 0x1020);                  /* entry */
    put32be(buf + 24, 0);                       /* spsz */
    put32be(buf + 28, 0);                       /* pcsz */

    /* text section data at offset 32 */
    memset(buf + 32, 0x90, 16); /* NOP fill */

    /* data section at offset 48 */
    memset(buf + 48, 0xAA, 8);

    /* syms section at offset 56 */
    put32be(buf + 56, 0x1020);  /* symbol value */
    buf[60] = 'T';              /* symbol type */
    memcpy(buf + 61, "main", 5); /* name + NUL */

    *out_len = total;
    return buf;
}

static uint8_t *build_plan9_paths(size_t *out_len) {
    const uint32_t syms_len = 30;
    size_t total = 32 + syms_len;
    uint8_t *buf = (uint8_t *)calloc(total, 1);
    if (!buf) return NULL;
    put32be(buf, NEVERC_PLAN9_MAGIC386);
    put32be(buf + 16, syms_len);

    uint8_t *p = buf + 32;
    put32be(p, 1);
    p[4] = 'f';
    memcpy(p + 5, "usr", 4);
    p += 9;
    put32be(p, 2);
    p[4] = 'f';
    memcpy(p + 5, "lib", 4);
    p += 9;
    put32be(p, 0);
    p[4] = 'z';
    p[5] = 0;
    p[6] = 0; p[7] = 1;
    p[8] = 0; p[9] = 2;
    p[10] = 0; p[11] = 0;

    *out_len = total;
    return buf;
}

static void test_magic(void) {
    CHECK("magic_386_valid",
          neverc_plan9_valid_magic(NEVERC_PLAN9_MAGIC386) == 1);
    CHECK("magic_amd64_valid",
          neverc_plan9_valid_magic(NEVERC_PLAN9_MAGICAMD64) == 1);
    CHECK("magic_arm_valid",
          neverc_plan9_valid_magic(NEVERC_PLAN9_MAGICARM) == 1);
    CHECK("magic_invalid",
          neverc_plan9_valid_magic(0xDEADBEEF) == 0);
    CHECK("magic_zero",
          neverc_plan9_valid_magic(0) == 0);
    CHECK("reject fake 64-bit 386 magic",
          neverc_plan9_valid_magic(
              NEVERC_PLAN9_MAGIC386 | NEVERC_PLAN9_MAGIC64) == 0);
    CHECK("reject fake 64-bit ARM magic",
          neverc_plan9_valid_magic(
              NEVERC_PLAN9_MAGICARM | NEVERC_PLAN9_MAGIC64) == 0);
}

static void test_parse_386(void) {
    size_t len;
    uint8_t *buf = build_plan9_386(&len);
    CHECK("build_386_ok", buf != NULL);
    if (!buf) return;

    neverc_plan9_file_t f;
    int rc = neverc_plan9_parse(&f, buf, len);
    CHECK("parse_386_ok", rc == 0);
    CHECK("parse_magic", f.magic == NEVERC_PLAN9_MAGIC386);
    CHECK("parse_bss", f.bss == 0);
    CHECK("parse_entry", f.entry == 0x1020);
    CHECK("parse_ptr_size", f.ptr_size == 4);
    CHECK("parse_load_addr", f.load_address == 0x1000);
    CHECK("parse_hdr_size", f.hdr_size == 32);
    CHECK("parse_num_sections", f.num_sections == 5);

    /* Section lookup */
    neverc_plan9_section_t *text = neverc_plan9_section(&f, "text");
    CHECK("section_text_found", text != NULL);
    if (text) {
        CHECK("text_size", text->size == 16);
        CHECK("text_offset", text->offset == 32);
    }

    neverc_plan9_section_t *data = neverc_plan9_section(&f, "data");
    CHECK("section_data_found", data != NULL);
    if (data) {
        CHECK("data_size", data->size == 8);
        CHECK("data_offset", data->offset == 48);
    }

    neverc_plan9_section_t *syms = neverc_plan9_section(&f, "syms");
    CHECK("section_syms_found", syms != NULL);

    neverc_plan9_section_t *none = neverc_plan9_section(&f, "nonexistent");
    CHECK("section_nonexistent_null", none == NULL);

    /* Section data read */
    if (text) {
        uint8_t tbuf[16];
        int rd = neverc_plan9_section_data(&f, text, tbuf, 16);
        CHECK("read_text_ok", rd == 0);
        CHECK("text_first_byte", tbuf[0] == 0x90);
    }

    /* Symbol table */
    rc = neverc_plan9_symbols(&f);
    CHECK("symbols_ok", rc == 0);
    CHECK("num_symbols", f.num_symbols == 1);
    if (f.num_symbols >= 1) {
        CHECK("sym_value", f.symbols[0].value == 0x1020);
        CHECK("sym_type", f.symbols[0].type == 'T');
        CHECK("sym_name", strcmp(f.symbols[0].name, "main") == 0);
    }

    neverc_plan9_close(&f);
    free(buf);
}

static void test_parse_amd64(void) {
    uint8_t buf[40] = {0};
    put32be(buf, NEVERC_PLAN9_MAGICAMD64);
    put32be(buf + 20, 0x1234);
    put64be(buf + 32, UINT64_C(0x123456789abcdef0));

    neverc_plan9_file_t f;
    CHECK("parse AMD64", neverc_plan9_parse(&f, buf, sizeof(buf)) == 0);
    CHECK("AMD64 pointer size", f.ptr_size == 8);
    CHECK("AMD64 entry", f.entry == UINT64_C(0x123456789abcdef0));
    CHECK("AMD64 load address", f.load_address == UINT64_C(0x200000));
    CHECK("AMD64 header size", f.hdr_size == 40);
    CHECK("empty symbol table succeeds", neverc_plan9_symbols(&f) == 0);
    CHECK("empty symbol count", f.num_symbols == 0);
    neverc_plan9_close(&f);
}

static void test_path_symbols(void) {
    size_t len = 0;
    uint8_t *buf = build_plan9_paths(&len);
    CHECK("build path symbols", buf != NULL);
    if (!buf) return;

    neverc_plan9_file_t f;
    CHECK("parse path symbols", neverc_plan9_parse(&f, buf, len) == 0);
    CHECK("decode path symbols", neverc_plan9_symbols(&f) == 0);
    CHECK("decoded path symbol count", f.num_symbols == 3);
    CHECK("decoded z path",
          f.num_symbols == 3 &&
              strcmp(f.symbols[2].name, "usr/lib") == 0);

    neverc_plan9_section_t *syms = neverc_plan9_section(&f, "syms");
    CHECK("path symbol section exists", syms != NULL);
    if (syms) {
        f.data[syms->offset + 27] = 3;
        CHECK("reject unknown filename code",
              neverc_plan9_symbols(&f) == -1);
        CHECK("failed replacement is atomic",
              f.num_symbols == 3 &&
                  strcmp(f.symbols[2].name, "usr/lib") == 0);
    }
    neverc_plan9_close(&f);
    free(buf);
}

static void test_path_expansion_cap(void) {
    const size_t copies = 5000;
    const char component[] =
        "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx";
    uint32_t f_len = 4 + 1 + (uint32_t)sizeof(component);
    uint32_t z_len = 4 + 1 + 1 + (uint32_t)(copies * 2U) + 2U;
    uint32_t syms_len = f_len + z_len;
    size_t total = 32 + (size_t)syms_len;
    uint8_t *buf = (uint8_t *)calloc(total, 1);
    CHECK("build oversized z path", buf != NULL);
    if (!buf) return;

    put32be(buf, NEVERC_PLAN9_MAGIC386);
    put32be(buf + 16, syms_len);
    uint8_t *p = buf + 32;
    put32be(p, 1);
    p[4] = 'f';
    memcpy(p + 5, component, sizeof(component));
    p += f_len;
    put32be(p, 0);
    p[4] = 'z';
    p[5] = 0;
    for (size_t i = 0; i < copies; i++) {
        p[6 + i * 2] = 0;
        p[7 + i * 2] = 1;
    }
    p[6 + copies * 2] = 0;
    p[7 + copies * 2] = 0;

    neverc_plan9_file_t f;
    CHECK("parse oversized z path",
          neverc_plan9_parse(&f, buf, total) == 0);
    CHECK("reject oversized z expansion",
          neverc_plan9_symbols(&f) == -1);
    neverc_plan9_close(&f);
    free(buf);
}

static void test_parse_invalid(void) {
    neverc_plan9_file_t f;

    /* Too short */
    uint8_t short_buf[4] = {0};
    CHECK("parse_short_fails", neverc_plan9_parse(&f, short_buf, 4) != 0);

    uint8_t trunc31[31] = {0};
    put32be(trunc31, NEVERC_PLAN9_MAGIC386);
    CHECK("reject truncated 386 header",
          neverc_plan9_parse(&f, trunc31, sizeof(trunc31)) != 0);

    /* Bad magic */
    uint8_t bad[32];
    memset(bad, 0, 32);
    put32be(bad, 0xDEADBEEF);
    CHECK("parse_bad_magic", neverc_plan9_parse(&f, bad, 32) != 0);
    CHECK("parse null buffer", neverc_plan9_parse(&f, NULL, 32) == -1);
    CHECK("parse null destination",
          neverc_plan9_parse(NULL, bad, sizeof(bad)) == -1);

    size_t len = 0;
    uint8_t *buf = build_plan9_386(&len);
    CHECK("build malformed symbol fixture", buf != NULL);
    if (!buf) return;
    put32be(buf + 16, 9);
    CHECK("parse structurally bounded symbol section",
          neverc_plan9_parse(&f, buf, len) == 0);
    CHECK("reject unterminated symbol name", neverc_plan9_symbols(&f) == -1);
    neverc_plan9_close(&f);

    put32be(buf + 4, UINT32_MAX);
    CHECK("reject section sizes beyond file",
          neverc_plan9_parse(&f, buf, len) == -1);

    uint8_t wrap[40] = {0};
    put32be(wrap, NEVERC_PLAN9_MAGIC386);
    put32be(wrap + 4, 0xFFFFFFF0u);
    put32be(wrap + 8, 0x20u);
    CHECK("reject text plus data past EOF without wrap",
          neverc_plan9_parse(&f, wrap, sizeof(wrap)) == -1);

    uint8_t trunc64[32] = {0};
    put32be(trunc64, NEVERC_PLAN9_MAGICAMD64);
    CHECK("reject truncated AMD64 header",
          neverc_plan9_parse(&f, trunc64, sizeof(trunc64)) == -1);

    uint8_t fval[32 + 7] = {0};
    put32be(fval, NEVERC_PLAN9_MAGIC386);
    put32be(fval + 16, 7);
    put32be(fval + 32, 0x10000);
    fval[36] = 'f';
    memcpy(fval + 37, "x", 2);
    CHECK("parse oversized filename index header",
          neverc_plan9_parse(&f, fval, sizeof(fval)) == 0);
    CHECK("reject filename index above 16 bits",
          neverc_plan9_symbols(&f) == -1);
    neverc_plan9_close(&f);
    free(buf);
}

static void test_section_data_bounds(void) {
    size_t len;
    uint8_t *buf = build_plan9_386(&len);
    if (!buf) return;

    neverc_plan9_file_t f;
    neverc_plan9_parse(&f, buf, len);

    neverc_plan9_section_t *text = neverc_plan9_section(&f, "text");
    if (text) {
        uint8_t small[4];
        CHECK("read_too_small", neverc_plan9_section_data(&f, text, small, 4) != 0);
        CHECK("read null destination",
              neverc_plan9_section_data(&f, text, NULL, 16) == -1);
    }

    neverc_plan9_close(&f);
    free(buf);
}

int main(void) {
    test_magic();
    test_parse_386();
    test_parse_amd64();
    test_path_symbols();
    test_path_expansion_cap();
    test_parse_invalid();
    test_section_data_bounds();

    printf("%d/%d tests passed\n", tests_passed, tests_run);
    if (tests_failed > 0)
        printf("%d tests FAILED\n", tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
