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

static void test_parse_invalid(void) {
    neverc_plan9_file_t f;

    /* Too short */
    uint8_t short_buf[4] = {0};
    CHECK("parse_short_fails", neverc_plan9_parse(&f, short_buf, 4) != 0);

    /* Bad magic */
    uint8_t bad[32];
    memset(bad, 0, 32);
    put32be(bad, 0xDEADBEEF);
    CHECK("parse_bad_magic", neverc_plan9_parse(&f, bad, 32) != 0);
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
    }

    neverc_plan9_close(&f);
    free(buf);
}

int main(void) {
    test_magic();
    test_parse_386();
    test_parse_invalid();
    test_section_data_bounds();

    printf("%d/%d tests passed\n", tests_passed, tests_run);
    if (tests_failed > 0)
        printf("%d tests FAILED\n", tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
