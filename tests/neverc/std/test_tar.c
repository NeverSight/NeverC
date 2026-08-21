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

static void test_fill_header(uint8_t *block, const char *name, int typeflag,
                             uint64_t size, const char *linkname) {
    memset(block, 0, NEVERC_TAR_BLOCK_SIZE);
    memcpy(block, name, strlen(name));
    test_write_octal(block + 100, 8, 0644);
    test_write_octal(block + 124, 12, size);
    block[156] = (uint8_t)typeflag;
    if (linkname)
        memcpy(block + 157, linkname, strlen(linkname));
    memcpy(block + 257, "ustar", 5);
    block[263] = '0';
    block[264] = '0';
    test_finish_header(block);
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
    archive[500] = 0xFF; /* high byte: signed sum differs from unsigned */
    memset(archive + 148, ' ', 8);
    {
        int signed_sum = 256;
        for (int i = 0; i < 148; i++) signed_sum += (int8_t)archive[i];
        for (int i = 156; i < 512; i++) signed_sum += (int8_t)archive[i];
        unsigned int unsigned_sum = test_checksum(archive);
        check_int("signed checksum positive", signed_sum > 0, 1);
        check_int("signed checksum differs",
                  signed_sum != (int)unsigned_sum, 1);
        test_write_octal(archive + 148, 7, (uint64_t)signed_sum);
        archive[155] = ' ';
    }
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    check_int("accept historical signed checksum",
              neverc_tar_reader_next(&reader, &decoded), 1);
    check_str("signed checksum name", decoded.name, "valid");

    memcpy(archive, writer.data, sizeof(archive));
    archive[500] = 0xFF;
    test_finish_header(archive);
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    check_int("accept posix unsigned checksum",
              neverc_tar_reader_next(&reader, &decoded), 1);

    memcpy(archive, writer.data, sizeof(archive));
    archive[500] = 0xFF;
    memset(archive + 148, ' ', 8);
    test_write_octal(archive + 148, 7, 1);
    archive[155] = ' ';
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    check_int("reject checksum matching neither sum",
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
    memcpy(unsafe.name, "/etc/passwd", 12);
    check_int("writer rejects absolute",
              neverc_tar_writer_write_header(&writer, &unsafe), -1);
    memcpy(unsafe.name, "foo/../bar", 11);
    memset(unsafe.linkname, 0, sizeof(unsafe.linkname));
    unsafe.typeflag = NEVERC_TAR_REG;
    check_int("writer rejects nested traversal",
              neverc_tar_writer_write_header(&writer, &unsafe), -1);
    memcpy(unsafe.name, "foo\\bar", 8);
    check_int("writer rejects backslash",
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

    memset(block, 0, sizeof(block));
    memcpy(block, "/etc/passwd", 11);
    test_finish_header(block);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject absolute path",
              neverc_tar_reader_next(&reader, &header), -1);

    memset(block, 0, sizeof(block));
    memcpy(block, "foo/../bar", 10);
    test_finish_header(block);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject nested traversal",
              neverc_tar_reader_next(&reader, &header), -1);

    memset(block, 0, sizeof(block));
    memcpy(block, "foo\\..\\bar", 10);
    test_finish_header(block);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject backslash traversal",
              neverc_tar_reader_next(&reader, &header), -1);

    memset(block, 0, sizeof(block));
    memcpy(block, "link", 4);
    block[156] = NEVERC_TAR_SYM;
    memcpy(block + 157, "../../etc/passwd", 16);
    test_finish_header(block);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject symlink escape",
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

static void test_header_only_does_not_swallow(const char *label, const char *name,
                                              int typeflag, const char *linkname) {
    uint8_t archive[NEVERC_TAR_BLOCK_SIZE * 4U];
    memset(archive, 0, sizeof(archive));
    test_fill_header(archive, name, typeflag, 512, linkname);
    test_fill_header(archive + NEVERC_TAR_BLOCK_SIZE, "visible.txt",
                     NEVERC_TAR_REG, 0, NULL);

    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    neverc_tar_header_t header = {0};
    int result = neverc_tar_reader_next(&reader, &header);
    check_int(label, result, 1);
    if (result != 1) return;
    check_str("header-only name", header.name, name);
    check_int("header-only size ignored", (int)header.size, 0);
    check_int("header-only type", header.typeflag, typeflag);
    result = neverc_tar_reader_next(&reader, &header);
    check_int("following member still visible", result, 1);
    if (result != 1) return;
    check_str("visible name", header.name, "visible.txt");
    check_int("header-only archive end",
              neverc_tar_reader_next(&reader, &header), 0);
}

static void test_header_only_and_typeflags(void) {
    printf("[header-only members and typeflags]\n");
    test_header_only_does_not_swallow(
        "symlink does not swallow next", "link", NEVERC_TAR_SYM, "target.txt");
    test_header_only_does_not_swallow(
        "hardlink does not swallow next", "alias", NEVERC_TAR_LINK, "target.txt");
    test_header_only_does_not_swallow(
        "directory does not swallow next", "dir", NEVERC_TAR_DIR, NULL);

    uint8_t short_archive[NEVERC_TAR_BLOCK_SIZE * 3U];
    memset(short_archive, 0, sizeof(short_archive));
    test_fill_header(short_archive, "link", NEVERC_TAR_SYM, 11, "target.txt");
    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, short_archive, sizeof(short_archive));
    neverc_tar_header_t header = {0};
    int result = neverc_tar_reader_next(&reader, &header);
    check_int("gnu-style symlink size ignored", result, 1);
    if (result == 1) {
        check_int("gnu-style symlink payload", (int)header.size, 0);
        check_int("gnu-style symlink end",
                  neverc_tar_reader_next(&reader, &header), 0);
    }

    uint8_t block[NEVERC_TAR_BLOCK_SIZE];
    test_fill_header(block, "PaxHeaders.0/a", 'x', 0, NULL);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject pax extended header",
              neverc_tar_reader_next(&reader, &header), -1);

    /* PAX/GNU specials must fail closed before skipping a claimed payload.
     * Treating typeflag 'x'/'L' as a regular file would swallow the next
     * member (the overflow/skip class of Go archive/tar PAX bugs). */
    {
        uint8_t pax_archive[NEVERC_TAR_BLOCK_SIZE * 5U];
        memset(pax_archive, 0, sizeof(pax_archive));
        test_fill_header(pax_archive, "PaxHeaders.0/a", 'x', 512, NULL);
        test_fill_header(pax_archive + NEVERC_TAR_BLOCK_SIZE * 2U,
                         "visible.txt", NEVERC_TAR_REG, 0, NULL);
        neverc_tar_reader_init(&reader, pax_archive, sizeof(pax_archive));
        check_int("reject pax header with payload size",
                  neverc_tar_reader_next(&reader, &header), -1);

        memset(pax_archive, 0, sizeof(pax_archive));
        test_fill_header(pax_archive, "longname", 'L', 512, NULL);
        test_fill_header(pax_archive + NEVERC_TAR_BLOCK_SIZE * 2U,
                         "visible.txt", NEVERC_TAR_REG, 0, NULL);
        neverc_tar_reader_init(&reader, pax_archive, sizeof(pax_archive));
        check_int("reject gnu long-name with payload size",
                  neverc_tar_reader_next(&reader, &header), -1);

        memset(pax_archive, 0, sizeof(pax_archive));
        test_fill_header(pax_archive, "PaxHeaders.0/g", 'g', 512, NULL);
        test_fill_header(pax_archive + NEVERC_TAR_BLOCK_SIZE * 2U,
                         "visible.txt", NEVERC_TAR_REG, 0, NULL);
        neverc_tar_reader_init(&reader, pax_archive, sizeof(pax_archive));
        check_int("reject pax global header with payload size",
                  neverc_tar_reader_next(&reader, &header), -1);
    }

    test_fill_header(block, "longname", 'L', 0, NULL);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject gnu long name",
              neverc_tar_reader_next(&reader, &header), -1);

    test_fill_header(block, "dev", '3', 0, NULL);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject device typeflag",
              neverc_tar_reader_next(&reader, &header), -1);

    test_fill_header(block, "longlink", 'K', 0, NULL);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject gnu long link",
              neverc_tar_reader_next(&reader, &header), -1);

    test_fill_header(block, "PaxHeaders.0/g", 'g', 0, NULL);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject pax global header",
              neverc_tar_reader_next(&reader, &header), -1);

    test_fill_header(block, "file.txt", 0, 0, NULL);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("typeflag NUL file",
              neverc_tar_reader_next(&reader, &header), 1);
    check_int("typeflag NUL becomes reg", header.typeflag, NEVERC_TAR_REG);

    uint8_t slash_archive[NEVERC_TAR_BLOCK_SIZE * 4U];
    memset(slash_archive, 0, sizeof(slash_archive));
    test_fill_header(slash_archive, "legacy-dir/", 0, 512, NULL);
    test_fill_header(slash_archive + NEVERC_TAR_BLOCK_SIZE, "visible.txt",
                     NEVERC_TAR_REG, 0, NULL);
    neverc_tar_reader_init(&reader, slash_archive, sizeof(slash_archive));
    result = neverc_tar_reader_next(&reader, &header);
    check_int("NUL slash directory", result, 1);
    if (result == 1) {
        check_int("NUL slash type", header.typeflag, NEVERC_TAR_DIR);
        check_int("NUL slash size", (int)header.size, 0);
        check_int("NUL slash next visible",
                  neverc_tar_reader_next(&reader, &header), 1);
        check_str("NUL slash visible", header.name, "visible.txt");
    }

    memset(slash_archive, 0, sizeof(slash_archive));
    test_fill_header(slash_archive, "posix-dir/", NEVERC_TAR_REG, 512, NULL);
    test_fill_header(slash_archive + NEVERC_TAR_BLOCK_SIZE, "visible.txt",
                     NEVERC_TAR_REG, 0, NULL);
    neverc_tar_reader_init(&reader, slash_archive, sizeof(slash_archive));
    result = neverc_tar_reader_next(&reader, &header);
    check_int("reg slash directory", result, 1);
    if (result == 1) {
        check_int("reg slash type", header.typeflag, NEVERC_TAR_DIR);
        check_int("reg slash size", (int)header.size, 0);
        check_int("reg slash next visible",
                  neverc_tar_reader_next(&reader, &header), 1);
        check_str("reg slash visible", header.name, "visible.txt");
    }

    uint8_t prefixed[NEVERC_TAR_BLOCK_SIZE * 4U];
    memset(prefixed, 0, sizeof(prefixed));
    test_fill_header(prefixed, "bar/", 0, 512, NULL);
    memcpy(prefixed + 345, "prefix", 6);
    test_finish_header(prefixed);
    test_fill_header(prefixed + NEVERC_TAR_BLOCK_SIZE, "visible.txt",
                     NEVERC_TAR_REG, 0, NULL);
    neverc_tar_reader_init(&reader, prefixed, sizeof(prefixed));
    memset(&header, 0, sizeof(header));
    result = neverc_tar_reader_next(&reader, &header);
    check_int("ustar prefix typeflag NUL", result, 1);
    if (result == 1) {
        check_str("ustar prefix dir name", header.name, "prefix/bar/");
        check_int("ustar prefix dir type", header.typeflag, NEVERC_TAR_DIR);
        check_int("ustar prefix dir size", (int)header.size, 0);
        check_int("ustar prefix next visible",
                  neverc_tar_reader_next(&reader, &header), 1);
        check_str("ustar prefix visible name", header.name, "visible.txt");
    }

    neverc_tar_writer_t writer;
    neverc_tar_writer_init(&writer);
    memset(&header, 0, sizeof(header));
    strcpy(header.name, "pax");
    header.typeflag = 'x';
    check_int("writer rejects pax typeflag",
              neverc_tar_writer_write_header(&writer, &header), -1);
    strcpy(header.name, "link");
    strcpy(header.linkname, "target.txt");
    header.typeflag = NEVERC_TAR_SYM;
    header.size = 5;
    check_int("writer rejects symlink payload",
              neverc_tar_writer_write_header(&writer, &header), -1);
    header.size = 0;
    header.typeflag = NEVERC_TAR_LINK;
    check_int("writer accepts hardlink",
              neverc_tar_writer_write_header(&writer, &header), 0);
    check_int("writer hardlink close", neverc_tar_writer_close(&writer), 0);
    neverc_tar_reader_init(&reader, writer.data, writer.len);
    memset(&header, 0, sizeof(header));
    check_int("hardlink roundtrip", neverc_tar_reader_next(&reader, &header), 1);
    check_str("hardlink name", header.name, "link");
    check_str("hardlink target", header.linkname, "target.txt");
    check_int("hardlink type", header.typeflag, NEVERC_TAR_LINK);
    neverc_tar_writer_free(&writer);

    neverc_tar_writer_init(&writer);
    memset(&header, 0, sizeof(header));
    strcpy(header.name, "legacy-dir/");
    header.mode = 0755;
    check_int("writer promotes NUL typeflag dir",
              neverc_tar_writer_write_header(&writer, &header), 0);
    check_int("writer NUL dir close", neverc_tar_writer_close(&writer), 0);
    neverc_tar_reader_init(&reader, writer.data, writer.len);
    memset(&header, 0, sizeof(header));
    check_int("writer NUL dir read",
              neverc_tar_reader_next(&reader, &header), 1);
    check_str("writer NUL dir name", header.name, "legacy-dir/");
    check_int("writer NUL dir type", header.typeflag, NEVERC_TAR_DIR);
    neverc_tar_writer_free(&writer);

    neverc_tar_writer_init(&writer);
    memset(&header, 0, sizeof(header));
    strcpy(header.name, "file.txt");
    header.mode = 0644;
    check_int("writer promotes NUL typeflag file",
              neverc_tar_writer_write_header(&writer, &header), 0);
    check_int("writer NUL file close", neverc_tar_writer_close(&writer), 0);
    neverc_tar_reader_init(&reader, writer.data, writer.len);
    memset(&header, 0, sizeof(header));
    check_int("writer NUL file read",
              neverc_tar_reader_next(&reader, &header), 1);
    check_str("writer NUL file name", header.name, "file.txt");
    check_int("writer NUL file type", header.typeflag, NEVERC_TAR_REG);
    neverc_tar_writer_free(&writer);

    neverc_tar_writer_init(&writer);
    memset(&header, 0, sizeof(header));
    strcpy(header.name, "file/");
    header.typeflag = NEVERC_TAR_REG;
    header.size = 5;
    check_int("writer rejects reg slash payload",
              neverc_tar_writer_write_header(&writer, &header), -1);
    header.size = 0;
    check_int("writer promotes reg slash to dir",
              neverc_tar_writer_write_header(&writer, &header), 0);
    check_int("writer reg slash close", neverc_tar_writer_close(&writer), 0);
    neverc_tar_reader_init(&reader, writer.data, writer.len);
    memset(&header, 0, sizeof(header));
    check_int("writer reg slash read",
              neverc_tar_reader_next(&reader, &header), 1);
    check_int("writer reg slash type", header.typeflag, NEVERC_TAR_DIR);
    neverc_tar_writer_free(&writer);
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
    test_header_only_and_typeflags();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
