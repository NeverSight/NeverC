/*
 * DWARF debug information parser (v2-v5).
 * Parses abbreviation tables, DIE trees, string tables.
 * Modeled after Go's debug/dwarf package.
 */
#include "neverc/std/debug/dwarf.h"
#include <string.h>
#include <stdlib.h>

/* ===== LEB128 decoding ===== */

static uint64_t read_uleb128(const uint8_t **p, const uint8_t *end) {
    /* Single-byte fast path: in DWARF the vast majority of LEB128 values
     * (abbrev codes, tags, attrs, forms, small constants) fit in one byte,
     * so skip the shift/continuation bookkeeping for the common case. */
    if (*p < end && **p < 0x80)
        return *(*p)++;

    uint64_t result = 0;
    int shift = 0;
    while (*p < end) {
        uint8_t b = **p; (*p)++;
        result |= ((uint64_t)(b & 0x7f)) << shift;
        if ((b & 0x80) == 0) break;
        shift += 7;
    }
    return result;
}

static int64_t read_sleb128(const uint8_t **p, const uint8_t *end) {
    /* Single-byte fast path with sign extension from bit 6. */
    if (*p < end && **p < 0x80) {
        uint8_t b = *(*p)++;
        int64_t result = b;
        if (b & 0x40)
            result |= -(((int64_t)1) << 7);
        return result;
    }

    int64_t result = 0;
    int shift = 0;
    uint8_t b = 0;
    while (*p < end) {
        b = **p; (*p)++;
        result |= ((int64_t)(b & 0x7f)) << shift;
        shift += 7;
        if ((b & 0x80) == 0) break;
    }
    if (shift < 64 && (b & 0x40))
        result |= -(((int64_t)1) << shift);
    return result;
}

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p+4) << 32);
}

/* ===== Abbreviation Table Parsing ===== */

static int parse_abbrevs(neverc_dwarf_data_t *d, uint64_t offset) {
    if (offset >= d->debug_abbrev_len) return -1;

    const uint8_t *p = d->debug_abbrev + offset;
    const uint8_t *end = d->debug_abbrev + d->debug_abbrev_len;

    /* First pass: count entries */
    int count = 0;
    const uint8_t *save = p;
    while (p < end) {
        uint64_t code = read_uleb128(&p, end);
        if (code == 0) break;
        read_uleb128(&p, end); /* tag */
        p++; /* children */
        while (p < end) {
            uint64_t attr = read_uleb128(&p, end);
            uint64_t form = read_uleb128(&p, end);
            if (form == NEVERC_DW_FORM_implicit_const)
                read_sleb128(&p, end);
            if (attr == 0 && form == 0) break;
        }
        count++;
    }

    d->abbrevs = (neverc_dwarf_abbrev_t *)calloc((size_t)count,
                                                   sizeof(neverc_dwarf_abbrev_t));
    if (!d->abbrevs) return -1;
    d->abbrev_count = count;

    /* Second pass: populate */
    p = save;
    for (int i = 0; i < count; i++) {
        d->abbrevs[i].code = (uint32_t)read_uleb128(&p, end);
        d->abbrevs[i].tag = (uint16_t)read_uleb128(&p, end);
        d->abbrevs[i].has_children = (*p++ != 0);

        /* Count attrs */
        const uint8_t *as = p;
        int ac = 0;
        while (p < end) {
            uint64_t attr = read_uleb128(&p, end);
            uint64_t form = read_uleb128(&p, end);
            if (form == NEVERC_DW_FORM_implicit_const)
                read_sleb128(&p, end);
            if (attr == 0 && form == 0) break;
            ac++;
        }

        d->abbrevs[i].attrs = (neverc_dwarf_attr_spec_t *)calloc(
            (size_t)(ac ? ac : 1), sizeof(neverc_dwarf_attr_spec_t));
        d->abbrevs[i].attr_count = ac;

        p = as;
        for (int j = 0; j < ac; j++) {
            d->abbrevs[i].attrs[j].attr = (uint16_t)read_uleb128(&p, end);
            d->abbrevs[i].attrs[j].form = (uint16_t)read_uleb128(&p, end);
            if (d->abbrevs[i].attrs[j].form == NEVERC_DW_FORM_implicit_const)
                d->abbrevs[i].attrs[j].implicit_const = read_sleb128(&p, end);
        }
        read_uleb128(&p, end); /* trailing 0 */
        read_uleb128(&p, end);
    }
    return 0;
}

static const neverc_dwarf_abbrev_t *find_abbrev(const neverc_dwarf_data_t *d,
                                                  uint32_t code) {
    for (int i = 0; i < d->abbrev_count; i++) {
        if (d->abbrevs[i].code == code) return &d->abbrevs[i];
    }
    return NULL;
}

/* ===== Public API ===== */

int neverc_dwarf_init(neverc_dwarf_data_t *d,
                       const uint8_t *debug_info, size_t info_len,
                       const uint8_t *debug_abbrev, size_t abbrev_len,
                       const uint8_t *debug_str, size_t str_len) {
    memset(d, 0, sizeof(*d));
    d->debug_info = debug_info;
    d->debug_info_len = info_len;
    d->debug_abbrev = debug_abbrev;
    d->debug_abbrev_len = abbrev_len;
    d->debug_str = debug_str;
    d->debug_str_len = str_len;
    return 0;
}

void neverc_dwarf_free(neverc_dwarf_data_t *d) {
    if (d->abbrevs) {
        for (int i = 0; i < d->abbrev_count; i++)
            free(d->abbrevs[i].attrs);
        free(d->abbrevs);
    }
    memset(d, 0, sizeof(*d));
}

const char *neverc_dwarf_get_string(const neverc_dwarf_data_t *d,
                                     uint64_t offset) {
    if (!d->debug_str || offset >= d->debug_str_len) return "";
    /* debug_str is file data and may lack a NUL before its end; require one
     * within the table bounds so callers' strlen() cannot read past it. */
    if (memchr(d->debug_str + offset, 0, d->debug_str_len - (size_t)offset) == NULL)
        return "";
    return (const char *)(d->debug_str + offset);
}

int neverc_dwarf_parse_comp_unit(const neverc_dwarf_data_t *d,
                                  size_t offset,
                                  neverc_dwarf_comp_unit_header_t *hdr) {
    if (offset > d->debug_info_len - 11) return -1;
    const uint8_t *p = d->debug_info + offset;

    uint32_t init_len = rd32(p);
    if (init_len == 0xFFFFFFFF) {
        hdr->is_64bit = 1;
        hdr->unit_length = rd64(p + 4);
        p += 12;
    } else {
        hdr->is_64bit = 0;
        hdr->unit_length = init_len;
        p += 4;
    }
    hdr->version = rd16(p); p += 2;

    if (hdr->version >= 5) {
        /* DWARF v5: unit_type, address_size, abbrev_offset */
        p++; /* unit_type */
        hdr->address_size = *p++;
        if (hdr->is_64bit) {
            hdr->abbrev_offset = rd64(p); p += 8;
        } else {
            hdr->abbrev_offset = rd32(p); p += 4;
        }
    } else {
        if (hdr->is_64bit) {
            hdr->abbrev_offset = rd64(p); p += 8;
        } else {
            hdr->abbrev_offset = rd32(p); p += 4;
        }
        hdr->address_size = *p++;
    }
    return 0;
}

static size_t skip_form(uint16_t form, uint8_t addr_size, int dwarf64,
                         const uint8_t **p, const uint8_t *end) {
    switch (form) {
    case NEVERC_DW_FORM_addr:       *p += addr_size; break;
    case NEVERC_DW_FORM_data1:
    case NEVERC_DW_FORM_ref1:
    case NEVERC_DW_FORM_flag:       *p += 1; break;
    case NEVERC_DW_FORM_data2:
    case NEVERC_DW_FORM_ref2:       *p += 2; break;
    case NEVERC_DW_FORM_data4:
    case NEVERC_DW_FORM_ref4:       *p += 4; break;
    case NEVERC_DW_FORM_data8:
    case NEVERC_DW_FORM_ref8:       *p += 8; break;
    case NEVERC_DW_FORM_string:
        while (*p < end && **p) (*p)++;
        if (*p < end) (*p)++;
        break;
    case NEVERC_DW_FORM_strp:
    case NEVERC_DW_FORM_sec_offset:
    case NEVERC_DW_FORM_line_strp:
    case NEVERC_DW_FORM_ref_addr:
        *p += dwarf64 ? 8 : 4;
        break;
    case NEVERC_DW_FORM_strx:
    case NEVERC_DW_FORM_addrx:
    case NEVERC_DW_FORM_udata:      read_uleb128(p, end); break;
    case NEVERC_DW_FORM_sdata:      read_sleb128(p, end); break;
    case NEVERC_DW_FORM_exprloc: {
        uint64_t sz = read_uleb128(p, end);
        *p += (size_t)sz;
        break;
    }
    case NEVERC_DW_FORM_flag_present: break;
    case NEVERC_DW_FORM_implicit_const: break;
    default: return 0;
    }
    return 1;
}

static uint64_t read_form_uint(uint16_t form, uint8_t addr_size, int dwarf64,
                                const uint8_t **p, const uint8_t *end) {
    switch (form) {
    case NEVERC_DW_FORM_addr:
        if (addr_size == 4) { uint32_t v = rd32(*p); *p += 4; return v; }
        else { uint64_t v = rd64(*p); *p += 8; return v; }
    case NEVERC_DW_FORM_data1: case NEVERC_DW_FORM_ref1:
    case NEVERC_DW_FORM_flag:
        return *(*p)++;
    case NEVERC_DW_FORM_data2: case NEVERC_DW_FORM_ref2:
        { uint16_t v = rd16(*p); *p += 2; return v; }
    case NEVERC_DW_FORM_data4: case NEVERC_DW_FORM_ref4:
        { uint32_t v = rd32(*p); *p += 4; return v; }
    case NEVERC_DW_FORM_data8: case NEVERC_DW_FORM_ref8:
        { uint64_t v = rd64(*p); *p += 8; return v; }
    case NEVERC_DW_FORM_sec_offset:
    case NEVERC_DW_FORM_strp:
    case NEVERC_DW_FORM_line_strp:
    case NEVERC_DW_FORM_ref_addr:
        if (dwarf64) { uint64_t v = rd64(*p); *p += 8; return v; }
        else { uint32_t v = rd32(*p); *p += 4; return v; }
    case NEVERC_DW_FORM_udata:
    case NEVERC_DW_FORM_strx:
    case NEVERC_DW_FORM_addrx:
        return read_uleb128(p, end);
    case NEVERC_DW_FORM_sdata:
        return (uint64_t)read_sleb128(p, end);
    case NEVERC_DW_FORM_flag_present:
        return 1;
    default:
        return 0;
    }
}

static const char *read_form_string(uint16_t form, const neverc_dwarf_data_t *d,
                                     int dwarf64, const uint8_t **p,
                                     const uint8_t *end) {
    switch (form) {
    case NEVERC_DW_FORM_string: {
        const char *s = (const char *)*p;
        while (*p < end && **p) (*p)++;
        if (*p < end) (*p)++;
        return s;
    }
    case NEVERC_DW_FORM_strp: {
        uint64_t off;
        if (dwarf64) { off = rd64(*p); *p += 8; }
        else { off = rd32(*p); *p += 4; }
        return neverc_dwarf_get_string(d, off);
    }
    case NEVERC_DW_FORM_line_strp: {
        uint64_t off;
        if (dwarf64) { off = rd64(*p); *p += 8; }
        else { off = rd32(*p); *p += 4; }
        if (d->debug_line_str && off < d->debug_line_str_len)
            return (const char *)(d->debug_line_str + off);
        return "";
    }
    default: return NULL;
    }
}

int neverc_dwarf_walk_entries(const neverc_dwarf_data_t *d,
                               neverc_dwarf_entry_cb cb, void *user) {
    if (!d->debug_info || d->debug_info_len == 0) return 0;

    size_t cu_offset = 0;
    while (cu_offset < d->debug_info_len) {
        neverc_dwarf_comp_unit_header_t hdr;
        if (neverc_dwarf_parse_comp_unit(d, cu_offset, &hdr) < 0) break;

        /* Parse abbreviation table for this CU */
        neverc_dwarf_data_t local = *d;
        if (parse_abbrevs(&local, hdr.abbrev_offset) < 0) break;

        size_t hdr_len = hdr.is_64bit ? 12 : 4;
        size_t cu_start_data;
        if (hdr.version >= 5)
            cu_start_data = cu_offset + hdr_len + 2 + 1 + 1 + (hdr.is_64bit ? 8 : 4);
        else
            cu_start_data = cu_offset + hdr_len + 2 + (hdr.is_64bit ? 8 : 4) + 1;

        size_t cu_end = cu_offset + hdr_len + (size_t)hdr.unit_length;
        if (cu_end > d->debug_info_len) cu_end = d->debug_info_len;

        const uint8_t *p = d->debug_info + cu_start_data;
        const uint8_t *end = d->debug_info + cu_end;

        int depth = 0;
        while (p < end) {
            uint64_t die_offset = (uint64_t)(p - d->debug_info);
            uint64_t abbrev_code = read_uleb128(&p, end);
            if (abbrev_code == 0) {
                depth--;
                if (depth < 0) break;
                continue;
            }

            const neverc_dwarf_abbrev_t *abbrev = find_abbrev(&local, (uint32_t)abbrev_code);
            if (!abbrev) break;

            neverc_dwarf_entry_t entry;
            memset(&entry, 0, sizeof(entry));
            entry.tag = abbrev->tag;
            entry.offset = die_offset;
            entry.has_children = abbrev->has_children;
            entry.depth = depth;

            for (int i = 0; i < abbrev->attr_count; i++) {
                uint16_t attr = abbrev->attrs[i].attr;
                uint16_t form = abbrev->attrs[i].form;

                if (form == NEVERC_DW_FORM_implicit_const) {
                    if (attr == NEVERC_DW_AT_decl_line)
                        entry.decl_line = (uint64_t)abbrev->attrs[i].implicit_const;
                    continue;
                }

                const uint8_t *before = p;
                if (attr == NEVERC_DW_AT_name || attr == NEVERC_DW_AT_comp_dir ||
                    attr == NEVERC_DW_AT_producer) {
                    const char *s = read_form_string(form, d, hdr.is_64bit, &p, end);
                    if (s) {
                        if (attr == NEVERC_DW_AT_name) entry.name = s;
                        else if (attr == NEVERC_DW_AT_comp_dir) entry.comp_dir = s;
                        else if (attr == NEVERC_DW_AT_producer) entry.producer = s;
                        continue;
                    }
                    p = before;
                }

                uint64_t val = read_form_uint(form, hdr.address_size,
                                               hdr.is_64bit, &p, end);
                switch (attr) {
                case NEVERC_DW_AT_low_pc:    entry.low_pc = val; break;
                case NEVERC_DW_AT_high_pc:
                    entry.high_pc = val;
                    if (form != NEVERC_DW_FORM_addr)
                        entry.high_pc_is_offset = 1;
                    break;
                case NEVERC_DW_AT_byte_size: entry.byte_size = val; break;
                case NEVERC_DW_AT_type:      entry.type_ref = val; break;
                case NEVERC_DW_AT_decl_line: entry.decl_line = val; break;
                case NEVERC_DW_AT_encoding:  entry.encoding = val; break;
                default: break;
                }

                if (p == before) {
                    skip_form(form, hdr.address_size, hdr.is_64bit, &p, end);
                }
            }

            if (cb(&entry, user) != 0) {
                for (int i = 0; i < local.abbrev_count; i++)
                    free(local.abbrevs[i].attrs);
                free(local.abbrevs);
                return 1;
            }

            if (abbrev->has_children) depth++;
        }

        for (int i = 0; i < local.abbrev_count; i++)
            free(local.abbrevs[i].attrs);
        free(local.abbrevs);

        cu_offset = cu_end;
    }
    return 0;
}

const char *neverc_dwarf_tag_string(uint16_t tag) {
    switch (tag) {
    case NEVERC_DW_TAG_compile_unit:     return "DW_TAG_compile_unit";
    case NEVERC_DW_TAG_subprogram:       return "DW_TAG_subprogram";
    case NEVERC_DW_TAG_variable:         return "DW_TAG_variable";
    case NEVERC_DW_TAG_formal_parameter: return "DW_TAG_formal_parameter";
    case NEVERC_DW_TAG_base_type:        return "DW_TAG_base_type";
    case NEVERC_DW_TAG_pointer_type:     return "DW_TAG_pointer_type";
    case NEVERC_DW_TAG_structure_type:   return "DW_TAG_structure_type";
    case NEVERC_DW_TAG_typedef:          return "DW_TAG_typedef";
    case NEVERC_DW_TAG_member:           return "DW_TAG_member";
    case NEVERC_DW_TAG_array_type:       return "DW_TAG_array_type";
    case NEVERC_DW_TAG_enumeration_type: return "DW_TAG_enumeration_type";
    case NEVERC_DW_TAG_enumerator:       return "DW_TAG_enumerator";
    case NEVERC_DW_TAG_subroutine_type:  return "DW_TAG_subroutine_type";
    case NEVERC_DW_TAG_const_type:       return "DW_TAG_const_type";
    case NEVERC_DW_TAG_volatile_type:    return "DW_TAG_volatile_type";
    case NEVERC_DW_TAG_union_type:       return "DW_TAG_union_type";
    case NEVERC_DW_TAG_namespace:        return "DW_TAG_namespace";
    case NEVERC_DW_TAG_class_type:       return "DW_TAG_class_type";
    case NEVERC_DW_TAG_lexical_block:    return "DW_TAG_lexical_block";
    default: return "DW_TAG_unknown";
    }
}

const char *neverc_dwarf_attr_string(uint16_t attr) {
    switch (attr) {
    case NEVERC_DW_AT_name:       return "DW_AT_name";
    case NEVERC_DW_AT_low_pc:     return "DW_AT_low_pc";
    case NEVERC_DW_AT_high_pc:    return "DW_AT_high_pc";
    case NEVERC_DW_AT_language:   return "DW_AT_language";
    case NEVERC_DW_AT_comp_dir:   return "DW_AT_comp_dir";
    case NEVERC_DW_AT_type:       return "DW_AT_type";
    case NEVERC_DW_AT_byte_size:  return "DW_AT_byte_size";
    case NEVERC_DW_AT_encoding:   return "DW_AT_encoding";
    case NEVERC_DW_AT_decl_file:  return "DW_AT_decl_file";
    case NEVERC_DW_AT_decl_line:  return "DW_AT_decl_line";
    case NEVERC_DW_AT_external:   return "DW_AT_external";
    case NEVERC_DW_AT_producer:   return "DW_AT_producer";
    case NEVERC_DW_AT_stmt_list:  return "DW_AT_stmt_list";
    default: return "DW_AT_unknown";
    }
}
