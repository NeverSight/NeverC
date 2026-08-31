#include "neverc/std/archive/zip.h"
#include "neverc/std/hash/crc32.h"
#include "neverc/std/io/fs.h"
#include <stdlib.h>
#include <string.h>

static uint16_t read16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t read32(const uint8_t *p) { return p[0] | (p[1]<<8) | (p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint64_t read64(const uint8_t *p) {
    return (uint64_t)read32(p) | ((uint64_t)read32(p + 4U) << 32U);
}
static void write16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void write32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v>>8; p[2] = v>>16; p[3] = v>>24; }

static int zip_path_is_safe(const char *name) {
    if (!name || !name[0]) return 0;
    size_t len = strlen(name);
    if (len >= sizeof(((neverc_zip_file_header_t *)0)->name)) return 0;
    while (len > 0 && name[len - 1U] == '/') len--;
    if (len == 0 || memchr(name, ':', len) != NULL) return 0;

    /* Dot components are lexical no-ops and cannot escape an extraction
     * root. Validate a dot-free spelling while preserving the archive's
     * original name bytes for interoperability (mirrors archive/tar). */
    char normalized[sizeof(((neverc_zip_file_header_t *)0)->name)];
    size_t input = 0;
    size_t output = 0;
    while (input < len) {
        size_t start = input;
        while (input < len && name[input] != '/') input++;
        size_t component_len = input - start;
        if (component_len == 0) return 0;
        if (!(component_len == 1U && name[start] == '.')) {
            if (output > 0) normalized[output++] = '/';
            memcpy(normalized + output, name + start, component_len);
            output += component_len;
        }
        if (input < len) input++;
    }
    if (output == 0) return 0;
    normalized[output] = '\0';
    return neverc_fs_valid_path(normalized);
}

/* Go archive/zip detectUTF8: a name outside the CP-437-compatible ASCII
 * subset must be announced with general-purpose bit 11, or readers decode
 * the UTF-8 bytes as CP437 and show mojibake. Names reaching the writer are
 * already validated UTF-8, so a byte test matches Go's rune test. */
static uint16_t zip_name_flags(const char *name, size_t len) {
    for (size_t i = 0; i < len; i++) {
        unsigned char c = (unsigned char)name[i];
        if (c < 0x20U || c > 0x7dU) return 0x0800U;
    }
    return 0;
}

static int zip_reader_error(neverc_zip_reader_t *r) {
    free(r->files);
    free(r->file_data);
    r->files = NULL;
    r->file_data = NULL;
    r->nfiles = 0;
    return -1;
}

typedef struct {
    uint64_t start;
    uint64_t end;
} zip_range_t;

static int zip_range_cmp(const void *left, const void *right) {
    uint64_t a = ((const zip_range_t *)left)->start;
    uint64_t b = ((const zip_range_t *)right)->start;
    return (a > b) - (a < b);
}

static int zip_reader_fail(neverc_zip_reader_t *r, zip_range_t *ranges) {
    free(ranges);
    return zip_reader_error(r);
}

static int find_eocd(const uint8_t *data, size_t len, size_t *offset) {
    if (len < 22U) return -1;
    size_t earliest = len > 22U + UINT16_MAX
        ? len - (22U + UINT16_MAX) : 0;
    size_t pos = len - 22U;
    for (;;) {
        if (read32(data + pos) == 0x06054b50U) {
            uint16_t comment_length = read16(data + pos + 20U);
            /* Go archive/zip.findSignatureInBlock (CVE-2024-24789): the
             * rightmost EOCD whose comment fits is authoritative. A
             * truncated comment fails the archive; a short comment must
             * not keep scanning for a hidden inner directory. */
            if ((size_t)comment_length > len - pos - 22U)
                return -1;
            *offset = pos;
            return 0;
        }
        if (pos == earliest) break;
        pos--;
    }
    return -1;
}

static int zip64_locator_references_end(const uint8_t *data,
                                        size_t eocd_offset) {
    if (eocd_offset < 20U) return 0;
    size_t locator_offset = eocd_offset - 20U;
    const uint8_t *locator = data + locator_offset;
    if (read32(locator) != 0x07064b50U || read32(locator + 4U) != 0U ||
        read32(locator + 16U) != 1U)
        return 0;

    uint64_t end_offset64 = read64(locator + 8U);
    if (end_offset64 > (uint64_t)locator_offset) return 0;
    size_t end_offset = (size_t)end_offset64;
    if (locator_offset - end_offset < 56U) return 0;
    const uint8_t *zip64_end = data + end_offset;
    if (read32(zip64_end) != 0x06064b50U) return 0;
    uint64_t record_size = read64(zip64_end + 4U);
    return record_size >= 44U &&
           record_size <= (uint64_t)(locator_offset - end_offset - 12U);
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
    /* APPNOTE 4.4.21 and 4.4.22 permit an exact classic count of 0xFFFF. A
     * four-byte locator signature can occur in an arbitrary central-file
     * comment, so only treat it as ZIP64 when the full single-disk locator
     * references a structurally bounded ZIP64 end record. */
    int zip64_locator = total_entries == UINT16_MAX &&
        zip64_locator_references_end(data, eocd_offset);
    if (disk != 0 || central_disk != 0 ||
        disk_entries != total_entries ||
        zip64_locator ||
        central_size == UINT32_MAX ||
        central_offset == UINT32_MAX ||
        (uint64_t)central_offset > eocd_offset ||
        (uint64_t)central_size > eocd_offset - central_offset ||
        (uint64_t)total_entries * 46U > central_size)
        return -1;

    /* Go archive/zip.readDirectoryEnd: directoryOffset is relative to the
     * start of the zip payload. A prefix (SFX stub, polyglot) becomes
     * baseOffset so CD/local records still resolve. Do not "trust" an
     * unadjusted offset that happens to look like a central header — that
     * zeros base and then fails the size identity on every prefixed zip. */
    size_t base = eocd_offset - (size_t)central_size - (size_t)central_offset;
    if (base + (uint64_t)central_offset + central_size != eocd_offset)
        return -1;
    size_t cd_offset = base + (size_t)central_offset;

    zip_range_t *ranges = NULL;
    if (total_entries > 0) {
        r->files = (neverc_zip_file_header_t *)malloc(
            (size_t)total_entries * sizeof(*r->files));
        r->file_data = (const uint8_t **)malloc(
            (size_t)total_entries * sizeof(*r->file_data));
        if (!r->files || !r->file_data) return zip_reader_error(r);
    }
    if (total_entries > 1) {
        ranges = (zip_range_t *)malloc(
            (size_t)total_entries * sizeof(*ranges));
        if (!ranges) return zip_reader_error(r);
    }

    size_t cursor = cd_offset;
    size_t central_end = cursor + central_size;
    for (uint16_t i = 0; i < total_entries; i++) {
        if (central_end - cursor < 46U ||
            read32(data + cursor) != 0x02014b50U)
            return zip_reader_fail(r, ranges);
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
            return zip_reader_fail(r, ranges);

        if ((uint64_t)base + local_offset + 30U > cd_offset ||
            read32(data + base + local_offset) != 0x04034b50U)
            return zip_reader_fail(r, ranges);
        const uint8_t *local = data + base + local_offset;
        uint16_t local_flags = read16(local + 6U);
        uint16_t local_method = read16(local + 8U);
        uint32_t local_crc = read32(local + 14U);
        uint32_t local_compressed = read32(local + 18U);
        uint32_t local_uncompressed = read32(local + 22U);
        uint16_t local_name_length = read16(local + 26U);
        uint16_t local_extra_length = read16(local + 28U);
        uint64_t data_offset =
            (uint64_t)base + local_offset + 30U +
            local_name_length + local_extra_length;
        if (local_flags != flags || local_method != method ||
            local_name_length != name_length ||
            data_offset > cd_offset ||
            compressed_size > cd_offset - data_offset ||
            memcmp(local + 30U, central + 46U, name_length) != 0)
            return zip_reader_fail(r, ranges);
        if ((flags & 0x0008U) == 0) {
            if (local_crc != crc ||
                local_compressed != compressed_size ||
                local_uncompressed != uncompressed_size)
                return zip_reader_fail(r, ranges);
        } else if ((local_crc != 0 && local_crc != crc) ||
                   (local_compressed != 0 &&
                    local_compressed != compressed_size) ||
                   (local_uncompressed != 0 &&
                    local_uncompressed != uncompressed_size)) {
            return zip_reader_fail(r, ranges);
        }
        const uint8_t *file_data = data + (size_t)data_offset;

        /* Bit 3: CRC/sizes live in a data descriptor immediately after the
         * file data (APPNOTE 4.3.9). Local CRC may be zero, so the descriptor
         * is the remaining CRC field; omitting it or storing a different CRC
         * used to be accepted. Include it in the local range so the next
         * header cannot overlap a truncated descriptor. */
        uint64_t record_end = data_offset + compressed_size;
        if (flags & 0x0008U) {
            if (record_end > cd_offset ||
                cd_offset - record_end < 12U)
                return zip_reader_fail(r, ranges);
            size_t desc = (size_t)record_end;
            uint64_t available = cd_offset - record_end;
            /* An unsigned descriptor's CRC can itself equal the optional
             * signature.  Disambiguate the two layouts using all three
             * values already known from the central directory. */
            int signed_ok = available >= 16U &&
                read32(data + desc) == 0x08074b50U &&
                read32(data + desc + 4U) == crc &&
                read32(data + desc + 8U) == compressed_size &&
                read32(data + desc + 12U) == uncompressed_size;
            int unsigned_ok =
                read32(data + desc) == crc &&
                read32(data + desc + 4U) == compressed_size &&
                read32(data + desc + 8U) == uncompressed_size;
            uint64_t desc_len;
            if (signed_ok)
                desc_len = 16U;
            else if (unsigned_ok)
                desc_len = 12U;
            else
                return zip_reader_fail(r, ranges);
            record_end += desc_len;
        }

        neverc_zip_file_header_t *file = &r->files[i];
        memset(file, 0, sizeof(*file));
        memcpy(file->name, central + 46U, name_length);
        file->name[name_length] = '\0';
        if (!zip_path_is_safe(file->name))
            return zip_reader_fail(r, ranges);
        /* APPNOTE 4.3.16 / Go archive/zip File.Open: a name ending in '/' is
         * a directory and must not carry a data section, otherwise one side
         * sees an empty directory while the other extracts smuggled bytes. */
        if (file->name[name_length - 1U] == '/' && uncompressed_size != 0)
            return zip_reader_fail(r, ranges);
        file->method = method;
        file->crc32 = crc;
        file->compressed_size = compressed_size;
        file->uncompressed_size = uncompressed_size;
        file->mod_time = mod_time;
        file->mod_date = mod_date;
        r->file_data[i] = file_data;
        if (ranges) {
            ranges[i].start = (uint64_t)base + local_offset;
            ranges[i].end = record_end;
        }
        r->nfiles++;
        cursor += (size_t)central_record_size;
    }
    if (cursor != central_end) return zip_reader_fail(r, ranges);
    if (ranges) {
        qsort(ranges, total_entries, sizeof(*ranges), zip_range_cmp);
        for (uint16_t i = 1; i < total_entries; i++) {
            if (ranges[i].start < ranges[i - 1U].end)
                return zip_reader_fail(r, ranges);
        }
    }
    /* Validate the local-range graph before touching payload bytes.  Central
     * entries can otherwise alias one large stored payload and amplify the
     * same CRC work once per entry before the overlap is finally rejected. */
    for (uint16_t i = 0; i < total_entries; i++) {
        if (neverc_crc32_ieee(r->file_data[i],
                              r->files[i].compressed_size) !=
            r->files[i].crc32)
            return zip_reader_fail(r, ranges);
    }
    free(ranges);
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
typedef struct {
    uint32_t magic;
    uint8_t closed;
    uint8_t failed;
} zip_writer_meta_t;

#define ZIP_WRITER_META_MAGIC UINT32_C(0x5a495057)

static zip_writer_meta_t zip_writer_meta_default(void) {
    zip_writer_meta_t meta;
    memset(&meta, 0, sizeof(meta));
    meta.magic = ZIP_WRITER_META_MAGIC;
    return meta;
}

static int zip_writer_meta_load(const neverc_zip_writer_t *w,
                                zip_writer_meta_t *meta) {
    if (!meta) return 0;
    *meta = zip_writer_meta_default();
    if (!w || !w->data || w->cap > SIZE_MAX - sizeof(*meta)) return 0;
    memcpy(meta, w->data + w->cap, sizeof(*meta));
    if (meta->magic != ZIP_WRITER_META_MAGIC) {
        *meta = zip_writer_meta_default();
        return 0;
    }
    return 1;
}

static void zip_writer_meta_store(neverc_zip_writer_t *w,
                                  const zip_writer_meta_t *meta) {
    if (!w || !w->data || !meta ||
        w->cap > SIZE_MAX - sizeof(*meta))
        return;
    memcpy(w->data + w->cap, meta, sizeof(*meta));
}

static int zip_writer_is_closed(const neverc_zip_writer_t *w) {
    zip_writer_meta_t meta;
    return zip_writer_meta_load(w, &meta) && meta.closed != 0;
}

static int zip_writer_has_failed(const neverc_zip_writer_t *w) {
    zip_writer_meta_t meta;
    return zip_writer_meta_load(w, &meta) && meta.failed != 0;
}

static void zip_writer_set_failed(neverc_zip_writer_t *w) {
    zip_writer_meta_t meta;
    (void)zip_writer_meta_load(w, &meta);
    meta.failed = 1;
    zip_writer_meta_store(w, &meta);
}

static void zip_writer_set_closed(neverc_zip_writer_t *w) {
    zip_writer_meta_t meta;
    (void)zip_writer_meta_load(w, &meta);
    meta.closed = 1;
    zip_writer_meta_store(w, &meta);
}

void neverc_zip_writer_init(neverc_zip_writer_t *w) {
    if (!w) return;
    memset(w, 0, sizeof(*w));
    w->cap = 4096;
    w->data = w->cap <= SIZE_MAX - sizeof(zip_writer_meta_t)
        ? (uint8_t *)malloc(w->cap + sizeof(zip_writer_meta_t)) : NULL;
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
    } else {
        zip_writer_meta_t meta = zip_writer_meta_default();
        zip_writer_meta_store(w, &meta);
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
    if (next > SIZE_MAX - sizeof(zip_writer_meta_t)) return 0;
    size_t old_cap = w->cap;
    zip_writer_meta_t meta;
    (void)zip_writer_meta_load(w, &meta);
    uint8_t *grown = (uint8_t *)realloc(
        w->data, next + sizeof(zip_writer_meta_t));
    if (!grown) return 0;
    w->data = grown;
    w->cap = next;
    if (old_cap < next) {
        size_t cleared = next - old_cap;
        if (cleared > sizeof(zip_writer_meta_t))
            cleared = sizeof(zip_writer_meta_t);
        memset(w->data + old_cap, 0, cleared);
    }
    zip_writer_meta_store(w, &meta);
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

static int zip_writer_data_offset(
    const neverc_zip_writer_t *w, const uint8_t *data, size_t len,
    size_t *offset) {
    if (!w || !w->data || !data || !offset) return 0;
    uintptr_t base = (uintptr_t)(const void *)w->data;
    uintptr_t source = (uintptr_t)(const void *)data;
    if (source < base) return 0;
    uintptr_t distance = source - base;
    if (distance > (uintptr_t)w->cap) return 0;
    size_t source_offset = (size_t)distance;
    if (len > w->cap - source_offset) return 0;
    *offset = source_offset;
    return 1;
}

int neverc_zip_writer_add(neverc_zip_writer_t *w, const char *name,
                          const uint8_t *data, size_t len) {
    if (!w || !name || zip_writer_is_closed(w) ||
        zip_writer_has_failed(w) ||
        (!data && len != 0) || len > UINT32_MAX ||
        w->len > UINT32_MAX || w->nentries < 0 ||
        w->nentries >= UINT16_MAX ||
        !zip_path_is_safe(name))
        return -1;
    size_t name_size = strlen(name);
    if (name_size == 0 || name_size > 255 ||
        name_size > SIZE_MAX - 30U ||
        len > SIZE_MAX - 30U - name_size ||
        (name[name_size - 1U] == '/' && len != 0))
        return -1;
    uint16_t name_len = (uint16_t)name_size;
    size_t record_len = 30U + name_size + len;
    if (record_len > UINT32_MAX - w->len)
        return -1;
    char name_copy[sizeof(((neverc_zip_file_header_t *)0)->name)];
    memcpy(name_copy, name, name_size + 1U);
    size_t data_offset = 0;
    int data_aliases_output =
        zip_writer_data_offset(w, data, len, &data_offset);
    uint32_t crc = neverc_crc32_ieee(data, len);
    if (!wgrow(w, record_len)) return -1;
    if (data_aliases_output) data = w->data + data_offset;

    /* Copy data before writing its header so even an overlapping view into the
     * writer allocation observes the bytes supplied at call entry. */
    uint8_t *p = w->data + w->len;
    if (len > 0) memmove(p + 30 + name_len, data, len);
    write32(p, 0x04034b50);
    write16(p + 4, 20);
    write16(p + 6, zip_name_flags(name_copy, name_size));
    write16(p + 8, NEVERC_ZIP_STORED);
    write16(p + 10, 0);
    write16(p + 12, 0);
    write32(p + 14, crc);
    write32(p + 18, (uint32_t)len);
    write32(p + 22, (uint32_t)len);
    write16(p + 26, name_len);
    write16(p + 28, 0);
    memcpy(p + 30, name_copy, name_len);

    /* Grow entry metadata only after all caller-owned input has been copied:
     * name/data may themselves be views into the old metadata arrays. */
    if (!wentries_grow(w)) return -1;
    w->offsets[w->nentries] = (uint32_t)w->len;
    neverc_zip_file_header_t *e = &w->entries[w->nentries];
    memset(e, 0, sizeof(*e));
    memcpy(e->name, name_copy, name_len < 255 ? name_len : 255);
    e->method = NEVERC_ZIP_STORED;
    e->crc32 = crc;
    e->compressed_size = len;
    e->uncompressed_size = len;
    w->nentries++;
    w->len += 30 + name_len + len;

    return 0;
}

int neverc_zip_writer_close(neverc_zip_writer_t *w) {
    if (!w || zip_writer_has_failed(w) || w->nentries < 0 ||
        w->nentries > UINT16_MAX || w->nentries > w->entries_cap ||
        (w->nentries > 0 && (!w->entries || !w->offsets)) ||
        w->len > UINT32_MAX)
        return -1;
    if (zip_writer_is_closed(w)) return 0;

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
        zip_writer_set_failed(w);
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
        write16(p + 8, zip_name_flags(e->name, name_len));
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
    zip_writer_set_closed(w);

    return 0;
}

void neverc_zip_writer_free(neverc_zip_writer_t *w) {
    if (!w) return;
    free(w->data);
    free(w->entries);
    free(w->offsets);
    memset(w, 0, sizeof(*w));
}
