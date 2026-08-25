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
    uint64_t cu_byte_size;
    uint64_t cu_encoding;
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
        ctx->cu_byte_size = e->byte_size;
        ctx->cu_encoding = e->encoding;
    } else if (e->tag == NEVERC_DW_TAG_subprogram) {
        ctx->func_name = e->name;
        ctx->func_low_pc = e->low_pc;
    }
    return 0;
}

static int ignore_entry_cb(const neverc_dwarf_entry_t *e, void *user) {
    (void)e;
    (void)user;
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
    CHECK("string past table is NULL", neverc_dwarf_get_string(&d, 99) == NULL);
    CHECK("string on NULL data is NULL",
          neverc_dwarf_get_string(NULL, 0) == NULL);

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
    CHECK("empty_init_ok",
          neverc_dwarf_init(&d, NULL, 0, NULL, 0, NULL, 0) == 0);

    walk_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    int rc = neverc_dwarf_walk_entries(&d, walk_cb, &ctx);
    CHECK("empty_walk_ok", rc == 0);
    CHECK("empty_count_0", ctx.count == 0);

    neverc_dwarf_free(&d);
}

static void build_v4_header(uint8_t *buf, uint32_t unit_length) {
    put32(buf, unit_length);
    put16(buf + 4, 4);
    put32(buf + 6, 0);
    buf[10] = 8;
}

static void build_v5_compile_header(uint8_t *buf, uint32_t unit_length) {
    put32(buf, unit_length);
    put16(buf + 4, 5);
    buf[6] = NEVERC_DW_UT_compile;
    buf[7] = 8;
    put32(buf + 8, 0);
}

static void test_dwarf_implicit_const(void) {
    printf("[implicit_const]\n");
    static const uint8_t abbrev[] = {
        1, NEVERC_DW_TAG_compile_unit, 0,
        NEVERC_DW_AT_byte_size, NEVERC_DW_FORM_implicit_const, 7,
        NEVERC_DW_AT_encoding, NEVERC_DW_FORM_implicit_const, 5,
        NEVERC_DW_AT_high_pc, NEVERC_DW_FORM_implicit_const, 0x20,
        0, 0, 0
    };
    uint8_t info[13] = {0};
    build_v5_compile_header(info, 9);
    info[12] = 1;

    neverc_dwarf_data_t d;
    walk_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    CHECK("implicit const init",
          neverc_dwarf_init(&d, info, sizeof(info),
                            abbrev, sizeof(abbrev), NULL, 0) == 0);
    CHECK("implicit const walk",
          neverc_dwarf_walk_entries(&d, walk_cb, &ctx) == 0);
    CHECK("implicit byte size", ctx.cu_byte_size == 7);
    CHECK("implicit encoding", ctx.cu_encoding == 5);
    CHECK("implicit high pc", ctx.cu_high_pc == 0x20);
    CHECK("implicit high pc is offset", ctx.cu_high_pc_is_offset == 1);

    uint8_t v4_info[12] = {0};
    build_v4_header(v4_info, 8);
    v4_info[11] = 1;
    CHECK("implicit const rejected before DWARF v5",
          neverc_dwarf_init(&d, v4_info, sizeof(v4_info),
                            abbrev, sizeof(abbrev), NULL, 0) == 0 &&
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) < 0);
    neverc_dwarf_free(&d);
}

static void test_dwarf_line_string(void) {
    printf("[line_string]\n");
    static const uint8_t abbrev[] = {
        1, NEVERC_DW_TAG_compile_unit, 0,
        NEVERC_DW_AT_name, NEVERC_DW_FORM_line_strp,
        0, 0, 0
    };
    uint8_t info[17] = {0};
    build_v5_compile_header(info, 13);
    info[12] = 1;
    put32(info + 13, 0);

    neverc_dwarf_data_t d;
    walk_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    CHECK("line string init",
          neverc_dwarf_init(&d, info, sizeof(info),
                            abbrev, sizeof(abbrev), NULL, 0) == 0);
    CHECK("missing line string table is rejected",
          neverc_dwarf_walk_entries(&d, walk_cb, &ctx) < 0);

    static const uint8_t line_str[] = "line.c";
    d.debug_line_str = line_str;
    d.debug_line_str_len = sizeof(line_str);
    memset(&ctx, 0, sizeof(ctx));
    CHECK("attached line string resolves",
          neverc_dwarf_walk_entries(&d, walk_cb, &ctx) == 0);
    CHECK("attached line string value",
          ctx.cu_name && strcmp(ctx.cu_name, "line.c") == 0);

    static const uint8_t bad_line_str[] = {'x'};
    d.debug_line_str = bad_line_str;
    d.debug_line_str_len = sizeof(bad_line_str);
    CHECK("unterminated line string rejected",
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) < 0);

    static const uint8_t indexed_abbrev[] = {
        1, NEVERC_DW_TAG_compile_unit, 0,
        NEVERC_DW_AT_name, NEVERC_DW_FORM_strx1,
        0, 0, 0
    };
    uint8_t indexed_info[14] = {0};
    build_v5_compile_header(indexed_info, 10);
    indexed_info[12] = 1;
    indexed_info[13] = 0;
    CHECK("indexed string fixture initializes",
          neverc_dwarf_init(&d, indexed_info, sizeof(indexed_info),
                            indexed_abbrev, sizeof(indexed_abbrev),
                            NULL, 0) == 0);
    CHECK("indexed string without offsets table is rejected",
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) < 0);
    neverc_dwarf_free(&d);
}

typedef struct {
    uint64_t refs[2];
    int count;
} ref_walk_ctx_t;

static int ref_walk_cb(const neverc_dwarf_entry_t *entry, void *user) {
    ref_walk_ctx_t *ctx = (ref_walk_ctx_t *)user;
    if (entry->tag == NEVERC_DW_TAG_compile_unit && ctx->count < 2)
        ctx->refs[ctx->count++] = entry->type_ref;
    return 0;
}

static void test_dwarf_cu_relative_refs(void) {
    printf("[cu_relative_refs]\n");
    static const uint8_t abbrev[] = {
        1, NEVERC_DW_TAG_compile_unit, 0,
        NEVERC_DW_AT_type, NEVERC_DW_FORM_ref4,
        0, 0, 0
    };
    uint8_t info[32] = {0};
    build_v4_header(info, 12);
    info[11] = 1;
    put32(info + 12, 11);
    build_v4_header(info + 16, 12);
    info[27] = 1;
    put32(info + 28, 11);

    neverc_dwarf_data_t d;
    ref_walk_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    CHECK("two-CU reference fixture initializes",
          neverc_dwarf_init(&d, info, sizeof(info),
                            abbrev, sizeof(abbrev), NULL, 0) == 0);
    CHECK("two-CU references walk",
          neverc_dwarf_walk_entries(&d, ref_walk_cb, &ctx) == 0);
    CHECK("CU-relative references include each CU base",
          ctx.count == 2 && ctx.refs[0] == 11 && ctx.refs[1] == 27);
    neverc_dwarf_free(&d);
}

static void test_dwarf_malformed(void) {
    printf("[malformed]\n");
    neverc_dwarf_data_t d;
    neverc_dwarf_comp_unit_header_t hdr;
    uint8_t short_info[16] = {0};

    CHECK("init rejects NULL destination",
          neverc_dwarf_init(NULL, NULL, 0, NULL, 0, NULL, 0) < 0);
    memset(&d, 0xa5, sizeof(d));
    CHECK("init rejects missing nonempty section",
          neverc_dwarf_init(&d, NULL, 1, NULL, 0, NULL, 0) < 0);
    CHECK("failed init leaves object freeable",
          d.debug_info == NULL && d.debug_abbrev == NULL &&
          d.debug_str == NULL && d.abbrevs == NULL &&
          d.abbrev_count == 0);
    neverc_dwarf_free(&d);
    CHECK("parse rejects NULL data",
          neverc_dwarf_parse_comp_unit(NULL, 0, &hdr) < 0);
    CHECK("parse rejects NULL output",
          neverc_dwarf_parse_comp_unit(&d, 0, NULL) < 0);

    for (size_t len = 0; len < 11; len++) {
        CHECK("truncated CU header rejected",
              neverc_dwarf_init(&d, short_info, len, NULL, 0, NULL, 0) == 0 &&
              neverc_dwarf_parse_comp_unit(&d, 0, &hdr) < 0);
    }

    uint8_t oversized[11] = {0};
    build_v4_header(oversized, 100);
    CHECK("declared CU beyond section rejected",
          neverc_dwarf_init(&d, oversized, sizeof(oversized),
                            NULL, 0, NULL, 0) == 0 &&
          neverc_dwarf_parse_comp_unit(&d, 0, &hdr) < 0);

    uint8_t dwarf64_truncated[11] = {0xff, 0xff, 0xff, 0xff};
    CHECK("truncated DWARF64 length rejected",
          neverc_dwarf_init(&d, dwarf64_truncated,
                            sizeof(dwarf64_truncated),
                            NULL, 0, NULL, 0) == 0 &&
          neverc_dwarf_parse_comp_unit(&d, 0, &hdr) < 0);

    uint8_t reserved_len[12] = {0};
    put32(reserved_len, 0xFFFFFFF0U);
    put16(reserved_len + 4, 4);
    CHECK("reserved DWARF initial length rejected",
          neverc_dwarf_init(&d, reserved_len, sizeof(reserved_len),
                            NULL, 0, NULL, 0) == 0 &&
          neverc_dwarf_parse_comp_unit(&d, 0, &hdr) < 0);

    uint8_t info_buf[512];
    int info_len = build_debug_info(info_buf);
    uint8_t valid_abbrev[256];
    int valid_abbrev_len = build_debug_abbrev(valid_abbrev);
    uint8_t overlong_uleb[10];
    memset(overlong_uleb, 0x80, sizeof(overlong_uleb));
    CHECK("overlong abbrev ULEB128 rejected",
          neverc_dwarf_init(&d, info_buf, (size_t)info_len,
                            overlong_uleb, sizeof(overlong_uleb),
                            NULL, 0) == 0 &&
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) < 0);

    uint8_t overflowing_uleb[10];
    memset(overflowing_uleb, 0x80, sizeof(overflowing_uleb));
    overflowing_uleb[9] = 0x02;
    CHECK("overflowing tenth ULEB128 byte rejected",
          neverc_dwarf_init(&d, info_buf, (size_t)info_len,
                            overflowing_uleb, sizeof(overflowing_uleb),
                            NULL, 0) == 0 &&
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) < 0);

    uint8_t overlong_sleb_abbrev[16] = {
        1, NEVERC_DW_TAG_compile_unit, 0,
        NEVERC_DW_AT_byte_size, NEVERC_DW_FORM_implicit_const
    };
    memset(overlong_sleb_abbrev + 5, 0x80, 10);
    uint8_t v5_sleb_info[13] = {0};
    build_v5_compile_header(v5_sleb_info, 9);
    v5_sleb_info[12] = 1;
    CHECK("overlong abbreviation SLEB128 rejected",
          neverc_dwarf_init(&d, v5_sleb_info, sizeof(v5_sleb_info),
                            overlong_sleb_abbrev,
                            sizeof(overlong_sleb_abbrev),
                            NULL, 0) == 0 &&
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) < 0);

    uint8_t overflowing_sleb_abbrev[16] = {
        1, NEVERC_DW_TAG_compile_unit, 0,
        NEVERC_DW_AT_byte_size, NEVERC_DW_FORM_implicit_const
    };
    memset(overflowing_sleb_abbrev + 5, 0x80, 9);
    overflowing_sleb_abbrev[14] = 0x02;
    CHECK("overflowing tenth SLEB128 byte rejected",
          neverc_dwarf_init(&d, v5_sleb_info, sizeof(v5_sleb_info),
                            overflowing_sleb_abbrev,
                            sizeof(overflowing_sleb_abbrev),
                            NULL, 0) == 0 &&
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) < 0);

    static const uint8_t strp_abbrev[] = {
        1, NEVERC_DW_TAG_compile_unit, 0,
        NEVERC_DW_AT_name, NEVERC_DW_FORM_strp,
        0, 0, 0
    };
    uint8_t truncated_strp[14] = {0};
    build_v4_header(truncated_strp, 10);
    truncated_strp[11] = 1;
    truncated_strp[12] = 0;
    truncated_strp[13] = 0;
    CHECK("truncated fixed-width form rejected",
          neverc_dwarf_init(&d, truncated_strp, sizeof(truncated_strp),
                            strp_abbrev, sizeof(strp_abbrev),
                            NULL, 0) == 0 &&
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) < 0);

    uint8_t external_string_info[16] = {0};
    static const uint8_t unterminated_debug_str[] = {'a', 'b'};
    build_v4_header(external_string_info, 12);
    external_string_info[11] = 1;
    CHECK("unterminated external string rejected",
          neverc_dwarf_init(&d, external_string_info,
                            sizeof(external_string_info),
                            strp_abbrev, sizeof(strp_abbrev),
                            unterminated_debug_str,
                            sizeof(unterminated_debug_str)) == 0 &&
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) < 0);

    static const uint8_t exprloc_abbrev[] = {
        1, NEVERC_DW_TAG_compile_unit, 0,
        NEVERC_DW_AT_location, NEVERC_DW_FORM_exprloc,
        0, 0, 0
    };
    uint8_t oversized_exprloc[13] = {0};
    build_v4_header(oversized_exprloc, 9);
    oversized_exprloc[11] = 1;
    oversized_exprloc[12] = 0x7f;
    CHECK("oversized exprloc rejected",
          neverc_dwarf_init(&d, oversized_exprloc,
                            sizeof(oversized_exprloc),
                            exprloc_abbrev, sizeof(exprloc_abbrev),
                            NULL, 0) == 0 &&
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) < 0);

    static const uint8_t addrx_abbrev[] = {
        1, NEVERC_DW_TAG_compile_unit, 0,
        NEVERC_DW_AT_low_pc, NEVERC_DW_FORM_addrx1,
        0, 0, 0
    };
    uint8_t addrx_info[13] = {0};
    build_v4_header(addrx_info, 9);
    addrx_info[11] = 1;
    addrx_info[12] = 0;
    CHECK("addrx without .debug_addr rejected",
          neverc_dwarf_init(&d, addrx_info, sizeof(addrx_info),
                            addrx_abbrev, sizeof(addrx_abbrev),
                            NULL, 0) == 0 &&
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) < 0);

    static const uint8_t inline_string_abbrev[] = {
        1, NEVERC_DW_TAG_compile_unit, 0,
        NEVERC_DW_AT_name, NEVERC_DW_FORM_string,
        0, 0, 0
    };
    uint8_t unterminated_string[14] = {0};
    build_v4_header(unterminated_string, 10);
    unterminated_string[11] = 1;
    unterminated_string[12] = 'a';
    unterminated_string[13] = 'b';
    CHECK("unterminated inline string rejected",
          neverc_dwarf_init(&d, unterminated_string,
                            sizeof(unterminated_string),
                            inline_string_abbrev,
                            sizeof(inline_string_abbrev),
                            NULL, 0) == 0 &&
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) < 0);

    uint8_t missing_child_end[512];
    memcpy(missing_child_end, info_buf, (size_t)info_len);
    put32(missing_child_end, (uint32_t)(info_len - 5));
    CHECK("unterminated DIE children rejected",
          neverc_dwarf_init(&d, missing_child_end, (size_t)info_len,
                            valid_abbrev, (size_t)valid_abbrev_len,
                            NULL, 0) == 0 &&
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) < 0);

    uint8_t padded_info[520];
    uint8_t str_buf[256];
    int str_len = build_debug_str(str_buf);
    memcpy(padded_info, info_buf, (size_t)info_len);
    memset(padded_info + info_len, 0, 8);
    CHECK("trailing zero CU padding accepted",
          neverc_dwarf_init(&d, padded_info, (size_t)info_len + 8,
                            valid_abbrev, (size_t)valid_abbrev_len,
                            str_buf, (size_t)str_len) == 0 &&
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) == 0);

    uint8_t short_pad[520];
    memcpy(short_pad, info_buf, (size_t)info_len);
    short_pad[info_len] = 0;
    short_pad[info_len + 1] = 0;
    short_pad[info_len + 2] = 0;
    CHECK("trailing 3-byte zero padding accepted",
          neverc_dwarf_init(&d, short_pad, (size_t)info_len + 3,
                            valid_abbrev, (size_t)valid_abbrev_len,
                            str_buf, (size_t)str_len) == 0 &&
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) == 0);

    short_pad[info_len + 2] = 1;
    CHECK("trailing truncated nonzero bytes rejected",
          neverc_dwarf_init(&d, short_pad, (size_t)info_len + 3,
                            valid_abbrev, (size_t)valid_abbrev_len,
                            str_buf, (size_t)str_len) == 0 &&
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) < 0);

    static const uint8_t unterminated_str[] = {'a', 'b'};
    CHECK("unterminated debug_str get_string is NULL",
          neverc_dwarf_init(&d, info_buf, (size_t)info_len,
                            valid_abbrev, (size_t)valid_abbrev_len,
                            unterminated_str,
                            sizeof(unterminated_str)) == 0 &&
          neverc_dwarf_get_string(&d, 0) == NULL);
    neverc_dwarf_free(&d);
}

static void test_dwarf_v5_type_header(void) {
    printf("[v5_type_header]\n");
    uint8_t info[25] = {0};
    put32(info, 21);
    put16(info + 4, 5);
    info[6] = NEVERC_DW_UT_type;
    info[7] = 8;
    put32(info + 8, 0x1234);
    put64(info + 12, UINT64_C(0x1122334455667788));
    put32(info + 20, 0x18);
    info[24] = 0;

    neverc_dwarf_data_t d;
    neverc_dwarf_comp_unit_header_ex_t hdr;
    CHECK("v5 type init",
          neverc_dwarf_init(&d, info, sizeof(info), NULL, 0, NULL, 0) == 0);
    CHECK("v5 type header parse",
          neverc_dwarf_parse_comp_unit_ex(&d, 0, &hdr) == 0);
    CHECK("v5 unit type", hdr.unit_type == NEVERC_DW_UT_type);
    CHECK("v5 header size", hdr.header_size == 24);
    CHECK("v5 abbrev offset", hdr.abbrev_offset == 0x1234);
    CHECK("v5 type signature",
          hdr.type_signature == UINT64_C(0x1122334455667788));
    CHECK("v5 type offset", hdr.type_offset == 0x18);

    struct {
        neverc_dwarf_comp_unit_header_t header;
        uint64_t canary;
    } legacy = {{0}, UINT64_C(0xa5a5a5a5a5a5a5a5)};
    CHECK("legacy header parse",
          neverc_dwarf_parse_comp_unit(&d, 0, &legacy.header) == 0);
    CHECK("legacy header fields",
          legacy.header.version == 5 &&
          legacy.header.abbrev_offset == 0x1234 &&
          legacy.header.address_size == 8);
    CHECK("legacy header size preserved",
          legacy.canary == UINT64_C(0xa5a5a5a5a5a5a5a5));
    neverc_dwarf_free(&d);
}

static void put16be(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v;
}
static void put32be(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16);
    p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}
static void put64be(uint8_t *p, uint64_t v) {
    put32be(p, (uint32_t)(v >> 32));
    put32be(p + 4, (uint32_t)v);
}

static void test_dwarf_big_endian(void) {
    printf("[big_endian]\n");
    static const uint8_t abbrev[] = {
        1, NEVERC_DW_TAG_compile_unit, 0,
        NEVERC_DW_AT_name, NEVERC_DW_FORM_strp,
        NEVERC_DW_AT_low_pc, NEVERC_DW_FORM_addr,
        0, 0, 0
    };
    static const uint8_t str[] = "be.c";
    uint8_t info[24] = {0};
    put32be(info, 20);
    put16be(info + 4, 4);
    info[10] = 8;
    info[11] = 1;
    put64be(info + 16, 0x401000);

    neverc_dwarf_data_t d;
    neverc_dwarf_comp_unit_header_t hdr;
    CHECK("BE init",
          neverc_dwarf_init(&d, info, sizeof(info),
                            abbrev, sizeof(abbrev), str, sizeof(str)) == 0);
    CHECK("LE parse rejects BE header",
          neverc_dwarf_parse_comp_unit(&d, 0, &hdr) < 0);

    CHECK("BE mode set", neverc_dwarf_set_big_endian(&d, 1) == 0);
    CHECK("BE header parse",
          neverc_dwarf_parse_comp_unit(&d, 0, &hdr) == 0);
    CHECK("BE version and addr size",
          hdr.version == 4 && hdr.address_size == 8 && !hdr.is_64bit);

    walk_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    CHECK("BE walk", neverc_dwarf_walk_entries(&d, walk_cb, &ctx) == 0);
    CHECK("BE name", ctx.cu_name && strcmp(ctx.cu_name, "be.c") == 0);
    CHECK("BE low_pc", ctx.cu_low_pc == 0x401000);
    neverc_dwarf_free(&d);
}

static void test_dwarf_abbrev_offset_and_dwarf64(void) {
    printf("[abbrev_offset_dwarf64]\n");
    static const uint8_t abbrev[] = {
        1, NEVERC_DW_TAG_compile_unit, 0,
        NEVERC_DW_AT_name, NEVERC_DW_FORM_string,
        0, 0, 0
    };
    uint8_t info[16] = {0};
    /* unit_length 10 = 7-byte v4 header tail + abbrev 1 + "a\0".
     * has_children=0, so a trailing 0 would be a depth-0 terminator. */
    build_v4_header(info, 10);
    info[11] = 1;
    info[12] = 'a';
    info[13] = '\0';

    neverc_dwarf_data_t d;
    walk_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    CHECK("in-range abbrev offset walks",
          neverc_dwarf_init(&d, info, sizeof(info),
                            abbrev, sizeof(abbrev), NULL, 0) == 0 &&
          neverc_dwarf_walk_entries(&d, walk_cb, &ctx) == 0);
    CHECK("in-range abbrev name",
          ctx.cu_name && strcmp(ctx.cu_name, "a") == 0);

    put32(info + 6, (uint32_t)sizeof(abbrev) + 4U);
    CHECK("out-of-range abbrev offset rejected",
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) < 0);
    put32(info + 6, 0);

    neverc_dwarf_free(&d);
    uint8_t dwarf64[26] = {0};
    put32(dwarf64, 0xFFFFFFFFU);
    put64(dwarf64 + 4, 14);
    put16(dwarf64 + 12, 4);
    put64(dwarf64 + 14, 0);
    dwarf64[22] = 8;
    dwarf64[23] = 1;
    dwarf64[24] = 'z';
    dwarf64[25] = 0;

    neverc_dwarf_comp_unit_header_t hdr;
    CHECK("DWARF64 init",
          neverc_dwarf_init(&d, dwarf64, sizeof(dwarf64),
                            abbrev, sizeof(abbrev), NULL, 0) == 0);
    CHECK("DWARF64 header parse",
          neverc_dwarf_parse_comp_unit(&d, 0, &hdr) == 0);
    CHECK("DWARF64 header fields",
          hdr.is_64bit == 1 && hdr.version == 4 &&
              hdr.address_size == 8 && hdr.unit_length == 14 &&
              hdr.abbrev_offset == 0);

    memset(&ctx, 0, sizeof(ctx));
    CHECK("DWARF64 walk", neverc_dwarf_walk_entries(&d, walk_cb, &ctx) == 0);
    CHECK("DWARF64 name", ctx.cu_name && strcmp(ctx.cu_name, "z") == 0);

    put64(dwarf64 + 14, 0x100);
    CHECK("DWARF64 out-of-range abbrev offset rejected",
          neverc_dwarf_walk_entries(&d, ignore_entry_cb, NULL) < 0);
    neverc_dwarf_free(&d);
}

int main(void) {
    printf("=== NeverC debug/dwarf Tests ===\n\n");

    test_dwarf_parse();
    test_dwarf_empty();
    test_dwarf_implicit_const();
    test_dwarf_line_string();
    test_dwarf_cu_relative_refs();
    test_dwarf_malformed();
    test_dwarf_v5_type_header();
    test_dwarf_big_endian();
    test_dwarf_abbrev_offset_and_dwarf64();

    printf("\n%d/%d tests passed", tests_passed, tests_run);
    if (tests_failed > 0)
        printf(", %d FAILED", tests_failed);
    printf("\n");
    return tests_failed > 0 ? 1 : 0;
}
