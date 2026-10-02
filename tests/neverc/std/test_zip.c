#include "neverc/std/archive/zip.h"
#include "neverc/std/compress/flate.h"
#include "neverc/std/hash/crc32.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %d, expected %d\n", name, got, expected); }
}
static void check_str(const char *name, const char *got, const char *expected) {
    tests_run++;
    if (got && expected && strcmp(got, expected) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got \"%s\", expected \"%s\"\n", name, got?got:"(null)", expected); }
}
static void check_size(const char *name, size_t got, size_t expected) {
    tests_run++;
    if (got == expected) tests_passed++;
    else { tests_failed++; printf("  FAIL: %s: got %zu, expected %zu\n", name, got, expected); }
}

static void test_roundtrip(void) {
    printf("[write/read roundtrip]\n");

    neverc_zip_writer_t w;
    neverc_zip_writer_init(&w);

    int ok =
        neverc_zip_writer_add(
            &w, "hello.txt", (const uint8_t *)"Hello!", 6) == 0 &&
        neverc_zip_writer_add(
            &w, "data.bin", (const uint8_t *)"\x01\x02\x03", 3) == 0 &&
        neverc_zip_writer_add(
            &w, "empty.txt", (const uint8_t *)"", 0) == 0 &&
        neverc_zip_writer_close(&w) == 0;
    check_int("writer succeeds", ok, 1);
    if (!ok) {
        neverc_zip_writer_free(&w);
        return;
    }

    /* Read back */
    neverc_zip_reader_t r;
    int init_result = neverc_zip_reader_init(&r, w.data, w.len);
    check_int("reader init", init_result, 0);
    if (init_result != 0) {
        neverc_zip_writer_free(&w);
        return;
    }

    check_int("count", neverc_zip_reader_count(&r), 3);

    const neverc_zip_file_header_t *f0 = neverc_zip_reader_file(&r, 0);
    if (!f0) {
        check_int("file 0 exists", 0, 1);
        neverc_zip_reader_free(&r);
        neverc_zip_writer_free(&w);
        return;
    }
    check_str("name 0", f0->name, "hello.txt");
    check_size("size 0", (size_t)f0->uncompressed_size, 6);

    size_t dlen;
    const uint8_t *d0 = neverc_zip_reader_file_data(&r, 0, &dlen);
    check_size("data len 0", dlen, 6);
    tests_run++;
    if (d0 && dlen == 6 && memcmp(d0, "Hello!", 6) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: data 0 content\n"); }

    const neverc_zip_file_header_t *f1 = neverc_zip_reader_file(&r, 1);
    if (!f1) {
        check_int("file 1 exists", 0, 1);
        neverc_zip_reader_free(&r);
        neverc_zip_writer_free(&w);
        return;
    }
    check_str("name 1", f1->name, "data.bin");
    const uint8_t *d1 = neverc_zip_reader_file_data(&r, 1, &dlen);
    check_size("data len 1", dlen, 3);
    check_int("data 1 exists", d1 != NULL, 1);
    if (d1) {
        check_int("byte 0", d1[0], 1);
        check_int("byte 2", d1[2], 3);
    }

    const neverc_zip_file_header_t *f2 = neverc_zip_reader_file(&r, 2);
    if (!f2) {
        check_int("file 2 exists", 0, 1);
        neverc_zip_reader_free(&r);
        neverc_zip_writer_free(&w);
        return;
    }
    check_str("name 2", f2->name, "empty.txt");
    check_size("size 2", (size_t)f2->uncompressed_size, 0);

    neverc_zip_reader_free(&r);
    neverc_zip_writer_free(&w);
}

static void test_crc_integrity(void) {
    printf("[crc integrity]\n");
    neverc_zip_writer_t w;
    neverc_zip_writer_init(&w);
    int built =
        neverc_zip_writer_add(
            &w, "test.txt", (const uint8_t *)"test data", 9) == 0 &&
        neverc_zip_writer_close(&w) == 0;
    check_int("crc fixture built", built, 1);
    if (!built) {
        neverc_zip_writer_free(&w);
        return;
    }

    neverc_zip_reader_t r;
    int init_result = neverc_zip_reader_init(&r, w.data, w.len);
    check_int("crc reader init", init_result, 0);
    if (init_result != 0) {
        neverc_zip_writer_free(&w);
        return;
    }
    const neverc_zip_file_header_t *f = neverc_zip_reader_file(&r, 0);
    check_int("crc file exists", f != NULL, 1);
    if (f) {
        check_int("crc nonzero", f->crc32 != 0, 1);
        check_int("method stored", f->method, NEVERC_ZIP_STORED);
    }

    neverc_zip_reader_free(&r);

    uint8_t *corrupt = (uint8_t *)malloc(w.len);
    if (!corrupt) {
        check_int("crc corruption allocation", 0, 1);
        neverc_zip_writer_free(&w);
        return;
    }
    memcpy(corrupt, w.data, w.len);
    corrupt[30U + strlen("test.txt")] ^= 1U;
    check_int("reject corrupt payload",
              neverc_zip_reader_init(&r, corrupt, w.len), -1);
    neverc_zip_reader_free(&r);
    free(corrupt);
    neverc_zip_writer_free(&w);
}

/* Crafted local header: huge comp_size must not advance pos past the buffer or
 * leave file_data pointing OOB. */
static void test_truncated_entry(void) {
    printf("[truncated entry]\n");
    uint8_t buf[64];
    memset(buf, 0, sizeof(buf));
    buf[0] = 0x50; buf[1] = 0x4b; buf[2] = 0x03; buf[3] = 0x04; /* PK\x03\x04 */
    buf[26] = 4; buf[27] = 0;   /* name_len LE */
    memcpy(buf + 30, "evil", 4);
    buf[18] = 0xFF; buf[19] = 0xFF; buf[20] = 0xFF; buf[21] = 0xFF; /* comp_size */

    neverc_zip_reader_t r;
    check_int("truncated archive rejected",
              neverc_zip_reader_init(&r, buf, sizeof(buf)), -1);
    neverc_zip_reader_free(&r);

    neverc_zip_writer_t writer;
    neverc_zip_writer_init(&writer);
    int built = neverc_zip_writer_add(
                    &writer, "t", (const uint8_t *)"xy", 2) == 0 &&
                neverc_zip_writer_close(&writer) == 0;
    check_int("truncation fixture", built, 1);
    if (built && writer.len > 22U) {
        check_int("reject truncated eocd",
                  neverc_zip_reader_init(&r, writer.data, writer.len - 1U), -1);
        neverc_zip_reader_free(&r);
    }
    neverc_zip_writer_free(&writer);
}

static void test_central_directory_and_writer_state(void) {
    printf("[central directory and writer state]\n");
    neverc_zip_writer_t writer;
    neverc_zip_writer_init(&writer);
    int add_result = neverc_zip_writer_add(
        &writer, "a", (const uint8_t *)"x", 1);
    check_int("state add", add_result, 0);
    int close_result = neverc_zip_writer_close(&writer);
    check_int("state close", close_result, 0);
    if (add_result != 0 || close_result != 0 || writer.len < 22U) {
        neverc_zip_writer_free(&writer);
        return;
    }
    size_t closed_length = writer.len;
    check_int("state close idempotent",
              neverc_zip_writer_close(&writer), 0);
    check_size("state close length stable", writer.len, closed_length);
    check_int("state reject add after close",
              neverc_zip_writer_add(
                  &writer, "b", (const uint8_t *)"y", 1), -1);

    uint8_t *mutated = (uint8_t *)malloc(writer.len);
    if (!mutated) {
        check_int("central mutation allocation", 0, 1);
        neverc_zip_writer_free(&writer);
        return;
    }
    memcpy(mutated, writer.data, writer.len);
    size_t eocd = writer.len - 22U;
    uint32_t central_offset =
        (uint32_t)mutated[eocd + 16U] |
        ((uint32_t)mutated[eocd + 17U] << 8U) |
        ((uint32_t)mutated[eocd + 18U] << 16U) |
        ((uint32_t)mutated[eocd + 19U] << 24U);
    mutated[central_offset + 10U] = NEVERC_ZIP_DEFLATED;
    mutated[central_offset + 11U] = 0;
    neverc_zip_reader_t reader;
    check_int("reject unsupported central method",
              neverc_zip_reader_init(
                  &reader, mutated, writer.len), -1);
    neverc_zip_reader_free(&reader);
    free(mutated);
    neverc_zip_writer_free(&writer);

    neverc_zip_writer_init(&writer);
    check_int("empty writer close", neverc_zip_writer_close(&writer), 0);
    check_int("empty archive read",
              neverc_zip_reader_init(
                  &reader, writer.data, writer.len), 0);
    check_int("empty archive count",
              neverc_zip_reader_count(&reader), 0);
    neverc_zip_reader_free(&reader);
    neverc_zip_writer_free(&writer);
}

static void test_invalid_args(void) {
    printf("[invalid args]\n");
    neverc_zip_writer_t w;
    neverc_zip_writer_init(&w);

    char long_name[257];
    memset(long_name, 'x', sizeof(long_name) - 1);
    long_name[sizeof(long_name) - 1] = '\0';
    check_int("oversized name rejected",
              neverc_zip_writer_add(&w, long_name,
                                     (const uint8_t *)"", 0), -1);

    uint8_t byte = 0;
    check_int("oversized data rejected",
              neverc_zip_writer_add(&w, "huge", &byte, SIZE_MAX), -1);
    neverc_zip_writer_free(&w);

    /* The writer state is public. An entry count beyond the metadata
     * capacity must be rejected before the arrays are regrown from it. */
    neverc_zip_writer_init(&w);
    w.nentries = w.entries_cap + 1;
    check_int("entry count beyond capacity rejected",
              neverc_zip_writer_add(&w, "x", (const uint8_t *)"x", 1), -1);
    neverc_zip_writer_free(&w);
}

static void test_reject_unsafe_paths(void) {
    printf("[reject_unsafe_paths]\n");
    neverc_zip_writer_t w;
    neverc_zip_writer_init(&w);
    const uint8_t payload[] = "x";
    check_int("writer rejects traversal",
              neverc_zip_writer_add(
                  &w, "../etc/passwd", payload, sizeof(payload) - 1), -1);
    check_int("writer rejects absolute",
              neverc_zip_writer_add(
                  &w, "/etc/passwd", payload, sizeof(payload) - 1), -1);
    check_int("writer rejects dot",
              neverc_zip_writer_add(
                  &w, ".", payload, sizeof(payload) - 1), -1);
    check_int("writer rejects dot slash",
              neverc_zip_writer_add(
                  &w, "./", payload, sizeof(payload) - 1), -1);
    check_int("writer rejects colon ads",
              neverc_zip_writer_add(
                  &w, "file:stream", payload, sizeof(payload) - 1), -1);
    check_int("writer rejects drive prefix",
              neverc_zip_writer_add(
                  &w, "C:foo", payload, sizeof(payload) - 1), -1);
    check_int("writer rejects nested traversal",
              neverc_zip_writer_add(
                  &w, "foo/../bar", payload, sizeof(payload) - 1), -1);
    check_int("writer rejects deep traversal",
              neverc_zip_writer_add(
                  &w, "foo/bar/../../etc/passwd", payload,
                  sizeof(payload) - 1), -1);
    check_int("writer rejects backslash",
              neverc_zip_writer_add(
                  &w, "foo\\..\\bar", payload, sizeof(payload) - 1), -1);
    check_int("writer rejects dot then traversal",
              neverc_zip_writer_add(
                  &w, "a/./../b", payload, sizeof(payload) - 1), -1);
    check_int("writer rejects dot only path",
              neverc_zip_writer_add(
                  &w, "./.", payload, sizeof(payload) - 1), -1);
    check_int("writer rejects directory with data",
              neverc_zip_writer_add(
                  &w, "dir/", payload, sizeof(payload) - 1), -1);
    neverc_zip_writer_free(&w);
}

/* Dot components are lexical no-ops, so archive/tar accepts them; zip kept an
 * older spelling that rejected the whole archive instead. */
static void test_dot_component_paths(void) {
    printf("[dot_component_paths]\n");
    neverc_zip_writer_t w;
    neverc_zip_writer_init(&w);
    check_int("writer accepts leading dot",
              neverc_zip_writer_add(&w, "./a.txt", (const uint8_t *)"x", 1), 0);
    check_int("writer accepts interior dot",
              neverc_zip_writer_add(&w, "b/./c.txt", (const uint8_t *)"y", 1),
              0);
    check_int("writer closes", neverc_zip_writer_close(&w), 0);

    neverc_zip_reader_t r;
    check_int("reader accepts dot components",
              neverc_zip_reader_init(&r, w.data, w.len), 0);
    check_int("dot component count", neverc_zip_reader_count(&r), 2);
    {
        const neverc_zip_file_header_t *first = neverc_zip_reader_file(&r, 0);
        const neverc_zip_file_header_t *second = neverc_zip_reader_file(&r, 1);
        check_int("dot entries exist", first != NULL && second != NULL, 1);
        if (first) check_str("preserve leading dot", first->name, "./a.txt");
        if (second) check_str("preserve interior dot", second->name,
                              "b/./c.txt");
    }
    neverc_zip_reader_free(&r);
    neverc_zip_writer_free(&w);
}


static void put16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
    p[2] = (uint8_t)(v >> 16);
    p[3] = (uint8_t)(v >> 24);
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* Stored zip: optional local extra, GP flag, and a data descriptor. */
static size_t build_stored_zip(uint8_t *out, size_t cap, const char *name,
                               const uint8_t *data, size_t len,
                               uint16_t flags, uint16_t local_extra,
                               int with_descriptor) {
    size_t name_len = strlen(name);
    if (name_len == 0 || name_len > 255U ||
        (local_extra > 0 && local_extra < 4U))
        return 0;
    uint32_t crc = neverc_crc32_ieee(data, len);
    size_t local_hdr = 30U + name_len + local_extra;
    size_t desc = with_descriptor ? 16U : 0U;
    size_t central = 46U + name_len;
    size_t need = local_hdr + len + desc + central + 22U;
    if (need > cap)
        return 0;

    memset(out, 0, need);
    put32(out, 0x04034b50U);
    put16(out + 4, 20);
    put16(out + 6, flags);
    put16(out + 8, NEVERC_ZIP_STORED);
    if ((flags & 0x0008U) == 0) {
        put32(out + 14, crc);
        put32(out + 18, (uint32_t)len);
        put32(out + 22, (uint32_t)len);
    }
    put16(out + 26, (uint16_t)name_len);
    put16(out + 28, local_extra);
    memcpy(out + 30, name, name_len);
    if (local_extra >= 4U) {
        put16(out + 30 + name_len, 0x0000);
        put16(out + 32 + name_len, (uint16_t)(local_extra - 4U));
    }
    if (len > 0) memcpy(out + local_hdr, data, len);
    size_t pos = local_hdr + len;
    if (with_descriptor) {
        put32(out + pos, 0x08074b50U);
        put32(out + pos + 4, crc);
        put32(out + pos + 8, (uint32_t)len);
        put32(out + pos + 12, (uint32_t)len);
        pos += 16U;
    }
    uint32_t central_offset = (uint32_t)pos;
    put32(out + pos, 0x02014b50U);
    put16(out + pos + 4, 20);
    put16(out + pos + 6, 20);
    put16(out + pos + 8, flags);
    put16(out + pos + 10, NEVERC_ZIP_STORED);
    put32(out + pos + 16, crc);
    put32(out + pos + 20, (uint32_t)len);
    put32(out + pos + 24, (uint32_t)len);
    put16(out + pos + 28, (uint16_t)name_len);
    memcpy(out + pos + 46, name, name_len);
    pos += central;
    put32(out + pos, 0x06054b50U);
    put16(out + pos + 8, 1);
    put16(out + pos + 10, 1);
    put32(out + pos + 12, (uint32_t)central);
    put32(out + pos + 16, central_offset);
    return pos + 22U;
}

/* APPNOTE APPENDIX D: without general-purpose bit 11 a name is CP437, so
 * UTF-8 bytes render as mojibake in every mainstream extractor. */
static void test_utf8_name_flag(void) {
    printf("[utf8_name_flag]\n");
    neverc_zip_writer_t w;
    neverc_zip_writer_init(&w);
    const char *utf8_name = "caf\xc3\xa9.txt";
    check_int("writer accepts utf8 name",
              neverc_zip_writer_add(&w, utf8_name, (const uint8_t *)"x", 1), 0);
    check_int("writer accepts ascii name",
              neverc_zip_writer_add(&w, "plain.txt", (const uint8_t *)"y", 1),
              0);
    check_int("writer closes", neverc_zip_writer_close(&w), 0);

    check_int("local header marks utf8",
              (int)(w.data[6] | (w.data[7] << 8)), 0x0800);
    {
        size_t cd = (size_t)get32(w.data + w.len - 22U + 16U);
        size_t ascii_cd = cd + 46U + strlen(utf8_name);
        check_int("central header marks utf8",
                  (int)(w.data[cd + 8U] | (w.data[cd + 9U] << 8)), 0x0800);
        check_int("ascii name stays cp437",
                  (int)(w.data[ascii_cd + 8U] | (w.data[ascii_cd + 9U] << 8)),
                  0);
    }

    neverc_zip_reader_t r;
    check_int("utf8 archive round-trips",
              neverc_zip_reader_init(&r, w.data, w.len), 0);
    {
        const neverc_zip_file_header_t *f = neverc_zip_reader_file(&r, 0);
        check_int("utf8 entry exists", f != NULL, 1);
        if (f) check_str("utf8 name", f->name, utf8_name);
    }
    neverc_zip_reader_free(&r);
    neverc_zip_writer_free(&w);
}

static void test_directory_extra_and_descriptor(void) {
    printf("[directory extra and descriptor]\n");
    neverc_zip_writer_t writer;
    neverc_zip_writer_init(&writer);
    int ok = neverc_zip_writer_add(&writer, "dir/", NULL, 0) == 0 &&
             neverc_zip_writer_add(
                 &writer, "dir/file.txt", (const uint8_t *)"ab", 2) == 0 &&
             neverc_zip_writer_close(&writer) == 0;
    check_int("directory writer", ok, 1);
    if (ok) {
        neverc_zip_reader_t reader;
        check_int("directory reader",
                  neverc_zip_reader_init(&reader, writer.data, writer.len), 0);
        check_int("directory count", neverc_zip_reader_count(&reader), 2);
        const neverc_zip_file_header_t *dir = neverc_zip_reader_file(&reader, 0);
        const neverc_zip_file_header_t *file = neverc_zip_reader_file(&reader, 1);
        check_int("directory header", dir != NULL, 1);
        check_int("directory file header", file != NULL, 1);
        if (dir) {
            check_str("directory name", dir->name, "dir/");
            check_size("directory size", (size_t)dir->uncompressed_size, 0);
        }
        if (file)
            check_str("nested file name", file->name, "dir/file.txt");
        neverc_zip_reader_free(&reader);
    }
    neverc_zip_writer_free(&writer);

    {
        uint8_t smuggled[256];
        neverc_zip_reader_t reader;
        size_t crafted_len = build_stored_zip(
            smuggled, sizeof(smuggled), "dir/", (const uint8_t *)"boom", 4,
            0, 0, 0);
        check_int("directory data fixture", crafted_len > 0, 1);
        if (crafted_len > 0)
            check_int("reject directory carrying data",
                      neverc_zip_reader_init(&reader, smuggled, crafted_len),
                      -1);
        neverc_zip_reader_free(&reader);
    }

    uint8_t crafted[256];
    const uint8_t payload[] = "xyz";
    size_t n = build_stored_zip(crafted, sizeof(crafted), "extra.txt",
                                payload, sizeof(payload) - 1U, 0, 4, 0);
    check_int("extra fixture", n > 0, 1);
    if (n > 0) {
        neverc_zip_reader_t reader;
        check_int("accept local extra field",
                  neverc_zip_reader_init(&reader, crafted, n), 0);
        const neverc_zip_file_header_t *file =
            neverc_zip_reader_file(&reader, 0);
        check_int("extra file", file != NULL, 1);
        if (file) {
            check_str("extra name", file->name, "extra.txt");
            check_size("extra size", (size_t)file->uncompressed_size, 3);
        }
        size_t dlen = 0;
        const uint8_t *data = neverc_zip_reader_file_data(&reader, 0, &dlen);
        check_int("extra data",
                  data != NULL && dlen == 3 && memcmp(data, "xyz", 3) == 0, 1);
        neverc_zip_reader_free(&reader);
    }

    {
        uint8_t prefixed[264];
        memset(prefixed, 'S', 8);
        n = build_stored_zip(prefixed + 8, sizeof(prefixed) - 8, "pref.txt",
                             payload, sizeof(payload) - 1U, 0, 0, 0);
        check_int("prefix fixture", n > 0, 1);
        if (n > 0) {
            neverc_zip_reader_t reader;
            check_int("accept zip with leading prefix",
                      neverc_zip_reader_init(&reader, prefixed, n + 8), 0);
            check_int("prefix zip file count",
                      neverc_zip_reader_count(&reader), 1);
            const neverc_zip_file_header_t *file =
                neverc_zip_reader_file(&reader, 0);
            check_int("prefix file", file != NULL, 1);
            if (file)
                check_str("prefix name", file->name, "pref.txt");
            neverc_zip_reader_free(&reader);
        }
    }

    n = build_stored_zip(crafted, sizeof(crafted), "desc.txt",
                         payload, sizeof(payload) - 1U, 0x0008, 0, 1);
    check_int("descriptor fixture", n > 0, 1);
    if (n > 0) {
        neverc_zip_reader_t reader;
        check_int("accept data descriptor flag",
                  neverc_zip_reader_init(&reader, crafted, n), 0);
        const neverc_zip_file_header_t *file =
            neverc_zip_reader_file(&reader, 0);
        check_int("descriptor file", file != NULL, 1);
        if (file)
            check_str("descriptor name", file->name, "desc.txt");
        neverc_zip_reader_free(&reader);
    }

    n = build_stored_zip(crafted, sizeof(crafted), "nodesc.txt",
                         payload, sizeof(payload) - 1U, 0x0008, 0, 0);
    check_int("missing-descriptor fixture", n > 0, 1);
    if (n > 0) {
        neverc_zip_reader_t reader;
        check_int("reject missing data descriptor",
                  neverc_zip_reader_init(&reader, crafted, n), -1);
        neverc_zip_reader_free(&reader);
    }

    n = build_stored_zip(crafted, sizeof(crafted), "desc.txt",
                         payload, sizeof(payload) - 1U, 0x0008, 0, 1);
    check_int("descriptor-crc fixture", n > 0, 1);
    if (n > 0) {
        uint8_t mutated[256];
        memcpy(mutated, crafted, n);
        size_t name_len = strlen("desc.txt");
        size_t desc_crc = 30U + name_len + sizeof(payload) - 1U + 4U;
        mutated[desc_crc] ^= 1U;
        neverc_zip_reader_t reader;
        check_int("reject data descriptor crc mismatch",
                  neverc_zip_reader_init(&reader, mutated, n), -1);
        neverc_zip_reader_free(&reader);

        memcpy(mutated, crafted, n);
        size_t eocd = n - 22U;
        uint32_t central = get32(mutated + eocd + 16U);
        if (central >= 8U) {
            memmove(mutated + central - 8U, mutated + central, n - central);
            n -= 8U;
            put32(mutated + n - 22U + 16U, central - 8U);
            check_int("reject truncated data descriptor",
                      neverc_zip_reader_init(&reader, mutated, n), -1);
            neverc_zip_reader_free(&reader);
        }
    }

    n = build_stored_zip(crafted, sizeof(crafted), "../evil",
                         payload, sizeof(payload) - 1U, 0, 0, 0);
    check_int("traversal fixture", n > 0, 1);
    if (n > 0) {
        neverc_zip_reader_t reader;
        check_int("reader rejects traversal name",
                  neverc_zip_reader_init(&reader, crafted, n), -1);
        neverc_zip_reader_free(&reader);
    }

    n = build_stored_zip(crafted, sizeof(crafted), "foo/../bar",
                         payload, sizeof(payload) - 1U, 0, 0, 0);
    check_int("nested traversal fixture", n > 0, 1);
    if (n > 0) {
        neverc_zip_reader_t reader;
        check_int("reader rejects nested traversal",
                  neverc_zip_reader_init(&reader, crafted, n), -1);
        neverc_zip_reader_free(&reader);
    }

    n = build_stored_zip(crafted, sizeof(crafted), "foo/bar/../../etc/passwd",
                         payload, sizeof(payload) - 1U, 0, 0, 0);
    check_int("deep traversal fixture", n > 0, 1);
    if (n > 0) {
        neverc_zip_reader_t reader;
        check_int("reader rejects deep traversal",
                  neverc_zip_reader_init(&reader, crafted, n), -1);
        neverc_zip_reader_free(&reader);
    }

    n = build_stored_zip(crafted, sizeof(crafted), "foo\\bar",
                         payload, sizeof(payload) - 1U, 0, 0, 0);
    check_int("backslash fixture", n > 0, 1);
    if (n > 0) {
        neverc_zip_reader_t reader;
        check_int("reader rejects backslash name",
                  neverc_zip_reader_init(&reader, crafted, n), -1);
        neverc_zip_reader_free(&reader);
    }

    n = build_stored_zip(crafted, sizeof(crafted), "/etc/passwd",
                         payload, sizeof(payload) - 1U, 0, 0, 0);
    check_int("absolute fixture", n > 0, 1);
    if (n > 0) {
        neverc_zip_reader_t reader;
        check_int("reader rejects absolute name",
                  neverc_zip_reader_init(&reader, crafted, n), -1);
        neverc_zip_reader_free(&reader);
    }

    n = build_stored_zip(crafted, sizeof(crafted), "abs.txt",
                         payload, sizeof(payload) - 1U, 0, 0, 0);
    check_int("zip64 size fixture", n > 0, 1);
    if (n > 0) {
        uint8_t mutated[256];
        memcpy(mutated, crafted, n);
        size_t eocd = n - 22U;
        uint32_t central =
            (uint32_t)mutated[eocd + 16U] |
            ((uint32_t)mutated[eocd + 17U] << 8U) |
            ((uint32_t)mutated[eocd + 18U] << 16U) |
            ((uint32_t)mutated[eocd + 19U] << 24U);
        put32(mutated + central + 20U, UINT32_MAX);
        neverc_zip_reader_t reader;
        check_int("reject zip64 file size",
                  neverc_zip_reader_init(&reader, mutated, n), -1);
        neverc_zip_reader_free(&reader);
    }
}

static size_t build_two_locals(uint8_t *out, size_t cap,
                               uint32_t off0, const char *n0, size_t d0,
                               uint32_t off1, const char *n1, size_t d1) {
    size_t n0l = strlen(n0);
    size_t n1l = strlen(n1);
    if (n0l == 0 || n0l > 255U || n1l == 0 || n1l > 255U)
        return 0;
    uint64_t end0 = (uint64_t)off0 + 30U + n0l + d0;
    uint64_t end1 = (uint64_t)off1 + 30U + n1l + d1;
    uint64_t locals_end = end0 > end1 ? end0 : end1;
    size_t cd_size = 46U + n0l + 46U + n1l;
    if (locals_end > SIZE_MAX - cd_size - 22U || locals_end > UINT32_MAX)
        return 0;
    size_t need = (size_t)locals_end + cd_size + 22U;
    if (need > cap)
        return 0;
    memset(out, 0, need);

    uint8_t *p = out + off0;
    put32(p, 0x04034b50U);
    put16(p + 4, 20);
    put16(p + 8, NEVERC_ZIP_STORED);
    put32(p + 18, (uint32_t)d0);
    put32(p + 22, (uint32_t)d0);
    put16(p + 26, (uint16_t)n0l);
    memcpy(p + 30, n0, n0l);

    p = out + off1;
    put32(p, 0x04034b50U);
    put16(p + 4, 20);
    put16(p + 8, NEVERC_ZIP_STORED);
    put32(p + 18, (uint32_t)d1);
    put32(p + 22, (uint32_t)d1);
    put16(p + 26, (uint16_t)n1l);
    memcpy(p + 30, n1, n1l);

    uint32_t crc0 = neverc_crc32_ieee(out + off0 + 30U + n0l, d0);
    uint32_t crc1 = neverc_crc32_ieee(out + off1 + 30U + n1l, d1);
    put32(out + off0 + 14U, crc0);
    put32(out + off1 + 14U, crc1);

    uint32_t cd_off = (uint32_t)locals_end;
    size_t pos = (size_t)locals_end;
    p = out + pos;
    put32(p, 0x02014b50U);
    put16(p + 4, 20);
    put16(p + 6, 20);
    put16(p + 10, NEVERC_ZIP_STORED);
    put32(p + 16, crc0);
    put32(p + 20, (uint32_t)d0);
    put32(p + 24, (uint32_t)d0);
    put16(p + 28, (uint16_t)n0l);
    put32(p + 42, off0);
    memcpy(p + 46, n0, n0l);
    pos += 46U + n0l;

    p = out + pos;
    put32(p, 0x02014b50U);
    put16(p + 4, 20);
    put16(p + 6, 20);
    put16(p + 10, NEVERC_ZIP_STORED);
    put32(p + 16, crc1);
    put32(p + 20, (uint32_t)d1);
    put32(p + 24, (uint32_t)d1);
    put16(p + 28, (uint16_t)n1l);
    put32(p + 42, off1);
    memcpy(p + 46, n1, n1l);
    pos += 46U + n1l;

    p = out + pos;
    put32(p, 0x06054b50U);
    put16(p + 8, 2);
    put16(p + 10, 2);
    put32(p + 12, (uint32_t)(pos - cd_off));
    put32(p + 16, cd_off);
    return pos + 22U;
}

static void test_unsigned_descriptor_crc_signature_collision(void) {
    printf("[unsigned descriptor CRC/signature collision]\n");
    static const uint8_t collision_payload[] = {
        0xac, 0x0a, 0x7a, 0xd5
    };
    const uint32_t descriptor_signature = 0x08074b50U;
    const size_t first_data = 30U + 1U;
    const size_t descriptor = first_data + sizeof(collision_payload);
    const uint32_t second_local = (uint32_t)(descriptor + 12U);
    uint8_t crafted[256];
    size_t n = build_two_locals(
        crafted, sizeof(crafted), 0, "a", sizeof(collision_payload),
        second_local, "b", 1);
    check_int("CRC/signature collision fixture", n > 0, 1);
    if (n == 0)
        return;

    check_int("collision payload CRC",
              neverc_crc32_ieee(collision_payload,
                                sizeof(collision_payload)) ==
                  descriptor_signature,
              1);
    memcpy(crafted + first_data, collision_payload,
           sizeof(collision_payload));

    /* Streamed first entry with an unsigned 12-byte descriptor.  Its CRC is
     * numerically identical to the optional descriptor signature, and a
     * second local record follows immediately. */
    put16(crafted + 6U, 0x0008U);
    put32(crafted + 14U, 0);
    put32(crafted + 18U, 0);
    put32(crafted + 22U, 0);
    put32(crafted + descriptor, descriptor_signature);
    put32(crafted + descriptor + 4U, (uint32_t)sizeof(collision_payload));
    put32(crafted + descriptor + 8U, (uint32_t)sizeof(collision_payload));

    size_t eocd = n - 22U;
    uint32_t central = get32(crafted + eocd + 16U);
    put16(crafted + central + 8U, 0x0008U);
    put32(crafted + central + 16U, descriptor_signature);

    neverc_zip_reader_t reader;
    check_int("accept unsigned descriptor whose CRC equals signature",
              neverc_zip_reader_init(&reader, crafted, n), 0);
    check_int("collision archive file count",
              neverc_zip_reader_count(&reader), 2);
    size_t data_len = 0;
    const uint8_t *file_data =
        neverc_zip_reader_file_data(&reader, 0, &data_len);
    check_int("collision archive first payload",
              file_data != NULL && data_len == sizeof(collision_payload) &&
                  memcmp(file_data, collision_payload,
                         sizeof(collision_payload)) == 0,
              1);
    neverc_zip_reader_free(&reader);
}

static void test_overlapping_entries(void) {
    printf("[overlapping entries]\n");
    neverc_zip_writer_t writer;
    neverc_zip_writer_init(&writer);
    const uint8_t payload[] = "overlap!";
    int built = neverc_zip_writer_add(
                    &writer, "a", payload, sizeof(payload) - 1U) == 0 &&
                neverc_zip_writer_close(&writer) == 0;
    check_int("shared-local fixture", built, 1);
    if (built && writer.data && writer.len >= 22U) {
        size_t eocd = writer.len - 22U;
        uint32_t cd_off =
            (uint32_t)writer.data[eocd + 16U] |
            ((uint32_t)writer.data[eocd + 17U] << 8U) |
            ((uint32_t)writer.data[eocd + 18U] << 16U) |
            ((uint32_t)writer.data[eocd + 19U] << 24U);
        uint32_t cd_size =
            (uint32_t)writer.data[eocd + 12U] |
            ((uint32_t)writer.data[eocd + 13U] << 8U) |
            ((uint32_t)writer.data[eocd + 14U] << 16U) |
            ((uint32_t)writer.data[eocd + 15U] << 24U);
        size_t new_len = writer.len + cd_size;
        uint8_t *dup = (uint8_t *)malloc(new_len);
        if (!dup) {
            check_int("shared-local allocation", 0, 1);
        } else {
            memcpy(dup, writer.data, cd_off + cd_size);
            memcpy(dup + cd_off + cd_size, writer.data + cd_off, cd_size);
            memcpy(dup + cd_off + 2U * cd_size, writer.data + eocd, 22U);
            put16(dup + new_len - 22U + 8U, 2);
            put16(dup + new_len - 22U + 10U, 2);
            put32(dup + new_len - 22U + 12U, cd_size * 2U);
            neverc_zip_reader_t reader;
            check_int("reject shared local range",
                      neverc_zip_reader_init(&reader, dup, new_len), -1);
            neverc_zip_reader_free(&reader);
            free(dup);
        }
    }
    neverc_zip_writer_free(&writer);

    uint8_t crafted[256];
    size_t n = build_two_locals(crafted, sizeof(crafted),
                                0, "a", 4, 35, "b", 4);
    check_int("adjacent fixture", n > 0, 1);
    if (n > 0) {
        neverc_zip_reader_t reader;
        check_int("accept adjacent locals",
                  neverc_zip_reader_init(&reader, crafted, n), 0);
        check_int("adjacent count", neverc_zip_reader_count(&reader), 2);
        neverc_zip_reader_free(&reader);
    }

    n = build_two_locals(crafted, sizeof(crafted),
                         0, "a", 40, 31, "b", 4);
    check_int("partial-overlap fixture", n > 0, 1);
    if (n > 0) {
        neverc_zip_reader_t reader;
        check_int("reject overlapping locals",
                  neverc_zip_reader_init(&reader, crafted, n), -1);
        neverc_zip_reader_free(&reader);
    }
}

static void test_zip64_sentinels(void) {
    printf("[zip64 sentinels]\n");
    neverc_zip_writer_t writer;
    neverc_zip_writer_init(&writer);
    writer.nentries = UINT16_MAX;
    check_int("writer rejects entry 65536",
              neverc_zip_writer_add(
                  &writer, "x", (const uint8_t *)"x", 1), -1);
    neverc_zip_writer_free(&writer);

    uint8_t eocd[22];
    memset(eocd, 0, sizeof(eocd));
    eocd[0] = 0x50;
    eocd[1] = 0x4b;
    eocd[2] = 0x05;
    eocd[3] = 0x06;
    eocd[8] = 0xff;
    eocd[9] = 0xff;
    eocd[10] = 0xff;
    eocd[11] = 0xff;
    neverc_zip_reader_t reader;
    check_int("reject zip64 eocd count",
              neverc_zip_reader_init(&reader, eocd, sizeof(eocd)), -1);
    neverc_zip_reader_free(&reader);

    memset(eocd, 0, sizeof(eocd));
    eocd[0] = 0x50;
    eocd[1] = 0x4b;
    eocd[2] = 0x05;
    eocd[3] = 0x06;
    eocd[16] = 0xff;
    eocd[17] = 0xff;
    eocd[18] = 0xff;
    eocd[19] = 0xff;
    check_int("reject zip64 eocd offset",
              neverc_zip_reader_init(&reader, eocd, sizeof(eocd)), -1);
    neverc_zip_reader_free(&reader);

    /* CVE-2024-24789: trailing truncated EOCD must not fall back to an
     * inner directory whose comment happens to fill to EOF. */
    {
        uint8_t polyglot[44];
        memset(polyglot, 0, sizeof(polyglot));
        polyglot[0] = 0x50;
        polyglot[1] = 0x4b;
        polyglot[2] = 0x05;
        polyglot[3] = 0x06;
        polyglot[20] = 22;
        polyglot[22] = 0x50;
        polyglot[23] = 0x4b;
        polyglot[24] = 0x05;
        polyglot[25] = 0x06;
        polyglot[42] = 1;
        check_int("reject truncated trailing eocd comment",
                  neverc_zip_reader_init(&reader, polyglot, sizeof(polyglot)),
                  -1);
        neverc_zip_reader_free(&reader);
    }

    /* CVE-2024-24789 other side: a rightmost EOCD whose comment fits but
     * does not reach EOF must still win. An earlier EOCD whose comment
     * fills to EOF must not hide the outer directory. */
    {
        neverc_zip_writer_t writer;
        neverc_zip_writer_init(&writer);
        int built = neverc_zip_writer_add(
                        &writer, "outer.txt", (const uint8_t *)"OUT", 3) == 0 &&
                    neverc_zip_writer_close(&writer) == 0;
        check_int("short-comment fixture", built, 1);
        if (built && writer.len > 22U && writer.len < UINT16_MAX) {
            size_t zip_len = writer.len;
            size_t total = 22U + zip_len + 1U;
            uint8_t *poly = (uint8_t *)malloc(total);
            uint16_t inner_comment;
            if (!poly) {
                check_int("short-comment allocation", 0, 1);
            } else {
                memset(poly, 0, 22U);
                poly[0] = 0x50;
                poly[1] = 0x4b;
                poly[2] = 0x05;
                poly[3] = 0x06;
                inner_comment = (uint16_t)(zip_len + 1U);
                poly[20] = (uint8_t)inner_comment;
                poly[21] = (uint8_t)(inner_comment >> 8);
                memcpy(poly + 22U, writer.data, zip_len);
                poly[22U + zip_len] = 0x58;
                check_int("short comment keeps rightmost eocd",
                          neverc_zip_reader_init(&reader, poly, total), 0);
                check_int("outer file count",
                          neverc_zip_reader_count(&reader), 1);
                {
                    const neverc_zip_file_header_t *f =
                        neverc_zip_reader_file(&reader, 0);
                    check_int("outer file exists", f != NULL, 1);
                    if (f)
                        check_str("outer name", f->name, "outer.txt");
                }
                neverc_zip_reader_free(&reader);
                free(poly);
            }
        }
        neverc_zip_writer_free(&writer);
    }
}

/* "hello, zip! " x20 + "\n" compressed by a mainstream command-line zip at
 * its maximum level: general-purpose bit 1 set, extended-timestamp and
 * Unix-owner extra fields in both headers. */
static const uint8_t cli_deflate_zip[] = {
    0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x02, 0x00, 0x08, 0x00, 0xab, 0x08,
    0x42, 0x5d, 0x22, 0x2c, 0x8e, 0x67, 0x12, 0x00, 0x00, 0x00, 0xf1, 0x00,
    0x00, 0x00, 0x09, 0x00, 0x1c, 0x00, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x2e,
    0x74, 0x78, 0x74, 0x55, 0x54, 0x09, 0x00, 0x03, 0xc1, 0x65, 0xbf, 0x6a,
    0xc1, 0x65, 0xbf, 0x6a, 0x75, 0x78, 0x0b, 0x00, 0x01, 0x04, 0xe8, 0x03,
    0x00, 0x00, 0x04, 0xe8, 0x03, 0x00, 0x00, 0xcb, 0x48, 0xcd, 0xc9, 0xc9,
    0xd7, 0x51, 0xa8, 0xca, 0x2c, 0x50, 0x54, 0xc8, 0x18, 0x01, 0x6c, 0x2e,
    0x00, 0x50, 0x4b, 0x01, 0x02, 0x1e, 0x03, 0x14, 0x00, 0x02, 0x00, 0x08,
    0x00, 0xab, 0x08, 0x42, 0x5d, 0x22, 0x2c, 0x8e, 0x67, 0x12, 0x00, 0x00,
    0x00, 0xf1, 0x00, 0x00, 0x00, 0x09, 0x00, 0x18, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0xb4, 0x81, 0x00, 0x00, 0x00, 0x00, 0x68,
    0x65, 0x6c, 0x6c, 0x6f, 0x2e, 0x74, 0x78, 0x74, 0x55, 0x54, 0x05, 0x00,
    0x03, 0xc1, 0x65, 0xbf, 0x6a, 0x75, 0x78, 0x0b, 0x00, 0x01, 0x04, 0xe8,
    0x03, 0x00, 0x00, 0x04, 0xe8, 0x03, 0x00, 0x00, 0x50, 0x4b, 0x05, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x4f, 0x00, 0x00, 0x00,
    0x55, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* 240 bytes of "hello, zip! " written by Go's archive/zip Writer.Create:
 * Deflate, zero CRC/sizes in the local header, signed data descriptor. */
static const uint8_t go_deflate_zip[] = {
    0x50, 0x4b, 0x03, 0x04, 0x14, 0x00, 0x08, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x2e,
    0x74, 0x78, 0x74, 0xca, 0x48, 0xcd, 0xc9, 0xc9, 0xd7, 0x51, 0xa8, 0xca,
    0x2c, 0x50, 0x54, 0x18, 0x09, 0x6c, 0xc0, 0x00, 0x50, 0x4b, 0x07, 0x08,
    0xb7, 0x9e, 0x0c, 0x5c, 0x11, 0x00, 0x00, 0x00, 0xf0, 0x00, 0x00, 0x00,
    0x50, 0x4b, 0x01, 0x02, 0x14, 0x00, 0x14, 0x00, 0x08, 0x00, 0x08, 0x00,
    0x00, 0x00, 0x00, 0x00, 0xb7, 0x9e, 0x0c, 0x5c, 0x11, 0x00, 0x00, 0x00,
    0xf0, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x68, 0x65,
    0x6c, 0x6c, 0x6f, 0x2e, 0x74, 0x78, 0x74, 0x50, 0x4b, 0x05, 0x06, 0x00,
    0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x37, 0x00, 0x00, 0x00, 0x48,
    0x00, 0x00, 0x00, 0x00, 0x00,
};

static int hello_matches(const uint8_t *data, size_t len, size_t expected) {
    static const char unit[] = "hello, zip! ";
    if (len != expected) return 0;
    for (size_t i = 0; i < len; i++) {
        uint8_t want = (uint8_t)unit[i % 12U];
        if (expected == 241U && i == 240U) want = '\n';
        if (data[i] != want) return 0;
    }
    return 1;
}

/* One-entry archive with an arbitrary method, payload and declared
 * uncompressed size/CRC, so malformed Deflate bodies can be exercised. */
static size_t build_method_zip(uint8_t *out, size_t cap, const char *name,
                               uint16_t method, uint16_t flags,
                               const uint8_t *payload, size_t payload_len,
                               uint32_t uncompressed, uint32_t crc) {
    size_t name_len = strlen(name);
    size_t central = 46U + name_len;
    size_t need = 30U + name_len + payload_len + central + 22U;
    if (name_len == 0 || name_len > 255U || need > cap) return 0;
    memset(out, 0, need);
    put32(out, 0x04034b50U);
    put16(out + 4, 20);
    put16(out + 6, flags);
    put16(out + 8, method);
    put32(out + 14, crc);
    put32(out + 18, (uint32_t)payload_len);
    put32(out + 22, uncompressed);
    put16(out + 26, (uint16_t)name_len);
    memcpy(out + 30, name, name_len);
    if (payload_len > 0) memcpy(out + 30 + name_len, payload, payload_len);
    size_t pos = 30U + name_len + payload_len;
    put32(out + pos, 0x02014b50U);
    put16(out + pos + 4, 20);
    put16(out + pos + 6, 20);
    put16(out + pos + 8, flags);
    put16(out + pos + 10, method);
    put32(out + pos + 16, crc);
    put32(out + pos + 20, (uint32_t)payload_len);
    put32(out + pos + 24, uncompressed);
    put16(out + pos + 28, (uint16_t)name_len);
    memcpy(out + pos + 46, name, name_len);
    put32(out + pos + central, 0x06054b50U);
    put16(out + pos + central + 8, 1);
    put16(out + pos + central + 10, 1);
    put32(out + pos + central + 12, (uint32_t)central);
    put32(out + pos + central + 16, (uint32_t)pos);
    return need;
}

static int read_entry(const uint8_t *archive, size_t len, uint8_t *dst,
                      size_t cap, size_t *got) {
    neverc_zip_reader_t reader;
    *got = cap;
    if (neverc_zip_reader_init(&reader, archive, len) != 0) {
        *got = 0;
        return -2;
    }
    int result = neverc_zip_reader_file_read(&reader, 0, dst, got);
    neverc_zip_reader_free(&reader);
    return result;
}

static void test_deflate_reader(void) {
    printf("[deflate reader]\n");
    uint8_t out[512];
    neverc_zip_reader_t reader;
    check_int("cli deflate archive",
              neverc_zip_reader_init(&reader, cli_deflate_zip,
                                     sizeof(cli_deflate_zip)), 0);
    {
        const neverc_zip_file_header_t *f = neverc_zip_reader_file(&reader, 0);
        check_int("cli deflate entry", f != NULL, 1);
        if (f) {
            check_str("cli deflate name", f->name, "hello.txt");
            check_int("cli deflate method", f->method, NEVERC_ZIP_DEFLATED);
            check_size("cli deflate size", (size_t)f->uncompressed_size, 241);
            check_size("cli deflate packed", (size_t)f->compressed_size, 18);
        }
        size_t view_len = 1;
        check_int("no raw view of deflate data",
                  neverc_zip_reader_file_data(&reader, 0, &view_len) == NULL,
                  1);
        check_size("raw view length cleared", view_len, 0);
        size_t got = sizeof(out);
        check_int("cli deflate read",
                  neverc_zip_reader_file_read(&reader, 0, out, &got), 0);
        check_int("cli deflate content", hello_matches(out, got, 241), 1);
        got = 240;
        check_int("reject short destination",
                  neverc_zip_reader_file_read(&reader, 0, out, &got), -1);
        check_size("short destination length", got, 0);
        got = 241;
        check_int("reject null destination",
                  neverc_zip_reader_file_read(&reader, 0, NULL, &got), -1);
        got = sizeof(out);
        check_int("reject out-of-range read",
                  neverc_zip_reader_file_read(&reader, 1, out, &got), -1);
    }
    neverc_zip_reader_free(&reader);

    size_t got = 0;
    check_int("go deflate descriptor archive",
              read_entry(go_deflate_zip, sizeof(go_deflate_zip), out,
                         sizeof(out), &got), 0);
    check_int("go deflate content", hello_matches(out, got, 240), 1);

    /* Stored entries read through the same call. */
    {
        uint8_t stored[128];
        size_t n = build_stored_zip(stored, sizeof(stored), "s.txt",
                                    (const uint8_t *)"stored", 6, 0, 0, 0);
        check_int("stored read fixture", n > 0, 1);
        check_int("stored entry read",
                  read_entry(stored, n, out, sizeof(out), &got), 0);
        check_int("stored entry content",
                  got == 6 && memcmp(out, "stored", 6) == 0, 1);
    }

    uint8_t plain[300];
    for (size_t i = 0; i < sizeof(plain); i++)
        plain[i] = (uint8_t)"deflate body "[i % 13U];
    uint32_t plain_crc = neverc_crc32_ieee(plain, sizeof(plain));
    uint8_t packed[400];
    size_t packed_len = sizeof(packed) - 4U;
    int compressed = neverc_flate_compress(plain, sizeof(plain), packed,
                                           &packed_len,
                                           NEVERC_FLATE_DEFAULT);
    check_int("compress fixture", compressed == 0 && packed_len > 4U, 1);
    if (compressed != 0 || packed_len <= 4U) return;

    uint8_t archive[1024];
    size_t n = build_method_zip(archive, sizeof(archive), "body.txt", 8, 0,
                                packed, packed_len, sizeof(plain), plain_crc);
    check_int("crafted deflate read",
              read_entry(archive, n, out, sizeof(out), &got), 0);
    check_int("crafted deflate content",
              got == sizeof(plain) && memcmp(out, plain, sizeof(plain)) == 0,
              1);

    /* Go's reader ignores bytes after the final block within the
     * compressed size. */
    memcpy(packed + packed_len, "junk", 4);
    n = build_method_zip(archive, sizeof(archive), "body.txt", 8, 0,
                         packed, packed_len + 4U, sizeof(plain), plain_crc);
    check_int("accept bytes after final block",
              read_entry(archive, n, out, sizeof(out), &got), 0);

    n = build_method_zip(archive, sizeof(archive), "body.txt", 8, 0,
                         packed, packed_len, sizeof(plain), plain_crc ^ 1U);
    memset(out, 0xa5, sizeof(out));
    check_int("reject deflate crc mismatch",
              read_entry(archive, n, out, sizeof(out), &got), -1);
    {
        int cleared = 1;
        for (size_t i = 0; i < sizeof(plain); i++)
            if (out[i] != 0) cleared = 0;
        check_int("failed read clears output", cleared, 1);
        check_size("failed read length", got, 0);
    }

    n = build_method_zip(archive, sizeof(archive), "body.txt", 8, 0,
                         packed, packed_len, sizeof(plain) + 1U, plain_crc);
    check_int("reject stream shorter than declared",
              read_entry(archive, n, out, sizeof(out), &got), -1);
    {
        /* The CRC must not be computed over destination bytes the stream
         * never wrote: here a zeroed tail would make it match. */
        uint8_t padded[sizeof(plain) + 1U];
        memcpy(padded, plain, sizeof(plain));
        padded[sizeof(plain)] = 0;
        n = build_method_zip(archive, sizeof(archive), "body.txt", 8, 0,
                             packed, packed_len, sizeof(padded),
                             neverc_crc32_ieee(padded, sizeof(padded)));
        memset(out, 0, sizeof(out));
        check_int("reject short stream whose padded crc matches",
                  read_entry(archive, n, out, sizeof(out), &got), -1);
    }
    n = build_method_zip(archive, sizeof(archive), "body.txt", 8, 0,
                         packed, packed_len, sizeof(plain) - 1U, plain_crc);
    check_int("reject stream longer than declared",
              read_entry(archive, n, out, sizeof(out), &got), -1);
    n = build_method_zip(archive, sizeof(archive), "body.txt", 8, 0,
                         packed, packed_len - 1U, sizeof(plain), plain_crc);
    check_int("reject truncated stream",
              read_entry(archive, n, out, sizeof(out), &got), -1);
    {
        uint8_t corrupt[400];
        memcpy(corrupt, packed, packed_len);
        corrupt[0] |= 0x06U; /* reserved block type 3 */
        n = build_method_zip(archive, sizeof(archive), "body.txt", 8, 0,
                             corrupt, packed_len, sizeof(plain), plain_crc);
        check_int("reject invalid block type",
                  read_entry(archive, n, out, sizeof(out), &got), -1);
    }

    /* "03 00" is the empty final fixed-Huffman block. No DEFLATE stream
     * expands past 1032 bytes per input byte, so 2 bytes cannot claim more
     * than 2064 output bytes. */
    static const uint8_t empty_stream[] = { 0x03, 0x00 };
    n = build_method_zip(archive, sizeof(archive), "bomb.bin", 8, 0,
                         empty_stream, 2, 2064, 0);
    check_int("accept maximum deflate ratio",
              neverc_zip_reader_init(&reader, archive, n), 0);
    neverc_zip_reader_free(&reader);
    n = build_method_zip(archive, sizeof(archive), "bomb.bin", 8, 0,
                         empty_stream, 2, 2065, 0);
    check_int("reject impossible deflate ratio",
              neverc_zip_reader_init(&reader, archive, n), -1);
    neverc_zip_reader_free(&reader);

    n = build_method_zip(archive, sizeof(archive), "empty.txt", 8, 0,
                         empty_stream, 2, 0, 0);
    check_int("empty deflate file",
              read_entry(archive, n, NULL, 0, &got), 0);
    check_size("empty deflate length", got, 0);
    n = build_method_zip(archive, sizeof(archive), "nostream.txt", 8, 0,
                         NULL, 0, 0, 0);
    check_int("reject empty deflate body for a file",
              read_entry(archive, n, NULL, 0, &got), -1);

    /* Some jar writers label empty directories as Deflate. */
    n = build_method_zip(archive, sizeof(archive), "dir/", 8, 0,
                         empty_stream, 2, 0, 0);
    check_int("deflate directory",
              read_entry(archive, n, NULL, 0, &got), 0);
    n = build_method_zip(archive, sizeof(archive), "dir/", 8, 0,
                         packed, packed_len, sizeof(plain), plain_crc);
    check_int("reject deflate directory with data",
              neverc_zip_reader_init(&reader, archive, n), -1);
    neverc_zip_reader_free(&reader);

    /* Level hints in bits 1-2 are accepted; encryption is not. */
    n = build_method_zip(archive, sizeof(archive), "body.txt", 8, 0x0006,
                         packed, packed_len, sizeof(plain), plain_crc);
    check_int("accept deflate level flags",
              read_entry(archive, n, out, sizeof(out), &got), 0);
    n = build_method_zip(archive, sizeof(archive), "body.txt", 8, 0x0001,
                         packed, packed_len, sizeof(plain), plain_crc);
    check_int("reject encrypted entry",
              neverc_zip_reader_init(&reader, archive, n), -1);
    neverc_zip_reader_free(&reader);
    n = build_method_zip(archive, sizeof(archive), "body.txt", 12, 0,
                         packed, packed_len, sizeof(plain), plain_crc);
    check_int("reject unsupported method",
              neverc_zip_reader_init(&reader, archive, n), -1);
    neverc_zip_reader_free(&reader);
}

static void put64(uint8_t *p, uint64_t v) {
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

/* hello.txt via a scripting-language zip library forcing ZIP64: saturated
 * local sizes carried by a local ZIP64 field, classic central values. */
static const uint8_t forced_zip64_zip[] = {
    0x50, 0x4b, 0x03, 0x04, 0x2d, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00,
    0x21, 0x00, 0x22, 0x2c, 0x8e, 0x67, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x09, 0x00, 0x14, 0x00, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x2e,
    0x74, 0x78, 0x74, 0x01, 0x00, 0x10, 0x00, 0xf1, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xcb,
    0x48, 0xcd, 0xc9, 0xc9, 0xd7, 0x51, 0xa8, 0xca, 0x2c, 0x50, 0x54, 0xc8,
    0x18, 0x01, 0x6c, 0x2e, 0x00, 0x50, 0x4b, 0x01, 0x02, 0x2d, 0x03, 0x2d,
    0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x21, 0x00, 0x22, 0x2c, 0x8e,
    0x67, 0x12, 0x00, 0x00, 0x00, 0xf1, 0x00, 0x00, 0x00, 0x09, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x01, 0x00,
    0x00, 0x00, 0x00, 0x68, 0x65, 0x6c, 0x6c, 0x6f, 0x2e, 0x74, 0x78, 0x74,
    0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x37, 0x00, 0x00, 0x00, 0x4d, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* The same text piped through the command-line zip into a seekable file
 * (entry "-"): local ZIP64 field, plus ZIP64 end records although no
 * classic EOCD field is saturated, so the directory ends at the ZIP64
 * record rather than at the EOCD. */
static const uint8_t cli_zip64_end_zip[] = {
    0x50, 0x4b, 0x03, 0x04, 0x2d, 0x00, 0x00, 0x00, 0x08, 0x00, 0x79, 0x0a,
    0x42, 0x5d, 0x22, 0x2c, 0x8e, 0x67, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x01, 0x00, 0x14, 0x00, 0x2d, 0x01, 0x00, 0x10, 0x00, 0xf1,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x12, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0xd7, 0x51, 0xa8, 0xca,
    0x2c, 0x50, 0x54, 0xc8, 0x18, 0x01, 0x6c, 0x2e, 0x00, 0x50, 0x4b, 0x01,
    0x02, 0x1e, 0x03, 0x2d, 0x00, 0x00, 0x00, 0x08, 0x00, 0x79, 0x0a, 0x42,
    0x5d, 0x22, 0x2c, 0x8e, 0x67, 0x12, 0x00, 0x00, 0x00, 0xf1, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0xb4, 0x81, 0x00, 0x00, 0x00, 0x00, 0x2d, 0x50, 0x4b, 0x06, 0x06,
    0x2c, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1e, 0x03, 0x2d, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x2f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x45, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x50, 0x4b, 0x06, 0x07, 0x00, 0x00, 0x00, 0x00,
    0x74, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00,
    0x50, 0x4b, 0x05, 0x06, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00,
    0x2f, 0x00, 0x00, 0x00, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00,
};

/* The same text piped through the command-line zip to a non-seekable
 * output: data descriptor flag, local ZIP64 field with a zero compressed
 * size, and a signed 24-byte ZIP64 data descriptor. */
static const uint8_t cli_zip64_stream_zip[] = {
    0x50, 0x4b, 0x03, 0x04, 0x2d, 0x00, 0x08, 0x00, 0x08, 0x00, 0x79, 0x0a,
    0x42, 0x5d, 0x00, 0x00, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0x01, 0x00, 0x14, 0x00, 0x2d, 0x01, 0x00, 0x10, 0x00, 0xf1,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0xcb, 0x48, 0xcd, 0xc9, 0xc9, 0xd7, 0x51, 0xa8, 0xca,
    0x2c, 0x50, 0x54, 0xc8, 0x18, 0x01, 0x6c, 0x2e, 0x00, 0x50, 0x4b, 0x07,
    0x08, 0x22, 0x2c, 0x8e, 0x67, 0x12, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0xf1, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x4b, 0x01,
    0x02, 0x1e, 0x03, 0x2d, 0x00, 0x08, 0x00, 0x08, 0x00, 0x79, 0x0a, 0x42,
    0x5d, 0x22, 0x2c, 0x8e, 0x67, 0x12, 0x00, 0x00, 0x00, 0xf1, 0x00, 0x00,
    0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00,
    0x00, 0xb4, 0x81, 0x00, 0x00, 0x00, 0x00, 0x2d, 0x50, 0x4b, 0x05, 0x06,
    0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x01, 0x00, 0x2f, 0x00, 0x00, 0x00,
    0x5d, 0x00, 0x00, 0x00, 0x00, 0x00,
};

typedef struct {
    int local_zip64;    /* saturated local sizes in a local ZIP64 field */
    int central_zip64;  /* saturated central sizes/offset in a ZIP64 field */
    int descriptor;     /* 0, or a 24/20-byte ZIP64 data descriptor */
    int end64;          /* ZIP64 end record and locator */
    int saturate_end;   /* saturated classic EOCD fields */
    int relative_end64; /* locator offset relative to the archive start */
    size_t prefix;      /* bytes before the archive */
} zip64_layout_t;

typedef struct {
    size_t central;
    size_t end64;
    size_t locator;
    size_t eocd;
    size_t total;
} zip64_offsets_t;

/* Single-entry ZIP64 archive builder. Archive offsets are relative to the
 * end of the prefix, which keeps a prefixed archive self-consistent. */
static size_t build_zip64(uint8_t *out, size_t cap,
                          const zip64_layout_t *layout, uint16_t method,
                          const uint8_t *payload, size_t payload_len,
                          uint64_t uncompressed, uint32_t crc,
                          zip64_offsets_t *at) {
    static const char name[] = "z.bin";
    const size_t name_len = sizeof(name) - 1U;
    const size_t p = layout->prefix;
    size_t need = p + 30U + name_len + 20U + payload_len + 24U +
                  46U + name_len + 28U + 56U + 20U + 22U;
    if (need > cap) return 0;
    memset(out, 0, need);
    memset(out, 'S', p);
    int descriptor = layout->descriptor != 0;
    uint16_t flags = descriptor ? 0x0008U : 0;

    uint8_t *l = out + p;
    put32(l, 0x04034b50U);
    put16(l + 4, 45);
    put16(l + 6, flags);
    put16(l + 8, method);
    put32(l + 14, descriptor ? 0 : crc);
    if (layout->local_zip64) {
        put32(l + 18, UINT32_MAX);
        put32(l + 22, UINT32_MAX);
    } else if (!descriptor) {
        put32(l + 18, (uint32_t)payload_len);
        put32(l + 22, (uint32_t)uncompressed);
    }
    put16(l + 26, (uint16_t)name_len);
    put16(l + 28, layout->local_zip64 ? 20 : 0);
    memcpy(l + 30, name, name_len);
    size_t pos = p + 30U + name_len;
    if (layout->local_zip64) {
        put16(out + pos, 0x0001);
        put16(out + pos + 2, 16);
        put64(out + pos + 4, descriptor ? 0 : uncompressed);
        put64(out + pos + 12, descriptor ? 0 : payload_len);
        pos += 20U;
    }
    if (payload_len > 0) memcpy(out + pos, payload, payload_len);
    pos += payload_len;
    if (layout->descriptor == 24) {
        put32(out + pos, 0x08074b50U);
        put32(out + pos + 4, crc);
        put64(out + pos + 8, payload_len);
        put64(out + pos + 16, uncompressed);
        pos += 24U;
    } else if (layout->descriptor == 20) {
        put32(out + pos, crc);
        put64(out + pos + 4, payload_len);
        put64(out + pos + 12, uncompressed);
        pos += 20U;
    }

    size_t central = pos;
    uint8_t *c = out + central;
    put32(c, 0x02014b50U);
    put16(c + 4, 45);
    put16(c + 6, 45);
    put16(c + 8, flags);
    put16(c + 10, method);
    put32(c + 16, crc);
    put32(c + 20, layout->central_zip64 ? UINT32_MAX : (uint32_t)payload_len);
    put32(c + 24, layout->central_zip64 ? UINT32_MAX : (uint32_t)uncompressed);
    put16(c + 28, (uint16_t)name_len);
    put16(c + 30, layout->central_zip64 ? 28 : 0);
    put32(c + 42, layout->central_zip64 ? UINT32_MAX : 0);
    memcpy(c + 46, name, name_len);
    pos = central + 46U + name_len;
    if (layout->central_zip64) {
        put16(out + pos, 0x0001);
        put16(out + pos + 2, 24);
        put64(out + pos + 4, uncompressed);
        put64(out + pos + 12, payload_len);
        put64(out + pos + 20, 0);
        pos += 28U;
    }
    size_t central_size = pos - central;
    size_t central_offset = central - p;

    size_t end64 = pos;
    if (layout->end64) {
        put32(out + pos, 0x06064b50U);
        put64(out + pos + 4, 44);
        put16(out + pos + 12, 45);
        put16(out + pos + 14, 45);
        put64(out + pos + 24, 1);
        put64(out + pos + 32, 1);
        put64(out + pos + 40, central_size);
        put64(out + pos + 48, central_offset);
        pos += 56U;
        put32(out + pos, 0x07064b50U);
        put64(out + pos + 8, layout->relative_end64 ? end64 - p : end64);
        put32(out + pos + 16, 1);
        pos += 20U;
    }
    size_t eocd = pos;
    put32(out + pos, 0x06054b50U);
    put16(out + pos + 8, layout->saturate_end ? UINT16_MAX : 1);
    put16(out + pos + 10, layout->saturate_end ? UINT16_MAX : 1);
    put32(out + pos + 12,
          layout->saturate_end ? UINT32_MAX : (uint32_t)central_size);
    put32(out + pos + 16,
          layout->saturate_end ? UINT32_MAX : (uint32_t)central_offset);
    pos += 22U;
    if (at) {
        at->central = central;
        at->end64 = end64;
        at->locator = end64 + 56U;
        at->eocd = eocd;
        at->total = pos;
    }
    return pos;
}

static int zip64_reads(const uint8_t *archive, size_t len,
                       const char *expected, size_t expected_len) {
    neverc_zip_reader_t reader;
    if (neverc_zip_reader_init(&reader, archive, len) != 0) return -2;
    uint8_t out[64];
    size_t got = sizeof(out);
    const neverc_zip_file_header_t *f = neverc_zip_reader_file(&reader, 0);
    int ok = neverc_zip_reader_count(&reader) == 1 && f != NULL &&
             f->uncompressed_size == expected_len &&
             neverc_zip_reader_file_read(&reader, 0, out, &got) == 0 &&
             got == expected_len && memcmp(out, expected, got) == 0;
    neverc_zip_reader_free(&reader);
    return ok ? 0 : -1;
}

static void test_zip64_reader(void) {
    printf("[zip64 reader]\n");
    uint8_t out[512];
    size_t got = 0;
    check_int("forced zip64 local field",
              read_entry(forced_zip64_zip, sizeof(forced_zip64_zip), out,
                         sizeof(out), &got), 0);
    check_int("forced zip64 content", hello_matches(out, got, 241), 1);
    check_int("zip64 end records with classic eocd",
              read_entry(cli_zip64_end_zip, sizeof(cli_zip64_end_zip), out,
                         sizeof(out), &got), 0);
    check_int("zip64 end records content", hello_matches(out, got, 241), 1);
    check_int("zip64 data descriptor",
              read_entry(cli_zip64_stream_zip, sizeof(cli_zip64_stream_zip),
                         out, sizeof(out), &got), 0);
    check_int("zip64 data descriptor content",
              hello_matches(out, got, 241), 1);

    static const char text[] = "zip64 data";
    const size_t text_len = sizeof(text) - 1U;
    const uint32_t crc = neverc_crc32_ieee(text, text_len);
    const uint8_t *body = (const uint8_t *)text;
    uint8_t archive[512];
    zip64_offsets_t at;
    zip64_layout_t full = { 1, 1, 0, 1, 1, 0, 0 };
    size_t n = build_zip64(archive, sizeof(archive), &full, 0, body,
                           text_len, text_len, crc, &at);
    check_int("zip64 fixture", n > 0, 1);
    if (n == 0) return;
    check_int("saturated zip64 archive",
              zip64_reads(archive, n, text, text_len), 0);

    zip64_layout_t layout = full;
    layout.local_zip64 = 0;
    n = build_zip64(archive, sizeof(archive), &layout, 0, body, text_len,
                    text_len, crc, &at);
    check_int("central zip64 field with classic local sizes",
              zip64_reads(archive, n, text, text_len), 0);
    layout = full;
    layout.descriptor = 24;
    n = build_zip64(archive, sizeof(archive), &layout, 0, body, text_len,
                    text_len, crc, &at);
    check_int("signed zip64 descriptor",
              zip64_reads(archive, n, text, text_len), 0);
    layout.descriptor = 20;
    n = build_zip64(archive, sizeof(archive), &layout, 0, body, text_len,
                    text_len, crc, &at);
    check_int("unsigned zip64 descriptor",
              zip64_reads(archive, n, text, text_len), 0);
    put32(archive + at.central - 20U, crc ^ 1U);
    check_int("reject zip64 descriptor crc mismatch",
              zip64_reads(archive, n, text, text_len), -2);

    layout = full;
    layout.prefix = 16;
    n = build_zip64(archive, sizeof(archive), &layout, 0, body, text_len,
                    text_len, crc, &at);
    check_int("prefixed zip64 with absolute locator",
              zip64_reads(archive, n, text, text_len), 0);
    layout.relative_end64 = 1;
    n = build_zip64(archive, sizeof(archive), &layout, 0, body, text_len,
                    text_len, crc, &at);
    check_int("prefixed zip64 with relative locator",
              zip64_reads(archive, n, text, text_len), 0);
    put64(archive + at.locator + 8U, at.end64 - 17U);
    check_int("reject relative locator disagreeing with directory",
              zip64_reads(archive, n, text, text_len), -2);

    layout = full;
    layout.saturate_end = 0;
    n = build_zip64(archive, sizeof(archive), &layout, 0, body, text_len,
                    text_len, crc, &at);
    check_int("zip64 records with unsaturated eocd",
              zip64_reads(archive, n, text, text_len), 0);
    put16(archive + at.eocd + 10U, 2);
    check_int("reject eocd count disagreeing with zip64",
              zip64_reads(archive, n, text, text_len), -2);
    n = build_zip64(archive, sizeof(archive), &layout, 0, body, text_len,
                    text_len, crc, &at);
    put32(archive + at.eocd + 16U, 0);
    check_int("reject eocd offset disagreeing with zip64",
              zip64_reads(archive, n, text, text_len), -2);

    n = build_zip64(archive, sizeof(archive), &full, 0, body, text_len,
                    text_len, crc, &at);
    put64(archive + at.end64 + 4U, 45);
    check_int("reject zip64 record not ending at locator",
              zip64_reads(archive, n, text, text_len), -2);
    n = build_zip64(archive, sizeof(archive), &full, 0, body, text_len,
                    text_len, crc, &at);
    put64(archive + at.end64 + 24U, 2);
    check_int("reject zip64 disk entry mismatch",
              zip64_reads(archive, n, text, text_len), -2);
    put64(archive + at.end64 + 32U, 2);
    check_int("reject zip64 entry count beyond directory",
              zip64_reads(archive, n, text, text_len), -2);
    n = build_zip64(archive, sizeof(archive), &full, 0, body, text_len,
                    text_len, crc, &at);
    put32(archive + at.end64 + 16U, 1);
    check_int("reject zip64 multi-disk record",
              zip64_reads(archive, n, text, text_len), -2);
    n = build_zip64(archive, sizeof(archive), &full, 0, body, text_len,
                    text_len, crc, &at);
    put64(archive + at.end64 + 48U, (uint64_t)1 << 40);
    check_int("reject zip64 directory offset out of range",
              zip64_reads(archive, n, text, text_len), -2);
    n = build_zip64(archive, sizeof(archive), &full, 0, body, text_len,
                    text_len, crc, &at);
    put32(archive + at.locator + 16U, 2);
    check_int("reject saturated eocd without single-disk locator",
              zip64_reads(archive, n, text, text_len), -2);

    /* Central ZIP64 field: fields appear only for saturated values. */
    n = build_zip64(archive, sizeof(archive), &full, 0, body, text_len,
                    text_len, crc, &at);
    put16(archive + at.central + 46U + 5U + 2U, 16);
    check_int("reject zip64 field missing a saturated value",
              zip64_reads(archive, n, text, text_len), -2);
    n = build_zip64(archive, sizeof(archive), &full, 0, body, text_len,
                    text_len, crc, &at);
    put16(archive + at.central + 46U + 5U, 0x5555);
    check_int("saturated sizes without zip64 field are literal",
              zip64_reads(archive, n, text, text_len), -2);
    n = build_zip64(archive, sizeof(archive), &full, 0, body, text_len,
                    text_len, crc, &at);
    put64(archive + at.central + 46U + 5U + 4U, text_len + 1U);
    check_int("reject central zip64 size disagreeing with local",
              zip64_reads(archive, n, text, text_len), -2);
    n = build_zip64(archive, sizeof(archive), &full, 0, body, text_len,
                    text_len, crc, &at);
    put64(archive + 30U + 5U + 12U, text_len - 1U);
    check_int("reject local zip64 size disagreeing with central",
              zip64_reads(archive, n, text, text_len), -2);
    n = build_zip64(archive, sizeof(archive), &full, 0, body, text_len,
                    text_len, crc, &at);
    put64(archive + at.central + 46U + 5U + 20U, (uint64_t)1 << 63);
    check_int("reject zip64 local offset past directory",
              zip64_reads(archive, n, text, text_len), -2);

    /* A huge ZIP64 compressed size must be bounded by the archive before
     * any arithmetic, including the DEFLATE expansion bound. */
    static const uint8_t empty_stream[] = { 0x03, 0x00 };
    n = build_zip64(archive, sizeof(archive), &full, 8, empty_stream, 2, 0,
                    0, &at);
    check_int("empty deflate zip64 entry",
              zip64_reads(archive, n, "", 0), 0);
    put64(archive + at.central + 46U + 5U + 12U, UINT64_MAX);
    check_int("reject zip64 compressed size past directory",
              zip64_reads(archive, n, "", 0), -2);
    n = build_zip64(archive, sizeof(archive), &full, 8, empty_stream, 2,
                    (uint64_t)1 << 40, 0, &at);
    check_int("reject impossible zip64 deflate size",
              zip64_reads(archive, n, "", 0), -2);
}

static void test_deflate_writer(void) {
    printf("[deflate writer]\n");
    enum { text_len = 300000 };
    uint8_t *text = (uint8_t *)malloc(text_len);
    uint8_t *noise = (uint8_t *)malloc(4096);
    uint8_t *out = (uint8_t *)malloc(text_len);
    if (!text || !noise || !out) {
        check_int("deflate writer allocation", 0, 1);
        free(text);
        free(noise);
        free(out);
        return;
    }
    for (size_t i = 0; i < text_len; i++)
        text[i] = (uint8_t)"line of compressible text\n"[i % 26U];
    uint32_t state = 0x12345678U;
    for (size_t i = 0; i < 4096; i++) {
        state = state * 1103515245U + 12345U;
        noise[i] = (uint8_t)(state >> 24);
    }

    neverc_zip_writer_t w;
    neverc_zip_writer_init(&w);
    check_int("reject unknown method",
              neverc_zip_writer_add_method(&w, "x", text, 10, 12), -1);
    int ok =
        neverc_zip_writer_add_method(&w, "text.txt", text, text_len,
                                     NEVERC_ZIP_DEFLATED) == 0 &&
        neverc_zip_writer_add_method(&w, "noise.bin", noise, 4096,
                                     NEVERC_ZIP_DEFLATED) == 0 &&
        neverc_zip_writer_add_method(&w, "empty.txt", NULL, 0,
                                     NEVERC_ZIP_DEFLATED) == 0 &&
        neverc_zip_writer_add_method(&w, "dir/", NULL, 0,
                                     NEVERC_ZIP_DEFLATED) == 0 &&
        neverc_zip_writer_add_method(&w, "stored.txt", text, 100,
                                     NEVERC_ZIP_STORED) == 0;
    check_int("deflate writer adds", ok, 1);
    /* Data may alias the writer's own output, as with writer_add: here the
     * stored.txt payload that was just written. */
    uint8_t alias_copy[100];
    if (ok) {
        const uint8_t *view = w.data + w.len - sizeof(alias_copy);
        memcpy(alias_copy, view, sizeof(alias_copy));
        ok = neverc_zip_writer_add_method(&w, "alias.bin", view,
                                          sizeof(alias_copy),
                                          NEVERC_ZIP_DEFLATED) == 0;
    }
    check_int("deflate writer aliased add", ok, 1);
    ok = ok && neverc_zip_writer_close(&w) == 0;
    check_int("deflate writer close", ok, 1);
    if (!ok) {
        neverc_zip_writer_free(&w);
        free(text);
        free(noise);
        free(out);
        return;
    }
    check_int("deflate writer shrinks archive", w.len < text_len / 10U, 1);

    neverc_zip_reader_t r;
    check_int("deflate writer output reads",
              neverc_zip_reader_init(&r, w.data, w.len), 0);
    check_int("deflate writer entry count", neverc_zip_reader_count(&r), 6);
    static const uint16_t methods[6] = {
        NEVERC_ZIP_DEFLATED, NEVERC_ZIP_STORED, NEVERC_ZIP_STORED,
        NEVERC_ZIP_STORED, NEVERC_ZIP_STORED, NEVERC_ZIP_DEFLATED
    };
    static const char *const names[6] = {
        "text.txt", "noise.bin", "empty.txt", "dir/", "stored.txt",
        "alias.bin"
    };
    for (int i = 0; i < neverc_zip_reader_count(&r) && i < 6; i++) {
        const neverc_zip_file_header_t *f = neverc_zip_reader_file(&r, i);
        check_str("deflate writer name", f ? f->name : NULL, names[i]);
        check_int("deflate writer method", f ? f->method : -1, methods[i]);
    }
    const neverc_zip_file_header_t *f = neverc_zip_reader_file(&r, 0);
    check_int("deflate writer compressed smaller",
              f && f->compressed_size < f->uncompressed_size / 10U, 1);
    size_t got = text_len;
    check_int("deflate writer text read",
              neverc_zip_reader_file_read(&r, 0, out, &got), 0);
    check_int("deflate writer text content",
              got == text_len && memcmp(out, text, text_len) == 0, 1);
    got = 4096;
    check_int("incompressible entry read",
              neverc_zip_reader_file_read(&r, 1, out, &got), 0);
    check_int("incompressible entry content",
              got == 4096 && memcmp(out, noise, 4096) == 0, 1);
    got = sizeof(alias_copy);
    check_int("aliased deflate entry read",
              neverc_zip_reader_file_read(&r, 5, out, &got), 0);
    check_int("aliased deflate entry content",
              got == sizeof(alias_copy) &&
                  memcmp(out, alias_copy, sizeof(alias_copy)) == 0,
              1);
    neverc_zip_reader_free(&r);
    neverc_zip_writer_free(&w);
    free(text);
    free(noise);
    free(out);
}

int main(void) {
    printf("=== NeverC Archive/ZIP Module Tests ===\n\n");
    test_roundtrip();
    test_crc_integrity();
    test_truncated_entry();
    test_central_directory_and_writer_state();
    test_invalid_args();
    test_reject_unsafe_paths();
    test_dot_component_paths();
    test_utf8_name_flag();
    test_directory_extra_and_descriptor();
    test_unsigned_descriptor_crc_signature_collision();
    test_overlapping_entries();
    test_zip64_sentinels();
    test_deflate_reader();
    test_zip64_reader();
    test_deflate_writer();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
