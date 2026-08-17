/*
 * PE (Portable Executable) format parser.
 * Supports PE32 and PE32+ (64-bit).
 * Modeled after Go's debug/pe package.
 */
#include "neverc/std/debug/pe.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

static uint16_t rd16(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t rd32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64(const uint8_t *p) {
    return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32);
}

static int pe_open_fail(neverc_pe_file_t *f) {
    free(f->sections);
    memset(f, 0, sizeof(*f));
    return -1;
}

/* Subtract, don't add: pointer+size as uint32 wraps for high PointerToRawData. */
static int pe_range_in_file(size_t len, uint64_t offset, uint64_t size) {
    return offset <= len && size <= (uint64_t)len - offset;
}

int neverc_pe_is_valid(const uint8_t *data, size_t len) {
    if (!data || len < 64) return 0;
    if (data[0] != 'M' || data[1] != 'Z') return 0;
    uint32_t pe_off = rd32(data + 60);
    /* pe_off is a 32-bit file value; `pe_off + 4` in 32-bit wraps for pe_off
     * near UINT32_MAX, slipping past the bound and dereferencing a wild address
     * at rd32(data + pe_off). Widen to 64-bit. */
    if ((uint64_t)pe_off + 4 > len) return 0;
    return rd32(data + pe_off) == NEVERC_PE_SIGNATURE;
}

/* pe_get_string removed — symbol name resolution is done inline in neverc_pe_symbols */

int neverc_pe_open(neverc_pe_file_t *f, const uint8_t *data, size_t len) {
    if (!f) return -1;
    memset(f, 0, sizeof(*f));
    if (!neverc_pe_is_valid(data, len)) return -1;

    f->data = data;
    f->data_len = len;
    f->owns_data = 0;

    uint32_t pe_off = rd32(data + 60);
    if ((uint64_t)pe_off + 4 + 20 > len) return pe_open_fail(f);
    const uint8_t *coff = data + pe_off + 4;

    f->file_header.machine               = rd16(coff + 0);
    f->file_header.number_of_sections    = rd16(coff + 2);
    f->file_header.time_date_stamp       = rd32(coff + 4);
    f->file_header.pointer_to_symbol_table = rd32(coff + 8);
    f->file_header.number_of_symbols     = rd32(coff + 12);
    f->file_header.size_of_optional_header = rd16(coff + 16);
    f->file_header.characteristics       = rd16(coff + 18);

    const uint8_t *opt = coff + 20;
    uint32_t opt_size = f->file_header.size_of_optional_header;
    /* size_of_optional_header is attacker-controlled and the reads below are
     * gated only on its *declared* value; require the declared optional header
     * to actually lie within the file, or opt+N runs past the buffer. opt is at
     * file offset pe_off+24 (PE sig 4 + COFF header 20). */
    if ((uint64_t)pe_off + 24 + opt_size > len)
        return pe_open_fail(f);
    if (opt_size == 0) {
        /* COFF objects omit the optional header. Images must include a PE32
         * or PE32+ header whose SizeOfOptionalHeader equals the standard
         * prefix plus NumberOfRvaAndSizes * 8. */
        f->is_64bit =
            f->file_header.machine == NEVERC_IMAGE_FILE_MACHINE_AMD64 ||
            f->file_header.machine == NEVERC_IMAGE_FILE_MACHINE_ARM64;
    } else {
        if (opt_size < 2)
            return pe_open_fail(f);
        f->optional_header.magic = rd16(opt);
        if (f->optional_header.magic == NEVERC_PE32P_MAGIC) {
            f->is_64bit = 1;
            if (opt_size < 112)
                return pe_open_fail(f);
            f->optional_header.image_base        = rd64(opt + 24);
            f->optional_header.section_alignment  = rd32(opt + 32);
            f->optional_header.file_alignment     = rd32(opt + 36);
            f->optional_header.size_of_image      = rd32(opt + 56);
            f->optional_header.size_of_headers    = rd32(opt + 60);
            f->optional_header.subsystem          = rd16(opt + 68);
            f->optional_header.number_of_rva_and_sizes = rd32(opt + 108);
            const uint8_t *dd = opt + 112;
            uint32_t ndd = f->optional_header.number_of_rva_and_sizes;
            if ((uint64_t)ndd * 8U != (uint64_t)opt_size - 112U)
                return pe_open_fail(f);
            uint32_t stored = ndd > 16 ? 16 : ndd;
            for (uint32_t i = 0; i < stored; i++) {
                f->optional_header.data_directory[i].virtual_address = rd32(dd + i*8);
                f->optional_header.data_directory[i].size = rd32(dd + i*8 + 4);
            }
        } else if (f->optional_header.magic == NEVERC_PE32_MAGIC) {
            if (opt_size < 96)
                return pe_open_fail(f);
            f->optional_header.image_base        = rd32(opt + 28);
            f->optional_header.section_alignment  = rd32(opt + 32);
            f->optional_header.file_alignment     = rd32(opt + 36);
            f->optional_header.size_of_image      = rd32(opt + 56);
            f->optional_header.size_of_headers    = rd32(opt + 60);
            f->optional_header.subsystem          = rd16(opt + 68);
            f->optional_header.number_of_rva_and_sizes = rd32(opt + 92);
            const uint8_t *dd = opt + 96;
            uint32_t ndd = f->optional_header.number_of_rva_and_sizes;
            if ((uint64_t)ndd * 8U != (uint64_t)opt_size - 96U)
                return pe_open_fail(f);
            uint32_t stored = ndd > 16 ? 16 : ndd;
            for (uint32_t i = 0; i < stored; i++) {
                f->optional_header.data_directory[i].virtual_address = rd32(dd + i*8);
                f->optional_header.data_directory[i].size = rd32(dd + i*8 + 4);
            }
        } else {
            return pe_open_fail(f);
        }

        if (f->optional_header.size_of_headers > len)
            return pe_open_fail(f);
    }

    /* Parse sections */
    const uint8_t *sec_start = opt + opt_size;
    uint16_t nsec = f->file_header.number_of_sections;
    /* sec_start <= data+len (optional header bounded above), so subtract rather
     * than add to keep the size product from overflowing size_t on 32-bit. */
    if ((size_t)nsec * 40 > len - (size_t)(sec_start - data))
        return pe_open_fail(f);

    f->sections = (neverc_pe_section_t *)calloc(nsec, sizeof(neverc_pe_section_t));
    if (nsec > 0 && !f->sections) return pe_open_fail(f);
    f->section_count = nsec;

    for (uint16_t i = 0; i < nsec; i++) {
        const uint8_t *s = sec_start + i * 40;
        memcpy(f->sections[i].name, s, 8);
        f->sections[i].name[8] = '\0';
        f->sections[i].virtual_size         = rd32(s + 8);
        f->sections[i].virtual_address      = rd32(s + 12);
        f->sections[i].size_of_raw_data     = rd32(s + 16);
        f->sections[i].pointer_to_raw_data  = rd32(s + 20);
        f->sections[i].pointer_to_relocations = rd32(s + 24);
        f->sections[i].number_of_relocations  = rd16(s + 32);
        f->sections[i].characteristics      = rd32(s + 36);
        if (!pe_range_in_file(len, f->sections[i].pointer_to_raw_data,
                              f->sections[i].size_of_raw_data) ||
            (f->sections[i].number_of_relocations != 0 &&
             !pe_range_in_file(
                 len, f->sections[i].pointer_to_relocations,
                 (uint64_t)f->sections[i].number_of_relocations * 10U)))
            return pe_open_fail(f);
    }

    /* COFF long names are stored as "/<decimal offset>" into the string table
     * after the symbol table. Short 8-byte names stay as-is. */
    int need_strtab = 0;
    for (uint16_t i = 0; i < nsec; i++) {
        if (f->sections[i].name[0] == '/') {
            need_strtab = 1;
            break;
        }
    }
    if (need_strtab) {
        uint32_t sym_off = f->file_header.pointer_to_symbol_table;
        uint32_t nsym = f->file_header.number_of_symbols;
        if (sym_off == 0)
            return pe_open_fail(f);
        uint64_t strtab_pos = (uint64_t)sym_off + (uint64_t)nsym * 18U;
        if (strtab_pos > len || len - (size_t)strtab_pos < 4)
            return pe_open_fail(f);
        uint32_t strtab_size = rd32(data + (size_t)strtab_pos);
        if (strtab_size < 4 || strtab_size > len - (size_t)strtab_pos)
            return pe_open_fail(f);
        const uint8_t *strtab = data + (size_t)strtab_pos;
        if (strtab_size > 4 && strtab[strtab_size - 1] != 0)
            return pe_open_fail(f);
        for (uint16_t i = 0; i < nsec; i++) {
            if (f->sections[i].name[0] != '/')
                continue;
            uint32_t off = 0;
            const char *p = f->sections[i].name + 1;
            if (*p == '\0')
                return pe_open_fail(f);
            while (*p) {
                if (*p < '0' || *p > '9')
                    return pe_open_fail(f);
                uint32_t digit = (uint32_t)(*p - '0');
                if (off > (UINT32_MAX - digit) / 10U)
                    return pe_open_fail(f);
                off = off * 10U + digit;
                p++;
            }
            if (off < 4 || off >= strtab_size)
                return pe_open_fail(f);
            const char *str = (const char *)(strtab + off);
            size_t maxn = strtab_size - off;
            const char *nul = (const char *)memchr(str, 0, maxn);
            if (!nul)
                return pe_open_fail(f);
            size_t slen = (size_t)(nul - str);
            if (slen >= sizeof(f->sections[i].name))
                slen = sizeof(f->sections[i].name) - 1;
            memcpy(f->sections[i].name, str, slen);
            f->sections[i].name[slen] = '\0';
        }
    }

    return 0;
}

void neverc_pe_close(neverc_pe_file_t *f) {
    if (!f) return;
    free(f->sections);
    if (f->owns_data) free((void *)f->data);
    memset(f, 0, sizeof(*f));
}

int neverc_pe_open_file(neverc_pe_file_t *f, const char *path) {
    if (!f) return -1;
    memset(f, 0, sizeof(*f));
    if (!path) return -1;
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    if (fseek(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return -1;
    }
    long sz = ftell(fp);
    if (sz <= 0) { fclose(fp); return -1; }
    if (fseek(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return -1;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(fp); return -1; }
    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf); fclose(fp); return -1;
    }
    fclose(fp);
    int rc = neverc_pe_open(f, buf, (size_t)sz);
    if (rc < 0) { free(buf); return -1; }
    f->owns_data = 1;
    return 0;
}

const neverc_pe_section_t *neverc_pe_section(const neverc_pe_file_t *f,
                                              const char *name) {
    if (!f || !name || (f->section_count != 0 && !f->sections))
        return NULL;
    for (uint32_t i = 0; i < f->section_count; i++) {
        if (strcmp(f->sections[i].name, name) == 0)
            return &f->sections[i];
    }
    return NULL;
}

int neverc_pe_section_data(const neverc_pe_file_t *f,
                            const neverc_pe_section_t *s,
                            uint8_t **out, size_t *out_len) {
    if (!out || !out_len) return -1;
    *out = NULL;
    *out_len = 0;
    if (!f || (!f->data && f->data_len != 0)) return -1;
    if (!s || s->size_of_raw_data == 0) {
        return s ? 0 : -1;
    }
    if (!pe_range_in_file(f->data_len, s->pointer_to_raw_data,
                          s->size_of_raw_data))
        return -1;
    *out = (uint8_t *)malloc(s->size_of_raw_data);
    if (!*out) return -1;
    memcpy(*out, f->data + s->pointer_to_raw_data, s->size_of_raw_data);
    *out_len = s->size_of_raw_data;
    return 0;
}

int neverc_pe_symbols(const neverc_pe_file_t *f,
                       neverc_pe_symbol_t **syms, int *count) {
    if (!syms || !count) return -1;
    *syms = NULL;
    *count = 0;
    if (!f || (!f->data && f->data_len != 0)) return -1;
    uint32_t sym_off = f->file_header.pointer_to_symbol_table;
    uint32_t nsym = f->file_header.number_of_symbols;
    if (sym_off == 0 || nsym == 0) return 0;
    if (nsym > INT_MAX ||
        (uint64_t)sym_off + (uint64_t)nsym * 18U > f->data_len)
        return -1;

    const uint8_t *sym_data = f->data + sym_off;
    size_t strtab_off = (size_t)nsym * 18;
    size_t strtab_pos = (size_t)sym_off + strtab_off;
    if (strtab_pos > f->data_len || f->data_len - strtab_pos < 4)
        return -1;
    uint32_t strtab_size = rd32(f->data + strtab_pos);
    if (strtab_size < 4 || strtab_size > f->data_len - strtab_pos)
        return -1;
    if (strtab_size > 4 && f->data[strtab_pos + strtab_size - 1] != 0)
        return -1;

    int real_count = 0;
    for (uint32_t i = 0; i < nsym; ) {
        real_count++;
        const uint8_t *rec = sym_data + (size_t)((uint64_t)i * 18U);
        uint8_t aux_count = rec[17];
        if ((uint32_t)aux_count > nsym - i - 1U)
            return -1;
        i += 1 + aux_count;
    }

    if (real_count < 0 ||
        (size_t)real_count > SIZE_MAX / sizeof(neverc_pe_symbol_t))
        return -1;

    *syms = (neverc_pe_symbol_t *)calloc((size_t)real_count, sizeof(neverc_pe_symbol_t));
    if (!*syms) return -1;

    int idx = 0;
    for (uint32_t i = 0; i < nsym && idx < real_count; ) {
        const uint8_t *e = sym_data + (size_t)((uint64_t)i * 18U);
        neverc_pe_symbol_t *s = &(*syms)[idx];

        if (e[0] || e[1] || e[2] || e[3]) {
            size_t n = 0;
            for (n = 0; n < 8 && e[n]; n++)
                s->name[n] = (char)e[n];
            s->name[n] = '\0';
        } else {
            uint32_t str_offset = rd32(e + 4);
            if (str_offset < 4 || str_offset >= strtab_size)
                goto fail;
            const char *str =
                (const char *)(f->data + strtab_pos + str_offset);
            size_t maxn = strtab_size - str_offset;
            const char *nul = (const char *)memchr(str, 0, maxn);
            if (!nul) goto fail;
            size_t slen = (size_t)(nul - str);
            if (slen >= sizeof(s->name)) slen = sizeof(s->name) - 1;
            memcpy(s->name, str, slen);
            s->name[slen] = '\0';
        }

        s->value          = rd32(e + 8);
        s->section_number = (int16_t)rd16(e + 12);
        s->type           = rd16(e + 14);
        s->storage_class  = e[16];

        uint8_t aux_count = e[17];
        i += 1 + aux_count;
        idx++;
    }
    *count = idx;
    return 0;

fail:
    free(*syms);
    *syms = NULL;
    *count = 0;
    return -1;
}

static int pe_rva_to_file_offset(const neverc_pe_file_t *f, uint32_t rva,
                                 size_t minimum, size_t *offset,
                                 size_t *available) {
    if (!f || !offset || !available ||
        (!f->data && f->data_len != 0) ||
        (f->section_count != 0 && !f->sections))
        return -1;

    uint64_t header_size = f->optional_header.size_of_headers;
    if (header_size > f->data_len)
        header_size = f->data_len;
    if ((uint64_t)rva < header_size) {
        uint64_t header_available = header_size - rva;
        size_t file_available = f->data_len - (size_t)rva;
        size_t n = header_available < file_available
                       ? (size_t)header_available
                       : file_available;
        if (minimum > n) return -1;
        *offset = (size_t)rva;
        *available = n;
        return 0;
    }

    for (uint32_t i = 0; i < f->section_count; i++) {
        const neverc_pe_section_t *section = &f->sections[i];
        uint64_t span = section->virtual_size;
        if (span < section->size_of_raw_data)
            span = section->size_of_raw_data;
        if ((uint64_t)rva < section->virtual_address)
            continue;
        uint64_t delta = (uint64_t)rva - section->virtual_address;
        if (delta >= span)
            continue;
        if (delta >= section->size_of_raw_data)
            return -1;
        uint64_t file_offset =
            (uint64_t)section->pointer_to_raw_data + delta;
        uint64_t raw_available = section->size_of_raw_data - delta;
        if (file_offset > f->data_len)
            return -1;
        size_t file_available = f->data_len - (size_t)file_offset;
        size_t n = raw_available < file_available
                       ? (size_t)raw_available
                       : file_available;
        if (minimum > n)
            return -1;
        *offset = (size_t)file_offset;
        *available = n;
        return 0;
    }
    return -1;
}

static int pe_cstring_at_rva(const neverc_pe_file_t *f, uint32_t rva,
                             size_t skip, char **result) {
    size_t offset = 0;
    size_t available = 0;
    if (!result || pe_rva_to_file_offset(f, rva, skip + 1U, &offset,
                                         &available) < 0)
        return -1;
    const uint8_t *start = f->data + offset + skip;
    if (!memchr(start, 0, available - skip))
        return -1;
    *result = (char *)start;
    return 0;
}

static int pe_import_table(const neverc_pe_file_t *f, const uint8_t **table,
                           size_t *descriptor_count) {
    if (!f || !table || !descriptor_count)
        return -1;
    *table = NULL;
    *descriptor_count = 0;
    if (f->optional_header.number_of_rva_and_sizes <=
        NEVERC_IMAGE_DIRECTORY_ENTRY_IMPORT)
        return 0;
    uint32_t import_rva =
        f->optional_header
            .data_directory[NEVERC_IMAGE_DIRECTORY_ENTRY_IMPORT]
            .virtual_address;
    uint32_t import_size =
        f->optional_header.data_directory[NEVERC_IMAGE_DIRECTORY_ENTRY_IMPORT]
            .size;
    if (import_rva == 0 && import_size == 0)
        return 0;
    if (import_rva == 0 || import_size < 20)
        return -1;

    size_t offset = 0;
    size_t available = 0;
    if (pe_rva_to_file_offset(f, import_rva, import_size, &offset,
                              &available) < 0)
        return -1;
    *table = f->data + offset;
    *descriptor_count = import_size / 20U;
    return 0;
}

static int pe_import_descriptor_is_zero(const uint8_t *entry) {
    for (size_t i = 0; i < 20; i++) {
        if (entry[i] != 0)
            return 0;
    }
    return 1;
}

static int pe_import_fail(char **names, int count) {
    for (int i = 0; i < count; i++)
        names[i] = NULL;
    return -1;
}

int neverc_pe_imported_libraries(const neverc_pe_file_t *f,
                                  char **names, int max_names) {
    if (max_names < 0 || (max_names > 0 && !names))
        return -1;
    if (max_names == 0)
        return 0;

    const uint8_t *table = NULL;
    size_t descriptor_count = 0;
    if (pe_import_table(f, &table, &descriptor_count) < 0)
        return -1;
    if (!table)
        return 0;

    int count = 0;
    for (size_t i = 0; i < descriptor_count; i++) {
        const uint8_t *entry = table + i * 20U;
        if (pe_import_descriptor_is_zero(entry))
            return count;
        uint32_t name_rva = rd32(entry + 12);
        char *name = NULL;
        if (name_rva == 0 || pe_cstring_at_rva(f, name_rva, 0, &name) < 0)
            return pe_import_fail(names, count);
        names[count++] = name;
        if (count == max_names)
            return count;
    }
    return pe_import_fail(names, count);
}

int neverc_pe_imported_symbols(const neverc_pe_file_t *f,
                                char **names, int max_names) {
    if (max_names < 0 || (max_names > 0 && !names))
        return -1;
    if (max_names == 0)
        return 0;

    const uint8_t *table = NULL;
    size_t descriptor_count = 0;
    if (pe_import_table(f, &table, &descriptor_count) < 0)
        return -1;
    if (!table)
        return 0;

    int count = 0;
    size_t thunk_size = f->is_64bit ? 8U : 4U;
    for (size_t i = 0; i < descriptor_count; i++) {
        const uint8_t *entry = table + i * 20U;
        if (pe_import_descriptor_is_zero(entry))
            return count;
        char *library_name = NULL;
        uint32_t name_rva = rd32(entry + 12);
        if (name_rva == 0 ||
            pe_cstring_at_rva(f, name_rva, 0, &library_name) < 0)
            return pe_import_fail(names, count);
        uint32_t thunk_rva = rd32(entry);
        if (thunk_rva == 0)
            thunk_rva = rd32(entry + 16);
        if (thunk_rva == 0)
            continue;

        size_t thunk_offset = 0;
        size_t available = 0;
        if (pe_rva_to_file_offset(f, thunk_rva, thunk_size, &thunk_offset,
                                  &available) < 0)
            return pe_import_fail(names, count);
        size_t thunk_count = available / thunk_size;
        int terminated = 0;
        for (size_t j = 0; j < thunk_count; j++) {
            const uint8_t *thunk = f->data + thunk_offset + j * thunk_size;
            uint64_t value = f->is_64bit ? rd64(thunk) : rd32(thunk);
            if (value == 0) {
                terminated = 1;
                break;
            }
            uint64_t ordinal_mask =
                f->is_64bit ? UINT64_C(0x8000000000000000)
                            : UINT64_C(0x80000000);
            if ((value & ordinal_mask) != 0)
                continue;
            if (value > UINT32_MAX)
                return pe_import_fail(names, count);
            char *name = NULL;
            if (pe_cstring_at_rva(f, (uint32_t)value, 2, &name) < 0)
                return pe_import_fail(names, count);
            names[count++] = name;
            if (count == max_names)
                return count;
        }
        if (!terminated)
            return pe_import_fail(names, count);
    }
    return pe_import_fail(names, count);
}

const char *neverc_pe_machine_string(uint16_t machine) {
    switch (machine) {
    case NEVERC_IMAGE_FILE_MACHINE_I386:  return "Intel 386";
    case NEVERC_IMAGE_FILE_MACHINE_AMD64: return "x86-64";
    case NEVERC_IMAGE_FILE_MACHINE_ARM:   return "ARM";
    case NEVERC_IMAGE_FILE_MACHINE_ARMNT: return "ARM Thumb-2";
    case NEVERC_IMAGE_FILE_MACHINE_ARM64: return "ARM64";
    default: return "Unknown";
    }
}
