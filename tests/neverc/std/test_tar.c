#include "neverc/std/archive/tar.h"
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

static unsigned int test_checksum(const uint8_t *block) {
    unsigned int sum = 256U;
    for (size_t i = 0; i < 148U; i++) sum += block[i];
    for (size_t i = 156U; i < NEVERC_TAR_BLOCK_SIZE; i++) sum += block[i];
    return sum;
}

static void test_write_octal(uint8_t *field, size_t width, uint64_t value) {
    memset(field, '0', width - 1U);
    field[width - 1U] = '\0';
    for (size_t i = width - 1U; i > 0 && value != 0; i--) {
        field[i - 1U] = (uint8_t)('0' + (value & 7U));
        value >>= 3U;
    }
}

static void test_finish_header(uint8_t *block) {
    memset(block + 148, ' ', 8);
    test_write_octal(block + 148, 7, test_checksum(block));
    block[155] = ' ';
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

    neverc_tar_header_t rhdr = {0};
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

static void test_invalid_lengths(void) {
    printf("[invalid lengths]\n");
    neverc_tar_writer_t w;
    neverc_tar_writer_init(&w);
    uint8_t byte = 0;
    check_int("write length overflow",
              neverc_tar_writer_write(&w, &byte, SIZE_MAX), -1);

    neverc_tar_header_t hdr = {0};
    strcpy(hdr.name, "bad");
    hdr.size = -1;
    check_int("negative header size",
              neverc_tar_writer_write_header(&w, &hdr), -1);
    neverc_tar_writer_free(&w);
}

static void test_full_width_linkname(void) {
    printf("[full-width linkname]\n");
    uint8_t block[NEVERC_TAR_BLOCK_SIZE] = {0};
    memcpy(block, "link", 4);
    block[156] = NEVERC_TAR_SYM;
    memset(block + 157, 'A', 100);
    test_finish_header(block);

    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, block, sizeof(block));
    neverc_tar_header_t header = {0};
    int result = neverc_tar_reader_next(&reader, &header);
    check_int("read full-width link", result, 1);
    if (result != 1) return;
    check_size("preserve full-width link", strlen(header.linkname), 100);
    check_int("terminate full-width link", header.linkname[100], '\0');
}

static void test_incremental_io_and_state(void) {
    printf("[incremental io and state]\n");
    neverc_tar_writer_t writer;
    neverc_tar_writer_init(&writer);
    neverc_tar_header_t first = {0};
    strcpy(first.name, "first");
    first.size = 5;
    first.mode = 0644;
    first.typeflag = NEVERC_TAR_REG;
    check_int("first header",
              neverc_tar_writer_write_header(&writer, &first), 0);
    check_int("first partial write",
              neverc_tar_writer_write(
                  &writer, (const uint8_t *)"ab", 2), 0);
    check_int("reject early close", neverc_tar_writer_close(&writer), -1);
    check_int("finish first write",
              neverc_tar_writer_write(
                  &writer, (const uint8_t *)"cde", 3), 0);

    neverc_tar_header_t second = {0};
    strcpy(second.name, "second");
    second.size = 3;
    second.mode = 0600;
    second.typeflag = NEVERC_TAR_REG;
    check_int("second header",
              neverc_tar_writer_write_header(&writer, &second), 0);
    check_int("reject body overrun",
              neverc_tar_writer_write(
                  &writer, (const uint8_t *)"toolong", 7), -1);
    check_int("second body",
              neverc_tar_writer_write(
                  &writer, (const uint8_t *)"xyz", 3), 0);
    int close_result = neverc_tar_writer_close(&writer);
    check_int("close complete writer", close_result, 0);
    check_int("close is idempotent", neverc_tar_writer_close(&writer), 0);
    check_int("reject write after close",
              neverc_tar_writer_write(
                  &writer, (const uint8_t *)"x", 1), -1);

    if (close_result != 0) {
        neverc_tar_writer_free(&writer);
        return;
    }
    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, writer.data, writer.len);
    neverc_tar_header_t header = {0};
    uint8_t buffer[8] = {0};
    size_t count = 0;
    int next_result = neverc_tar_reader_next(&reader, &header);
    check_int("read first header", next_result, 1);
    if (next_result != 1) {
        neverc_tar_writer_free(&writer);
        return;
    }
    check_int("read first part", neverc_tar_reader_read(
                  &reader, &header, buffer, 2, &count), 0);
    check_size("first part size", count, 2);
    check_int("first part bytes", memcmp(buffer, "ab", 2), 0);
    check_int("read another byte", neverc_tar_reader_read(
                  &reader, &header, buffer, 1, &count), 0);
    check_size("another byte size", count, 1);
    check_int("another byte", buffer[0], 'c');

    /* next() must discard unread entry bytes and alignment padding. */
    next_result = neverc_tar_reader_next(&reader, &header);
    check_int("skip unread entry", next_result, 1);
    if (next_result != 1) {
        neverc_tar_writer_free(&writer);
        return;
    }
    check_str("second name", header.name, "second");
    check_int("read second body", neverc_tar_reader_read(
                  &reader, &header, buffer, sizeof(buffer), &count), 0);
    check_size("second body size", count, 3);
    check_int("second body bytes", memcmp(buffer, "xyz", 3), 0);
    check_int("reader end", neverc_tar_reader_next(
                  &reader, &header), 0);
    neverc_tar_writer_free(&writer);
}

static void test_ustar_metadata_and_long_name(void) {
    printf("[ustar metadata and long name]\n");
    neverc_tar_writer_t writer;
    neverc_tar_writer_init(&writer);
    neverc_tar_header_t header = {0};
    memset(header.name, 'p', 120);
    header.name[120] = '/';
    memcpy(header.name + 121, "file.txt", 9);
    strcpy(header.linkname, "target.txt");
    strcpy(header.uname, "neverc");
    strcpy(header.gname, "builders");
    header.mode = 0777;
    header.mtime = 1700000000;
    header.typeflag = NEVERC_TAR_SYM;
    int header_result =
        neverc_tar_writer_write_header(&writer, &header);
    check_int("long-name header", header_result, 0);
    int close_result = neverc_tar_writer_close(&writer);
    check_int("long-name close", close_result, 0);
    if (header_result != 0 || close_result != 0) {
        neverc_tar_writer_free(&writer);
        return;
    }

    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, writer.data, writer.len);
    neverc_tar_header_t decoded = {0};
    int next_result = neverc_tar_reader_next(&reader, &decoded);
    check_int("long-name read", next_result, 1);
    if (next_result != 1) {
        neverc_tar_writer_free(&writer);
        return;
    }
    check_str("long-name roundtrip", decoded.name, header.name);
    check_str("link roundtrip", decoded.linkname, "target.txt");
    check_str("uname roundtrip", decoded.uname, "neverc");
    check_str("gname roundtrip", decoded.gname, "builders");
    neverc_tar_writer_free(&writer);

    neverc_tar_writer_init(&writer);
    memset(&header, 0, sizeof(header));
    memset(header.name, 'p', 155);
    header.name[155] = '/';
    memset(header.name + 156, 'n', 100);
    header.name[256] = '\0';
    memset(header.linkname, 'l', 100);
    header.linkname[100] = '\0';
    memset(header.uname, 'u', 32);
    header.uname[32] = '\0';
    memset(header.gname, 'g', 32);
    header.gname[32] = '\0';
    header.typeflag = NEVERC_TAR_REG;
    check_int("max ustar path header",
              neverc_tar_writer_write_header(&writer, &header), 0);
    check_int("max ustar path close",
              neverc_tar_writer_close(&writer), 0);
    neverc_tar_reader_init(&reader, writer.data, writer.len);
    memset(&decoded, 0, sizeof(decoded));
    check_int("max ustar path read",
              neverc_tar_reader_next(&reader, &decoded), 1);
    check_size("max ustar path length", strlen(decoded.name), 256);
    check_str("max ustar path roundtrip", decoded.name, header.name);
    check_size("max linkname length", strlen(decoded.linkname), 100);
    check_str("max linkname roundtrip", decoded.linkname, header.linkname);
    check_size("max uname length", strlen(decoded.uname), 32);
    check_str("max uname roundtrip", decoded.uname, header.uname);
    check_size("max gname length", strlen(decoded.gname), 32);
    check_str("max gname roundtrip", decoded.gname, header.gname);
    neverc_tar_writer_free(&writer);

    neverc_tar_writer_init(&writer);
    memset(&header, 0, sizeof(header));
    memset(header.name, 'x', 101);
    header.name[101] = '\0';
    header.typeflag = NEVERC_TAR_REG;
    check_int("reject unsplittable name",
              neverc_tar_writer_write_header(&writer, &header), -1);
    strcpy(header.name, "oversized-mode");
    header.mode = UINT32_MAX;
    check_int("reject oversized octal",
              neverc_tar_writer_write_header(&writer, &header), -1);
    neverc_tar_writer_free(&writer);
}

static void test_malformed_headers(void) {
    printf("[malformed headers]\n");
    neverc_tar_writer_t writer;
    neverc_tar_writer_init(&writer);
    neverc_tar_header_t header = {0};
    strcpy(header.name, "valid");
    header.mode = 0644;
    header.typeflag = NEVERC_TAR_REG;
    int header_result =
        neverc_tar_writer_write_header(&writer, &header);
    check_int("malformed fixture header", header_result, 0);
    int close_result = neverc_tar_writer_close(&writer);
    check_int("malformed fixture close", close_result, 0);
    if (header_result != 0 || close_result != 0 ||
        !writer.data ||
        writer.len < NEVERC_TAR_BLOCK_SIZE * 3U) {
        neverc_tar_writer_free(&writer);
        return;
    }

    uint8_t archive[NEVERC_TAR_BLOCK_SIZE * 3U];
    memcpy(archive, writer.data, sizeof(archive));
    archive[0] ^= 1U;
    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    neverc_tar_header_t decoded = {0};
    check_int("reject bad checksum",
              neverc_tar_reader_next(&reader, &decoded), -1);

    memcpy(archive, writer.data, sizeof(archive));
    archive[124] = '9';
    test_finish_header(archive);
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    check_int("reject invalid octal",
              neverc_tar_reader_next(&reader, &decoded), -1);

    memcpy(archive, writer.data, sizeof(archive));
    test_write_octal(archive + 124, 12, 1);
    test_finish_header(archive);
    neverc_tar_reader_init(
        &reader, archive, NEVERC_TAR_BLOCK_SIZE);
    check_int("reject truncated entry",
              neverc_tar_reader_next(&reader, &decoded), -1);

    memcpy(archive, writer.data, sizeof(archive));
    neverc_tar_reader_init(
        &reader, archive, NEVERC_TAR_BLOCK_SIZE);
    check_int("unterminated entry header",
              neverc_tar_reader_next(&reader, &decoded), 1);
    check_int("reject missing end blocks",
              neverc_tar_reader_next(&reader, &decoded), -1);

    neverc_tar_reader_init(
        &reader, archive, NEVERC_TAR_BLOCK_SIZE * 2U);
    check_int("single-zero entry header",
              neverc_tar_reader_next(&reader, &decoded), 1);
    check_int("reject single zero end block",
              neverc_tar_reader_next(&reader, &decoded), -1);

    memcpy(archive, writer.data, sizeof(archive));
    archive[NEVERC_TAR_BLOCK_SIZE * 2U] = 1;
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    check_int("nonzero terminator entry header",
              neverc_tar_reader_next(&reader, &decoded), 1);
    check_int("reject nonzero second end block",
              neverc_tar_reader_next(&reader, &decoded), -1);

    uint8_t padded_archive[NEVERC_TAR_BLOCK_SIZE * 4U] = {0};
    memcpy(padded_archive, writer.data, writer.len);
    neverc_tar_reader_init(
        &reader, padded_archive, sizeof(padded_archive));
    check_int("padded archive entry header",
              neverc_tar_reader_next(&reader, &decoded), 1);
    check_int("accept zero record padding",
              neverc_tar_reader_next(&reader, &decoded), 0);
    check_int("stable archive end",
              neverc_tar_reader_next(&reader, &decoded), 0);
    padded_archive[sizeof(padded_archive) - 1U] = 1;
    neverc_tar_reader_init(
        &reader, padded_archive, sizeof(padded_archive));
    check_int("trailing-data entry header",
              neverc_tar_reader_next(&reader, &decoded), 1);
    check_int("reject data after end blocks",
              neverc_tar_reader_next(&reader, &decoded), -1);
    neverc_tar_writer_free(&writer);
}

static void test_reject_unsafe_paths(void) {
    printf("[reject_unsafe_paths]\n");
    uint8_t block[NEVERC_TAR_BLOCK_SIZE] = {0};
    memcpy(block, "../etc/passwd", 13);
    test_finish_header(block);

    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, block, sizeof(block));
    neverc_tar_header_t header = {0};
    check_int("reject parent traversal",
              neverc_tar_reader_next(&reader, &header), -1);

    neverc_tar_writer_t writer;
    neverc_tar_writer_init(&writer);
    neverc_tar_header_t unsafe = {0};
    memcpy(unsafe.name, "../etc/passwd", 14);
    unsafe.typeflag = NEVERC_TAR_REG;
    check_int("writer rejects traversal",
              neverc_tar_writer_write_header(&writer, &unsafe), -1);
    memcpy(unsafe.name, "link", 5);
    memcpy(unsafe.linkname, "../../etc/passwd", 17);
    unsafe.typeflag = NEVERC_TAR_SYM;
    check_int("writer rejects unsafe link",
              neverc_tar_writer_write_header(&writer, &unsafe), -1);
    memcpy(unsafe.name, ".", 2);
    memset(unsafe.linkname, 0, sizeof(unsafe.linkname));
    unsafe.typeflag = NEVERC_TAR_REG;
    check_int("writer rejects dot name",
              neverc_tar_writer_write_header(&writer, &unsafe), -1);
    memcpy(unsafe.name, "C:foo", 6);
    check_int("writer rejects drive prefix",
              neverc_tar_writer_write_header(&writer, &unsafe), -1);
    memcpy(unsafe.name, "file:stream", 12);
    check_int("writer rejects colon ads",
              neverc_tar_writer_write_header(&writer, &unsafe), -1);
    neverc_tar_writer_free(&writer);

    memset(block, 0, sizeof(block));
    memcpy(block, "link", 4);
    block[156] = NEVERC_TAR_SYM;
    test_finish_header(block);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject empty symlink",
              neverc_tar_reader_next(&reader, &header), -1);

    memset(block, 0, sizeof(block));
    memcpy(block, ".", 1);
    test_finish_header(block);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject dot name",
              neverc_tar_reader_next(&reader, &header), -1);

    memset(block, 0, sizeof(block));
    memcpy(block, "C:foo", 5);
    test_finish_header(block);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject drive prefix",
              neverc_tar_reader_next(&reader, &header), -1);

    memset(block, 0, sizeof(block));
    memcpy(block, "file:stream", 11);
    test_finish_header(block);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject colon ads",
              neverc_tar_reader_next(&reader, &header), -1);
}

static void test_gnu_magic_ignores_prefix(void) {
    printf("[gnu magic ignores prefix]\n");
    uint8_t block[NEVERC_TAR_BLOCK_SIZE] = {0};
    memcpy(block, "hello.txt", 9);
    memcpy(block + 257, "ustar ", 6);
    memcpy(block + 345, "evilprefix", 10);
    test_finish_header(block);

    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, block, sizeof(block));
    neverc_tar_header_t header = {0};
    int result = neverc_tar_reader_next(&reader, &header);
    check_int("gnu header", result, 1);
    if (result != 1) return;
    check_str("gnu name ignores atime field", header.name, "hello.txt");
}

int main(void) {
    printf("=== NeverC Archive/Tar Module Tests ===\n\n");
    test_write_read_roundtrip();
    test_empty_tar();
    test_invalid_lengths();
    test_full_width_linkname();
    test_incremental_io_and_state();
    test_ustar_metadata_and_long_name();
    test_malformed_headers();
    test_reject_unsafe_paths();
    test_gnu_magic_ignores_prefix();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    return tests_failed > 0 ? 1 : 0;
}
