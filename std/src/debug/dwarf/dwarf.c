/*
 * DWARF debug information parser (v2-v5).
 * Parses abbreviation tables, DIE trees, string tables.
 * Modeled after Go's debug/dwarf package.
 */
#include "neverc/std/debug/dwarf.h"
#include <limits.h>
#include <string.h>
#include <stdlib.h>

/* ===== LEB128 decoding ===== */

static int has_bytes(const uint8_t *p, const uint8_t *end, size_t n) {
    return p <= end && n <= (size_t)(end - p);
}

static int read_uleb128(const uint8_t **p, const uint8_t *end,
                        uint64_t *value) {
    const uint8_t *q = *p;
    uint64_t result = 0;
    for (unsigned i = 0; i < 10; i++) {
        if (q >= end) return -1;
        uint8_t b = *q++;
        uint8_t payload = (uint8_t)(b & 0x7f);
        if (i == 9 && (payload > 1 || (b & 0x80) != 0))
            return -1;
        result |= (uint64_t)payload << (i * 7);
        if ((b & 0x80) == 0) {
            *p = q;
            *value = result;
            return 0;
        }
    }
    return -1;
}

static int read_sleb128(const uint8_t **p, const uint8_t *end,
                        int64_t *value) {
    const uint8_t *q = *p;
    uint64_t result = 0;
    for (unsigned i = 0; i < 10; i++) {
        if (q >= end) return -1;
        uint8_t b = *q++;
        uint8_t payload = (uint8_t)(b & 0x7f);
        if (i == 9 &&
            ((payload != 0 && payload != 0x7f) || (b & 0x80) != 0))
            return -1;
        result |= (uint64_t)payload << (i * 7);
        if ((b & 0x80) == 0) {
            unsigned used_bits = (i + 1) * 7;
            if ((b & 0x40) != 0 && used_bits < 64)
                result |= UINT64_MAX << used_bits;
            *p = q;
            *value = (int64_t)result;
            return 0;
        }
    }
    return -1;
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

static void free_abbrevs(neverc_dwarf_data_t *d) {
    if (!d) return;
    for (int i = 0; i < d->abbrev_count; i++)
        free(d->abbrevs[i].attrs);
    free(d->abbrevs);
    d->abbrevs = NULL;
    d->abbrev_count = 0;
}

static int parse_abbrevs(neverc_dwarf_data_t *d, uint64_t offset) {
    if (!d || !d->debug_abbrev || offset >= d->debug_abbrev_len)
        return -1;

    const uint8_t *p = d->debug_abbrev + (size_t)offset;
    const uint8_t *end = d->debug_abbrev + d->debug_abbrev_len;
    size_t abbrev_cap = 0;

    free_abbrevs(d);
    for (;;) {
        uint64_t code;
        if (read_uleb128(&p, end, &code) < 0) goto malformed;
        if (code == 0) return 0;
        if (code > UINT32_MAX || d->abbrev_count == INT_MAX)
            goto malformed;

        uint64_t tag;
        if (read_uleb128(&p, end, &tag) < 0 || tag == 0 ||
            tag > UINT16_MAX || !has_bytes(p, end, 1))
            goto malformed;
        uint8_t children = *p++;
        if (children > 1) goto malformed;

        if ((size_t)d->abbrev_count == abbrev_cap) {
            size_t new_cap = abbrev_cap ? abbrev_cap * 2 : 8;
            if (new_cap < abbrev_cap ||
                new_cap > SIZE_MAX / sizeof(*d->abbrevs))
                goto malformed;
            neverc_dwarf_abbrev_t *grown =
                (neverc_dwarf_abbrev_t *)realloc(
                    d->abbrevs, new_cap * sizeof(*d->abbrevs));
            if (!grown) goto malformed;
            memset(grown + abbrev_cap, 0,
                   (new_cap - abbrev_cap) * sizeof(*grown));
            d->abbrevs = grown;
            abbrev_cap = new_cap;
        }

        neverc_dwarf_abbrev_t *abbrev =
            &d->abbrevs[d->abbrev_count++];
        abbrev->code = (uint32_t)code;
        abbrev->tag = (uint16_t)tag;
        abbrev->has_children = children != 0;

        size_t attr_cap = 0;
        for (;;) {
            uint64_t attr;
            uint64_t form;
            if (read_uleb128(&p, end, &attr) < 0 ||
                read_uleb128(&p, end, &form) < 0)
                goto malformed;
            if (attr == 0 || form == 0) {
                if (attr == 0 && form == 0) break;
                goto malformed;
            }
            if (attr > UINT16_MAX || form > UINT16_MAX ||
                abbrev->attr_count == INT_MAX)
                goto malformed;

            int64_t implicit_const = 0;
            if (form == NEVERC_DW_FORM_implicit_const &&
                read_sleb128(&p, end, &implicit_const) < 0)
                goto malformed;

            if ((size_t)abbrev->attr_count == attr_cap) {
                size_t new_cap = attr_cap ? attr_cap * 2 : 8;
                if (new_cap < attr_cap ||
                    new_cap > SIZE_MAX / sizeof(*abbrev->attrs))
                    goto malformed;
                neverc_dwarf_attr_spec_t *grown =
                    (neverc_dwarf_attr_spec_t *)realloc(
                        abbrev->attrs, new_cap * sizeof(*abbrev->attrs));
                if (!grown) goto malformed;
                abbrev->attrs = grown;
                attr_cap = new_cap;
            }
            neverc_dwarf_attr_spec_t *spec =
                &abbrev->attrs[abbrev->attr_count++];
            spec->attr = (uint16_t)attr;
            spec->form = (uint16_t)form;
            spec->implicit_const = implicit_const;
        }
    }

malformed:
    free_abbrevs(d);
    return -1;
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
    if (!d || (!debug_info && info_len != 0) ||
        (!debug_abbrev && abbrev_len != 0) ||
        (!debug_str && str_len != 0))
        return -1;
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
    if (!d) return;
    free_abbrevs(d);
    memset(d, 0, sizeof(*d));
}

const char *neverc_dwarf_get_string(const neverc_dwarf_data_t *d,
                                     uint64_t offset) {
    if (!d || !d->debug_str || offset >= d->debug_str_len) return "";
    /* debug_str is file data and may lack a NUL before its end; require one
     * within the table bounds so callers' strlen() cannot read past it. */
    if (memchr(d->debug_str + offset, 0, d->debug_str_len - (size_t)offset) == NULL)
        return "";
    return (const char *)(d->debug_str + offset);
}

int neverc_dwarf_parse_comp_unit(const neverc_dwarf_data_t *d,
                                  size_t offset,
                                  neverc_dwarf_comp_unit_header_t *hdr) {
    if (!d || !hdr || !d->debug_info ||
        offset > d->debug_info_len ||
        d->debug_info_len - offset < 4)
        return -1;
    memset(hdr, 0, sizeof(*hdr));

    const uint8_t *p = d->debug_info + offset;
    const uint8_t *end = d->debug_info + d->debug_info_len;

    uint32_t init_len = rd32(p);
    if (init_len == 0xFFFFFFFF) {
        if (!has_bytes(p, end, 12)) return -1;
        hdr->is_64bit = 1;
        hdr->unit_length = rd64(p + 4);
        p += 12;
    } else {
        /* 0xfffffff0 through 0xfffffffe are reserved initial lengths. */
        if (init_len >= 0xFFFFFFF0U) return -1;
        hdr->is_64bit = 0;
        hdr->unit_length = init_len;
        p += 4;
    }
    if (hdr->unit_length > (uint64_t)(end - p)) return -1;
    const uint8_t *unit_end = p + (size_t)hdr->unit_length;
    if (!has_bytes(p, unit_end, 2)) return -1;

    hdr->version = rd16(p); p += 2;
    if (hdr->version < 2 || hdr->version > 5) return -1;

    if (hdr->version >= 5) {
        /* DWARF v5: unit_type, address_size, abbrev_offset */
        size_t offset_size = hdr->is_64bit ? 8 : 4;
        if (!has_bytes(p, unit_end, 2 + offset_size)) return -1;
        hdr->unit_type = *p++;
        hdr->address_size = *p++;
        if (hdr->is_64bit) {
            hdr->abbrev_offset = rd64(p);
            p += 8;
        } else {
            hdr->abbrev_offset = rd32(p);
            p += 4;
        }

        switch (hdr->unit_type) {
        case NEVERC_DW_UT_compile:
        case NEVERC_DW_UT_partial:
            break;
        case NEVERC_DW_UT_skeleton:
        case NEVERC_DW_UT_split_compile:
            if (!has_bytes(p, unit_end, 8)) return -1;
            hdr->dwo_id = rd64(p);
            p += 8;
            break;
        case NEVERC_DW_UT_type:
        case NEVERC_DW_UT_split_type:
            if (!has_bytes(p, unit_end, 8 + offset_size)) return -1;
            hdr->type_signature = rd64(p);
            p += 8;
            if (hdr->is_64bit) {
                hdr->type_offset = rd64(p);
                p += 8;
            } else {
                hdr->type_offset = rd32(p);
                p += 4;
            }
            break;
        default:
            return -1;
        }
    } else {
        size_t offset_size = hdr->is_64bit ? 8 : 4;
        if (!has_bytes(p, unit_end, offset_size + 1)) return -1;
        if (hdr->is_64bit) {
            hdr->abbrev_offset = rd64(p); p += 8;
        } else {
            hdr->abbrev_offset = rd32(p); p += 4;
        }
        hdr->address_size = *p++;
    }
    if (hdr->address_size == 0 || hdr->address_size > 8) return -1;
    hdr->header_size = (size_t)(p - (d->debug_info + offset));
    if ((hdr->unit_type == NEVERC_DW_UT_type ||
         hdr->unit_type == NEVERC_DW_UT_split_type) &&
        (hdr->type_offset < hdr->header_size ||
         hdr->type_offset >=
             (uint64_t)(hdr->is_64bit ? 12U : 4U) + hdr->unit_length))
        return -1;
    return 0;
}

static int read_uint_le(const uint8_t **p, const uint8_t *end, size_t size,
                        uint64_t *value) {
    if (size == 0 || size > 8 || !has_bytes(*p, end, size)) return -1;
    uint64_t result = 0;
    for (size_t i = 0; i < size; i++)
        result |= (uint64_t)(*p)[i] << (i * 8);
    *p += size;
    *value = result;
    return 0;
}

static size_t ref_addr_size(uint16_t version, uint8_t addr_size, int dwarf64) {
    return version == 2 ? addr_size : (dwarf64 ? 8U : 4U);
}

static int skip_form_depth(uint16_t form, uint8_t addr_size, int dwarf64,
                           uint16_t version, const uint8_t **p,
                           const uint8_t *end, unsigned depth) {
    if (depth > 8) return 0;

    size_t fixed_size = 0;
    switch (form) {
    case NEVERC_DW_FORM_addr:       fixed_size = addr_size; break;
    case NEVERC_DW_FORM_data1:
    case NEVERC_DW_FORM_ref1:
    case NEVERC_DW_FORM_flag:
    case NEVERC_DW_FORM_strx1:
    case NEVERC_DW_FORM_addrx1:     fixed_size = 1; break;
    case NEVERC_DW_FORM_data2:
    case NEVERC_DW_FORM_ref2:
    case NEVERC_DW_FORM_strx2:
    case NEVERC_DW_FORM_addrx2:     fixed_size = 2; break;
    case NEVERC_DW_FORM_strx3:
    case NEVERC_DW_FORM_addrx3:     fixed_size = 3; break;
    case NEVERC_DW_FORM_data4:
    case NEVERC_DW_FORM_ref4:
    case NEVERC_DW_FORM_ref_sup4:
    case NEVERC_DW_FORM_strx4:
    case NEVERC_DW_FORM_addrx4:     fixed_size = 4; break;
    case NEVERC_DW_FORM_data8:
    case NEVERC_DW_FORM_ref8:
    case NEVERC_DW_FORM_ref_sig8:
    case NEVERC_DW_FORM_ref_sup8:   fixed_size = 8; break;
    case NEVERC_DW_FORM_data16:     fixed_size = 16; break;
    case NEVERC_DW_FORM_string: {
        if (*p >= end) return 0;
        const uint8_t *nul = (const uint8_t *)memchr(
            *p, 0, (size_t)(end - *p));
        if (!nul) return 0;
        *p = nul + 1;
        return 1;
    }
    case NEVERC_DW_FORM_strp:
    case NEVERC_DW_FORM_sec_offset:
    case NEVERC_DW_FORM_line_strp:
    case NEVERC_DW_FORM_strp_sup:
        fixed_size = dwarf64 ? 8U : 4U;
        break;
    case NEVERC_DW_FORM_ref_addr:
        fixed_size = ref_addr_size(version, addr_size, dwarf64);
        break;
    case NEVERC_DW_FORM_strx:
    case NEVERC_DW_FORM_addrx:
    case NEVERC_DW_FORM_udata:
    case NEVERC_DW_FORM_ref_udata:
    case NEVERC_DW_FORM_loclistx:
    case NEVERC_DW_FORM_rnglistx: {
        uint64_t ignored;
        return read_uleb128(p, end, &ignored) == 0;
    }
    case NEVERC_DW_FORM_sdata: {
        int64_t ignored;
        return read_sleb128(p, end, &ignored) == 0;
    }
    case NEVERC_DW_FORM_block1:
    case NEVERC_DW_FORM_block2:
    case NEVERC_DW_FORM_block4:
    case NEVERC_DW_FORM_block:
    case NEVERC_DW_FORM_exprloc: {
        uint64_t size;
        if (form == NEVERC_DW_FORM_block1) {
            if (read_uint_le(p, end, 1, &size) < 0) return 0;
        } else if (form == NEVERC_DW_FORM_block2) {
            if (read_uint_le(p, end, 2, &size) < 0) return 0;
        } else if (form == NEVERC_DW_FORM_block4) {
            if (read_uint_le(p, end, 4, &size) < 0) return 0;
        } else if (read_uleb128(p, end, &size) < 0) {
            return 0;
        }
        if (size > (uint64_t)(end - *p)) return 0;
        *p += (size_t)size;
        return 1;
    }
    case NEVERC_DW_FORM_indirect: {
        uint64_t actual;
        if (read_uleb128(p, end, &actual) < 0 ||
            actual == 0 || actual > UINT16_MAX)
            return 0;
        return skip_form_depth((uint16_t)actual, addr_size, dwarf64,
                               version, p, end, depth + 1);
    }
    case NEVERC_DW_FORM_flag_present:
    case NEVERC_DW_FORM_implicit_const:
        return 1;
    default: return 0;
    }
    if (!has_bytes(*p, end, fixed_size)) return 0;
    *p += fixed_size;
    return 1;
}

static int skip_form(uint16_t form, uint8_t addr_size, int dwarf64,
                     uint16_t version, const uint8_t **p,
                     const uint8_t *end) {
    return skip_form_depth(form, addr_size, dwarf64, version, p, end, 0);
}

static int read_form_uint_depth(uint16_t form, uint8_t addr_size, int dwarf64,
                                uint16_t version, const uint8_t **p,
                                const uint8_t *end, uint64_t *value,
                                uint16_t *resolved_form, unsigned depth) {
    if (depth > 8) return -1;
    size_t size;
    switch (form) {
    case NEVERC_DW_FORM_addr:
        size = addr_size;
        break;
    case NEVERC_DW_FORM_data1:
    case NEVERC_DW_FORM_ref1:
    case NEVERC_DW_FORM_flag:
    case NEVERC_DW_FORM_strx1:
        size = 1;
        break;
    case NEVERC_DW_FORM_data2:
    case NEVERC_DW_FORM_ref2:
    case NEVERC_DW_FORM_strx2:
        size = 2;
        break;
    case NEVERC_DW_FORM_strx3:
        size = 3;
        break;
    case NEVERC_DW_FORM_data4:
    case NEVERC_DW_FORM_ref4:
    case NEVERC_DW_FORM_ref_sup4:
    case NEVERC_DW_FORM_strx4:
        size = 4;
        break;
    case NEVERC_DW_FORM_data8:
    case NEVERC_DW_FORM_ref8:
    case NEVERC_DW_FORM_ref_sig8:
    case NEVERC_DW_FORM_ref_sup8:
        size = 8;
        break;
    case NEVERC_DW_FORM_sec_offset:
    case NEVERC_DW_FORM_strp:
    case NEVERC_DW_FORM_line_strp:
    case NEVERC_DW_FORM_strp_sup:
        size = dwarf64 ? 8U : 4U;
        break;
    case NEVERC_DW_FORM_ref_addr:
        size = ref_addr_size(version, addr_size, dwarf64);
        break;
    case NEVERC_DW_FORM_udata:
    case NEVERC_DW_FORM_ref_udata:
    case NEVERC_DW_FORM_strx:
    case NEVERC_DW_FORM_loclistx:
    case NEVERC_DW_FORM_rnglistx:
        if (read_uleb128(p, end, value) < 0) return -1;
        *resolved_form = form;
        return 1;
    case NEVERC_DW_FORM_sdata: {
        int64_t signed_value;
        if (read_sleb128(p, end, &signed_value) < 0) return -1;
        *value = (uint64_t)signed_value;
        *resolved_form = form;
        return 1;
    }
    case NEVERC_DW_FORM_flag_present:
        *value = 1;
        *resolved_form = form;
        return 1;
    case NEVERC_DW_FORM_indirect: {
        uint64_t actual;
        if (read_uleb128(p, end, &actual) < 0 ||
            actual == 0 || actual > UINT16_MAX)
            return -1;
        return read_form_uint_depth(
            (uint16_t)actual, addr_size, dwarf64, version, p, end,
            value, resolved_form, depth + 1);
    }
    default:
        return 0;
    }
    if (read_uint_le(p, end, size, value) < 0) return -1;
    *resolved_form = form;
    return 1;
}

static int read_form_uint(uint16_t form, uint8_t addr_size, int dwarf64,
                          uint16_t version, const uint8_t **p,
                          const uint8_t *end, uint64_t *value,
                          uint16_t *resolved_form) {
    return read_form_uint_depth(form, addr_size, dwarf64, version, p, end,
                                value, resolved_form, 0);
}

static int read_form_string_depth(uint16_t form,
                                  const neverc_dwarf_data_t *d,
                                  uint8_t addr_size, int dwarf64,
                                  uint16_t version, const uint8_t **p,
                                  const uint8_t *end, const char **value,
                                  unsigned depth) {
    if (depth > 8) return -1;
    switch (form) {
    case NEVERC_DW_FORM_string: {
        if (*p >= end) return -1;
        const uint8_t *nul = (const uint8_t *)memchr(
            *p, 0, (size_t)(end - *p));
        if (!nul) return -1;
        *value = (const char *)*p;
        *p = nul + 1;
        return 1;
    }
    case NEVERC_DW_FORM_strp:
    case NEVERC_DW_FORM_line_strp:
    case NEVERC_DW_FORM_strp_sup:
    case NEVERC_DW_FORM_strx:
    case NEVERC_DW_FORM_strx1:
    case NEVERC_DW_FORM_strx2:
    case NEVERC_DW_FORM_strx3:
    case NEVERC_DW_FORM_strx4: {
        uint64_t off;
        uint16_t resolved_form = form;
        int rc = read_form_uint(form, addr_size, dwarf64, version, p, end,
                                &off, &resolved_form);
        if (rc <= 0) return rc;
        if (resolved_form == NEVERC_DW_FORM_strp) {
            *value = neverc_dwarf_get_string(d, off);
        } else if (resolved_form == NEVERC_DW_FORM_line_strp) {
            if (d->debug_line_str && off < d->debug_line_str_len &&
                memchr(d->debug_line_str + (size_t)off, 0,
                       d->debug_line_str_len - (size_t)off) != NULL)
                *value = (const char *)(d->debug_line_str + (size_t)off);
            else
                *value = "";
        } else {
            /* Supplementary and indexed strings need sections that are not
             * part of this compact API. Consume them safely and leave empty. */
            *value = "";
        }
        return 1;
    }
    case NEVERC_DW_FORM_indirect: {
        uint64_t actual;
        if (read_uleb128(p, end, &actual) < 0 ||
            actual == 0 || actual > UINT16_MAX)
            return -1;
        return read_form_string_depth(
            (uint16_t)actual, d, addr_size, dwarf64, version, p, end,
            value, depth + 1);
    }
    default:
        return 0;
    }
}

static int read_form_string(uint16_t form, const neverc_dwarf_data_t *d,
                            uint8_t addr_size, int dwarf64,
                            uint16_t version, const uint8_t **p,
                            const uint8_t *end, const char **value) {
    return read_form_string_depth(form, d, addr_size, dwarf64, version,
                                  p, end, value, 0);
}

static void set_entry_uint_attr(neverc_dwarf_entry_t *entry,
                                uint16_t attr, uint16_t form,
                                uint64_t value) {
    switch (attr) {
    case NEVERC_DW_AT_low_pc:
        entry->low_pc = value;
        break;
    case NEVERC_DW_AT_high_pc:
        entry->high_pc = value;
        if (form != NEVERC_DW_FORM_addr)
            entry->high_pc_is_offset = 1;
        break;
    case NEVERC_DW_AT_byte_size:
        entry->byte_size = value;
        break;
    case NEVERC_DW_AT_type:
        entry->type_ref = value;
        break;
    case NEVERC_DW_AT_decl_line:
        entry->decl_line = value;
        break;
    case NEVERC_DW_AT_encoding:
        entry->encoding = value;
        break;
    default:
        break;
    }
}

int neverc_dwarf_walk_entries(const neverc_dwarf_data_t *d,
                               neverc_dwarf_entry_cb cb, void *user) {
    if (!d || !cb) return -1;
    if (d->debug_info_len == 0) return 0;
    if (!d->debug_info) return -1;

    size_t cu_offset = 0;
    while (cu_offset < d->debug_info_len) {
        neverc_dwarf_comp_unit_header_t hdr;
        if (neverc_dwarf_parse_comp_unit(d, cu_offset, &hdr) < 0)
            return -1;

        /* Parse abbreviation table for this CU */
        neverc_dwarf_data_t local = *d;
        local.abbrevs = NULL;
        local.abbrev_count = 0;
        if (parse_abbrevs(&local, hdr.abbrev_offset) < 0)
            return -1;

        size_t initial_size = hdr.is_64bit ? 12U : 4U;
        size_t cu_start_data = cu_offset + hdr.header_size;
        size_t cu_end = cu_offset + initial_size + (size_t)hdr.unit_length;
        if (cu_start_data > cu_end) {
            free_abbrevs(&local);
            return -1;
        }

        const uint8_t *p = d->debug_info + cu_start_data;
        const uint8_t *end = d->debug_info + cu_end;

        int depth = 0;
        while (p < end) {
            uint64_t die_offset = (uint64_t)(p - d->debug_info);
            uint64_t abbrev_code;
            if (read_uleb128(&p, end, &abbrev_code) < 0) {
                free_abbrevs(&local);
                return -1;
            }
            if (abbrev_code == 0) {
                if (depth == 0) {
                    free_abbrevs(&local);
                    return -1;
                }
                depth--;
                continue;
            }
            if (abbrev_code > UINT32_MAX) {
                free_abbrevs(&local);
                return -1;
            }

            const neverc_dwarf_abbrev_t *abbrev =
                find_abbrev(&local, (uint32_t)abbrev_code);
            if (!abbrev) {
                free_abbrevs(&local);
                return -1;
            }

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
                    if (hdr.version < 5) {
                        free_abbrevs(&local);
                        return -1;
                    }
                    set_entry_uint_attr(
                        &entry, attr, form,
                        (uint64_t)abbrev->attrs[i].implicit_const);
                    continue;
                }

                const uint8_t *before = p;
                if (attr == NEVERC_DW_AT_name || attr == NEVERC_DW_AT_comp_dir ||
                    attr == NEVERC_DW_AT_producer) {
                    const char *s = NULL;
                    int rc = read_form_string(
                        form, d, hdr.address_size, hdr.is_64bit,
                        hdr.version, &p, end, &s);
                    if (rc < 0) {
                        free_abbrevs(&local);
                        return -1;
                    }
                    if (rc > 0) {
                        if (attr == NEVERC_DW_AT_name) entry.name = s;
                        else if (attr == NEVERC_DW_AT_comp_dir) entry.comp_dir = s;
                        else if (attr == NEVERC_DW_AT_producer) entry.producer = s;
                        continue;
                    }
                    p = before;
                }

                uint64_t val = 0;
                uint16_t resolved_form = form;
                int rc = read_form_uint(
                    form, hdr.address_size, hdr.is_64bit, hdr.version,
                    &p, end, &val, &resolved_form);
                if (rc < 0) {
                    free_abbrevs(&local);
                    return -1;
                }
                if (rc > 0) {
                    set_entry_uint_attr(
                        &entry, attr, resolved_form, val);
                } else {
                    p = before;
                    if (!skip_form(form, hdr.address_size, hdr.is_64bit,
                                   hdr.version, &p, end)) {
                        free_abbrevs(&local);
                        return -1;
                    }
                }
            }

            if (cb(&entry, user) != 0) {
                free_abbrevs(&local);
                return 1;
            }

            if (abbrev->has_children) {
                if (depth == INT_MAX) {
                    free_abbrevs(&local);
                    return -1;
                }
                depth++;
            }
        }

        if (depth != 0) {
            free_abbrevs(&local);
            return -1;
        }
        free_abbrevs(&local);

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
