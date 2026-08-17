#include "neverc/std/debug/plan9obj.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <limits.h>

static uint32_t be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  | (uint32_t)p[3];
}

static uint64_t be64(const uint8_t *p) {
    return ((uint64_t)be32(p) << 32) | be32(p + 4);
}

static char *plan9_strdup(const char *s) {
    size_t len = strlen(s);
    char *copy = (char *)malloc(len + 1U);
    if (copy)
        memcpy(copy, s, len + 1U);
    return copy;
}

int neverc_plan9_valid_magic(uint32_t magic) {
    return magic == NEVERC_PLAN9_MAGIC386 ||
           magic == NEVERC_PLAN9_MAGICAMD64 ||
           magic == NEVERC_PLAN9_MAGICARM;
}

int neverc_plan9_parse(neverc_plan9_file_t *f, const uint8_t *buf, size_t len) {
    if (!f) return -1;
    memset(f, 0, sizeof(*f));

    if (!buf || len < 32)
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
            goto fail;
        f->entry = be64(buf + 32);
        f->ptr_size = 8;
        f->load_address = 0x200000;
        f->hdr_size += 8;
    }

    static const char *sect_names[] = {"text", "data", "syms", "spsz", "pcsz"};
    uint32_t sect_sizes[] = {ph.text, ph.data, ph.syms, ph.spsz, ph.pcsz};

    uint64_t off = f->hdr_size;
    for (int i = 0; i < 5; i++) {
        if (off > len || sect_sizes[i] > (uint64_t)len - off)
            goto fail;
        off += sect_sizes[i];
    }

    f->data = (uint8_t *)malloc(len);
    if (!f->data)
        goto fail;
    memcpy(f->data, buf, len);
    f->data_len = len;

    f->num_sections = 5;
    f->sections = (neverc_plan9_section_t *)calloc(5, sizeof(neverc_plan9_section_t));
    if (!f->sections)
        goto fail;

    off = f->hdr_size;
    for (int i = 0; i < 5; i++) {
        f->sections[i].name   = plan9_strdup(sect_names[i]);
        if (!f->sections[i].name)
            goto fail;
        f->sections[i].size   = sect_sizes[i];
        f->sections[i].offset = off;
        off += sect_sizes[i];
    }

    return 0;

fail:
    neverc_plan9_close(f);
    return -1;
}

int neverc_plan9_open(neverc_plan9_file_t *f, const char *path) {
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

    int rc = neverc_plan9_parse(f, buf, (size_t)sz);
    free(buf);
    return rc;
}

void neverc_plan9_close(neverc_plan9_file_t *f) {
    if (!f) return;
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
    if (!f || !name || (f->num_sections != 0 && !f->sections))
        return NULL;
    for (int i = 0; i < f->num_sections; i++) {
        if (f->sections[i].name && strcmp(f->sections[i].name, name) == 0)
            return &f->sections[i];
    }
    return NULL;
}

int neverc_plan9_section_data(neverc_plan9_file_t *f,
                               neverc_plan9_section_t *sect,
                               uint8_t *buf, size_t cap) {
    if (!f || !sect || (!f->data && f->data_len != 0) ||
        (sect->size != 0 && !buf))
        return -1;
    if (cap < sect->size)
        return -1;
    if (sect->offset > f->data_len ||
        sect->size > (uint64_t)f->data_len - sect->offset)
        return -1;
    if (sect->size != 0)
        memcpy(buf, f->data + (size_t)sect->offset, sect->size);
    return 0;
}

typedef struct {
    uint64_t value;
    char type;
    const uint8_t *name;
    size_t name_len;
} plan9_raw_symbol_t;

static int plan9_next_symbol(const uint8_t **cursor, const uint8_t *end,
                             int ptr_size, plan9_raw_symbol_t *symbol) {
    if (!cursor || !*cursor || !end || !symbol ||
        (ptr_size != 4 && ptr_size != 8) || *cursor > end)
        return -1;
    const uint8_t *p = *cursor;
    size_t remaining = (size_t)(end - p);
    if (remaining == 0)
        return 0;
    if (remaining < (size_t)ptr_size + 1U)
        return -1;

    symbol->value = ptr_size == 8 ? be64(p) : (uint64_t)be32(p);
    p += ptr_size;
    symbol->type = (char)(*p++ & 0x7fU);
    remaining = (size_t)(end - p);

    if (symbol->type == 'z' || symbol->type == 'Z') {
        const uint8_t *prefix_end =
            (const uint8_t *)memchr(p, 0, remaining);
        if (!prefix_end)
            return -1;
        p = prefix_end + 1;
        const uint8_t *name_start = p;
        while ((size_t)(end - p) >= 2U) {
            if (p[0] == 0 && p[1] == 0) {
                symbol->name = name_start;
                symbol->name_len = (size_t)(p - name_start);
                *cursor = p + 2;
                return 1;
            }
            p += 2;
        }
        return -1;
    }

    const uint8_t *name_end = (const uint8_t *)memchr(p, 0, remaining);
    if (!name_end)
        return -1;
    symbol->name = p;
    symbol->name_len = (size_t)(name_end - p);
    *cursor = name_end + 1;
    return 1;
}

static void plan9_free_symbol_array(neverc_plan9_sym_t *symbols, int count) {
    if (!symbols) return;
    for (int i = 0; i < count; i++)
        free(symbols[i].name);
    free(symbols);
}

static char *plan9_copy_symbol_name(const plan9_raw_symbol_t *raw) {
    char *name = (char *)malloc(raw->name_len + 1U);
    if (!name)
        return NULL;
    memcpy(name, raw->name, raw->name_len);
    name[raw->name_len] = '\0';
    return name;
}

static char *plan9_expand_path_name(const plan9_raw_symbol_t *raw,
                                    char *const *filename_map) {
    if ((raw->name_len & 1U) != 0)
        return NULL;
    size_t length = 0;
    int ends_with_slash = 0;
    for (size_t i = 0; i < raw->name_len; i += 2) {
        uint16_t code =
            (uint16_t)((uint16_t)raw->name[i] << 8) | raw->name[i + 1];
        const char *component = filename_map[code];
        if (!component)
            return NULL;
        size_t component_len = strlen(component);
        if (length != 0 && !ends_with_slash) {
            if (length == SIZE_MAX)
                return NULL;
            length++;
            ends_with_slash = 1;
        }
        if (component_len > SIZE_MAX - length)
            return NULL;
        length += component_len;
        if (component_len != 0)
            ends_with_slash = component[component_len - 1] == '/';
    }
    if (length == SIZE_MAX)
        return NULL;

    char *name = (char *)malloc(length + 1U);
    if (!name)
        return NULL;
    size_t pos = 0;
    for (size_t i = 0; i < raw->name_len; i += 2) {
        uint16_t code =
            (uint16_t)((uint16_t)raw->name[i] << 8) | raw->name[i + 1];
        const char *component = filename_map[code];
        if (pos != 0 && name[pos - 1] != '/')
            name[pos++] = '/';
        size_t component_len = strlen(component);
        memcpy(name + pos, component, component_len);
        pos += component_len;
    }
    name[pos] = '\0';
    return name;
}

int neverc_plan9_symbols(neverc_plan9_file_t *f) {
    if (!f || (!f->data && f->data_len != 0) ||
        (f->ptr_size != 4 && f->ptr_size != 8))
        return -1;
    neverc_plan9_section_t *syms = neverc_plan9_section(f, "syms");
    if (!syms)
        return -1;
    if (syms->offset > f->data_len ||
        syms->size > (uint64_t)f->data_len - syms->offset)
        return -1;

    const uint8_t *p = f->data + (size_t)syms->offset;
    const uint8_t *end = p + syms->size;
    const uint8_t *scan = p;
    size_t count = 0;
    int needs_filename_map = 0;
    plan9_raw_symbol_t raw;
    int result;
    while ((result = plan9_next_symbol(&scan, end, f->ptr_size, &raw)) > 0) {
        if (count == INT_MAX)
            return -1;
        count++;
        if (raw.type == 'z' || raw.type == 'Z')
            needs_filename_map = 1;
    }
    if (result < 0)
        return -1;

    neverc_plan9_sym_t *parsed = NULL;
    char **filename_map = NULL;
    if (count != 0) {
        if (count > SIZE_MAX / sizeof(neverc_plan9_sym_t))
            return -1;
        parsed = (neverc_plan9_sym_t *)calloc(
            count, sizeof(neverc_plan9_sym_t));
        if (!parsed)
            return -1;
    }
    if (needs_filename_map) {
        filename_map = (char **)calloc(65536U, sizeof(char *));
        if (!filename_map) {
            free(parsed);
            return -1;
        }
    }

    scan = p;
    size_t index = 0;
    while ((result = plan9_next_symbol(&scan, end, f->ptr_size, &raw)) > 0) {
        parsed[index].value = raw.value;
        parsed[index].type = raw.type;
        if (raw.type == 'z' || raw.type == 'Z')
            parsed[index].name = plan9_expand_path_name(&raw, filename_map);
        else
            parsed[index].name = plan9_copy_symbol_name(&raw);
        if (!parsed[index].name)
            goto fail;
        if (raw.type == 'f' && raw.value > 0xFFFFU)
            goto fail;
        if (filename_map && raw.type == 'f')
            filename_map[(uint16_t)raw.value] = parsed[index].name;
        index++;
    }
    if (result < 0)
        goto fail;

    free(filename_map);
    plan9_free_symbol_array(f->symbols, f->num_symbols);
    f->symbols = parsed;
    f->num_symbols = (int)count;
    return 0;

fail:
    free(filename_map);
    plan9_free_symbol_array(parsed, (int)index + 1);
    return -1;
}
