/*
 * PE (Portable Executable) format parser.
 * Supports PE32 and PE32+ (64-bit).
 * Modeled after Go's debug/pe package.
 */
#include "neverc/std/debug/pe.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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

int neverc_pe_is_valid(const uint8_t *data, size_t len) {
    if (len < 64) return 0;
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
    memset(f, 0, sizeof(*f));
    if (!neverc_pe_is_valid(data, len)) return -1;

    f->data = data;
    f->data_len = len;
    f->owns_data = 0;

    uint32_t pe_off = rd32(data + 60);
    if ((uint64_t)pe_off + 4 + 20 > len) return -1;   /* 64-bit: avoid u32 wrap */
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
    if ((uint64_t)pe_off + 24 + opt_size > len) return -1;
    if (opt_size >= 2) {
        f->optional_header.magic = rd16(opt);
        f->is_64bit = (f->optional_header.magic == NEVERC_PE32P_MAGIC);

        if (f->is_64bit && opt_size >= 112) {
            f->optional_header.image_base        = rd64(opt + 24);
            f->optional_header.section_alignment  = rd32(opt + 32);
            f->optional_header.file_alignment     = rd32(opt + 36);
            f->optional_header.size_of_image      = rd32(opt + 56);
            f->optional_header.size_of_headers    = rd32(opt + 60);
            f->optional_header.subsystem          = rd16(opt + 68);
            f->optional_header.number_of_rva_and_sizes = rd32(opt + 108);
            const uint8_t *dd = opt + 112;
            uint32_t ndd = f->optional_header.number_of_rva_and_sizes;
            if (ndd > 16) ndd = 16;
            if (ndd > (opt_size - 112) / 8) ndd = (opt_size - 112) / 8;  /* fit in opt header */
            for (uint32_t i = 0; i < ndd; i++) {
                f->optional_header.data_directory[i].virtual_address = rd32(dd + i*8);
                f->optional_header.data_directory[i].size = rd32(dd + i*8 + 4);
            }
        } else if (!f->is_64bit && opt_size >= 96) {
            f->optional_header.image_base        = rd32(opt + 28);
            f->optional_header.section_alignment  = rd32(opt + 32);
            f->optional_header.file_alignment     = rd32(opt + 36);
            f->optional_header.size_of_image      = rd32(opt + 56);
            f->optional_header.size_of_headers    = rd32(opt + 60);
            f->optional_header.subsystem          = rd16(opt + 68);
            f->optional_header.number_of_rva_and_sizes = rd32(opt + 92);
            const uint8_t *dd = opt + 96;
            uint32_t ndd = f->optional_header.number_of_rva_and_sizes;
            if (ndd > 16) ndd = 16;
            if (ndd > (opt_size - 96) / 8) ndd = (opt_size - 96) / 8;  /* fit in opt header */
            for (uint32_t i = 0; i < ndd; i++) {
                f->optional_header.data_directory[i].virtual_address = rd32(dd + i*8);
                f->optional_header.data_directory[i].size = rd32(dd + i*8 + 4);
            }
        }
    }

    /* Parse sections */
    const uint8_t *sec_start = opt + opt_size;
    uint16_t nsec = f->file_header.number_of_sections;
    /* sec_start <= data+len (optional header bounded above), so subtract rather
     * than add to keep the size product from overflowing size_t on 32-bit. */
    if ((size_t)nsec * 40 > len - (size_t)(sec_start - data)) return -1;

    f->sections = (neverc_pe_section_t *)calloc(nsec, sizeof(neverc_pe_section_t));
    if (!f->sections) return -1;
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
    }

    return 0;
}

void neverc_pe_close(neverc_pe_file_t *f) {
    free(f->sections);
    if (f->owns_data) free((void *)f->data);
    memset(f, 0, sizeof(*f));
}

int neverc_pe_open_file(neverc_pe_file_t *f, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    if (sz <= 0) { fclose(fp); return -1; }
    fseek(fp, 0, SEEK_SET);
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
    for (uint32_t i = 0; i < f->section_count; i++) {
        if (strcmp(f->sections[i].name, name) == 0)
            return &f->sections[i];
    }
    return NULL;
}

int neverc_pe_section_data(const neverc_pe_file_t *f,
                            const neverc_pe_section_t *s,
                            uint8_t **out, size_t *out_len) {
    if (!s || s->size_of_raw_data == 0) {
        *out = NULL; *out_len = 0; return 0;
    }
    if ((uint64_t)s->pointer_to_raw_data + s->size_of_raw_data > f->data_len)
        return -1;
    *out = (uint8_t *)malloc(s->size_of_raw_data);
    if (!*out) return -1;
    memcpy(*out, f->data + s->pointer_to_raw_data, s->size_of_raw_data);
    *out_len = s->size_of_raw_data;
    return 0;
}

int neverc_pe_symbols(const neverc_pe_file_t *f,
                       neverc_pe_symbol_t **syms, int *count) {
    *syms = NULL; *count = 0;
    uint32_t sym_off = f->file_header.pointer_to_symbol_table;
    uint32_t nsym = f->file_header.number_of_symbols;
    if (sym_off == 0 || nsym == 0) return 0;
    if ((uint64_t)sym_off + (uint64_t)nsym * 18 > f->data_len) return -1;

    const uint8_t *sym_data = f->data + sym_off;
    size_t strtab_off = (size_t)nsym * 18;

    int real_count = 0;
    for (uint32_t i = 0; i < nsym; ) {
        real_count++;
        uint8_t aux_count = sym_data[i * 18 + 17];
        i += 1 + aux_count;
    }

    *syms = (neverc_pe_symbol_t *)calloc((size_t)real_count, sizeof(neverc_pe_symbol_t));
    if (!*syms) return -1;

    int idx = 0;
    for (uint32_t i = 0; i < nsym && idx < real_count; ) {
        const uint8_t *e = sym_data + i * 18;
        neverc_pe_symbol_t *s = &(*syms)[idx];

        if (e[0] || e[1] || e[2] || e[3]) {
            size_t n = 0;
            for (n = 0; n < 8 && e[n]; n++)
                s->name[n] = (char)e[n];
            s->name[n] = '\0';
        } else {
            uint32_t str_offset = rd32(e + 4);
            /* The COFF string table follows the symbol table; str_offset is
             * attacker-controlled, so bound the name to the file and stop at the
             * buffer end if it lacks a NUL (strlen would otherwise over-read). */
            uint64_t abs = (uint64_t)sym_off + strtab_off + str_offset;
            s->name[0] = '\0';
            if (abs < f->data_len) {
                const char *str = (const char *)(f->data + abs);
                size_t maxn = (size_t)(f->data_len - abs);
                size_t slen = 0;
                while (slen < maxn && str[slen]) slen++;
                if (slen >= sizeof(s->name)) slen = sizeof(s->name) - 1;
                memcpy(s->name, str, slen);
                s->name[slen] = '\0';
            }
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
}

int neverc_pe_imported_libraries(const neverc_pe_file_t *f,
                                  char **names, int max_names) {
    if (f->optional_header.number_of_rva_and_sizes <= NEVERC_IMAGE_DIRECTORY_ENTRY_IMPORT)
        return 0;
    uint32_t import_rva = f->optional_header.data_directory[NEVERC_IMAGE_DIRECTORY_ENTRY_IMPORT].virtual_address;
    uint32_t import_size = f->optional_header.data_directory[NEVERC_IMAGE_DIRECTORY_ENTRY_IMPORT].size;
    if (import_rva == 0 || import_size == 0) return 0;

    /* Find section containing import directory */
    const neverc_pe_section_t *sec = NULL;
    for (uint32_t i = 0; i < f->section_count; i++) {
        uint32_t va = f->sections[i].virtual_address;
        uint32_t sz = f->sections[i].virtual_size;
        if (sz == 0) sz = f->sections[i].size_of_raw_data;
        if (import_rva >= va && import_rva < va + sz) {
            sec = &f->sections[i];
            break;
        }
    }
    if (!sec) return 0;

    uint32_t delta = sec->virtual_address - sec->pointer_to_raw_data;
    uint32_t import_file_off = import_rva - delta;
    /* Overflow-safe: import_file_off + import_size are attacker-controlled u32. */
    if (import_file_off > f->data_len ||
        import_size > f->data_len - import_file_off) return 0;

    const uint8_t *idt = f->data + import_file_off;
    int lib_count = 0;

    for (int i = 0; lib_count < max_names; i++) {
        const uint8_t *entry = idt + i * 20;
        if (entry + 20 > f->data + f->data_len) break;
        uint32_t name_rva = rd32(entry + 12);
        if (name_rva == 0) break;
        uint32_t name_off = name_rva - delta;
        /* Only return a pointer the caller can safely strlen: require a NUL in
         * the file from name_off onward. */
        if (name_off < f->data_len &&
            memchr(f->data + name_off, 0, f->data_len - name_off) != NULL) {
            names[lib_count++] = (char *)(f->data + name_off);
        }
    }
    return lib_count;
}

int neverc_pe_imported_symbols(const neverc_pe_file_t *f,
                                char **names, int max_names) {
    (void)f; (void)names; (void)max_names;
    return 0;
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
