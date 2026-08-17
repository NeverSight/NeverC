/*
 * ELF (Executable and Linkable Format) parser.
 * Supports ELF32 and ELF64 on all platforms.
 * Modeled after Go's debug/elf package.
 */
#include "neverc/std/debug/elf.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <limits.h>

/* ===== Endian Helpers ===== */

static uint16_t rd16le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}
static uint32_t rd32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static uint64_t rd64le(const uint8_t *p) {
    return (uint64_t)rd32le(p) | ((uint64_t)rd32le(p + 4) << 32);
}

static uint16_t rd16be(const uint8_t *p) {
    return ((uint16_t)p[0] << 8) | (uint16_t)p[1];
}
static uint32_t rd32be(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
static uint64_t rd64be(const uint8_t *p) {
    return ((uint64_t)rd32be(p) << 32) | (uint64_t)rd32be(p + 4);
}

typedef uint16_t (*rd16_fn)(const uint8_t *);
typedef uint32_t (*rd32_fn)(const uint8_t *);
typedef uint64_t (*rd64_fn)(const uint8_t *);

/* ===== String Table Helper ===== */

static int elf_copy_strtab_name(const uint8_t *strtab, size_t strtab_len,
                                uint64_t offset, char *dst, size_t dstsz) {
    if (!strtab || !dst || dstsz == 0 || offset >= strtab_len)
        return -1;
    const uint8_t *start = strtab + (size_t)offset;
    const uint8_t *nul = (const uint8_t *)memchr(
        start, 0, strtab_len - (size_t)offset);
    if (!nul)
        return -1;
    size_t nlen = (size_t)(nul - start);
    if (nlen >= dstsz)
        nlen = dstsz - 1;
    memcpy(dst, start, nlen);
    dst[nlen] = '\0';
    return 0;
}

static int elf_borrow_string(const uint8_t *strtab, size_t strtab_len,
                             uint64_t offset, const char **out) {
    if (!strtab || !out || offset >= strtab_len)
        return -1;
    if (memchr(strtab + (size_t)offset, 0,
               strtab_len - (size_t)offset) == NULL)
        return -1;
    *out = (const char *)(strtab + (size_t)offset);
    return 0;
}

static int elf_strtab_terminated(const uint8_t *strtab, size_t strtab_len) {
    return strtab != NULL && strtab_len > 0 && strtab[strtab_len - 1] == 0;
}

/* Resolve section names from e_shstrndx. SHN_UNDEF (0) means the file has no
 * name table — do not treat section 0 as one (its sh_size is a section count
 * when extended numbering is in use). A declared table must be SHT_STRTAB,
 * lie inside the file, and contain a NUL-terminated string for every name
 * index; otherwise Open would accept a malformed header and leave names empty
 * or, worse, interpret an out-of-range sh_size as a string-table length. */
static int elf_load_section_names(neverc_elf_file_t *f, uint32_t shstrndx) {
    if (shstrndx == NEVERC_SHN_UNDEF)
        return 0;
    if (shstrndx >= f->section_count || !f->data || !f->sections)
        return -1;

    const neverc_elf_section_t *str = &f->sections[shstrndx];
    if (str->type != NEVERC_SHT_STRTAB)
        return -1;
    if (str->offset > f->data_len ||
        str->size > f->data_len - str->offset)
        return -1;

    const uint8_t *strtab = f->data + (size_t)str->offset;
    size_t strtab_len = (size_t)str->size;
    if (!elf_strtab_terminated(strtab, strtab_len))
        return -1;
    for (uint32_t i = 0; i < f->section_count; i++) {
        uint64_t name_idx = f->sections[i].name_idx;
        if (name_idx >= strtab_len)
            return -1;
        const uint8_t *start = strtab + (size_t)name_idx;
        const uint8_t *nul = (const uint8_t *)memchr(
            start, 0, strtab_len - (size_t)name_idx);
        if (!nul)
            return -1;
        size_t nlen = (size_t)(nul - start);
        if (nlen >= sizeof(f->sections[i].name))
            nlen = sizeof(f->sections[i].name) - 1;
        memcpy(f->sections[i].name, start, nlen);
        f->sections[i].name[nlen] = '\0';
    }
    return 0;
}

/* ===== Parsing ===== */

int neverc_elf_is_valid(const uint8_t *data, size_t len) {
    if (!data || len < 16) return 0;
    return data[0] == 0x7f && data[1] == 'E' && data[2] == 'L' && data[3] == 'F';
}

static int elf_range_in_file(size_t len, uint64_t offset, uint64_t size) {
    return offset <= len && size <= (uint64_t)len - offset;
}

static int elf_alloc_count_ok(uint32_t count, size_t elem_size) {
    return elem_size == 0 || count <= SIZE_MAX / elem_size;
}

/* Resolve ELF extended numbering (e_shnum == 0, e_phnum == PN_XNUM,
 * e_shstrndx == SHN_XINDEX) from section header 0. Without this, a file that
 * uses the sentinels is either accepted as empty or parsed as 65535 headers. */
static int elf_resolve_layout(neverc_elf_file_t *f, rd16_fn r16, rd32_fn r32,
                              rd64_fn r64, uint64_t *phoff, uint16_t *phentsize,
                              uint32_t *phnum, uint64_t *shoff,
                              uint16_t *shentsize, uint32_t *shnum,
                              uint32_t *shstrndx) {
    const uint8_t *d = f->data;
    int is64 = f->header.class_ == NEVERC_ELFCLASS64;
    uint16_t min_shentsize = is64 ? 64 : 40;

    if (is64) {
        *phoff = r64(d + 32);
        *phentsize = r16(d + 54);
        *phnum = r16(d + 56);
        *shoff = r64(d + 40);
        *shentsize = r16(d + 58);
        *shnum = r16(d + 60);
        *shstrndx = r16(d + 62);
    } else {
        *phoff = r32(d + 28);
        *phentsize = r16(d + 42);
        *phnum = r16(d + 44);
        *shoff = r32(d + 32);
        *shentsize = r16(d + 46);
        *shnum = r16(d + 48);
        *shstrndx = r16(d + 50);
    }

    int need_sh0 = (*shstrndx == NEVERC_SHN_XINDEX) ||
                   (*phnum == NEVERC_PN_XNUM) ||
                   (*shnum == 0 && *shoff != 0);
    if (!need_sh0)
        return 0;

    if (*shentsize < min_shentsize)
        return -1;
    if (*shoff == 0 || !elf_range_in_file(f->data_len, *shoff, *shentsize))
        return -1;

    const uint8_t *sh0 = d + (size_t)*shoff;
    uint32_t sh_type = r32(sh0 + 4);
    uint64_t sh_size;
    uint32_t sh_link, sh_info;
    if (sh_type != NEVERC_SHT_NULL)
        return -1;
    if (is64) {
        sh_size = r64(sh0 + 32);
        sh_link = r32(sh0 + 40);
        sh_info = r32(sh0 + 44);
    } else {
        sh_size = r32(sh0 + 20);
        sh_link = r32(sh0 + 24);
        sh_info = r32(sh0 + 28);
    }

    if (*shnum == 0) {
        if (sh_size == 0 || sh_size > UINT32_MAX)
            return -1;
        *shnum = (uint32_t)sh_size;
    }
    if (*phnum == NEVERC_PN_XNUM)
        *phnum = sh_info;
    if (*shstrndx == NEVERC_SHN_XINDEX)
        *shstrndx = sh_link;
    return 0;
}

static int parse_sections_32(neverc_elf_file_t *f, rd32_fn r32, uint32_t shoff,
                             uint16_t shentsize, uint32_t shnum,
                             uint32_t shstrndx) {
    const uint8_t *d = f->data;

    if (shnum == 0) return 0;
    if (shoff == 0) return -1;
    /* Each entry is read as a fixed 40-byte Elf32_Shdr; a smaller (or zero)
     * shentsize would defeat the table bound while the fixed-offset reads still
     * run off the buffer. The shoff>len guard + division avoid overflow. */
    if (shentsize < 40) return -1;
    if (shoff > f->data_len ||
        (uint64_t)shnum > (f->data_len - shoff) / shentsize) return -1;
    if (!elf_alloc_count_ok(shnum, sizeof(neverc_elf_section_t))) return -1;

    f->sections = (neverc_elf_section_t *)calloc(shnum, sizeof(neverc_elf_section_t));
    if (!f->sections) return -1;
    f->section_count = shnum;

    for (uint32_t i = 0; i < shnum; i++) {
        const uint8_t *sh =
            d + (size_t)shoff + (size_t)((uint64_t)i * shentsize);
        f->sections[i].name_idx = r32(sh + 0);
        f->sections[i].type     = r32(sh + 4);
        f->sections[i].flags    = r32(sh + 8);
        f->sections[i].addr     = r32(sh + 12);
        f->sections[i].offset   = r32(sh + 16);
        f->sections[i].size     = r32(sh + 20);
        f->sections[i].link     = r32(sh + 24);
        f->sections[i].info     = r32(sh + 28);
        f->sections[i].addralign= r32(sh + 32);
        f->sections[i].entsize  = r32(sh + 36);
        if (f->sections[i].type != NEVERC_SHT_NOBITS &&
            f->sections[i].type != NEVERC_SHT_NULL &&
            !elf_range_in_file(f->data_len, f->sections[i].offset,
                               f->sections[i].size))
            return -1;
    }

    return elf_load_section_names(f, shstrndx);
}

static int parse_sections_64(neverc_elf_file_t *f, rd32_fn r32, rd64_fn r64,
                             uint64_t shoff, uint16_t shentsize, uint32_t shnum,
                             uint32_t shstrndx) {
    const uint8_t *d = f->data;

    if (shnum == 0) return 0;
    if (shoff == 0) return -1;
    /* Each entry is read as a fixed 64-byte Elf64_Shdr; reject a smaller/zero
     * shentsize and bound the table without overflowing (shoff is 64-bit and
     * attacker-controlled, so shoff + shnum*shentsize could wrap). */
    if (shentsize < 64) return -1;
    if (shoff > f->data_len ||
        (uint64_t)shnum > (f->data_len - shoff) / shentsize) return -1;
    if (!elf_alloc_count_ok(shnum, sizeof(neverc_elf_section_t))) return -1;

    f->sections = (neverc_elf_section_t *)calloc(shnum, sizeof(neverc_elf_section_t));
    if (!f->sections) return -1;
    f->section_count = shnum;

    for (uint32_t i = 0; i < shnum; i++) {
        const uint8_t *sh =
            d + (size_t)shoff + (size_t)((uint64_t)i * shentsize);
        f->sections[i].name_idx = r32(sh + 0);
        f->sections[i].type     = r32(sh + 4);
        f->sections[i].flags    = r64(sh + 8);
        f->sections[i].addr     = r64(sh + 16);
        f->sections[i].offset   = r64(sh + 24);
        f->sections[i].size     = r64(sh + 32);
        f->sections[i].link     = r32(sh + 40);
        f->sections[i].info     = r32(sh + 44);
        f->sections[i].addralign= r64(sh + 48);
        f->sections[i].entsize  = r64(sh + 56);
        if (f->sections[i].type != NEVERC_SHT_NOBITS &&
            f->sections[i].type != NEVERC_SHT_NULL &&
            !elf_range_in_file(f->data_len, f->sections[i].offset,
                               f->sections[i].size))
            return -1;
    }

    return elf_load_section_names(f, shstrndx);
}

static int parse_progs_32(neverc_elf_file_t *f, rd32_fn r32, uint32_t phoff,
                          uint16_t phentsize, uint32_t phnum) {
    const uint8_t *d = f->data;

    if (phnum == 0) return 0;
    if (phoff == 0) return -1;
    /* Each entry is read as a fixed 32-byte Elf32_Phdr; reject a smaller/zero
     * phentsize and bound the table without overflowing. */
    if (phentsize < 32) return -1;
    if (phoff > f->data_len ||
        (uint64_t)phnum > (f->data_len - phoff) / phentsize) return -1;
    if (!elf_alloc_count_ok(phnum, sizeof(neverc_elf_prog_t))) return -1;

    f->progs = (neverc_elf_prog_t *)calloc(phnum, sizeof(neverc_elf_prog_t));
    if (!f->progs) return -1;
    f->prog_count = phnum;

    for (uint32_t i = 0; i < phnum; i++) {
        const uint8_t *ph =
            d + (size_t)phoff + (size_t)((uint64_t)i * phentsize);
        f->progs[i].type   = r32(ph + 0);
        f->progs[i].offset = r32(ph + 4);
        f->progs[i].vaddr  = r32(ph + 8);
        f->progs[i].paddr  = r32(ph + 12);
        f->progs[i].filesz = r32(ph + 16);
        f->progs[i].memsz  = r32(ph + 20);
        f->progs[i].flags  = r32(ph + 24);
        f->progs[i].align  = r32(ph + 28);
        if (f->progs[i].filesz != 0 &&
            !elf_range_in_file(f->data_len, f->progs[i].offset,
                               f->progs[i].filesz))
            return -1;
    }
    return 0;
}

static int parse_progs_64(neverc_elf_file_t *f, rd32_fn r32, rd64_fn r64,
                          uint64_t phoff, uint16_t phentsize, uint32_t phnum) {
    const uint8_t *d = f->data;

    if (phnum == 0) return 0;
    if (phoff == 0) return -1;
    /* Each entry is read as a fixed 56-byte Elf64_Phdr; reject a smaller/zero
     * phentsize and bound the table without overflowing (phoff is 64-bit). */
    if (phentsize < 56) return -1;
    if (phoff > f->data_len ||
        (uint64_t)phnum > (f->data_len - phoff) / phentsize) return -1;
    if (!elf_alloc_count_ok(phnum, sizeof(neverc_elf_prog_t))) return -1;

    f->progs = (neverc_elf_prog_t *)calloc(phnum, sizeof(neverc_elf_prog_t));
    if (!f->progs) return -1;
    f->prog_count = phnum;

    for (uint32_t i = 0; i < phnum; i++) {
        const uint8_t *ph =
            d + (size_t)phoff + (size_t)((uint64_t)i * phentsize);
        f->progs[i].type   = r32(ph + 0);
        f->progs[i].flags  = r32(ph + 4);
        f->progs[i].offset = r64(ph + 8);
        f->progs[i].vaddr  = r64(ph + 16);
        f->progs[i].paddr  = r64(ph + 24);
        f->progs[i].filesz = r64(ph + 32);
        f->progs[i].memsz  = r64(ph + 40);
        f->progs[i].align  = r64(ph + 48);
        if (f->progs[i].filesz != 0 &&
            !elf_range_in_file(f->data_len, f->progs[i].offset,
                               f->progs[i].filesz))
            return -1;
    }
    return 0;
}

int neverc_elf_open(neverc_elf_file_t *f, const uint8_t *data, size_t len) {
    if (!f) return -1;
    memset(f, 0, sizeof(*f));
    if (!neverc_elf_is_valid(data, len)) return -1;

    uint8_t class_ = data[4];
    uint8_t data_enc = data[5];
    if (data[6] != 1 ||
        (data_enc != NEVERC_ELFDATA2LSB &&
         data_enc != NEVERC_ELFDATA2MSB))
        return -1;
    if ((class_ == NEVERC_ELFCLASS32 && len < 52) ||
        (class_ == NEVERC_ELFCLASS64 && len < 64) ||
        (class_ != NEVERC_ELFCLASS32 && class_ != NEVERC_ELFCLASS64))
        return -1;

    f->data = data;
    f->data_len = len;
    f->owns_data = 0;

    f->header.class_ = class_;
    f->header.data = data_enc;
    f->header.osabi = data[7];
    f->header.abi_version = data[8];
    memcpy(f->header.ident, data, 16);

    rd16_fn r16 = (data_enc == NEVERC_ELFDATA2MSB) ? rd16be : rd16le;
    rd32_fn r32 = (data_enc == NEVERC_ELFDATA2MSB) ? rd32be : rd32le;
    rd64_fn r64 = (data_enc == NEVERC_ELFDATA2MSB) ? rd64be : rd64le;

    uint64_t phoff = 0, shoff = 0;
    uint16_t phentsize = 0, shentsize = 0;
    uint32_t phnum = 0, shnum = 0, shstrndx = 0;
    if (elf_resolve_layout(f, r16, r32, r64, &phoff, &phentsize, &phnum,
                           &shoff, &shentsize, &shnum, &shstrndx) < 0)
        goto fail;

    if (class_ == NEVERC_ELFCLASS32) {
        f->header.type = r16(data + 16);
        f->header.machine = r16(data + 18);
        f->header.version = r32(data + 20);
        f->header.entry = r32(data + 24);
        if (parse_progs_32(f, r32, (uint32_t)phoff, phentsize, phnum) < 0)
            goto fail;
        if (parse_sections_32(f, r32, (uint32_t)shoff, shentsize, shnum,
                              shstrndx) < 0)
            goto fail;
    } else if (class_ == NEVERC_ELFCLASS64) {
        f->header.type = r16(data + 16);
        f->header.machine = r16(data + 18);
        f->header.version = r32(data + 20);
        f->header.entry = r64(data + 24);
        if (parse_progs_64(f, r32, r64, phoff, phentsize, phnum) < 0)
            goto fail;
        if (parse_sections_64(f, r32, r64, shoff, shentsize, shnum,
                              shstrndx) < 0)
            goto fail;
    }
    return 0;

fail:
    neverc_elf_close(f);
    return -1;
}

void neverc_elf_close(neverc_elf_file_t *f) {
    if (!f) return;
    free(f->sections);
    free(f->progs);
    if (f->owns_data) free((void *)f->data);
    memset(f, 0, sizeof(*f));
}

int neverc_elf_open_file(neverc_elf_file_t *f, const char *path) {
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
        free(buf);
        fclose(fp);
        return -1;
    }
    fclose(fp);

    int rc = neverc_elf_open(f, buf, (size_t)sz);
    if (rc < 0) { free(buf); return -1; }
    f->owns_data = 1;
    return 0;
}

const neverc_elf_section_t *neverc_elf_section(const neverc_elf_file_t *f,
                                                const char *name) {
    if (!f || !name || (f->section_count != 0 && !f->sections)) return NULL;
    for (uint32_t i = 0; i < f->section_count; i++) {
        if (strcmp(f->sections[i].name, name) == 0)
            return &f->sections[i];
    }
    return NULL;
}

const neverc_elf_section_t *neverc_elf_section_by_type(const neverc_elf_file_t *f,
                                                        uint32_t type) {
    if (!f || (f->section_count != 0 && !f->sections)) return NULL;
    for (uint32_t i = 0; i < f->section_count; i++) {
        if (f->sections[i].type == type)
            return &f->sections[i];
    }
    return NULL;
}

int neverc_elf_section_data(const neverc_elf_file_t *f,
                             const neverc_elf_section_t *s,
                             uint8_t **out, size_t *out_len) {
    if (!out || !out_len) return -1;
    *out = NULL;
    *out_len = 0;
    if (!f) return -1;
    if (!s || s->type == NEVERC_SHT_NOBITS || s->type == NEVERC_SHT_NULL) {
        return 0;
    }
    if (s->offset > f->data_len || s->size > f->data_len - s->offset) return -1;
    if (s->size == 0) return 0;
    if (!f->data) return -1;

    *out = (uint8_t *)malloc((size_t)s->size);
    if (!*out) return -1;
    memcpy(*out, f->data + (size_t)s->offset, (size_t)s->size);
    *out_len = (size_t)s->size;
    return 0;
}

static int get_symbols_common(const neverc_elf_file_t *f, uint32_t sh_type,
                               neverc_elf_symbol_t **syms, int *count) {
    if (!syms || !count) return -1;
    *syms = NULL;
    *count = 0;
    if (!f || !f->data || (f->section_count != 0 && !f->sections))
        return -1;

    const neverc_elf_section_t *symtab = NULL;
    for (uint32_t i = 0; i < f->section_count; i++) {
        if (f->sections[i].type == sh_type) { symtab = &f->sections[i]; break; }
    }
    if (!symtab) return 0;

    if (!elf_range_in_file(f->data_len, symtab->offset, symtab->size))
        return -1;
    const uint8_t *sym_data = f->data + (size_t)symtab->offset;

    rd32_fn r32 = (f->header.data == NEVERC_ELFDATA2MSB) ? rd32be : rd32le;
    rd64_fn r64 = (f->header.data == NEVERC_ELFDATA2MSB) ? rd64be : rd64le;
    rd16_fn r16 = (f->header.data == NEVERC_ELFDATA2MSB) ? rd16be : rd16le;

    uint64_t entry_size;
    if (f->header.class_ == NEVERC_ELFCLASS32) {
        entry_size = 16;
    } else if (f->header.class_ == NEVERC_ELFCLASS64) {
        entry_size = 24;
    } else {
        return -1;
    }
    /* Producers sometimes leave sh_entsize 0; use the class-defined size. */
    uint64_t stride = symtab->entsize ? symtab->entsize : entry_size;
    if (stride < entry_size || symtab->size % stride != 0) return -1;
    uint64_t sym_count = symtab->size / stride;
    if (sym_count <= 1) return 0;
    if (sym_count - 1 > INT_MAX ||
        sym_count - 1 > SIZE_MAX / sizeof(**syms))
        return -1;

    if (symtab->link >= f->section_count) return -1;
    const neverc_elf_section_t *strtab = &f->sections[symtab->link];
    if (strtab->type != NEVERC_SHT_STRTAB ||
        !elf_range_in_file(f->data_len, strtab->offset, strtab->size))
        return -1;
    const uint8_t *str_data = f->data + (size_t)strtab->offset;
    size_t str_len = (size_t)strtab->size;
    if (!elf_strtab_terminated(str_data, str_len))
        return -1;

    /* Skip first null entry */
    *syms = (neverc_elf_symbol_t *)calloc((size_t)(sym_count - 1),
                                          sizeof(neverc_elf_symbol_t));
    if (!*syms) return -1;
    *count = (int)(sym_count - 1);

    for (uint64_t i = 1; i < sym_count; i++) {
        const uint8_t *e = sym_data + (size_t)(i * stride);
        neverc_elf_symbol_t *s = &(*syms)[(size_t)i - 1];
        uint32_t name_idx;

        if (f->header.class_ == NEVERC_ELFCLASS32) {
            name_idx = r32(e + 0);
            s->value   = r32(e + 4);
            s->size    = r32(e + 8);
            uint8_t info = e[12];
            s->bind    = info >> 4;
            s->type    = info & 0xf;
            s->visibility = e[13] & 0x3;
            s->section = r16(e + 14);
        } else {
            name_idx = r32(e + 0);
            uint8_t info = e[4];
            s->bind    = info >> 4;
            s->type    = info & 0xf;
            s->visibility = e[5] & 0x3;
            s->section = r16(e + 6);
            s->value   = r64(e + 8);
            s->size    = r64(e + 16);
        }
        if (elf_copy_strtab_name(str_data, str_len, name_idx, s->name,
                                 sizeof(s->name)) < 0) {
            free(*syms);
            *syms = NULL;
            *count = 0;
            return -1;
        }
    }
    return 0;
}

int neverc_elf_symbols(const neverc_elf_file_t *f,
                        neverc_elf_symbol_t **syms, int *count) {
    return get_symbols_common(f, NEVERC_SHT_SYMTAB, syms, count);
}

int neverc_elf_dynamic_symbols(const neverc_elf_file_t *f,
                                neverc_elf_symbol_t **syms, int *count) {
    return get_symbols_common(f, NEVERC_SHT_DYNSYM, syms, count);
}

int neverc_elf_imported_libraries(const neverc_elf_file_t *f,
                                   char **names, int max_names) {
    if (!f || !f->data || (f->section_count != 0 && !f->sections) ||
        max_names < 0 ||
        (max_names > 0 && !names))
        return -1;
    if (max_names == 0) return 0;

    rd64_fn r64 = (f->header.data == NEVERC_ELFDATA2MSB) ? rd64be : rd64le;
    rd32_fn r32 = (f->header.data == NEVERC_ELFDATA2MSB) ? rd32be : rd32le;

    const neverc_elf_section_t *dyn = NULL;
    for (uint32_t i = 0; i < f->section_count; i++) {
        if (f->sections[i].type == NEVERC_SHT_DYNAMIC) {
            dyn = &f->sections[i];
            break;
        }
    }
    if (!dyn) return 0;
    if (dyn->link >= f->section_count) return -1;

    const neverc_elf_section_t *strtab = &f->sections[dyn->link];
    if (strtab->type != NEVERC_SHT_STRTAB ||
        !elf_range_in_file(f->data_len, strtab->offset, strtab->size) ||
        !elf_range_in_file(f->data_len, dyn->offset, dyn->size))
        return -1;
    const uint8_t *str_data = f->data + (size_t)strtab->offset;
    size_t str_len = (size_t)strtab->size;
    if (!elf_strtab_terminated(str_data, str_len))
        return -1;
    const uint8_t *dyn_data = f->data + (size_t)dyn->offset;

    int lib_count = 0;
    uint64_t entry_size;
    if (f->header.class_ == NEVERC_ELFCLASS32)
        entry_size = 8;
    else if (f->header.class_ == NEVERC_ELFCLASS64)
        entry_size = 16;
    else
        return -1;
    uint64_t stride = dyn->entsize ? dyn->entsize : entry_size;
    if (stride < entry_size || dyn->size % stride != 0) return -1;
    uint64_t entries = dyn->size / stride;

    for (uint64_t i = 0; i < entries && lib_count < max_names; i++) {
        const uint8_t *e = dyn_data + (size_t)(i * stride);
        int64_t tag;
        uint64_t val;
        if (f->header.class_ == NEVERC_ELFCLASS32) {
            tag = (int32_t)r32(e);
            val = r32(e + 4);
        } else {
            tag = (int64_t)r64(e);
            val = r64(e + 8);
        }
        /* DT_NEEDED = 1 */
        if (tag == 1) {
            const char *name = NULL;
            if (elf_borrow_string(str_data, str_len, val, &name) < 0)
                return -1;
            names[lib_count++] = (char *)name;
        }
        /* DT_NULL = 0 terminates */
        if (tag == 0) break;
    }
    return lib_count;
}

const char *neverc_elf_machine_string(uint16_t machine) {
    switch (machine) {
    case NEVERC_EM_NONE:    return "None";
    case NEVERC_EM_386:     return "Intel 80386";
    case NEVERC_EM_ARM:     return "ARM";
    case NEVERC_EM_X86_64:  return "x86-64";
    case NEVERC_EM_AARCH64: return "AArch64";
    case NEVERC_EM_RISCV:   return "RISC-V";
    case NEVERC_EM_MIPS:    return "MIPS";
    case NEVERC_EM_PPC:     return "PowerPC";
    case NEVERC_EM_PPC64:   return "PowerPC64";
    default: return "Unknown";
    }
}

const char *neverc_elf_type_string(uint16_t type) {
    switch (type) {
    case NEVERC_ET_NONE: return "NONE";
    case NEVERC_ET_REL:  return "REL";
    case NEVERC_ET_EXEC: return "EXEC";
    case NEVERC_ET_DYN:  return "DYN";
    case NEVERC_ET_CORE: return "CORE";
    default: return "Unknown";
    }
}
