#include "neverc/std/archive/zip.h"
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
    writer.nentries = UINT16_MAX - 1;
    check_int("writer rejects zip64 entry count",
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
}

int main(void) {
    printf("=== NeverC Archive/ZIP Module Tests ===\n\n");
    test_roundtrip();
    test_crc_integrity();
    test_truncated_entry();
    test_central_directory_and_writer_state();
    test_invalid_args();
    test_reject_unsafe_paths();
    test_directory_extra_and_descriptor();
    test_overlapping_entries();
    test_zip64_sentinels();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
