/*
 * Mach-O format parser.
 * Supports 32-bit and 64-bit Mach-O files.
 * Modeled after Go's debug/macho package.
 */
#include "neverc/std/debug/macho.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

static uint32_t rd32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64le(const uint8_t *p) {
    return (uint64_t)rd32le(p) | ((uint64_t)rd32le(p+4) << 32);
}
static uint32_t rd32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint64_t rd64be(const uint8_t *p) {
    return ((uint64_t)rd32be(p) << 32) | (uint64_t)rd32be(p+4);
}

typedef uint32_t (*rd32_fn)(const uint8_t *);
typedef uint64_t (*rd64_fn)(const uint8_t *);

int neverc_macho_is_valid(const uint8_t *data, size_t len) {
    if (len < 4) return 0;
    uint32_t magic = rd32le(data);
    return magic == NEVERC_MH_MAGIC || magic == NEVERC_MH_MAGIC_64 ||
           magic == NEVERC_MH_CIGAM || magic == NEVERC_MH_CIGAM_64;
}

static void copy_name(char *dst, size_t dstsz, const uint8_t *src, size_t srcsz) {
    size_t n = 0;
    while (n < srcsz && n < dstsz - 1 && src[n]) {
        dst[n] = (char)src[n]; n++;
    }
    dst[n] = '\0';
}

int neverc_macho_open(neverc_macho_file_t *f, const uint8_t *data, size_t len) {
    memset(f, 0, sizeof(*f));
    if (!neverc_macho_is_valid(data, len)) return -1;

    f->data = data;
    f->data_len = len;
    f->owns_data = 0;

    uint32_t magic = rd32le(data);
    f->is_swap = (magic == NEVERC_MH_CIGAM || magic == NEVERC_MH_CIGAM_64);
    f->is_64bit = (magic == NEVERC_MH_MAGIC_64 || magic == NEVERC_MH_CIGAM_64);

    rd32_fn r32 = f->is_swap ? rd32be : rd32le;
    rd64_fn r64 = f->is_swap ? rd64be : rd64le;

    f->header.magic = r32(data);
    f->header.cpu = (int32_t)r32(data + 4);
    f->header.subcpu = (int32_t)r32(data + 8);
    f->header.type = r32(data + 12);
    f->header.ncmds = r32(data + 16);
    f->header.sizeofcmds = r32(data + 20);
    f->header.flags = r32(data + 24);

    uint32_t hdr_size = f->is_64bit ? 32 : 28;
    if (hdr_size + f->header.sizeofcmds > len) return -1;

    /* First pass: count segments, sections, dylibs */
    uint32_t seg_count = 0, sec_count = 0, dylib_count = 0;
    const uint8_t *cmd = data + hdr_size;
    for (uint32_t i = 0; i < f->header.ncmds; i++) {
        if ((size_t)(cmd - data) + 8 > len) break;
        uint32_t cmd_type = r32(cmd);
        uint32_t cmd_size = r32(cmd + 4);
        if (cmd_type == NEVERC_LC_SEGMENT) {
            seg_count++;
            sec_count += r32(cmd + 48);
        } else if (cmd_type == NEVERC_LC_SEGMENT_64) {
            seg_count++;
            sec_count += r32(cmd + 64);
        } else if (cmd_type == NEVERC_LC_LOAD_DYLIB) {
            dylib_count++;
        }
        cmd += cmd_size;
    }

    f->segments = (neverc_macho_segment_t *)calloc(seg_count ? seg_count : 1,
                                                    sizeof(neverc_macho_segment_t));
    f->sections = (neverc_macho_section_t *)calloc(sec_count ? sec_count : 1,
                                                    sizeof(neverc_macho_section_t));
    f->dylibs = (neverc_macho_dylib_t *)calloc(dylib_count ? dylib_count : 1,
                                                sizeof(neverc_macho_dylib_t));
    if (!f->segments || !f->sections || !f->dylibs) goto fail;

    /* Second pass: populate */
    cmd = data + hdr_size;
    uint32_t si = 0, sci = 0, di = 0;
    for (uint32_t i = 0; i < f->header.ncmds; i++) {
        if ((size_t)(cmd - data) + 8 > len) break;
        uint32_t cmd_type = r32(cmd);
        uint32_t cmd_size = r32(cmd + 4);

        if (cmd_type == NEVERC_LC_SEGMENT && si < seg_count) {
            neverc_macho_segment_t *seg = &f->segments[si++];
            copy_name(seg->name, sizeof(seg->name), cmd + 8, 16);
            seg->addr    = r32(cmd + 24);
            seg->memsz   = r32(cmd + 28);
            seg->offset  = r32(cmd + 32);
            seg->filesz  = r32(cmd + 36);
            seg->maxprot = r32(cmd + 40);
            seg->prot    = r32(cmd + 44);
            uint32_t nsects = r32(cmd + 48);
            seg->nsects = nsects;
            const uint8_t *sec = cmd + 56;
            for (uint32_t j = 0; j < nsects && sci < sec_count; j++) {
                neverc_macho_section_t *s = &f->sections[sci++];
                copy_name(s->name, sizeof(s->name), sec, 16);
                copy_name(s->segname, sizeof(s->segname), sec + 16, 16);
                s->addr   = r32(sec + 32);
                s->size   = r32(sec + 36);
                s->offset = r32(sec + 40);
                s->align  = r32(sec + 44);
                s->reloff = r32(sec + 48);
                s->nreloc = r32(sec + 52);
                s->flags  = r32(sec + 56);
                sec += 68;
            }
        } else if (cmd_type == NEVERC_LC_SEGMENT_64 && si < seg_count) {
            neverc_macho_segment_t *seg = &f->segments[si++];
            copy_name(seg->name, sizeof(seg->name), cmd + 8, 16);
            seg->addr    = r64(cmd + 24);
            seg->memsz   = r64(cmd + 32);
            seg->offset  = r64(cmd + 40);
            seg->filesz  = r64(cmd + 48);
            seg->maxprot = r32(cmd + 56);
            seg->prot    = r32(cmd + 60);
            uint32_t nsects = r32(cmd + 64);
            seg->nsects = nsects;
            const uint8_t *sec = cmd + 72;
            for (uint32_t j = 0; j < nsects && sci < sec_count; j++) {
                neverc_macho_section_t *s = &f->sections[sci++];
                copy_name(s->name, sizeof(s->name), sec, 16);
                copy_name(s->segname, sizeof(s->segname), sec + 16, 16);
                s->addr   = r64(sec + 32);
                s->size   = r64(sec + 40);
                s->offset = r32(sec + 48);
                s->align  = r32(sec + 52);
                s->reloff = r32(sec + 56);
                s->nreloc = r32(sec + 60);
                s->flags  = r32(sec + 64);
                sec += 80;
            }
        } else if (cmd_type == NEVERC_LC_SYMTAB) {
            f->symoff  = r32(cmd + 8);
            f->nsyms   = r32(cmd + 12);
            f->stroff  = r32(cmd + 16);
            f->strsize = r32(cmd + 20);
        } else if (cmd_type == NEVERC_LC_LOAD_DYLIB && di < dylib_count) {
            uint32_t name_off = r32(cmd + 8);
            if (name_off < cmd_size) {
                copy_name(f->dylibs[di].name, sizeof(f->dylibs[di].name),
                          cmd + name_off, cmd_size - name_off);
            }
            f->dylibs[di].time = r32(cmd + 12);
            f->dylibs[di].current_version = r32(cmd + 16);
            f->dylibs[di].compat_version = r32(cmd + 20);
            di++;
        }
        cmd += cmd_size;
    }

    f->segment_count = si;
    f->section_count = sci;
    f->dylib_count = di;
    return 0;

fail:
    neverc_macho_close(f);
    return -1;
}

void neverc_macho_close(neverc_macho_file_t *f) {
    free(f->segments);
    free(f->sections);
    free(f->dylibs);
    if (f->owns_data) free((void *)f->data);
    memset(f, 0, sizeof(*f));
}

int neverc_macho_open_file(neverc_macho_file_t *f, const char *path) {
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
    int rc = neverc_macho_open(f, buf, (size_t)sz);
    if (rc < 0) { free(buf); return -1; }
    f->owns_data = 1;
    return 0;
}

const neverc_macho_section_t *neverc_macho_section(const neverc_macho_file_t *f,
                                                     const char *name) {
    for (uint32_t i = 0; i < f->section_count; i++) {
        if (strcmp(f->sections[i].name, name) == 0)
            return &f->sections[i];
    }
    return NULL;
}

const neverc_macho_segment_t *neverc_macho_segment(const neverc_macho_file_t *f,
                                                     const char *name) {
    for (uint32_t i = 0; i < f->segment_count; i++) {
        if (strcmp(f->segments[i].name, name) == 0)
            return &f->segments[i];
    }
    return NULL;
}

int neverc_macho_section_data(const neverc_macho_file_t *f,
                               const neverc_macho_section_t *s,
                               uint8_t **out, size_t *out_len) {
    if (!s || s->size == 0) {
        *out = NULL; *out_len = 0; return 0;
    }
    if ((uint64_t)s->offset + s->size > f->data_len) return -1;
    *out = (uint8_t *)malloc((size_t)s->size);
    if (!*out) return -1;
    memcpy(*out, f->data + s->offset, (size_t)s->size);
    *out_len = (size_t)s->size;
    return 0;
}

int neverc_macho_symbols(const neverc_macho_file_t *f,
                          neverc_macho_symbol_t **syms, int *count) {
    *syms = NULL; *count = 0;
    if (f->nsyms == 0 || f->symoff == 0) return 0;

    rd32_fn r32 = f->is_swap ? rd32be : rd32le;
    rd64_fn r64 = f->is_swap ? rd64be : rd64le;

    int entry_size = f->is_64bit ? 16 : 12;
    if ((uint64_t)f->symoff + (uint64_t)f->nsyms * entry_size > f->data_len) return -1;
    if ((uint64_t)f->stroff + f->strsize > f->data_len) return -1;

    const uint8_t *sym_data = f->data + f->symoff;
    const uint8_t *str_data = f->data + f->stroff;

    *syms = (neverc_macho_symbol_t *)calloc(f->nsyms, sizeof(neverc_macho_symbol_t));
    if (!*syms) return -1;
    *count = (int)f->nsyms;

    for (uint32_t i = 0; i < f->nsyms; i++) {
        const uint8_t *e = sym_data + i * entry_size;
        neverc_macho_symbol_t *s = &(*syms)[i];

        uint32_t strx = r32(e);
        if (strx < f->strsize) {
            const char *name = (const char *)(str_data + strx);
            size_t nlen = strlen(name);
            if (nlen >= sizeof(s->name)) nlen = sizeof(s->name) - 1;
            memcpy(s->name, name, nlen);
            s->name[nlen] = '\0';
        }

        s->type = e[4];
        s->sect = e[5];
        s->desc = (int16_t)(e[6] | ((uint16_t)e[7] << 8));
        if (f->is_swap) s->desc = (int16_t)(((uint16_t)e[6] << 8) | e[7]);

        if (f->is_64bit) {
            s->value = r64(e + 8);
        } else {
            s->value = r32(e + 8);
        }
    }
    return 0;
}

const char *neverc_macho_cpu_string(int32_t cpu) {
    switch (cpu) {
    case NEVERC_CPU_TYPE_X86:    return "x86";
    case NEVERC_CPU_TYPE_X86_64: return "x86-64";
    case NEVERC_CPU_TYPE_ARM:    return "ARM";
    case NEVERC_CPU_TYPE_ARM64:  return "ARM64";
    default: return "Unknown";
    }
}

const char *neverc_macho_type_string(uint32_t type) {
    switch (type) {
    case NEVERC_MH_OBJECT:  return "Object";
    case NEVERC_MH_EXECUTE: return "Exec";
    case NEVERC_MH_DYLIB:   return "Dylib";
    case NEVERC_MH_BUNDLE:  return "Bundle";
    case NEVERC_MH_DSYM:    return "dSYM";
    default: return "Unknown";
    }
}
