#include "neverc/std/debug/plan9obj.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static uint64_t be64(const uint8_t *p) {
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

int neverc_plan9_valid_magic(uint32_t magic) {
    uint32_t base = magic & ~(uint32_t)NEVERC_PLAN9_MAGIC64;
    return base == NEVERC_PLAN9_MAGIC386 ||
           base == (NEVERC_PLAN9_MAGICAMD64 & ~(uint32_t)NEVERC_PLAN9_MAGIC64) ||
           base == NEVERC_PLAN9_MAGICARM;
}

int neverc_plan9_parse(neverc_plan9_file_t *f, const uint8_t *buf, size_t len) {
    memset(f, 0, sizeof(*f));

    if (len < 32)
        return -1;

    uint32_t magic = be32(buf);
    if (!neverc_plan9_valid_magic(magic))
        return -1;

    neverc_plan9_prog_t ph;
    ph.magic    = magic;
    ph.text     = be32(buf + 4);
    ph.data     = be32(buf + 8);
    ph.bss      = be32(buf + 12);
    ph.syms     = be32(buf + 16);
    ph.entry_lo = be32(buf + 20);
    ph.spsz     = be32(buf + 24);
    ph.pcsz     = be32(buf + 28);

    f->magic = magic;
    f->bss   = ph.bss;
    f->entry = (uint64_t)ph.entry_lo;
    f->ptr_size = 4;
    f->load_address = 0x1000;
    f->hdr_size = 4 * 8;

    if (magic & NEVERC_PLAN9_MAGIC64) {
        if (len < 40)
            return -1;
        f->entry = be64(buf + 32);
        f->ptr_size = 8;
        f->load_address = 0x200000;
        f->hdr_size += 8;
    }

    f->data = (uint8_t *)malloc(len);
    if (!f->data)
        return -1;
    memcpy(f->data, buf, len);
    f->data_len = len;

    static const char *sect_names[] = {"text", "data", "syms", "spsz", "pcsz"};
    uint32_t sect_sizes[] = {ph.text, ph.data, ph.syms, ph.spsz, ph.pcsz};

    f->num_sections = 5;
    f->sections = (neverc_plan9_section_t *)calloc(5, sizeof(neverc_plan9_section_t));
    if (!f->sections) {
        free(f->data);
        f->data = NULL;
        return -1;
    }

    uint32_t off = (uint32_t)f->hdr_size;
    for (int i = 0; i < 5; i++) {
        f->sections[i].name   = strdup(sect_names[i]);
        f->sections[i].size   = sect_sizes[i];
        f->sections[i].offset = off;
        off += sect_sizes[i];
    }

    return 0;
}

int neverc_plan9_open(neverc_plan9_file_t *f, const char *path) {
    FILE *fp = fopen(path, "rb");
    if (!fp) return -1;

    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    if (sz <= 0) { fclose(fp); return -1; }

    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) { fclose(fp); return -1; }

    if (fread(buf, 1, (size_t)sz, fp) != (size_t)sz) {
        free(buf);
        fclose(fp);
        return -1;
    }
    fclose(fp);

    int rc = neverc_plan9_parse(f, buf, (size_t)sz);
    free(buf);
    return rc;
}

void neverc_plan9_close(neverc_plan9_file_t *f) {
    if (f->sections) {
        for (int i = 0; i < f->num_sections; i++)
            free(f->sections[i].name);
        free(f->sections);
    }
    if (f->symbols) {
        for (int i = 0; i < f->num_symbols; i++)
            free(f->symbols[i].name);
        free(f->symbols);
    }
    free(f->data);
    memset(f, 0, sizeof(*f));
}

neverc_plan9_section_t *neverc_plan9_section(neverc_plan9_file_t *f, const char *name) {
    for (int i = 0; i < f->num_sections; i++) {
        if (f->sections[i].name && strcmp(f->sections[i].name, name) == 0)
            return &f->sections[i];
    }
    return NULL;
}

int neverc_plan9_section_data(neverc_plan9_file_t *f,
                               neverc_plan9_section_t *sect,
                               uint8_t *buf, size_t cap) {
    if (!f->data || !sect)
        return -1;
    if (cap < sect->size)
        return -1;
    if ((size_t)sect->offset + sect->size > f->data_len)
        return -1;
    memcpy(buf, f->data + sect->offset, sect->size);
    return 0;
}

int neverc_plan9_symbols(neverc_plan9_file_t *f) {
    neverc_plan9_section_t *syms = neverc_plan9_section(f, "syms");
    if (!syms || syms->size == 0)
        return -1;
    if ((size_t)syms->offset + syms->size > f->data_len)
        return -1;

    const uint8_t *p = f->data + syms->offset;
    const uint8_t *end = p + syms->size;
    int ptrsz = f->ptr_size;

    int count = 0;
    const uint8_t *scan = p;
    while (scan + ptrsz + 1 <= end) {
        scan += ptrsz;
        if (scan >= end) break;
        scan++;
        while (scan < end && *scan != 0) scan++;
        if (scan < end) scan++;
        count++;
    }

    if (count == 0) return 0;

    f->symbols = (neverc_plan9_sym_t *)calloc((size_t)count, sizeof(neverc_plan9_sym_t));
    if (!f->symbols) return -1;

    scan = p;
    int idx = 0;
    while (scan + ptrsz + 1 <= end && idx < count) {
        uint64_t val;
        if (ptrsz == 8)
            val = be64(scan);
        else
            val = (uint64_t)be32(scan);
        scan += ptrsz;

        if (scan >= end) break;
        char typ = (char)(*scan & 0x7F);
        scan++;

        const uint8_t *name_start = scan;
        while (scan < end && *scan != 0) scan++;
        size_t name_len = (size_t)(scan - name_start);
        if (scan < end) scan++;

        f->symbols[idx].value = val;
        f->symbols[idx].type  = typ;
        f->symbols[idx].name  = (char *)malloc(name_len + 1);
        if (f->symbols[idx].name) {
            memcpy(f->symbols[idx].name, name_start, name_len);
            f->symbols[idx].name[name_len] = '\0';
        }
        idx++;
    }

    f->num_symbols = idx;
    return 0;
}
