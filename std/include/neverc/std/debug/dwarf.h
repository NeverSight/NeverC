#ifndef NEVERC_DEBUG_DWARF_H
#define NEVERC_DEBUG_DWARF_H

/*
 * DWARF debug information parser (v2-v5).
 * Parses .debug_info, .debug_abbrev, .debug_str sections.
 * API modeled after Go's debug/dwarf package.
 */

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ===== DWARF Constants ===== */

/* Tags (DW_TAG_*) */
#define NEVERC_DW_TAG_compile_unit   0x11
#define NEVERC_DW_TAG_subprogram     0x2e
#define NEVERC_DW_TAG_variable       0x34
#define NEVERC_DW_TAG_formal_parameter 0x05
#define NEVERC_DW_TAG_base_type      0x24
#define NEVERC_DW_TAG_pointer_type   0x0f
#define NEVERC_DW_TAG_structure_type 0x13
#define NEVERC_DW_TAG_typedef        0x16
#define NEVERC_DW_TAG_member         0x0d
#define NEVERC_DW_TAG_array_type     0x01
#define NEVERC_DW_TAG_enumeration_type 0x04
#define NEVERC_DW_TAG_enumerator     0x28
#define NEVERC_DW_TAG_subroutine_type 0x15
#define NEVERC_DW_TAG_const_type     0x26
#define NEVERC_DW_TAG_volatile_type  0x35
#define NEVERC_DW_TAG_union_type     0x17
#define NEVERC_DW_TAG_lexical_block  0x0b
#define NEVERC_DW_TAG_namespace      0x39
#define NEVERC_DW_TAG_class_type     0x02

/* Attributes (DW_AT_*) */
#define NEVERC_DW_AT_name            0x03
#define NEVERC_DW_AT_stmt_list       0x10
#define NEVERC_DW_AT_low_pc          0x11
#define NEVERC_DW_AT_high_pc         0x12
#define NEVERC_DW_AT_language        0x13
#define NEVERC_DW_AT_comp_dir        0x1b
#define NEVERC_DW_AT_type            0x49
#define NEVERC_DW_AT_byte_size       0x0b
#define NEVERC_DW_AT_bit_size        0x0d
#define NEVERC_DW_AT_encoding        0x3e
#define NEVERC_DW_AT_decl_file       0x3a
#define NEVERC_DW_AT_decl_line       0x3b
#define NEVERC_DW_AT_external        0x3f
#define NEVERC_DW_AT_location        0x02
#define NEVERC_DW_AT_producer        0x25
#define NEVERC_DW_AT_prototyped      0x27
#define NEVERC_DW_AT_data_member_location 0x38
#define NEVERC_DW_AT_str_offsets_base 0x72
#define NEVERC_DW_AT_addr_base       0x73

/* Forms (DW_FORM_*) */
#define NEVERC_DW_FORM_addr       0x01
#define NEVERC_DW_FORM_block2     0x03
#define NEVERC_DW_FORM_block4     0x04
#define NEVERC_DW_FORM_data2      0x05
#define NEVERC_DW_FORM_data4      0x06
#define NEVERC_DW_FORM_data8      0x07
#define NEVERC_DW_FORM_string     0x08
#define NEVERC_DW_FORM_block      0x09
#define NEVERC_DW_FORM_block1     0x0a
#define NEVERC_DW_FORM_data1      0x0b
#define NEVERC_DW_FORM_flag       0x0c
#define NEVERC_DW_FORM_strp       0x0e
#define NEVERC_DW_FORM_udata      0x0f
#define NEVERC_DW_FORM_ref4       0x13
#define NEVERC_DW_FORM_ref_addr   0x10
#define NEVERC_DW_FORM_sdata      0x0d
#define NEVERC_DW_FORM_sec_offset 0x17
#define NEVERC_DW_FORM_exprloc    0x18
#define NEVERC_DW_FORM_flag_present 0x19
#define NEVERC_DW_FORM_ref1       0x11
#define NEVERC_DW_FORM_ref2       0x12
#define NEVERC_DW_FORM_ref8       0x14
#define NEVERC_DW_FORM_ref_udata  0x15
#define NEVERC_DW_FORM_indirect   0x16
#define NEVERC_DW_FORM_line_strp  0x1f
#define NEVERC_DW_FORM_strx       0x1a
#define NEVERC_DW_FORM_addrx      0x1b
#define NEVERC_DW_FORM_ref_sup4   0x1c
#define NEVERC_DW_FORM_strp_sup   0x1d
#define NEVERC_DW_FORM_data16     0x1e
#define NEVERC_DW_FORM_ref_sig8   0x20
#define NEVERC_DW_FORM_implicit_const 0x21
#define NEVERC_DW_FORM_loclistx   0x22
#define NEVERC_DW_FORM_rnglistx   0x23
#define NEVERC_DW_FORM_ref_sup8   0x24
#define NEVERC_DW_FORM_strx1      0x25
#define NEVERC_DW_FORM_strx2      0x26
#define NEVERC_DW_FORM_strx3      0x27
#define NEVERC_DW_FORM_strx4      0x28
#define NEVERC_DW_FORM_addrx1     0x29
#define NEVERC_DW_FORM_addrx2     0x2a
#define NEVERC_DW_FORM_addrx3     0x2b
#define NEVERC_DW_FORM_addrx4     0x2c

/* DWARF v5 unit types (DW_UT_*) */
#define NEVERC_DW_UT_compile       0x01
#define NEVERC_DW_UT_type          0x02
#define NEVERC_DW_UT_partial       0x03
#define NEVERC_DW_UT_skeleton      0x04
#define NEVERC_DW_UT_split_compile 0x05
#define NEVERC_DW_UT_split_type    0x06

/* ===== Types ===== */

typedef struct {
    uint16_t attr;
    uint16_t form;
    int64_t  implicit_const;
} neverc_dwarf_attr_spec_t;

typedef struct {
    uint32_t                code;
    uint16_t                tag;
    int                     has_children;
    neverc_dwarf_attr_spec_t *attrs;
    int                     attr_count;
} neverc_dwarf_abbrev_t;

typedef struct {
    uint16_t tag;
    uint64_t offset;
    int      has_children;
    int      depth;

    /* Common attributes (populated if present) */
    const char *name;
    uint64_t    low_pc;
    uint64_t    high_pc;
    int         high_pc_is_offset;
    uint64_t    byte_size;
    uint64_t    type_ref;
    uint64_t    decl_line;
    const char *comp_dir;
    const char *producer;
    uint64_t    encoding;
} neverc_dwarf_entry_t;

typedef struct {
    uint64_t unit_length;
    uint16_t version;
    uint64_t abbrev_offset;
    uint8_t  address_size;
    int      is_64bit;
} neverc_dwarf_comp_unit_header_t;

/* Extended v5 unit metadata. Kept separate so the original public header
 * layout and parse_comp_unit ABI remain stable. */
typedef struct {
    uint64_t unit_length;
    uint16_t version;
    uint64_t abbrev_offset;
    uint8_t  address_size;
    int      is_64bit;
    uint8_t  unit_type;
    size_t   header_size;
    uint64_t type_signature;
    uint64_t type_offset;
    uint64_t dwo_id;
} neverc_dwarf_comp_unit_header_ex_t;

typedef struct {
    const uint8_t *debug_info;
    size_t         debug_info_len;
    const uint8_t *debug_abbrev;
    size_t         debug_abbrev_len;
    const uint8_t *debug_str;
    size_t         debug_str_len;
    const uint8_t *debug_line_str;
    size_t         debug_line_str_len;

    neverc_dwarf_abbrev_t *abbrevs;
    int                    abbrev_count;

    /* 0 = little-endian (default after init), 1 = big-endian. DWARF follows
     * the object file's EI_DATA / equivalent; LEB128 is still little-endian. */
    int big_endian;
} neverc_dwarf_data_t;

/* ===== Functions ===== */

/* Initialize DWARF data from raw section buffers. */
int neverc_dwarf_init(neverc_dwarf_data_t *d,
                       const uint8_t *debug_info, size_t info_len,
                       const uint8_t *debug_abbrev, size_t abbrev_len,
                       const uint8_t *debug_str, size_t str_len);

void neverc_dwarf_free(neverc_dwarf_data_t *d);

/* Parse compilation unit header at given offset within .debug_info. */
int neverc_dwarf_parse_comp_unit(const neverc_dwarf_data_t *d,
                                  size_t offset,
                                  neverc_dwarf_comp_unit_header_t *hdr);
int neverc_dwarf_parse_comp_unit_ex(
    const neverc_dwarf_data_t *d, size_t offset,
    neverc_dwarf_comp_unit_header_ex_t *hdr);

/* Iterate DIEs (Debug Information Entries) within a compilation unit.
   Calls callback for each entry. Return non-zero from callback to stop. */
typedef int (*neverc_dwarf_entry_cb)(const neverc_dwarf_entry_t *entry,
                                      void *user);
int neverc_dwarf_walk_entries(const neverc_dwarf_data_t *d,
                               neverc_dwarf_entry_cb cb, void *user);

/* Get string from .debug_str at offset. */
const char *neverc_dwarf_get_string(const neverc_dwarf_data_t *d,
                                     uint64_t offset);

/* Tag name string. */
const char *neverc_dwarf_tag_string(uint16_t tag);

/* Attribute name string. */
const char *neverc_dwarf_attr_string(uint16_t attr);

#ifdef __cplusplus
}
#endif

#ifdef __neverc__
#include <neverc/std/_modules.h>
#endif

#endif /* NEVERC_DEBUG_DWARF_H */
