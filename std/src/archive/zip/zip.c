#include "neverc/std/archive/zip.h"
#include "neverc/std/compress/flate.h"
#include "neverc/std/hash/crc32.h"
#include "neverc/std/io/fs.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

/* DEFLATE's densest encoding spends two bits (a one-bit length code for 258
 * and a one-bit distance code) per 258 output bytes, so no stream inflates
 * past 1032 bytes per input byte. A larger declared size can never be
 * produced and is rejected before callers size buffers from it. */
#define ZIP_DEFLATE_MAX_RATIO 1032U

static uint16_t read16(const uint8_t *p) { return p[0] | (p[1] << 8); }
static uint32_t read32(const uint8_t *p) { return p[0] | (p[1]<<8) | (p[2]<<16) | ((uint32_t)p[3]<<24); }
static uint64_t read64(const uint8_t *p) {
    return (uint64_t)read32(p) | ((uint64_t)read32(p + 4U) << 32U);
}
static void write16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void write32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v>>8; p[2] = v>>16; p[3] = v>>24; }
static void write64(uint8_t *p, uint64_t v) {
    write32(p, (uint32_t)v);
    write32(p + 4U, (uint32_t)(v >> 32U));
}

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

typedef struct {
    uint64_t entries;
    uint64_t central_size;
    uint64_t central_offset;
    /* File offset of the central directory's first byte and of the record
     * that must immediately follow it (the EOCD, or the ZIP64 EOCD record). */
    size_t central_start;
    size_t central_end;
    size_t base;
} zip_directory_t;

/* APPNOTE 4.3.14-4.3.15 and Go archive/zip.readDirectory64End. The locator
 * offset is used as an absolute file offset as Go does. A prefixed archive
 * whose writer left that offset relative to its own start is also accepted
 * when a fixed-size record sits directly before the locator and the offset
 * agrees with the directory it describes. The record must end at the
 * locator, and every classic EOCD field must be saturated or agree with it,
 * so readers that ignore ZIP64 cannot see a different directory. */
static int zip64_read_directory_end(const uint8_t *data, size_t eocd_offset,
                                    zip_directory_t *dir) {
    size_t locator_offset = eocd_offset - 20U;
    const uint8_t *locator = data + locator_offset;
    uint64_t referenced = read64(locator + 8U);
    size_t record = 0;
    int relative = 0;
    if (referenced <= (uint64_t)locator_offset &&
        locator_offset - (size_t)referenced >= 56U &&
        read32(data + (size_t)referenced) == 0x06064b50U) {
        record = (size_t)referenced;
    } else if (locator_offset >= 56U &&
               read32(data + locator_offset - 56U) == 0x06064b50U &&
               read64(data + locator_offset - 52U) == 44U) {
        record = locator_offset - 56U;
        relative = 1;
    } else {
        return -1;
    }

    const uint8_t *end = data + record;
    uint64_t record_size = read64(end + 4U);
    uint32_t disk = read32(end + 16U);
    uint32_t central_disk = read32(end + 20U);
    uint64_t disk_entries = read64(end + 24U);
    uint64_t entries = read64(end + 32U);
    uint64_t central_size = read64(end + 40U);
    uint64_t central_offset = read64(end + 48U);
    if (record_size < 44U ||
        record_size != (uint64_t)(locator_offset - record - 12U) ||
        disk != 0 || central_disk != 0 || disk_entries != entries ||
        central_size > (uint64_t)record ||
        central_offset > (uint64_t)record - central_size)
        return -1;

    const uint8_t *eocd = data + eocd_offset;
    uint16_t classic_disk = read16(eocd + 4U);
    uint16_t classic_central_disk = read16(eocd + 6U);
    uint16_t classic_disk_entries = read16(eocd + 8U);
    uint16_t classic_entries = read16(eocd + 10U);
    uint32_t classic_size = read32(eocd + 12U);
    uint32_t classic_offset = read32(eocd + 16U);
    if ((classic_disk != 0 && classic_disk != UINT16_MAX) ||
        (classic_central_disk != 0 && classic_central_disk != UINT16_MAX) ||
        (classic_disk_entries != UINT16_MAX &&
         classic_disk_entries != entries) ||
        (classic_entries != UINT16_MAX && classic_entries != entries) ||
        (classic_size != UINT32_MAX && classic_size != central_size) ||
        (classic_offset != UINT32_MAX && classic_offset != central_offset))
        return -1;

    size_t base = record - (size_t)central_size - (size_t)central_offset;
    if (relative && referenced != central_offset + central_size)
        return -1;
    dir->entries = entries;
    dir->central_size = central_size;
    dir->central_offset = central_offset;
    dir->central_start = base + (size_t)central_offset;
    dir->central_end = record;
    dir->base = base;
    return 0;
}

/* Go archive/zip.readDirectoryEnd. Without a well-formed single-disk ZIP64
 * locator before the EOCD, saturated classic values are literal (APPNOTE
 * 4.4.21 permits exactly 0xFFFF entries). A locator that is present must
 * reference a consistent ZIP64 record even when no classic field is
 * saturated: writers emit the records whenever an entry needed ZIP64, and
 * the central directory then ends at the record rather than at the EOCD. */
static int zip_read_directory_end(const uint8_t *data, size_t len,
                                  zip_directory_t *dir) {
    size_t eocd_offset = 0;
    if (find_eocd(data, len, &eocd_offset) != 0) return -1;
    const uint8_t *eocd = data + eocd_offset;
    uint16_t disk = read16(eocd + 4U);
    uint16_t central_disk = read16(eocd + 6U);
    uint16_t disk_entries = read16(eocd + 8U);
    uint16_t entries = read16(eocd + 10U);
    uint32_t central_size = read32(eocd + 12U);
    uint32_t central_offset = read32(eocd + 16U);
    if (eocd_offset >= 20U &&
        read32(eocd - 20U) == 0x07064b50U &&
        read32(eocd - 16U) == 0U && read32(eocd - 4U) == 1U)
        return zip64_read_directory_end(data, eocd_offset, dir);

    if (disk != 0 || central_disk != 0 || disk_entries != entries ||
        (uint64_t)central_offset > eocd_offset ||
        (uint64_t)central_size > eocd_offset - central_offset)
        return -1;
    /* Go archive/zip.readDirectoryEnd: directoryOffset is relative to the
     * start of the zip payload. A prefix (SFX stub, polyglot) becomes
     * baseOffset so CD/local records still resolve. Do not "trust" an
     * unadjusted offset that happens to look like a central header — that
     * zeros base and then fails the size identity on every prefixed zip. */
    size_t base = eocd_offset - (size_t)central_size - (size_t)central_offset;
    dir->entries = entries;
    dir->central_size = central_size;
    dir->central_offset = central_offset;
    dir->central_start = base + (size_t)central_offset;
    dir->central_end = eocd_offset;
    dir->base = base;
    return 0;
}

/* Go archive/zip.readDirectoryHeader: the ZIP64 extended-information field
 * holds, in order, the uncompressed size, compressed size and local header
 * offset, each present only when its fixed-width field is saturated. Without
 * the field a saturated value is literal. Like Go, scanning stops at a
 * truncated extra record and a later ZIP64 field overrides an earlier one.
 * fields[] lists the outputs in that order, NULL for unsaturated ones. */
static int zip64_extra_values(const uint8_t *extra, size_t length,
                              uint64_t *const *fields, size_t count) {
    size_t pos = 0;
    while (length - pos >= 4U) {
        uint16_t tag = read16(extra + pos);
        size_t size = read16(extra + pos + 2U);
        pos += 4U;
        if (size > length - pos) break;
        if (tag == 0x0001U) {
            size_t used = 0;
            for (size_t i = 0; i < count; i++) {
                if (!fields[i]) continue;
                if (size - used < 8U) return -1;
                *fields[i] = read64(extra + pos + used);
                used += 8U;
            }
        }
        pos += size;
    }
    return 0;
}

/* A local header records the CRC and sizes again, or zeros when bit 3 defers
 * them to a data descriptor. */
static int zip_local_value_matches(uint64_t local, uint64_t central,
                                   int descriptor) {
    return local == central || (descriptor && local == 0);
}

/* APPNOTE 4.3.9: the descriptor's optional signature, the CRC, then sizes
 * that are 8 bytes wide for ZIP64 entries. An unsigned descriptor's CRC can
 * itself equal the optional signature, so each layout must match all three
 * values already known from the central directory; the shortest match
 * claims the fewest bytes. Returns the descriptor length or 0. */
static size_t zip_descriptor_length(const uint8_t *desc, uint64_t available,
                                    uint32_t crc, uint64_t compressed,
                                    uint64_t uncompressed) {
    int has_signature = available >= 4U && read32(desc) == 0x08074b50U;
    if (available >= 16U && has_signature && read32(desc + 4U) == crc &&
        read32(desc + 8U) == compressed &&
        read32(desc + 12U) == uncompressed)
        return 16U;
    if (available >= 12U && read32(desc) == crc &&
        read32(desc + 4U) == compressed &&
        read32(desc + 8U) == uncompressed)
        return 12U;
    if (available >= 24U && has_signature && read32(desc + 4U) == crc &&
        read64(desc + 8U) == compressed &&
        read64(desc + 16U) == uncompressed)
        return 24U;
    if (available >= 20U && read32(desc) == crc &&
        read64(desc + 4U) == compressed &&
        read64(desc + 12U) == uncompressed)
        return 20U;
    return 0;
}

int neverc_zip_reader_init(neverc_zip_reader_t *r, const uint8_t *data, size_t len) {
    if (!r) return -1;
    memset(r, 0, sizeof(*r));
    r->data = data;
    r->len = len;
    if (!data || len < 22U) return -1;

    zip_directory_t dir;
    if (zip_read_directory_end(data, len, &dir) != 0 ||
        dir.entries > (uint64_t)INT_MAX ||
        dir.entries > dir.central_size / 46U ||
        dir.entries > SIZE_MAX / sizeof(*r->files))
        return -1;
    size_t total_entries = (size_t)dir.entries;
    size_t base = dir.base;
    size_t cd_offset = dir.central_start;

    zip_range_t *ranges = NULL;
    if (total_entries > 0) {
        r->files = (neverc_zip_file_header_t *)malloc(
            total_entries * sizeof(*r->files));
        r->file_data = (const uint8_t **)malloc(
            total_entries * sizeof(*r->file_data));
        if (!r->files || !r->file_data) return zip_reader_error(r);
    }
    if (total_entries > 1) {
        ranges = (zip_range_t *)malloc(total_entries * sizeof(*ranges));
        if (!ranges) return zip_reader_error(r);
    }

    size_t cursor = cd_offset;
    size_t central_end = dir.central_end;
    for (size_t i = 0; i < total_entries; i++) {
        if (central_end - cursor < 46U ||
            read32(data + cursor) != 0x02014b50U)
            return zip_reader_fail(r, ranges);
        const uint8_t *central = data + cursor;
        uint16_t flags = read16(central + 8U);
        uint16_t method = read16(central + 10U);
        uint16_t mod_time = read16(central + 12U);
        uint16_t mod_date = read16(central + 14U);
        uint32_t crc = read32(central + 16U);
        uint32_t compressed32 = read32(central + 20U);
        uint32_t uncompressed32 = read32(central + 24U);
        uint16_t name_length = read16(central + 28U);
        uint16_t extra_length = read16(central + 30U);
        uint16_t comment_length = read16(central + 32U);
        uint16_t start_disk = read16(central + 34U);
        uint32_t local_offset32 = read32(central + 42U);
        uint64_t central_record_size =
            46U + (uint64_t)name_length + extra_length + comment_length;
        /* Bits 1-2 carry the DEFLATE compression-level hint, which writers
         * also leave on entries they fell back to storing; any other bit
         * besides the data descriptor and UTF-8 flags (encryption, patched
         * data, reserved) is unsupported. */
        int deflated = method == NEVERC_ZIP_DEFLATED;
        if (central_record_size > central_end - cursor ||
            start_disk != 0 || (flags & ~(uint16_t)0x080EU) != 0 ||
            (method != NEVERC_ZIP_STORED && !deflated) ||
            name_length > 255U || name_length == 0 ||
            memchr(central + 46U, '\0', name_length) != NULL)
            return zip_reader_fail(r, ranges);

        uint64_t compressed_size = compressed32;
        uint64_t uncompressed_size = uncompressed32;
        uint64_t local_offset = local_offset32;
        uint64_t *const central_fields[3] = {
            uncompressed32 == UINT32_MAX ? &uncompressed_size : NULL,
            compressed32 == UINT32_MAX ? &compressed_size : NULL,
            local_offset32 == UINT32_MAX ? &local_offset : NULL,
        };
        if (zip64_extra_values(central + 46U + name_length, extra_length,
                               central_fields, 3U) != 0)
            return zip_reader_fail(r, ranges);

        /* Every offset below is bounded by the central directory before it
         * is added to anything, so 64-bit ZIP64 values cannot wrap. */
        size_t locals_space = cd_offset - base;
        if (local_offset > (uint64_t)locals_space ||
            locals_space - (size_t)local_offset < 30U ||
            read32(data + base + (size_t)local_offset) != 0x04034b50U)
            return zip_reader_fail(r, ranges);
        size_t local_start = base + (size_t)local_offset;
        const uint8_t *local = data + local_start;
        uint16_t local_flags = read16(local + 6U);
        uint16_t local_method = read16(local + 8U);
        uint32_t local_crc = read32(local + 14U);
        uint32_t local_compressed32 = read32(local + 18U);
        uint32_t local_uncompressed32 = read32(local + 22U);
        uint16_t local_name_length = read16(local + 26U);
        uint16_t local_extra_length = read16(local + 28U);
        if (local_flags != flags || local_method != method ||
            local_name_length != name_length ||
            cd_offset - local_start - 30U <
                (size_t)local_name_length + local_extra_length ||
            memcmp(local + 30U, central + 46U, name_length) != 0)
            return zip_reader_fail(r, ranges);
        size_t data_offset =
            local_start + 30U + local_name_length + local_extra_length;
        if (compressed_size > (uint64_t)(cd_offset - data_offset) ||
            (!deflated && compressed_size != uncompressed_size) ||
            (deflated &&
             compressed_size <= UINT64_MAX / ZIP_DEFLATE_MAX_RATIO &&
             uncompressed_size > compressed_size * ZIP_DEFLATE_MAX_RATIO))
            return zip_reader_fail(r, ranges);

        /* Local sizes may be saturated and carried by the local ZIP64
         * field, even for small entries; compare the resolved values. */
        uint64_t local_compressed = local_compressed32;
        uint64_t local_uncompressed = local_uncompressed32;
        uint64_t *const local_fields[2] = {
            local_uncompressed32 == UINT32_MAX ? &local_uncompressed : NULL,
            local_compressed32 == UINT32_MAX ? &local_compressed : NULL,
        };
        int descriptor = (flags & 0x0008U) != 0;
        if (zip64_extra_values(local + 30U + name_length,
                               local_extra_length, local_fields, 2U) != 0 ||
            !zip_local_value_matches(local_crc, crc, descriptor) ||
            !zip_local_value_matches(local_compressed, compressed_size,
                                     descriptor) ||
            !zip_local_value_matches(local_uncompressed, uncompressed_size,
                                     descriptor))
            return zip_reader_fail(r, ranges);
        const uint8_t *file_data = data + data_offset;

        /* Bit 3: CRC/sizes live in a data descriptor immediately after the
         * file data (APPNOTE 4.3.9). Local CRC may be zero, so the descriptor
         * is the remaining CRC field; omitting it or storing a different CRC
         * used to be accepted. Include it in the local range so the next
         * header cannot overlap a truncated descriptor. */
        size_t record_end = data_offset + (size_t)compressed_size;
        if (descriptor) {
            size_t desc_len = zip_descriptor_length(
                data + record_end, cd_offset - record_end, crc,
                compressed_size, uncompressed_size);
            if (desc_len == 0) return zip_reader_fail(r, ranges);
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
            ranges[i].start = local_start;
            ranges[i].end = record_end;
        }
        r->nfiles++;
        cursor += (size_t)central_record_size;
    }
    if (cursor != central_end) return zip_reader_fail(r, ranges);
    if (ranges) {
        qsort(ranges, total_entries, sizeof(*ranges), zip_range_cmp);
        for (size_t i = 1; i < total_entries; i++) {
            if (ranges[i].start < ranges[i - 1U].end)
                return zip_reader_fail(r, ranges);
        }
    }
    /* Validate the local-range graph before touching payload bytes.  Central
     * entries can otherwise alias one large stored payload and amplify the
     * same CRC work once per entry before the overlap is finally rejected.
     * Compressed entries are checked when reader_file_read inflates them. */
    for (size_t i = 0; i < total_entries; i++) {
        if (r->files[i].method == NEVERC_ZIP_STORED &&
            neverc_crc32_ieee(r->file_data[i],
                              (size_t)r->files[i].compressed_size) !=
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
    if (!r || idx < 0 || idx >= r->nfiles ||
        r->files[idx].method != NEVERC_ZIP_STORED)
        return NULL;
    *len = (size_t)r->files[idx].compressed_size;
    return r->file_data[idx];
}

int neverc_zip_reader_file_read(const neverc_zip_reader_t *r, int idx,
                                uint8_t *dst, size_t *dst_len) {
    if (!dst_len) return -1;
    size_t capacity = *dst_len;
    *dst_len = 0;
    if (!r || !r->files || !r->file_data || idx < 0 || idx >= r->nfiles)
        return -1;
    const neverc_zip_file_header_t *file = &r->files[idx];
    if (file->uncompressed_size > (uint64_t)SIZE_MAX ||
        file->compressed_size > (uint64_t)SIZE_MAX)
        return -1;
    size_t size = (size_t)file->uncompressed_size;
    if (size > capacity || (!dst && size != 0)) return -1;
    const uint8_t *source = r->file_data[idx];

    if (file->method == NEVERC_ZIP_STORED) {
        /* reader_init already verified the stored bytes' CRC-32. */
        if (size > 0) memcpy(dst, source, size);
        *dst_len = size;
        return 0;
    }
    if (file->method != NEVERC_ZIP_DEFLATED) return -1;

    /* Go archive/zip File.Open: a directory never has data to inflate, even
     * when a writer labelled its empty body as Deflate. reader_init already
     * guarantees its uncompressed size is zero. */
    size_t name_length = strlen(file->name);
    if (name_length > 0 && file->name[name_length - 1U] == '/')
        return 0;

    /* Bound the output by the declared size rather than the caller's
     * capacity: a stream that produces more is as malformed as one that ends
     * short. Bytes after the final block inside the compressed size are
     * ignored, as Go's reader does. */
    size_t produced = size;
    size_t consumed = 0;
    if (neverc_flate_decompress_consumed(
            source, (size_t)file->compressed_size, dst, &produced,
            &consumed) != 0 ||
        produced != size || neverc_crc32_ieee(dst, size) != file->crc32) {
        if (size > 0) memset(dst, 0, size);
        return -1;
    }
    *dst_len = size;
    return 0;
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

/* Compresses data for a Deflate entry into a new buffer. Returns NULL when
 * DEFLATE would not make the entry smaller (or cannot run), in which case the
 * entry is stored instead, as mainstream archivers do. */
static uint8_t *zip_writer_deflate(const uint8_t *data, size_t len,
                                   size_t *packed_len) {
    /* Compressed DEFLATE levels accept at most UINT32_MAX input bytes. */
    if (len < 2U || len > UINT32_MAX) return NULL;
    uint8_t *packed = (uint8_t *)malloc(len - 1U);
    if (!packed) return NULL;
    *packed_len = len - 1U;
    if (neverc_flate_compress(data, len, packed, packed_len,
                              NEVERC_FLATE_DEFAULT) != 0) {
        free(packed);
        return NULL;
    }
    return packed;
}

/* Go archive/zip writes a ZIP64 field for any value that reaches
 * 0xFFFFFFFF: readers treat that value as the ZIP64 marker. */
static int zip_needs_zip64(uint64_t value) {
    return value >= UINT32_MAX;
}

/* Local records carry both sizes in a 20-byte ZIP64 field (APPNOTE 4.5.3)
 * when either needs it, so a record's length follows from its entry alone
 * and writer_close can rebuild 64-bit offsets from the entries. */
static int zip_entry_local_zip64(const neverc_zip_file_header_t *e) {
    return zip_needs_zip64(e->compressed_size) ||
           zip_needs_zip64(e->uncompressed_size);
}

static uint64_t zip_entry_local_length(const neverc_zip_file_header_t *e,
                                       size_t name_length) {
    return 30U + (uint64_t)name_length +
           (zip_entry_local_zip64(e) ? 20U : 0U) + e->compressed_size;
}

static int zip_writer_add_entry(neverc_zip_writer_t *w, const char *name,
                                const uint8_t *data, size_t len,
                                uint16_t method) {
    if (!w || !name || zip_writer_is_closed(w) ||
        zip_writer_has_failed(w) ||
        (!data && len != 0) || w->nentries < 0 ||
        w->nentries > w->entries_cap ||
        !zip_path_is_safe(name))
        return -1;
    size_t name_size = strlen(name);
    if (name_size == 0 || name_size > 255 ||
        len > SIZE_MAX - 50U - name_size ||
        (name[name_size - 1U] == '/' && len != 0))
        return -1;
    uint16_t name_len = (uint16_t)name_size;
    size_t record_len =
        30U + name_size + (zip_needs_zip64(len) ? 20U : 0U) + len;
    if (record_len > SIZE_MAX - w->len)
        return -1;
    char name_copy[sizeof(((neverc_zip_file_header_t *)0)->name)];
    memcpy(name_copy, name, name_size + 1U);
    size_t data_offset = 0;
    int data_aliases_output =
        zip_writer_data_offset(w, data, len, &data_offset);
    uint32_t crc = neverc_crc32_ieee(data, len);

    /* Compress before growing the output: data may be a view into it. A
     * directory never carries data, so it is always Stored (as in Go). */
    size_t packed_len = 0;
    uint8_t *packed = method == NEVERC_ZIP_DEFLATED
        ? zip_writer_deflate(data, len, &packed_len) : NULL;
    size_t payload_len = packed ? packed_len : len;
    int zip64 = zip_needs_zip64(payload_len) || zip_needs_zip64(len);
    size_t header_len = 30U + name_size + (zip64 ? 20U : 0U);
    record_len = header_len + payload_len;
    if (!wgrow(w, record_len)) {
        free(packed);
        return -1;
    }
    if (data_aliases_output) data = w->data + data_offset;

    /* Copy data before writing its header so even an overlapping view into the
     * writer allocation observes the bytes supplied at call entry. */
    uint8_t *p = w->data + w->len;
    if (packed) {
        memcpy(p + header_len, packed, packed_len);
        free(packed);
    } else if (len > 0) {
        memmove(p + header_len, data, len);
    }
    uint16_t entry_method = packed ? NEVERC_ZIP_DEFLATED : NEVERC_ZIP_STORED;
    write32(p, 0x04034b50);
    write16(p + 4, zip64 ? 45 : 20);
    write16(p + 6, zip_name_flags(name_copy, name_size));
    write16(p + 8, entry_method);
    write16(p + 10, 0);
    write16(p + 12, 0);
    write32(p + 14, crc);
    write32(p + 18, zip64 ? UINT32_MAX : (uint32_t)payload_len);
    write32(p + 22, zip64 ? UINT32_MAX : (uint32_t)len);
    write16(p + 26, name_len);
    write16(p + 28, zip64 ? 20 : 0);
    memcpy(p + 30, name_copy, name_len);
    if (zip64) {
        uint8_t *extra = p + 30 + name_len;
        write16(extra, 0x0001);
        write16(extra + 2, 16);
        write64(extra + 4, len);
        write64(extra + 12, payload_len);
    }

    /* Grow entry metadata only after all caller-owned input has been copied:
     * name/data may themselves be views into the old metadata arrays. */
    if (!wentries_grow(w)) return -1;
    /* Only the low 32 bits fit; writer_close rebuilds the full offsets. */
    w->offsets[w->nentries] = (uint32_t)w->len;
    neverc_zip_file_header_t *e = &w->entries[w->nentries];
    memset(e, 0, sizeof(*e));
    memcpy(e->name, name_copy, name_len < 255 ? name_len : 255);
    e->method = entry_method;
    e->crc32 = crc;
    e->compressed_size = payload_len;
    e->uncompressed_size = len;
    w->nentries++;
    w->len += record_len;

    return 0;
}

int neverc_zip_writer_add(neverc_zip_writer_t *w, const char *name,
                          const uint8_t *data, size_t len) {
    return zip_writer_add_entry(w, name, data, len, NEVERC_ZIP_STORED);
}

int neverc_zip_writer_add_method(neverc_zip_writer_t *w, const char *name,
                                 const uint8_t *data, size_t len,
                                 uint16_t method) {
    if (method != NEVERC_ZIP_STORED && method != NEVERC_ZIP_DEFLATED)
        return -1;
    return zip_writer_add_entry(w, name, data, len, method);
}

/* Size of an entry's central ZIP64 field: Go archive/zip lists exactly the
 * uncompressed size, compressed size and local offset that reach
 * 0xFFFFFFFF, in that order. */
static size_t zip_central_zip64_length(const neverc_zip_file_header_t *e,
                                       uint64_t offset) {
    size_t fields = (size_t)zip_needs_zip64(e->uncompressed_size) +
                    (size_t)zip_needs_zip64(e->compressed_size) +
                    (size_t)zip_needs_zip64(offset);
    return fields ? 4U + 8U * fields : 0U;
}

static uint32_t zip_saturate32(uint64_t value) {
    return zip_needs_zip64(value) ? UINT32_MAX : (uint32_t)value;
}

int neverc_zip_writer_close(neverc_zip_writer_t *w) {
    if (!w || zip_writer_has_failed(w) || w->nentries < 0 ||
        w->nentries > w->entries_cap ||
        (w->nentries > 0 && (!w->entries || !w->offsets)))
        return -1;
    if (zip_writer_is_closed(w)) return 0;

    /* Rebuild each local record's 64-bit offset from the entries, checked
     * against the low 32 bits recorded by add and the output length. */
    uint64_t offset = 0;
    size_t central_bytes = 0;
    int zip64 = w->nentries > UINT16_MAX;
    for (int i = 0; i < w->nentries; i++) {
        const neverc_zip_file_header_t *e = &w->entries[i];
        size_t name_length = strlen(e->name);
        if (name_length == 0 || name_length > 255U ||
            (uint32_t)offset != w->offsets[i])
            return -1;
        size_t extra = zip_central_zip64_length(e, offset);
        if (extra != 0) zip64 = 1;
        uint64_t local = zip_entry_local_length(e, name_length);
        if (central_bytes > SIZE_MAX - 46U - name_length - extra ||
            e->compressed_size > (uint64_t)w->len ||
            local > UINT64_MAX - offset)
            return -1;
        central_bytes += 46U + name_length + extra;
        offset += local;
    }
    if (offset != (uint64_t)w->len) return -1;
    uint64_t central_offset = w->len;
    if (zip_needs_zip64(central_bytes) || zip_needs_zip64(central_offset))
        zip64 = 1;
    size_t end_bytes = (zip64 ? 56U + 20U : 0U) + 22U;
    if (central_bytes > SIZE_MAX - end_bytes ||
        !wgrow(w, central_bytes + end_bytes)) {
        zip_writer_set_failed(w);
        return -1;
    }

    offset = 0;
    for (int i = 0; i < w->nentries; i++) {
        neverc_zip_file_header_t *e = &w->entries[i];
        uint16_t name_len = (uint16_t)strlen(e->name);
        size_t extra = zip_central_zip64_length(e, offset);

        uint8_t *p = w->data + w->len;
        write32(p, 0x02014b50);
        write16(p + 4, 20);
        write16(p + 6, extra != 0 ? 45 : 20);
        write16(p + 8, zip_name_flags(e->name, name_len));
        write16(p + 10, e->method);
        write16(p + 12, e->mod_time);
        write16(p + 14, e->mod_date);
        write32(p + 16, e->crc32);
        write32(p + 20, zip_saturate32(e->compressed_size));
        write32(p + 24, zip_saturate32(e->uncompressed_size));
        write16(p + 28, name_len);
        write16(p + 30, (uint16_t)extra);
        write16(p + 32, 0);
        write16(p + 34, 0);
        write16(p + 36, 0);
        write32(p + 38, 0);
        write32(p + 42, zip_saturate32(offset));
        memcpy(p + 46, e->name, name_len);
        if (extra != 0) {
            uint8_t *field = p + 46 + name_len;
            write16(field, 0x0001);
            write16(field + 2, (uint16_t)(extra - 4U));
            field += 4;
            if (zip_needs_zip64(e->uncompressed_size)) {
                write64(field, e->uncompressed_size);
                field += 8;
            }
            if (zip_needs_zip64(e->compressed_size)) {
                write64(field, e->compressed_size);
                field += 8;
            }
            if (zip_needs_zip64(offset)) write64(field, offset);
        }
        w->len += 46 + name_len + extra;
        offset += zip_entry_local_length(e, name_len);
    }

    uint64_t central_size = (uint64_t)w->len - central_offset;
    uint64_t records = (uint64_t)w->nentries;
    if (zip64) {
        /* ZIP64 end record and locator (APPNOTE 4.3.14-4.3.15), written
         * whenever any entry or end field needed ZIP64, as Go does. */
        uint64_t end64 = w->len;
        uint8_t *p = w->data + w->len;
        write32(p, 0x06064b50);
        write64(p + 4, 44);
        write16(p + 12, 45);
        write16(p + 14, 45);
        write32(p + 16, 0);
        write32(p + 20, 0);
        write64(p + 24, records);
        write64(p + 32, records);
        write64(p + 40, central_size);
        write64(p + 48, central_offset);
        write32(p + 56, 0x07064b50);
        write32(p + 60, 0);
        write64(p + 64, end64);
        write32(p + 72, 1);
        w->len += 76;
    }

    /* End of central directory; values that do not fit are saturated. A
     * classic archive may hold exactly 0xFFFF entries. */
    uint8_t *p = w->data + w->len;
    write32(p, 0x06054b50);
    write16(p + 4, 0);
    write16(p + 6, 0);
    write16(p + 8, records > UINT16_MAX ? UINT16_MAX : (uint16_t)records);
    write16(p + 10, records > UINT16_MAX ? UINT16_MAX : (uint16_t)records);
    write32(p + 12, zip_saturate32(central_size));
    write32(p + 16, zip_saturate32(central_offset));
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
