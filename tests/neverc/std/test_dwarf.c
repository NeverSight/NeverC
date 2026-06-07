/*
 * DWARF parser test suite.
 * Tests with synthesized minimal DWARF v4 debug data.
 */
#include "neverc/std/debug/dwarf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

#define CHECK(name, cond) do { \
    tests_run++; \
    if (cond) { tests_passed++; } \
    else { tests_failed++; printf("  FAIL: %s\n", name); } \
} while(0)

/*
 * Synthesize minimal DWARF v4 sections:
 *
 * .debug_str:
 *   offset 0: "test.c\0"
 *   offset 7: "main\0"
 *   offset 12: "/home/user\0"
 *   offset 23: "NeverC 1.0\0"
 *
 * .debug_abbrev:
 *   [1] DW_TAG_compile_unit, has_children=1
 *       DW_AT_name (DW_FORM_strp)
 *       DW_AT_comp_dir (DW_FORM_strp)
 *       DW_AT_producer (DW_FORM_strp)
 *       DW_AT_low_pc (DW_FORM_addr)
 *       DW_AT_high_pc (DW_FORM_data4)
 *       (0,0)
 *   [2] DW_TAG_subprogram, has_children=0
 *       DW_AT_name (DW_FORM_strp)
 *       DW_AT_low_pc (DW_FORM_addr)
 *       DW_AT_high_pc (DW_FORM_data4)
 *       (0,0)
 *   (0) terminator
 *
 * .debug_info:
 *   CompUnit header: length=XX, version=4, abbrev_offset=0, addr_size=8
 *   DIE [1] compile_unit:
 *       name -> strp(0) = "test.c"
 *       comp_dir -> strp(12) = "/home/user"
 *       producer -> strp(23) = "NeverC 1.0"
 *       low_pc -> 0x401000
 *       high_pc -> 0x100 (offset)
 *   DIE [2] subprogram:
 *       name -> strp(7) = "main"
 *       low_pc -> 0x401000
 *       high_pc -> 0x50 (offset)
 *   (0) end of children
 */

/* LEB128 encoding helper */
static int write_uleb128(uint8_t *buf, uint64_t val) {
    int n = 0;
    do {
        uint8_t b = (uint8_t)(val & 0x7f);
        val >>= 7;
        if (val) b |= 0x80;
        buf[n++] = b;
    } while (val);
    return n;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
    p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static void put16(uint8_t *p, uint16_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8);
}
static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v); put32(p+4, (uint32_t)(v>>32));
}

static int build_debug_str(uint8_t *buf) {
    int off = 0;
    memcpy(buf + off, "test.c", 7); off += 7;      /* offset 0 */
    memcpy(buf + off, "main", 5); off += 5;         /* offset 7 */
    memcpy(buf + off, "/home/user", 11); off += 11; /* offset 12 */
    memcpy(buf + off, "NeverC 1.0", 11); off += 11; /* offset 23 */
    return off;
}

static int build_debug_abbrev(uint8_t *buf) {
    int off = 0;

    /* Abbrev [1]: DW_TAG_compile_unit, has_children */
    off += write_uleb128(buf + off, 1);    /* code */
    off += write_uleb128(buf + off, 0x11); /* DW_TAG_compile_unit */
    buf[off++] = 1;                         /* has_children */
    off += write_uleb128(buf + off, 0x03); /* DW_AT_name */
    off += write_uleb128(buf + off, 0x0e); /* DW_FORM_strp */
    off += write_uleb128(buf + off, 0x1b); /* DW_AT_comp_dir */
    off += write_uleb128(buf + off, 0x0e); /* DW_FORM_strp */
    off += write_uleb128(buf + off, 0x25); /* DW_AT_producer */
    off += write_uleb128(buf + off, 0x0e); /* DW_FORM_strp */
    off += write_uleb128(buf + off, 0x11); /* DW_AT_low_pc */
    off += write_uleb128(buf + off, 0x01); /* DW_FORM_addr */
    off += write_uleb128(buf + off, 0x12); /* DW_AT_high_pc */
    off += write_uleb128(buf + off, 0x06); /* DW_FORM_data4 */
    buf[off++] = 0; buf[off++] = 0;        /* attr terminator */

    /* Abbrev [2]: DW_TAG_subprogram, no children */
    off += write_uleb128(buf + off, 2);    /* code */
    off += write_uleb128(buf + off, 0x2e); /* DW_TAG_subprogram */
    buf[off++] = 0;                         /* no children */
    off += write_uleb128(buf + off, 0x03); /* DW_AT_name */
    off += write_uleb128(buf + off, 0x0e); /* DW_FORM_strp */
    off += write_uleb128(buf + off, 0x11); /* DW_AT_low_pc */
    off += write_uleb128(buf + off, 0x01); /* DW_FORM_addr */
    off += write_uleb128(buf + off, 0x12); /* DW_AT_high_pc */
    off += write_uleb128(buf + off, 0x06); /* DW_FORM_data4 */
    buf[off++] = 0; buf[off++] = 0;        /* attr terminator */

    /* Table terminator */
    buf[off++] = 0;
    return off;
}

static int build_debug_info(uint8_t *buf) {
    int off = 0;

    /* CU header: unit_length (placeholder), version=4, abbrev_offset=0, addr_size=8 */
    int length_off = off;
    off += 4;              /* unit_length (fill later) */
    put16(buf + off, 4);   /* version */
    off += 2;
    put32(buf + off, 0);   /* debug_abbrev_offset */
    off += 4;
    buf[off++] = 8;        /* address_size */

    int die_start = off;

    /* DIE [1] compile_unit */
    off += write_uleb128(buf + off, 1); /* abbrev code */
    put32(buf + off, 0);   /* DW_AT_name = strp(0) "test.c" */
    off += 4;
    put32(buf + off, 12);  /* DW_AT_comp_dir = strp(12) "/home/user" */
    off += 4;
    put32(buf + off, 23);  /* DW_AT_producer = strp(23) "NeverC 1.0" */
    off += 4;
    put64(buf + off, 0x401000); /* DW_AT_low_pc */
    off += 8;
    put32(buf + off, 0x100);    /* DW_AT_high_pc (offset) */
    off += 4;

    /* DIE [2] subprogram */
    off += write_uleb128(buf + off, 2); /* abbrev code */
    put32(buf + off, 7);   /* DW_AT_name = strp(7) "main" */
    off += 4;
    put64(buf + off, 0x401000); /* DW_AT_low_pc */
    off += 8;
    put32(buf + off, 0x50);     /* DW_AT_high_pc (offset) */
    off += 4;

    /* End of children (null entry) */
    buf[off++] = 0;

    /* Fill unit_length = total - 4 */
    put32(buf + length_off, (uint32_t)(off - 4));

    (void)die_start;
    return off;
}

typedef struct {
    int count;
    const char *cu_name;
    const char *cu_comp_dir;
    const char *cu_producer;
    uint64_t cu_low_pc;
    uint64_t cu_high_pc;
    int cu_high_pc_is_offset;
    const char *func_name;
    uint64_t func_low_pc;
} walk_ctx_t;

static int walk_cb(const neverc_dwarf_entry_t *e, void *user) {
    walk_ctx_t *ctx = (walk_ctx_t *)user;
    ctx->count++;
    if (e->tag == NEVERC_DW_TAG_compile_unit) {
        ctx->cu_name = e->name;
        ctx->cu_comp_dir = e->comp_dir;
        ctx->cu_producer = e->producer;
        ctx->cu_low_pc = e->low_pc;
        ctx->cu_high_pc = e->high_pc;
        ctx->cu_high_pc_is_offset = e->high_pc_is_offset;
    } else if (e->tag == NEVERC_DW_TAG_subprogram) {
        ctx->func_name = e->name;
        ctx->func_low_pc = e->low_pc;
    }
    return 0;
}

static void test_dwarf_parse(void) {
    uint8_t str_buf[256], abbrev_buf[256], info_buf[512];
    int str_len = build_debug_str(str_buf);
    int abbrev_len = build_debug_abbrev(abbrev_buf);
    int info_len = build_debug_info(info_buf);

    neverc_dwarf_data_t d;
    int rc = neverc_dwarf_init(&d, info_buf, (size_t)info_len,
                                abbrev_buf, (size_t)abbrev_len,
                                str_buf, (size_t)str_len);
    CHECK("init_success", rc == 0);

    /* Test comp unit header parsing */
    neverc_dwarf_comp_unit_header_t hdr;
    rc = neverc_dwarf_parse_comp_unit(&d, 0, &hdr);
    CHECK("cu_header_parse", rc == 0);
    CHECK("cu_version_4", hdr.version == 4);
    CHECK("cu_addr_size_8", hdr.address_size == 8);
    CHECK("cu_not_64bit", hdr.is_64bit == 0);

    /* Test string access */
    const char *s = neverc_dwarf_get_string(&d, 0);
    CHECK("string_0", strcmp(s, "test.c") == 0);
    s = neverc_dwarf_get_string(&d, 7);
    CHECK("string_7", strcmp(s, "main") == 0);
    s = neverc_dwarf_get_string(&d, 12);
    CHECK("string_12", strcmp(s, "/home/user") == 0);

    /* Test walking entries */
    walk_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    rc = neverc_dwarf_walk_entries(&d, walk_cb, &ctx);
    CHECK("walk_success", rc == 0);
    CHECK("walk_count_2", ctx.count == 2);
    CHECK("cu_name_test_c", ctx.cu_name && strcmp(ctx.cu_name, "test.c") == 0);
    CHECK("cu_comp_dir", ctx.cu_comp_dir && strcmp(ctx.cu_comp_dir, "/home/user") == 0);
    CHECK("cu_producer", ctx.cu_producer && strcmp(ctx.cu_producer, "NeverC 1.0") == 0);
    CHECK("cu_low_pc", ctx.cu_low_pc == 0x401000);
    CHECK("cu_high_pc", ctx.cu_high_pc == 0x100);
    CHECK("cu_high_pc_offset", ctx.cu_high_pc_is_offset == 1);
    CHECK("func_name_main", ctx.func_name && strcmp(ctx.func_name, "main") == 0);
    CHECK("func_low_pc", ctx.func_low_pc == 0x401000);

    /* Test tag/attr strings */
    CHECK("tag_compile_unit", strcmp(neverc_dwarf_tag_string(NEVERC_DW_TAG_compile_unit),
                                     "DW_TAG_compile_unit") == 0);
    CHECK("tag_subprogram", strcmp(neverc_dwarf_tag_string(NEVERC_DW_TAG_subprogram),
                                   "DW_TAG_subprogram") == 0);
    CHECK("attr_name", strcmp(neverc_dwarf_attr_string(NEVERC_DW_AT_name),
                               "DW_AT_name") == 0);

    neverc_dwarf_free(&d);
}

static void test_dwarf_empty(void) {
    neverc_dwarf_data_t d;
    neverc_dwarf_init(&d, NULL, 0, NULL, 0, NULL, 0);

    walk_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    int rc = neverc_dwarf_walk_entries(&d, walk_cb, &ctx);
    CHECK("empty_walk_ok", rc == 0);
    CHECK("empty_count_0", ctx.count == 0);

    neverc_dwarf_free(&d);
}

int main(void) {
    printf("=== NeverC debug/dwarf Tests ===\n\n");

    test_dwarf_parse();
    test_dwarf_empty();

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf("\n");
    return tests_failed > 0 ? 1 : 0;
}
