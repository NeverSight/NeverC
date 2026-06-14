#include "neverc/std/archive/zip.h"
#include "neverc/std/hash/crc32.h"
#include <stdlib.h>
#include <string.h>

static uint16_t read16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t read32(const uint8_t *p) { return p[0] | (p[1]<<8) | (p[2]<<16) | ((uint32_t)p[3]<<24); }
static void write16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void write32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v>>8; p[2] = v>>16; p[3] = v>>24; }

int neverc_zip_reader_init(neverc_zip_reader_t *r, const uint8_t *data, size_t len) {
    r->data = data;
    r->len = len;
    r->files = NULL;
    r->nfiles = 0;
    r->file_data = NULL;

    int cap = 16;
    r->files = (neverc_zip_file_header_t *)malloc(cap * sizeof(neverc_zip_file_header_t));
    r->file_data = (const uint8_t **)malloc(cap * sizeof(uint8_t *));

    size_t pos = 0;
    while (pos + 30 <= len) {
        uint32_t sig = read32(data + pos);
        if (sig != 0x04034b50) break;

        uint16_t method = read16(data + pos + 8);
        uint16_t mod_time = read16(data + pos + 10);
        uint16_t mod_date = read16(data + pos + 12);
        uint32_t crc = read32(data + pos + 14);
        uint32_t comp_size = read32(data + pos + 18);
        uint32_t uncomp_size = read32(data + pos + 22);
        uint16_t name_len = read16(data + pos + 26);
        uint16_t extra_len = read16(data + pos + 28);

        /* Bound every field read and the advance step. comp_size/name_len/extra_len
         * are attacker-controlled; `pos + 30 + name_len + extra_len + comp_size`
         * can overflow size_t or run past `len`, leaving file_data dangling past the
         * buffer (and pos stuck or wrapping on 32-bit). Use 64-bit sums and reject
         * entries that do not fully fit. */
        uint64_t hdr_end = (uint64_t)pos + 30u + (uint64_t)name_len + (uint64_t)extra_len;
        if (hdr_end > len) break;
        if ((uint64_t)comp_size > len - hdr_end) break;

        if (r->nfiles >= cap) {
            cap *= 2;
            r->files = (neverc_zip_file_header_t *)realloc(r->files, cap * sizeof(neverc_zip_file_header_t));
            r->file_data = (const uint8_t **)realloc(r->file_data, cap * sizeof(uint8_t *));
            if (!r->files || !r->file_data) return -1;
        }

        neverc_zip_file_header_t *f = &r->files[r->nfiles];
        memset(f, 0, sizeof(*f));
        size_t nl = name_len < 255 ? name_len : 255;
        if (name_len > 0)
            memcpy(f->name, data + pos + 30, nl);
        f->name[nl] = '\0';
        f->method = method;
        f->crc32 = crc;
        f->compressed_size = comp_size;
        f->uncompressed_size = uncomp_size;
        f->mod_time = mod_time;
        f->mod_date = mod_date;

        r->file_data[r->nfiles] = data + (size_t)hdr_end;
        r->nfiles++;

        pos = (size_t)(hdr_end + comp_size);
    }
    return 0;
}

int neverc_zip_reader_count(const neverc_zip_reader_t *r) { return r->nfiles; }

const neverc_zip_file_header_t *neverc_zip_reader_file(const neverc_zip_reader_t *r, int idx) {
    if (idx < 0 || idx >= r->nfiles) return NULL;
    return &r->files[idx];
}

const uint8_t *neverc_zip_reader_file_data(const neverc_zip_reader_t *r, int idx, size_t *len) {
    if (idx < 0 || idx >= r->nfiles) { *len = 0; return NULL; }
    *len = (size_t)r->files[idx].compressed_size;
    return r->file_data[idx];
}

void neverc_zip_reader_free(neverc_zip_reader_t *r) {
    free(r->files);
    free(r->file_data);
    r->files = NULL;
    r->file_data = NULL;
    r->nfiles = 0;
}

/* Writer */
void neverc_zip_writer_init(neverc_zip_writer_t *w) {
    w->cap = 4096;
    w->data = (uint8_t *)malloc(w->cap);
    w->len = 0;
    w->entries_cap = 16;
    w->entries = (neverc_zip_file_header_t *)malloc(w->entries_cap * sizeof(neverc_zip_file_header_t));
    w->offsets = (uint32_t *)malloc(w->entries_cap * sizeof(uint32_t));
    w->nentries = 0;
}

static void wgrow(neverc_zip_writer_t *w, size_t need) {
    while (w->len + need > w->cap) { w->cap *= 2; w->data = (uint8_t *)realloc(w->data, w->cap); }
}

int neverc_zip_writer_add(neverc_zip_writer_t *w, const char *name,
                          const uint8_t *data, size_t len) {
    uint16_t name_len = (uint16_t)strlen(name);
    uint32_t crc = neverc_crc32_ieee(data, len);

    if (w->nentries >= w->entries_cap) {
        w->entries_cap *= 2;
        w->entries = (neverc_zip_file_header_t *)realloc(w->entries, w->entries_cap * sizeof(neverc_zip_file_header_t));
        w->offsets = (uint32_t *)realloc(w->offsets, w->entries_cap * sizeof(uint32_t));
    }

    w->offsets[w->nentries] = (uint32_t)w->len;

    neverc_zip_file_header_t *e = &w->entries[w->nentries];
    memset(e, 0, sizeof(*e));
    memcpy(e->name, name, name_len < 255 ? name_len : 255);
    e->method = NEVERC_ZIP_STORED;
    e->crc32 = crc;
    e->compressed_size = len;
    e->uncompressed_size = len;
    w->nentries++;

    /* Local file header */
    wgrow(w, 30 + name_len + len);
    uint8_t *p = w->data + w->len;
    write32(p, 0x04034b50);
    write16(p + 4, 20);
    write16(p + 6, 0);
    write16(p + 8, NEVERC_ZIP_STORED);
    write16(p + 10, 0);
    write16(p + 12, 0);
    write32(p + 14, crc);
    write32(p + 18, (uint32_t)len);
    write32(p + 22, (uint32_t)len);
    write16(p + 26, name_len);
    write16(p + 28, 0);
    memcpy(p + 30, name, name_len);
    memcpy(p + 30 + name_len, data, len);
    w->len += 30 + name_len + len;

    return 0;
}

int neverc_zip_writer_close(neverc_zip_writer_t *w) {
    uint32_t cd_start = (uint32_t)w->len;

    for (int i = 0; i < w->nentries; i++) {
        neverc_zip_file_header_t *e = &w->entries[i];
        uint16_t name_len = (uint16_t)strlen(e->name);

        wgrow(w, 46 + name_len);
        uint8_t *p = w->data + w->len;
        write32(p, 0x02014b50);
        write16(p + 4, 20);
        write16(p + 6, 20);
        write16(p + 8, 0);
        write16(p + 10, e->method);
        write16(p + 12, e->mod_time);
        write16(p + 14, e->mod_date);
        write32(p + 16, e->crc32);
        write32(p + 20, (uint32_t)e->compressed_size);
        write32(p + 24, (uint32_t)e->uncompressed_size);
        write16(p + 28, name_len);
        write16(p + 30, 0);
        write16(p + 32, 0);
        write16(p + 34, 0);
        write16(p + 36, 0);
        write32(p + 38, 0);
        write32(p + 42, w->offsets[i]);
        memcpy(p + 46, e->name, name_len);
        w->len += 46 + name_len;
    }

    uint32_t cd_size = (uint32_t)w->len - cd_start;

    /* End of central directory */
    wgrow(w, 22);
    uint8_t *p = w->data + w->len;
    write32(p, 0x06054b50);
    write16(p + 4, 0);
    write16(p + 6, 0);
    write16(p + 8, (uint16_t)w->nentries);
    write16(p + 10, (uint16_t)w->nentries);
    write32(p + 12, cd_size);
    write32(p + 16, cd_start);
    write16(p + 20, 0);
    w->len += 22;

    return 0;
}

void neverc_zip_writer_free(neverc_zip_writer_t *w) {
    free(w->data);
    free(w->entries);
    free(w->offsets);
    w->data = NULL;
    w->entries = NULL;
    w->offsets = NULL;
}
