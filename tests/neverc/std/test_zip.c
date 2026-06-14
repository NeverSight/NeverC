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

    neverc_zip_writer_add(&w, "hello.txt", (const uint8_t *)"Hello!", 6);
    neverc_zip_writer_add(&w, "data.bin", (const uint8_t *)"\x01\x02\x03", 3);
    neverc_zip_writer_add(&w, "empty.txt", (const uint8_t *)"", 0);
    neverc_zip_writer_close(&w);

    /* Read back */
    neverc_zip_reader_t r;
    neverc_zip_reader_init(&r, w.data, w.len);

    check_int("count", neverc_zip_reader_count(&r), 3);

    const neverc_zip_file_header_t *f0 = neverc_zip_reader_file(&r, 0);
    check_str("name 0", f0->name, "hello.txt");
    check_size("size 0", (size_t)f0->uncompressed_size, 6);

    size_t dlen;
    const uint8_t *d0 = neverc_zip_reader_file_data(&r, 0, &dlen);
    check_size("data len 0", dlen, 6);
    tests_run++;
    if (dlen == 6 && memcmp(d0, "Hello!", 6) == 0) tests_passed++;
    else { tests_failed++; printf("  FAIL: data 0 content\n"); }

    const neverc_zip_file_header_t *f1 = neverc_zip_reader_file(&r, 1);
    check_str("name 1", f1->name, "data.bin");
    const uint8_t *d1 = neverc_zip_reader_file_data(&r, 1, &dlen);
    check_size("data len 1", dlen, 3);
    check_int("byte 0", d1[0], 1);
    check_int("byte 2", d1[2], 3);

    const neverc_zip_file_header_t *f2 = neverc_zip_reader_file(&r, 2);
    check_str("name 2", f2->name, "empty.txt");
    check_size("size 2", (size_t)f2->uncompressed_size, 0);

    neverc_zip_reader_free(&r);
    neverc_zip_writer_free(&w);
}

static void test_crc_integrity(void) {
    printf("[crc integrity]\n");
    neverc_zip_writer_t w;
    neverc_zip_writer_init(&w);
    neverc_zip_writer_add(&w, "test.txt", (const uint8_t *)"test data", 9);
    neverc_zip_writer_close(&w);

    neverc_zip_reader_t r;
    neverc_zip_reader_init(&r, w.data, w.len);
    const neverc_zip_file_header_t *f = neverc_zip_reader_file(&r, 0);
    check_int("crc nonzero", f->crc32 != 0, 1);
    check_int("method stored", f->method, NEVERC_ZIP_STORED);

    neverc_zip_reader_free(&r);
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
    check_int("init ok", neverc_zip_reader_init(&r, buf, sizeof(buf)), 0);
    check_int("no files", neverc_zip_reader_count(&r), 0);
    neverc_zip_reader_free(&r);
}

int main(void) {
    printf("=== NeverC Archive/ZIP Module Tests ===\n\n");
    test_roundtrip();
    test_crc_integrity();
    test_truncated_entry();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
