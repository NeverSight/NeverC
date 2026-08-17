/*
 * Mach-O format parser.
 * Supports 32-bit and 64-bit Mach-O files.
 * Modeled after Go's debug/macho package.
 */
#include "neverc/std/debug/macho.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

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

static int macho_allocation_fits(uint32_t count, size_t element_size) {
    return element_size == 0 ||
           (size_t)count <= SIZE_MAX / element_size;
}

typedef uint32_t (*rd32_fn)(const uint8_t *);
typedef uint64_t (*rd64_fn)(const uint8_t *);

static int macho_open_fail(neverc_macho_file_t *f) {
    neverc_macho_close(f);
    return -1;
}

static int macho_range_in_file(uint64_t offset, uint64_t size, size_t len) {
    return offset <= len && size <= (uint64_t)len - offset;
}

static int macho_section_has_file_data(uint32_t flags) {
    uint32_t type = flags & 0xffU;
    return type != 0x01U && type != 0x0cU && type != 0x12U;
}

int neverc_macho_is_valid(const uint8_t *data, size_t len) {
    if (!data || len < 4) return 0;
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
    if (!f) return -1;
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

    /* is_valid only checked the 4-byte magic; confirm the whole header fits
     * before reading its fields, or a truncated buffer is read past its end. */
    uint32_t hdr_size = f->is_64bit ? 32 : 28;
    if (len < hdr_size) return macho_open_fail(f);

    f->header.magic = r32(data);
    f->header.cpu = (int32_t)r32(data + 4);
    f->header.subcpu = (int32_t)r32(data + 8);
    f->header.type = r32(data + 12);
    f->header.ncmds = r32(data + 16);
    f->header.sizeofcmds = r32(data + 20);
    f->header.flags = r32(data + 24);

    if ((uint64_t)hdr_size + f->header.sizeofcmds > len ||
        f->header.ncmds > f->header.sizeofcmds / 8U)
        return macho_open_fail(f);
    size_t commands_end = (size_t)hdr_size + f->header.sizeofcmds;

    /* First pass: count segments, sections, dylibs. Every field is bounded to
     * the declared command and the file: cmd_size < 8 (no forward progress) or a
     * command overrunning the file stops the walk, the nsects read is gated on a
     * full segment header, and the section count mirrors the populate pass below
     * exactly so the allocations are always sufficient. */
    uint32_t seg_count = 0, sec_count = 0, dylib_count = 0;
    uint32_t symtab_count = 0;
    const uint8_t *cmd = data + hdr_size;
    for (uint32_t i = 0; i < f->header.ncmds; i++) {
        size_t cmd_offset = (size_t)(cmd - data);
        if (cmd_offset > commands_end || commands_end - cmd_offset < 8)
            return macho_open_fail(f);
        uint32_t cmd_type = r32(cmd);
        uint32_t cmd_size = r32(cmd + 4);
        if (cmd_size < 8 || cmd_size > commands_end - cmd_offset ||
            cmd_size % (f->is_64bit ? 8U : 4U) != 0)
            return macho_open_fail(f);
        if (cmd_type == NEVERC_LC_SEGMENT) {
            if (f->is_64bit || cmd_size < 56)
                return macho_open_fail(f);
            uint32_t ns = r32(cmd + 48);
            if ((uint64_t)ns * 68U != cmd_size - 56U ||
                sec_count > UINT32_MAX - ns)
                return macho_open_fail(f);
            seg_count++;
            sec_count += ns;
        } else if (cmd_type == NEVERC_LC_SEGMENT_64) {
            if (!f->is_64bit || cmd_size < 72)
                return macho_open_fail(f);
            uint32_t ns = r32(cmd + 64);
            if ((uint64_t)ns * 80U != cmd_size - 72U ||
                sec_count > UINT32_MAX - ns)
                return macho_open_fail(f);
            seg_count++;
            sec_count += ns;
        } else if (cmd_type == NEVERC_LC_LOAD_DYLIB) {
            if (cmd_size < 24)
                return macho_open_fail(f);
            uint32_t name_off = r32(cmd + 8);
            if (name_off < 24 || name_off >= cmd_size ||
                !memchr(cmd + name_off, 0, cmd_size - name_off))
                return macho_open_fail(f);
            dylib_count++;
        } else if (cmd_type == NEVERC_LC_SYMTAB) {
            if (cmd_size != 24 || ++symtab_count > 1)
                return macho_open_fail(f);
        }
        cmd += cmd_size;
    }
    if ((size_t)(cmd - data) != commands_end)
        return macho_open_fail(f);

    if (!macho_allocation_fits(
            seg_count, sizeof(neverc_macho_segment_t)) ||
        !macho_allocation_fits(
            sec_count, sizeof(neverc_macho_section_t)) ||
        !macho_allocation_fits(
            dylib_count, sizeof(neverc_macho_dylib_t)))
        return macho_open_fail(f);

    if (seg_count)
        f->segments = (neverc_macho_segment_t *)calloc(
            seg_count, sizeof(neverc_macho_segment_t));
    if (sec_count)
        f->sections = (neverc_macho_section_t *)calloc(
            sec_count, sizeof(neverc_macho_section_t));
    if (dylib_count)
        f->dylibs = (neverc_macho_dylib_t *)calloc(
            dylib_count, sizeof(neverc_macho_dylib_t));
    if ((seg_count && !f->segments) || (sec_count && !f->sections) ||
        (dylib_count && !f->dylibs))
        return macho_open_fail(f);

    /* Second pass: populate. Bounds mirror the counting pass exactly: the same
     * whole-command and per-section guards, so sci/si/di never exceed the
     * allocated arrays and no field is read past the command (and thus the
     * file). */
    cmd = data + hdr_size;
    uint32_t si = 0, sci = 0, di = 0;
    for (uint32_t i = 0; i < f->header.ncmds; i++) {
        if ((size_t)(cmd - data) + 8 > len) break;
        uint32_t cmd_type = r32(cmd);
        uint32_t cmd_size = r32(cmd + 4);
        if (cmd_size < 8 || (size_t)(cmd - data) + cmd_size > len) break;
        size_t cmd_end = (size_t)(cmd - data) + cmd_size;

        if (cmd_type == NEVERC_LC_SEGMENT && cmd_size >= 56 && si < seg_count) {
            neverc_macho_segment_t *seg = &f->segments[si++];
            copy_name(seg->name, sizeof(seg->name), cmd + 8, 16);
            seg->addr    = r32(cmd + 24);
            seg->memsz   = r32(cmd + 28);
            seg->offset  = r32(cmd + 32);
            seg->filesz  = r32(cmd + 36);
            seg->maxprot = r32(cmd + 40);
            seg->prot    = r32(cmd + 44);
            if (!macho_range_in_file(seg->offset, seg->filesz, len))
                goto fail;
            uint32_t nsects = r32(cmd + 48);
            seg->nsects = nsects;
            seg->flag = r32(cmd + 52);
            const uint8_t *sec = cmd + 56;
            for (uint32_t j = 0; j < nsects && sci < sec_count &&
                                (size_t)(sec - data) + 68 <= cmd_end; j++) {
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
                if ((macho_section_has_file_data(s->flags) &&
                     !macho_range_in_file(s->offset, s->size, len)) ||
                    (s->nreloc != 0 &&
                     !macho_range_in_file(
                         s->reloff, (uint64_t)s->nreloc * 8U, len)))
                    goto fail;
                sec += 68;
            }
        } else if (cmd_type == NEVERC_LC_SEGMENT_64 && cmd_size >= 72 && si < seg_count) {
            neverc_macho_segment_t *seg = &f->segments[si++];
            copy_name(seg->name, sizeof(seg->name), cmd + 8, 16);
            seg->addr    = r64(cmd + 24);
            seg->memsz   = r64(cmd + 32);
            seg->offset  = r64(cmd + 40);
            seg->filesz  = r64(cmd + 48);
            seg->maxprot = r32(cmd + 56);
            seg->prot    = r32(cmd + 60);
            if (!macho_range_in_file(seg->offset, seg->filesz, len))
                goto fail;
            uint32_t nsects = r32(cmd + 64);
            seg->nsects = nsects;
            seg->flag = r32(cmd + 68);
            const uint8_t *sec = cmd + 72;
            for (uint32_t j = 0; j < nsects && sci < sec_count &&
                                (size_t)(sec - data) + 80 <= cmd_end; j++) {
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
                if ((macho_section_has_file_data(s->flags) &&
                     !macho_range_in_file(s->offset, s->size, len)) ||
                    (s->nreloc != 0 &&
                     !macho_range_in_file(
                         s->reloff, (uint64_t)s->nreloc * 8U, len)))
                    goto fail;
                sec += 80;
            }
        } else if (cmd_type == NEVERC_LC_SYMTAB && cmd_size >= 24) {
            f->symoff  = r32(cmd + 8);
            f->nsyms   = r32(cmd + 12);
            f->stroff  = r32(cmd + 16);
            f->strsize = r32(cmd + 20);
            size_t entry_size = f->is_64bit ? 16U : 12U;
            if (!macho_range_in_file(
                    f->symoff, (uint64_t)f->nsyms * entry_size, len) ||
                !macho_range_in_file(f->stroff, f->strsize, len) ||
                (f->nsyms != 0 && f->strsize == 0))
                goto fail;
        } else if (cmd_type == NEVERC_LC_LOAD_DYLIB && cmd_size >= 24 && di < dylib_count) {
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
    if (!f) return;
    free(f->segments);
    free(f->sections);
    free(f->dylibs);
    if (f->owns_data) free((void *)f->data);
    memset(f, 0, sizeof(*f));
}

int neverc_macho_open_file(neverc_macho_file_t *f, const char *path) {
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
    int rc = neverc_macho_open(f, buf, (size_t)sz);
    if (rc < 0) { free(buf); return -1; }
    f->owns_data = 1;
    return 0;
}

const neverc_macho_section_t *neverc_macho_section(const neverc_macho_file_t *f,
                                                     const char *name) {
    if (!f || !name || (f->section_count != 0 && !f->sections))
        return NULL;
    for (uint32_t i = 0; i < f->section_count; i++) {
        if (strcmp(f->sections[i].name, name) == 0)
            return &f->sections[i];
    }
    return NULL;
}

const neverc_macho_segment_t *neverc_macho_segment(const neverc_macho_file_t *f,
                                                     const char *name) {
    if (!f || !name || (f->segment_count != 0 && !f->segments))
        return NULL;
    for (uint32_t i = 0; i < f->segment_count; i++) {
        if (strcmp(f->segments[i].name, name) == 0)
            return &f->segments[i];
    }
    return NULL;
}

int neverc_macho_section_data(const neverc_macho_file_t *f,
                               const neverc_macho_section_t *s,
                               uint8_t **out, size_t *out_len) {
    if (!out || !out_len) return -1;
    *out = NULL;
    *out_len = 0;
    if (!f || (!f->data && f->data_len != 0) || !s)
        return -1;
    if (s->size == 0) {
        return 0;
    }
    if (!macho_section_has_file_data(s->flags))
        return 0;
    if (!macho_range_in_file(s->offset, s->size, f->data_len) ||
        s->size > SIZE_MAX)
        return -1;
    *out = (uint8_t *)malloc((size_t)s->size);
    if (!*out) return -1;
    memcpy(*out, f->data + s->offset, (size_t)s->size);
    *out_len = (size_t)s->size;
    return 0;
}

int neverc_macho_symbols(const neverc_macho_file_t *f,
                          neverc_macho_symbol_t **syms, int *count) {
    if (!syms || !count) return -1;
    *syms = NULL;
    *count = 0;
    if (!f || (!f->data && f->data_len != 0)) return -1;
    if (f->nsyms == 0) return 0;
    if (f->symoff == 0 || f->strsize == 0 || f->nsyms > INT_MAX)
        return -1;

    rd32_fn r32 = f->is_swap ? rd32be : rd32le;
    rd64_fn r64 = f->is_swap ? rd64be : rd64le;

    size_t entry_size = f->is_64bit ? 16U : 12U;
    if (!macho_range_in_file(f->symoff,
                             (uint64_t)f->nsyms * entry_size,
                             f->data_len) ||
        !macho_range_in_file(f->stroff, f->strsize, f->data_len))
        return -1;

    const uint8_t *sym_data = f->data + f->symoff;
    const uint8_t *str_data = f->data + f->stroff;

    if (!macho_allocation_fits(f->nsyms, sizeof(neverc_macho_symbol_t)))
        return -1;

    *syms = (neverc_macho_symbol_t *)calloc(f->nsyms, sizeof(neverc_macho_symbol_t));
    if (!*syms) return -1;
    for (uint32_t i = 0; i < f->nsyms; i++) {
        const uint8_t *e = sym_data + i * entry_size;
        neverc_macho_symbol_t *s = &(*syms)[i];

        uint32_t strx = r32(e);
        if (strx >= f->strsize)
            goto fail;
        const char *name = (const char *)(str_data + strx);
        size_t maxn = (size_t)(f->strsize - strx);
        const char *nul = (const char *)memchr(name, 0, maxn);
        if (!nul)
            goto fail;
        size_t nlen = (size_t)(nul - name);
        if (nlen >= sizeof(s->name)) nlen = sizeof(s->name) - 1;
        memcpy(s->name, name, nlen);
        s->name[nlen] = '\0';

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
    *count = (int)f->nsyms;
    return 0;

fail:
    free(*syms);
    *syms = NULL;
    *count = 0;
    return -1;
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
