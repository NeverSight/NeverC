#include "neverc/std/archive/zip.h"
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
    check_int("writer accepts unsafe name for now",
              neverc_zip_writer_add(
                  &w, "../etc/passwd", payload, sizeof(payload) - 1), 0);
    check_int("writer close", neverc_zip_writer_close(&w), 0);

    neverc_zip_reader_t r;
    check_int("reader rejects unsafe path",
              neverc_zip_reader_init(&r, w.data, w.len), -1);
    neverc_zip_reader_free(&r);
    neverc_zip_writer_free(&w);
}

int main(void) {
    printf("=== NeverC Archive/ZIP Module Tests ===\n\n");
    test_roundtrip();
    test_crc_integrity();
    test_truncated_entry();
    test_central_directory_and_writer_state();
    test_invalid_args();
    test_reject_unsafe_paths();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
