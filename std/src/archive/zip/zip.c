#include "neverc/std/archive/zip.h"
#include "neverc/std/hash/crc32.h"
#include "neverc/std/io/fs.h"
#include <stdlib.h>
#include <string.h>

static uint16_t read16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t read32(const uint8_t *p) { return p[0] | (p[1]<<8) | (p[2]<<16) | ((uint32_t)p[3]<<24); }
static void write16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void write32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v>>8; p[2] = v>>16; p[3] = v>>24; }

static int zip_path_is_safe(const char *name) {
    if (!name || !name[0]) return 0;
    size_t len = strlen(name);
    if (len >= sizeof(((neverc_zip_file_header_t *)0)->name)) return 0;
    char trimmed[sizeof(((neverc_zip_file_header_t *)0)->name)];
    memcpy(trimmed, name, len + 1);
    while (len > 0 && trimmed[len - 1] == '/')
        trimmed[--len] = '\0';
    if (len == 0 || strcmp(trimmed, ".") == 0) return 0;
    return neverc_fs_valid_path(trimmed);
}

static int zip_reader_error(neverc_zip_reader_t *r) {
    free(r->files);
    free(r->file_data);
    r->files = NULL;
    r->file_data = NULL;
    r->nfiles = 0;
    return -1;
}

static int find_eocd(const uint8_t *data, size_t len, size_t *offset) {
    if (len < 22U) return -1;
    size_t earliest = len > 22U + UINT16_MAX
        ? len - (22U + UINT16_MAX) : 0;
    size_t pos = len - 22U;
    for (;;) {
        if (read32(data + pos) == 0x06054b50U) {
            uint16_t comment_length = read16(data + pos + 20U);
            if ((size_t)comment_length == len - pos - 22U) {
                *offset = pos;
                return 0;
            }
        }
        if (pos == earliest) break;
        pos--;
    }
    return -1;
}

int neverc_zip_reader_init(neverc_zip_reader_t *r, const uint8_t *data, size_t len) {
    if (!r) return -1;
    memset(r, 0, sizeof(*r));
    r->data = data;
    r->len = len;
    if (!data || len < 22U) return -1;

    size_t eocd_offset = 0;
    if (find_eocd(data, len, &eocd_offset) != 0) return -1;
    const uint8_t *eocd = data + eocd_offset;
    uint16_t disk = read16(eocd + 4U);
    uint16_t central_disk = read16(eocd + 6U);
    uint16_t disk_entries = read16(eocd + 8U);
    uint16_t total_entries = read16(eocd + 10U);
    uint32_t central_size = read32(eocd + 12U);
    uint32_t central_offset = read32(eocd + 16U);
    if (disk != 0 || central_disk != 0 ||
        disk_entries != total_entries ||
        total_entries == UINT16_MAX ||
        central_size == UINT32_MAX ||
        central_offset == UINT32_MAX ||
        (uint64_t)central_offset + central_size != eocd_offset ||
        (uint64_t)total_entries * 46U > central_size)
        return -1;

    if (total_entries > 0) {
        r->files = (neverc_zip_file_header_t *)malloc(
            (size_t)total_entries * sizeof(*r->files));
        r->file_data = (const uint8_t **)malloc(
            (size_t)total_entries * sizeof(*r->file_data));
        if (!r->files || !r->file_data) return zip_reader_error(r);
    }

    size_t cursor = central_offset;
    size_t central_end = cursor + central_size;
    size_t validated_bytes = 0;
    for (uint16_t i = 0; i < total_entries; i++) {
        if (central_end - cursor < 46U ||
            read32(data + cursor) != 0x02014b50U)
            return zip_reader_error(r);
        const uint8_t *central = data + cursor;
        uint16_t flags = read16(central + 8U);
        uint16_t method = read16(central + 10U);
        uint16_t mod_time = read16(central + 12U);
        uint16_t mod_date = read16(central + 14U);
        uint32_t crc = read32(central + 16U);
        uint32_t compressed_size = read32(central + 20U);
        uint32_t uncompressed_size = read32(central + 24U);
        uint16_t name_length = read16(central + 28U);
        uint16_t extra_length = read16(central + 30U);
        uint16_t comment_length = read16(central + 32U);
        uint16_t start_disk = read16(central + 34U);
        uint32_t local_offset = read32(central + 42U);
        uint64_t central_record_size =
            46U + (uint64_t)name_length + extra_length + comment_length;
        if (central_record_size > central_end - cursor ||
            start_disk != 0 || (flags & ~(uint16_t)0x0808U) != 0 ||
            method != NEVERC_ZIP_STORED ||
            compressed_size == UINT32_MAX ||
            uncompressed_size == UINT32_MAX ||
            local_offset == UINT32_MAX ||
            compressed_size != uncompressed_size ||
            name_length > 255U || name_length == 0 ||
            memchr(central + 46U, '\0', name_length) != NULL)
            return zip_reader_error(r);

        if ((uint64_t)local_offset + 30U > central_offset ||
            read32(data + local_offset) != 0x04034b50U)
            return zip_reader_error(r);
        const uint8_t *local = data + local_offset;
        uint16_t local_flags = read16(local + 6U);
        uint16_t local_method = read16(local + 8U);
        uint32_t local_crc = read32(local + 14U);
        uint32_t local_compressed = read32(local + 18U);
        uint32_t local_uncompressed = read32(local + 22U);
        uint16_t local_name_length = read16(local + 26U);
        uint16_t local_extra_length = read16(local + 28U);
        uint64_t data_offset =
            (uint64_t)local_offset + 30U +
            local_name_length + local_extra_length;
        if (local_flags != flags || local_method != method ||
            local_name_length != name_length ||
            data_offset > central_offset ||
            compressed_size > central_offset - data_offset ||
            memcmp(local + 30U, central + 46U, name_length) != 0)
            return zip_reader_error(r);
        if ((flags & 0x0008U) == 0) {
            if (local_crc != crc ||
                local_compressed != compressed_size ||
                local_uncompressed != uncompressed_size)
                return zip_reader_error(r);
        } else if ((local_crc != 0 && local_crc != crc) ||
                   (local_compressed != 0 &&
                    local_compressed != compressed_size) ||
                   (local_uncompressed != 0 &&
                    local_uncompressed != uncompressed_size)) {
            return zip_reader_error(r);
        }
        if (compressed_size > len - validated_bytes)
            return zip_reader_error(r);
        validated_bytes += compressed_size;
        const uint8_t *file_data = data + (size_t)data_offset;
        if (neverc_crc32_ieee(file_data, compressed_size) != crc)
            return zip_reader_error(r);

        neverc_zip_file_header_t *file = &r->files[i];
        memset(file, 0, sizeof(*file));
        memcpy(file->name, central + 46U, name_length);
        file->name[name_length] = '\0';
        if (!zip_path_is_safe(file->name))
            return zip_reader_error(r);
        file->method = method;
        file->crc32 = crc;
        file->compressed_size = compressed_size;
        file->uncompressed_size = uncompressed_size;
        file->mod_time = mod_time;
        file->mod_date = mod_date;
        r->file_data[i] = file_data;
        r->nfiles++;
        cursor += (size_t)central_record_size;
    }
    if (cursor != central_end) return zip_reader_error(r);
    return 0;
}

int neverc_zip_reader_count(const neverc_zip_reader_t *r) {
    return r ? r->nfiles : 0;
}

const neverc_zip_file_header_t *neverc_zip_reader_file(const neverc_zip_reader_t *r, int idx) {
    if (!r || idx < 0 || idx >= r->nfiles) return NULL;
    return &r->files[idx];
}

const uint8_t *neverc_zip_reader_file_data(const neverc_zip_reader_t *r, int idx, size_t *len) {
    if (!len) return NULL;
    *len = 0;
    if (!r || idx < 0 || idx >= r->nfiles) return NULL;
    *len = (size_t)r->files[idx].compressed_size;
    return r->file_data[idx];
}

void neverc_zip_reader_free(neverc_zip_reader_t *r) {
    if (!r) return;
    free(r->files);
    free(r->file_data);
    r->files = NULL;
    r->file_data = NULL;
    r->nfiles = 0;
    r->data = NULL;
    r->len = 0;
}

/* Writer */
void neverc_zip_writer_init(neverc_zip_writer_t *w) {
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->cap = 4096;
    w->data = (uint8_t *)malloc(w->cap);
    w->entries_cap = 16;
    w->entries = (neverc_zip_file_header_t *)malloc(w->entries_cap * sizeof(neverc_zip_file_header_t));
    w->offsets = (uint32_t *)malloc(w->entries_cap * sizeof(uint32_t));
    if (!w->data || !w->entries || !w->offsets) {
        free(w->data);
        free(w->entries);
        free(w->offsets);
        w->data = NULL;
        w->entries = NULL;
        w->offsets = NULL;
        w->cap = 0;
        w->entries_cap = 0;
    }
}

static int wgrow(neverc_zip_writer_t *w, size_t need) {
    if (!w || need > SIZE_MAX - w->len) return 0;
    size_t required = w->len + need;
    if (w->data && required <= w->cap) return 1;
    size_t next = w->cap < 4096 ? 4096 : w->cap;
    while (next < required) {
        if (next > SIZE_MAX / 2) {
            next = required;
            break;
        }
        next *= 2;
    }
    uint8_t *grown = (uint8_t *)realloc(w->data, next);
    if (!grown) return 0;
    w->data = grown;
    w->cap = next;
    return 1;
}

static int wentries_grow(neverc_zip_writer_t *w) {
    if (w->entries && w->offsets && w->nentries < w->entries_cap) return 1;
    if (w->nentries < 0 || (w->nentries > 0 && (!w->entries || !w->offsets)) ||
        w->entries_cap > INT32_MAX / 2) return 0;
    int next_cap = w->entries_cap < 16 ? 16 : w->entries_cap * 2;
    neverc_zip_file_header_t *entries =
        (neverc_zip_file_header_t *)malloc(
            (size_t)next_cap * sizeof(*entries));
    uint32_t *offsets = (uint32_t *)malloc(
        (size_t)next_cap * sizeof(*offsets));
    if (!entries || !offsets) {
        free(entries);
        free(offsets);
        return 0;
    }
    if (w->nentries > 0) {
        memcpy(entries, w->entries,
               (size_t)w->nentries * sizeof(*entries));
        memcpy(offsets, w->offsets,
               (size_t)w->nentries * sizeof(*offsets));
    }
    free(w->entries);
    free(w->offsets);
    w->entries = entries;
    w->offsets = offsets;
    w->entries_cap = next_cap;
    return 1;
}

int neverc_zip_writer_add(neverc_zip_writer_t *w, const char *name,
                          const uint8_t *data, size_t len) {
    if (!w || !name || w->closed || w->failed ||
        (!data && len != 0) || len > UINT32_MAX ||
        w->len > UINT32_MAX || w->nentries < 0 || w->nentries >= UINT16_MAX ||
        !zip_path_is_safe(name))
        return -1;
    size_t name_size = strlen(name);
    if (name_size == 0 || name_size > 255 ||
        name_size > SIZE_MAX - 30U ||
        len > SIZE_MAX - 30U - name_size)
        return -1;
    uint16_t name_len = (uint16_t)name_size;
    size_t record_len = 30U + name_size + len;
    if (record_len > UINT32_MAX - w->len ||
        !wentries_grow(w) || !wgrow(w, record_len))
        return -1;
    uint32_t crc = neverc_crc32_ieee(data, len);

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
    if (len > 0) memcpy(p + 30 + name_len, data, len);
    w->len += 30 + name_len + len;

    return 0;
}

int neverc_zip_writer_close(neverc_zip_writer_t *w) {
    if (!w || w->failed || w->nentries < 0 ||
        w->nentries > UINT16_MAX || w->nentries > w->entries_cap ||
        (w->nentries > 0 && (!w->entries || !w->offsets)) ||
        w->len > UINT32_MAX)
        return -1;
    if (w->closed) return 0;

    size_t central_bytes = 0;
    for (int i = 0; i < w->nentries; i++) {
        size_t name_length = strlen(w->entries[i].name);
        if (name_length == 0 || name_length > 255U ||
            name_length > SIZE_MAX - 46U ||
            central_bytes > SIZE_MAX - 46U - name_length)
            return -1;
        central_bytes += 46U + name_length;
    }
    if (central_bytes > UINT32_MAX - 22U ||
        w->len > UINT32_MAX - central_bytes - 22U ||
        central_bytes > SIZE_MAX - 22U ||
        !wgrow(w, central_bytes + 22U)) {
        w->failed = 1;
        return -1;
    }
    uint32_t cd_start = (uint32_t)w->len;

    for (int i = 0; i < w->nentries; i++) {
        neverc_zip_file_header_t *e = &w->entries[i];
        uint16_t name_len = (uint16_t)strlen(e->name);

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
    w->closed = 1;

    return 0;
}

void neverc_zip_writer_free(neverc_zip_writer_t *w) {
    if (!w) return;
    free(w->data);
    free(w->entries);
    free(w->offsets);
    memset(w, 0, sizeof(*w));
}
