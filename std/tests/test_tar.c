#include "neverc/archive/tar.h"
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

static void test_write_read_roundtrip(void) {
    printf("[write/read roundtrip]\n");

    neverc_tar_writer_t w;
    neverc_tar_writer_init(&w);

    neverc_tar_header_t hdr1 = {0};
    strcpy(hdr1.name, "hello.txt");
    hdr1.size = 13;
    hdr1.mode = 0644;
    hdr1.typeflag = NEVERC_TAR_REG;
    hdr1.mtime = 1705321845;

    neverc_tar_writer_write_header(&w, &hdr1);
    neverc_tar_writer_write(&w, (const uint8_t *)"Hello, World!", 13);

    neverc_tar_header_t hdr2 = {0};
    strcpy(hdr2.name, "dir/");
    hdr2.size = 0;
    hdr2.mode = 0755;
    hdr2.typeflag = NEVERC_TAR_DIR;

    neverc_tar_writer_write_header(&w, &hdr2);

    neverc_tar_header_t hdr3 = {0};
    strcpy(hdr3.name, "data.bin");
    hdr3.size = 5;
    hdr3.mode = 0600;
    hdr3.typeflag = NEVERC_TAR_REG;

    neverc_tar_writer_write_header(&w, &hdr3);
    neverc_tar_writer_write(&w, (const uint8_t *)"\x01\x02\x03\x04\x05", 5);

    neverc_tar_writer_close(&w);

    /* Read back */
    neverc_tar_reader_t r;
    neverc_tar_reader_init(&r, w.data, w.len);

    neverc_tar_header_t rhdr;
    check_int("entry 1", neverc_tar_reader_next(&r, &rhdr), 1);
    check_str("name 1", rhdr.name, "hello.txt");
    check_int("size 1", (int)rhdr.size, 13);
    check_int("type 1", rhdr.typeflag, NEVERC_TAR_REG);

    uint8_t buf[64];
    size_t nread;
    neverc_tar_reader_read(&r, &rhdr, buf, sizeof(buf), &nread);
    check_size("read 1", nread, 13);
    buf[nread] = '\0';
    check_str("content 1", (char *)buf, "Hello, World!");

    check_int("entry 2", neverc_tar_reader_next(&r, &rhdr), 1);
    check_str("name 2", rhdr.name, "dir/");
    check_int("type 2", rhdr.typeflag, NEVERC_TAR_DIR);

    check_int("entry 3", neverc_tar_reader_next(&r, &rhdr), 1);
    check_str("name 3", rhdr.name, "data.bin");
    check_int("size 3", (int)rhdr.size, 5);
    neverc_tar_reader_read(&r, &rhdr, buf, sizeof(buf), &nread);
    check_size("read 3", nread, 5);
    check_int("bin byte 0", buf[0], 1);
    check_int("bin byte 4", buf[4], 5);

    neverc_tar_writer_free(&w);
}

static void test_empty_tar(void) {
    printf("[empty tar]\n");
    neverc_tar_writer_t w;
    neverc_tar_writer_init(&w);
    neverc_tar_writer_close(&w);

    neverc_tar_reader_t r;
    neverc_tar_reader_init(&r, w.data, w.len);
    neverc_tar_header_t hdr;
    check_int("empty no entries", neverc_tar_reader_next(&r, &hdr), 0);

    neverc_tar_writer_free(&w);
}

int main(void) {
    printf("=== NeverC Archive/Tar Module Tests ===\n\n");
    test_write_read_roundtrip();
    test_empty_tar();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
