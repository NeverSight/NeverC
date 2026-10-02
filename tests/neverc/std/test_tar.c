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

static void test_dot_component_paths(void) {
    printf("[dot component paths]\n");

    neverc_tar_writer_t writer;
    neverc_tar_writer_init(&writer);
    neverc_tar_header_t header = {0};

    strcpy(header.name, "./");
    header.mode = 0755;
    header.typeflag = NEVERC_TAR_DIR;
    check_int("write dot root directory",
              neverc_tar_writer_write_header(&writer, &header), 0);

    memset(&header, 0, sizeof(header));
    strcpy(header.name, "./file");
    header.mode = 0644;
    header.typeflag = NEVERC_TAR_REG;
    check_int("write leading dot component",
              neverc_tar_writer_write_header(&writer, &header), 0);

    memset(&header, 0, sizeof(header));
    strcpy(header.name, "foo/./bar");
    header.mode = 0644;
    header.typeflag = NEVERC_TAR_REG;
    check_int("write interior dot component",
              neverc_tar_writer_write_header(&writer, &header), 0);
    check_int("close dot component archive",
              neverc_tar_writer_close(&writer), 0);

    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, writer.data, writer.len);
    memset(&header, 0, sizeof(header));
    check_int("read dot root directory",
              neverc_tar_reader_next(&reader, &header), 1);
    check_str("preserve dot root directory", header.name, "./");
    check_int("dot root directory type", header.typeflag, NEVERC_TAR_DIR);
    check_int("read leading dot component",
              neverc_tar_reader_next(&reader, &header), 1);
    check_str("preserve leading dot component", header.name, "./file");
    check_int("read interior dot component",
              neverc_tar_reader_next(&reader, &header), 1);
    check_str("preserve interior dot component", header.name, "foo/./bar");
    check_int("dot component archive end",
              neverc_tar_reader_next(&reader, &header), 0);
    neverc_tar_writer_free(&writer);
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

#if SIZE_MAX == UINT32_MAX
    neverc_tar_writer_t boundary_writer;
    neverc_tar_writer_init(&boundary_writer);
    memset(&hdr, 0, sizeof(hdr));
    strcpy(hdr.name, "largest-padded-size");
    hdr.typeflag = NEVERC_TAR_REG;
    hdr.size = (int64_t)(SIZE_MAX - (NEVERC_TAR_BLOCK_SIZE - 1U));
    check_int("accept largest 32-bit padded size",
              neverc_tar_writer_write_header(&boundary_writer, &hdr), 0);
    neverc_tar_writer_free(&boundary_writer);

    neverc_tar_writer_init(&boundary_writer);
    hdr.size = (int64_t)(SIZE_MAX - (NEVERC_TAR_BLOCK_SIZE - 2U));
    check_int("reject overflowing 32-bit padded size",
              neverc_tar_writer_write_header(&boundary_writer, &hdr), -1);
    check_size("overflowing size writes no header", boundary_writer.len, 0);
    neverc_tar_writer_free(&boundary_writer);
#endif
}

static void test_legacy_rejects_full_width_linkname(void) {
    printf("[legacy rejects full-width linkname]\n");
    uint8_t block[NEVERC_TAR_BLOCK_SIZE] = {0};
    memcpy(block, "link", 4);
    block[156] = NEVERC_TAR_SYM;
    memset(block + 157, 'A', 100);
    test_finish_header(block);

    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, block, sizeof(block));
    neverc_tar_header_t header = {0};
    int result = neverc_tar_reader_next(&reader, &header);
    check_int("legacy rejects unrepresentable full-width link", result, -1);
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
    neverc_tar_header_v2_t full_header = {0};
    neverc_tar_header_v2_t full_decoded = {0};
    memset(full_header.name, 'p', 155);
    full_header.name[155] = '/';
    memset(full_header.name + 156, 'n', 100);
    full_header.name[256] = '\0';
    memset(full_header.linkname, 'l', 100);
    full_header.linkname[100] = '\0';
    memset(full_header.uname, 'u', 32);
    full_header.uname[32] = '\0';
    memset(full_header.gname, 'g', 32);
    full_header.gname[32] = '\0';
    full_header.typeflag = NEVERC_TAR_REG;
    check_int("max ustar path header",
              neverc_tar_writer_write_header_v2(&writer, &full_header), 0);
    check_int("max ustar path close",
              neverc_tar_writer_close(&writer), 0);
    neverc_tar_reader_init(&reader, writer.data, writer.len);
    check_int("max ustar path read",
              neverc_tar_reader_next_v2(&reader, &full_decoded), 1);
    check_size("max ustar path length", strlen(full_decoded.name), 256);
    check_str("max ustar path roundtrip",
              full_decoded.name, full_header.name);
    check_size("max linkname length", strlen(full_decoded.linkname), 100);
    check_str("max linkname roundtrip",
              full_decoded.linkname, full_header.linkname);
    check_size("max uname length", strlen(full_decoded.uname), 32);
    check_str("max uname roundtrip",
              full_decoded.uname, full_header.uname);
    check_size("max gname length", strlen(full_decoded.gname), 32);
    check_str("max gname roundtrip",
              full_decoded.gname, full_header.gname);
    neverc_tar_writer_free(&writer);

    /* A name that cannot be split at a slash goes into a pax record. */
    neverc_tar_writer_init(&writer);
    memset(&header, 0, sizeof(header));
    memset(header.name, 'x', 101);
    header.name[101] = '\0';
    header.typeflag = NEVERC_TAR_REG;
    check_int("unsplittable name uses pax",
              neverc_tar_writer_write_header(&writer, &header), 0);
    char unsplittable[102];
    memcpy(unsplittable, header.name, sizeof(unsplittable));
    strcpy(header.name, "oversized-mode");
    header.mode = UINT32_MAX;
    check_int("reject oversized octal",
              neverc_tar_writer_write_header(&writer, &header), -1);
    header.mode = 07777777;
    check_int("accept largest octal mode",
              neverc_tar_writer_write_header(&writer, &header), 0);
    check_int("unsplittable name close", neverc_tar_writer_close(&writer), 0);
    neverc_tar_reader_init(&reader, writer.data, writer.len);
    check_int("unsplittable name read",
              neverc_tar_reader_next_v2(&reader, &full_decoded), 1);
    check_str("unsplittable name roundtrip", full_decoded.name, unsplittable);
    check_int("largest mode read",
              neverc_tar_reader_next(&reader, &decoded), 1);
    check_int("largest mode roundtrip", (int)decoded.mode, 07777777);
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
    /* POSIX permits undefined logical records after the two zero end blocks
     * when a blocking factor pads the final physical record. */
    memset(padded_archive + NEVERC_TAR_BLOCK_SIZE * 3U, 0xA5,
           NEVERC_TAR_BLOCK_SIZE);
    neverc_tar_reader_init(
        &reader, padded_archive, sizeof(padded_archive));
    check_int("physical-padding entry header",
              neverc_tar_reader_next(&reader, &decoded), 1);
    check_int("accept undefined data after end blocks",
              neverc_tar_reader_next(&reader, &decoded), 0);
    check_int("stable padded archive end",
              neverc_tar_reader_next(&reader, &decoded), 0);

    /* Go archive/tar also treats a partial trailing record after the two
     * zero blocks as EOF. This specifically guards removal of the former
     * remaining-length modulo-512 check. */
    uint8_t short_tail_archive[NEVERC_TAR_BLOCK_SIZE * 3U + 1U];
    memcpy(short_tail_archive, writer.data, NEVERC_TAR_BLOCK_SIZE * 3U);
    short_tail_archive[sizeof(short_tail_archive) - 1U] = 0xA5;
    neverc_tar_reader_init(
        &reader, short_tail_archive, sizeof(short_tail_archive));
    check_int("short-tail entry header",
              neverc_tar_reader_next(&reader, &decoded), 1);
    check_int("accept one-byte tail after end blocks",
              neverc_tar_reader_next(&reader, &decoded), 0);
    check_int("stable short-tail archive end",
              neverc_tar_reader_next(&reader, &decoded), 0);
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
    unsafe.typeflag = NEVERC_TAR_DIR;
    check_int("writer rejects bare dot directory",
              neverc_tar_writer_write_header(&writer, &unsafe), -1);
    strcpy(unsafe.name, "./../escape");
    unsafe.typeflag = NEVERC_TAR_REG;
    check_int("writer rejects dot parent traversal",
              neverc_tar_writer_write_header(&writer, &unsafe), -1);
    strcpy(unsafe.name, "foo/./../escape");
    check_int("writer rejects interior dot parent traversal",
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
    memcpy(block, "./../escape", 11);
    test_finish_header(block);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject dot parent traversal",
              neverc_tar_reader_next(&reader, &header), -1);

    memset(block, 0, sizeof(block));
    memcpy(block, "foo/./../escape", 15);
    test_finish_header(block);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject interior dot parent traversal",
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

static void test_failed_reads_clear_headers(void) {
    printf("[failed_reads_clear_headers]\n");
    uint8_t block[NEVERC_TAR_BLOCK_SIZE];
    test_fill_header(block, "link", NEVERC_TAR_SYM, 0,
                     "../../etc/passwd");

    neverc_tar_reader_t reader;
    neverc_tar_header_v2_t header_v2;
    neverc_tar_header_v2_t zero_v2;
    memset(&header_v2, 0xA5, sizeof(header_v2));
    memset(&zero_v2, 0, sizeof(zero_v2));
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("v2 rejects unsafe link",
              neverc_tar_reader_next_v2(&reader, &header_v2), -1);
    check_size("v2 failure keeps position", reader.pos, 0);
    check_int("v2 failure clears header",
              memcmp(&header_v2, &zero_v2, sizeof(header_v2)) == 0, 1);

    neverc_tar_header_t header;
    neverc_tar_header_t zero;
    memset(&header, 0xA5, sizeof(header));
    memset(&zero, 0, sizeof(zero));
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("legacy rejects unsafe link",
              neverc_tar_reader_next(&reader, &header), -1);
    check_size("legacy failure keeps position", reader.pos, 0);
    check_int("legacy failure clears header",
              memcmp(&header, &zero, sizeof(header)) == 0, 1);
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

static void test_pax_linkdata_hardlink(void) {
    printf("[pax linkdata hardlink]\n");

    /* POSIX pax -o linkdata permits a typeflag '1' hard link to carry a
     * non-empty data section. Keep a following member in the fixture so an
     * incorrect header-only interpretation cannot hide a boundary error. */
    uint8_t archive[NEVERC_TAR_BLOCK_SIZE * 5U] = {0};
    test_fill_header(archive, "alias", NEVERC_TAR_LINK, 3, "target.txt");
    memcpy(archive + NEVERC_TAR_BLOCK_SIZE, "abc", 3);
    test_fill_header(archive + NEVERC_TAR_BLOCK_SIZE * 2U,
                     "visible.txt", NEVERC_TAR_REG, 0, NULL);

    neverc_tar_reader_t reader;
    neverc_tar_header_t header = {0};
    uint8_t body[8] = {0};
    size_t count = 0;
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    int result = neverc_tar_reader_next(&reader, &header);
    check_int("fixed linkdata header", result, 1);
    if (result == 1) {
        check_str("fixed linkdata name", header.name, "alias");
        check_str("fixed linkdata target", header.linkname, "target.txt");
        check_int("fixed linkdata type", header.typeflag, NEVERC_TAR_LINK);
        check_int("fixed linkdata size", (int)header.size, 3);
        check_int("fixed linkdata read",
                  neverc_tar_reader_read(
                      &reader, &header, body, sizeof(body), &count), 0);
        check_size("fixed linkdata read size", count, 3);
        check_int("fixed linkdata body", memcmp(body, "abc", 3), 0);
        result = neverc_tar_reader_next(&reader, &header);
        check_int("fixed linkdata next member", result, 1);
        if (result == 1)
            check_str("fixed linkdata next name", header.name, "visible.txt");
        check_int("fixed linkdata archive end",
                  neverc_tar_reader_next(&reader, &header), 0);
    }

    /* next() must also skip an unread linkdata body plus its block padding. */
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    result = neverc_tar_reader_next(&reader, &header);
    check_int("unread linkdata header", result, 1);
    if (result == 1) {
        result = neverc_tar_reader_next(&reader, &header);
        check_int("skip unread linkdata", result, 1);
        if (result == 1)
            check_str("unread linkdata next name", header.name, "visible.txt");
    }

    neverc_tar_writer_t writer;
    neverc_tar_writer_init(&writer);
    neverc_tar_header_t link = {0};
    strcpy(link.name, "alias");
    strcpy(link.linkname, "target.txt");
    link.size = 3;
    link.mode = 0644;
    link.typeflag = NEVERC_TAR_LINK;
    int header_result = neverc_tar_writer_write_header(&writer, &link);
    check_int("writer linkdata header", header_result, 0);
    if (header_result != 0) {
        neverc_tar_writer_free(&writer);
        return;
    }
    check_int("writer linkdata body",
              neverc_tar_writer_write(
                  &writer, (const uint8_t *)"abc", 3), 0);

    neverc_tar_header_t following = {0};
    strcpy(following.name, "visible.txt");
    following.mode = 0600;
    following.typeflag = NEVERC_TAR_REG;
    check_int("writer linkdata following header",
              neverc_tar_writer_write_header(&writer, &following), 0);
    int close_result = neverc_tar_writer_close(&writer);
    check_int("writer linkdata close", close_result, 0);
    if (close_result == 0) {
        memset(&header, 0, sizeof(header));
        memset(body, 0, sizeof(body));
        neverc_tar_reader_init(&reader, writer.data, writer.len);
        result = neverc_tar_reader_next(&reader, &header);
        check_int("roundtrip linkdata header", result, 1);
        if (result == 1) {
            check_int("roundtrip linkdata size", (int)header.size, 3);
            check_int("roundtrip linkdata read",
                      neverc_tar_reader_read(
                          &reader, &header, body, sizeof(body), &count), 0);
            check_size("roundtrip linkdata read size", count, 3);
            check_int("roundtrip linkdata body", memcmp(body, "abc", 3), 0);
            result = neverc_tar_reader_next(&reader, &header);
            check_int("roundtrip linkdata next member", result, 1);
            if (result == 1)
                check_str("roundtrip linkdata next name",
                          header.name, "visible.txt");
            check_int("roundtrip linkdata archive end",
                      neverc_tar_reader_next(&reader, &header), 0);
        }
    }
    neverc_tar_writer_free(&writer);
}

static void test_header_only_does_not_swallow(const char *label, const char *name,
                                              int typeflag, uint64_t size,
                                              const char *linkname) {
    uint8_t archive[NEVERC_TAR_BLOCK_SIZE * 4U];
    memset(archive, 0, sizeof(archive));
    test_fill_header(archive, name, typeflag, size, linkname);
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
        "symlink does not swallow next", "link", NEVERC_TAR_SYM, 512,
        "target.txt");
    test_header_only_does_not_swallow(
        "zero-size hardlink does not swallow next", "alias", NEVERC_TAR_LINK,
        0, "target.txt");
    test_header_only_does_not_swallow(
        "directory does not swallow next", "dir", NEVERC_TAR_DIR, 512, NULL);

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
    check_int("reject pax header without file header",
              neverc_tar_reader_next(&reader, &header), -1);

    /* PAX/GNU payloads are metadata, never a regular file's data: an invalid
     * pax payload fails, and an empty GNU long name (all NUL bytes, as in Go)
     * leaves the following header's own name. */
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
        check_int("gnu long name with empty payload",
                  neverc_tar_reader_next(&reader, &header), 1);
        check_str("empty gnu long name keeps header name",
                  header.name, "visible.txt");

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
    check_int("reject gnu long name without file header",
              neverc_tar_reader_next(&reader, &header), -1);

    test_fill_header(block, "dev", '3', 0, NULL);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject device typeflag",
              neverc_tar_reader_next(&reader, &header), -1);

    test_fill_header(block, "longlink", 'K', 0, NULL);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject gnu long link without file header",
              neverc_tar_reader_next(&reader, &header), -1);

    test_fill_header(block, "PaxHeaders.0/g", 'g', 0, NULL);
    neverc_tar_reader_init(&reader, block, sizeof(block));
    check_int("reject pax global header without terminator",
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

static void test_octal_bytes_after_nul(void) {
    printf("[octal bytes after nul]\n");
    /* Go archive/tar trims spaces and NULs, then parses only the digits
     * before the first remaining NUL; bytes after it are ignored. */
    uint8_t archive[NEVERC_TAR_BLOCK_SIZE * 4U];
    memset(archive, 0, sizeof(archive));
    test_fill_header(archive, "file.txt", NEVERC_TAR_REG, 0, NULL);
    memcpy(archive + 124, "3\0zzzzzzzzzz", 12);
    test_finish_header(archive);
    memcpy(archive + NEVERC_TAR_BLOCK_SIZE, "abc", 3);

    neverc_tar_reader_t reader;
    neverc_tar_header_t header = {0};
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    int result = neverc_tar_reader_next(&reader, &header);
    check_int("octal field with bytes after nul", result, 1);
    if (result != 1) return;
    check_int("octal field value before nul", (int)header.size, 3);
    uint8_t body[8] = {0};
    size_t count = 0;
    check_int("octal field body read",
              neverc_tar_reader_read(&reader, &header, body, sizeof(body),
                                     &count), 0);
    check_size("octal field body size", count, 3);
    check_int("octal field body", memcmp(body, "abc", 3), 0);
    check_int("octal field archive end",
              neverc_tar_reader_next(&reader, &header), 0);

    /* Bytes between digits remain invalid. */
    memset(archive, 0, sizeof(archive));
    test_fill_header(archive, "file.txt", NEVERC_TAR_REG, 0, NULL);
    memcpy(archive + 100, "1 2\0\0\0\0\0", 8);
    test_finish_header(archive);
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    check_int("reject space between octal digits",
              neverc_tar_reader_next(&reader, &header), -1);
}

static void test_star_prefix_width(void) {
    printf("[star prefix width]\n");
    /* The star format shares the ustar magic but ends the block with a
     * "tar\0" trailer; its prefix is 131 bytes, followed by atime/ctime. */
    uint8_t archive[NEVERC_TAR_BLOCK_SIZE * 3U];
    memset(archive, 0, sizeof(archive));
    test_fill_header(archive, "name", NEVERC_TAR_REG, 0, NULL);
    memset(archive + 345, 'p', 131);
    test_write_octal(archive + 476, 12, 5);
    test_write_octal(archive + 488, 12, 6);
    memcpy(archive + 508, "tar", 4);
    test_finish_header(archive);

    char expected[160];
    memset(expected, 'p', 131);
    memcpy(expected + 131, "/name", 6);
    neverc_tar_reader_t reader;
    neverc_tar_header_t header = {0};
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    int result = neverc_tar_reader_next(&reader, &header);
    check_int("star header", result, 1);
    if (result == 1)
        check_str("star prefix stops before atime", header.name, expected);
}

static void test_v7_header_has_no_owner_names(void) {
    printf("[v7 header has no owner names]\n");
    /* Without the ustar ("ustar\0") or GNU ("ustar " + " \0") magic, a
     * header is pre-POSIX v7 and offsets 265/297 are not owner names. */
    uint8_t archive[NEVERC_TAR_BLOCK_SIZE * 3U];
    memset(archive, 0, sizeof(archive));
    test_fill_header(archive, "file.txt", NEVERC_TAR_REG, 0, NULL);
    memset(archive + 257, 0, 8);
    memcpy(archive + 265, "someuser", 8);
    memcpy(archive + 297, "somegroup", 9);
    test_finish_header(archive);

    neverc_tar_reader_t reader;
    neverc_tar_header_t header;
    memset(&header, 0xA5, sizeof(header));
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    int result = neverc_tar_reader_next(&reader, &header);
    check_int("v7 header", result, 1);
    if (result == 1) {
        check_str("v7 header has no uname", header.uname, "");
        check_str("v7 header has no gname", header.gname, "");
    }

    memcpy(archive + 257, "ustar \0\0", 8);
    test_finish_header(archive);
    memset(&header, 0xA5, sizeof(header));
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    result = neverc_tar_reader_next(&reader, &header);
    check_int("gnu magic with bad version", result, 1);
    if (result == 1)
        check_str("bad gnu version is v7", header.uname, "");

    memcpy(archive + 257, "ustar  \0", 8);
    test_finish_header(archive);
    memset(&header, 0xA5, sizeof(header));
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    result = neverc_tar_reader_next(&reader, &header);
    check_int("gnu header", result, 1);
    if (result == 1) {
        check_str("gnu header uname", header.uname, "someuser");
        check_str("gnu header gname", header.gname, "somegroup");
    }
}

static void test_device_and_time_fields_are_numeric(void) {
    printf("[device and time fields are numeric]\n");
    /* Go rejects a header whose device numbers (ustar, star, GNU) or star
     * atime/ctime are not numeric. GNU atime/ctime stay lenient, and v7
     * blocks have no such fields. */
    uint8_t archive[NEVERC_TAR_BLOCK_SIZE * 3U];
    neverc_tar_reader_t reader;
    neverc_tar_header_t header;

    memset(archive, 0, sizeof(archive));
    test_fill_header(archive, "file.txt", NEVERC_TAR_REG, 0, NULL);
    memcpy(archive + 329, "zzzzzzz", 8);
    test_finish_header(archive);
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    check_int("reject non-numeric ustar devmajor",
              neverc_tar_reader_next(&reader, &header), -1);

    memset(archive, 0, sizeof(archive));
    test_fill_header(archive, "file.txt", NEVERC_TAR_REG, 0, NULL);
    memcpy(archive + 257, "ustar  \0", 8);
    memcpy(archive + 337, "9", 2);
    test_finish_header(archive);
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    check_int("reject non-octal gnu devminor",
              neverc_tar_reader_next(&reader, &header), -1);

    memset(archive, 0, sizeof(archive));
    test_fill_header(archive, "file.txt", NEVERC_TAR_REG, 0, NULL);
    memcpy(archive + 476, "zzzzzzzzzzz", 12);
    memcpy(archive + 508, "tar", 4);
    test_finish_header(archive);
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    check_int("reject non-numeric star atime",
              neverc_tar_reader_next(&reader, &header), -1);

    memset(archive, 0, sizeof(archive));
    test_fill_header(archive, "file.txt", NEVERC_TAR_REG, 0, NULL);
    memcpy(archive + 488, "zzzzzzzzzzz", 12);
    memcpy(archive + 508, "tar", 4);
    test_finish_header(archive);
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    check_int("reject non-numeric star ctime",
              neverc_tar_reader_next(&reader, &header), -1);

    memset(archive, 0, sizeof(archive));
    test_fill_header(archive, "file.txt", NEVERC_TAR_REG, 0, NULL);
    memset(archive + 257, 0, 8);
    memcpy(archive + 329, "zzzzzzz", 8);
    test_finish_header(archive);
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    check_int("v7 header has no device fields",
              neverc_tar_reader_next(&reader, &header), 1);

    memset(archive, 0, sizeof(archive));
    test_fill_header(archive, "file.txt", NEVERC_TAR_REG, 0, NULL);
    test_write_octal(archive + 329, 8, 7);
    test_write_octal(archive + 337, 8, 3);
    test_finish_header(archive);
    neverc_tar_reader_init(&reader, archive, sizeof(archive));
    check_int("accept numeric device fields",
              neverc_tar_reader_next(&reader, &header), 1);
}

static void test_cursor_iteration(void) {
    printf("[cursor iteration]\n");
    /* The reader keeps a cursor instead of rescanning earlier headers, so a
     * large archive iterates in linear time. Mix full, partial, and skipped
     * reads with payload sizes on both sides of a block boundary. */
    enum { ENTRY_COUNT = 20000 };
    neverc_tar_writer_t writer;
    neverc_tar_writer_init(&writer);
    uint8_t body[1100];
    for (size_t i = 0; i < sizeof(body); i++) body[i] = (uint8_t)(i * 7U + 1U);
    int write_failures = 0;
    for (int i = 0; i < ENTRY_COUNT; i++) {
        neverc_tar_header_t header = {0};
        snprintf(header.name, sizeof(header.name), "dir/entry-%d", i);
        header.size = (int64_t)((size_t)i % sizeof(body));
        header.mode = 0644;
        header.typeflag = NEVERC_TAR_REG;
        if (neverc_tar_writer_write_header(&writer, &header) != 0 ||
            neverc_tar_writer_write(&writer, body, (size_t)header.size) != 0)
            write_failures++;
    }
    check_int("cursor fixture writes", write_failures, 0);
    check_int("cursor fixture close", neverc_tar_writer_close(&writer), 0);

    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, writer.data, writer.len);
    neverc_tar_header_t header;
    uint8_t buffer[sizeof(body)];
    int entries = 0, mismatches = 0, result;
    while ((result = neverc_tar_reader_next(&reader, &header)) == 1) {
        char expected_name[64];
        snprintf(expected_name, sizeof(expected_name), "dir/entry-%d",
                 entries);
        size_t expected_size = (size_t)entries % sizeof(body);
        if (strcmp(header.name, expected_name) != 0 ||
            header.size != (int64_t)expected_size)
            mismatches++;
        size_t total = 0, count = 0;
        size_t limit = entries % 3 == 0 ? expected_size
                     : entries % 3 == 1 ? expected_size / 2U : 0U;
        while (total < limit) {
            size_t chunk = 1U + (size_t)entries % 700U;
            if (chunk > limit - total) chunk = limit - total;
            if (neverc_tar_reader_read(&reader, &header, buffer + total,
                                       chunk, &count) != 0 ||
                count != chunk) {
                mismatches++;
                break;
            }
            total += count;
        }
        if (memcmp(buffer, body, total) != 0) mismatches++;
        if (limit == expected_size &&
            (neverc_tar_reader_read(&reader, &header, buffer,
                                    sizeof(buffer), &count) != 0 ||
             count != 0))
            mismatches++;
        entries++;
    }
    check_int("cursor iteration end", result, 0);
    check_int("cursor iteration count", entries, ENTRY_COUNT);
    check_int("cursor iteration mismatches", mismatches, 0);
    check_int("cursor end is stable",
              neverc_tar_reader_next(&reader, &header), 0);
    neverc_tar_writer_free(&writer);
}

static void test_cursor_state_is_self_contained(void) {
    printf("[cursor state is self-contained]\n");
    neverc_tar_writer_t writer;
    neverc_tar_writer_init(&writer);
    neverc_tar_header_t header = {0};
    uint8_t body[600];
    for (size_t i = 0; i < sizeof(body); i++) body[i] = (uint8_t)i;
    strcpy(header.name, "first");
    header.size = (int64_t)sizeof(body);
    header.typeflag = NEVERC_TAR_REG;
    check_int("state fixture first",
              neverc_tar_writer_write_header(&writer, &header), 0);
    check_int("state fixture first body",
              neverc_tar_writer_write(&writer, body, sizeof(body)), 0);
    strcpy(header.name, "second");
    header.size = 1;
    check_int("state fixture second",
              neverc_tar_writer_write_header(&writer, &header), 0);
    check_int("state fixture second body",
              neverc_tar_writer_write(&writer, (const uint8_t *)"z", 1), 0);
    check_int("state fixture close", neverc_tar_writer_close(&writer), 0);

    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, writer.data, writer.len);
    check_int("state first entry",
              neverc_tar_reader_next(&reader, &header), 1);
    uint8_t buffer[sizeof(body)];
    size_t count = 0;
    check_int("state partial read",
              neverc_tar_reader_read(&reader, &header, buffer, 100, &count),
              0);
    check_size("state partial count", count, 100);

    /* A copy carries the whole position; both continue independently. */
    neverc_tar_reader_t copy = reader;
    check_int("state copy continues",
              neverc_tar_reader_read(&copy, &header, buffer, sizeof(buffer),
                                     &count), 0);
    check_size("state copy rest", count, sizeof(body) - 100U);
    check_int("state copy bytes", memcmp(buffer, body + 100, count), 0);

    neverc_tar_header_t small = header;
    small.size = 10;
    check_int("state rejects header smaller than unread payload",
              neverc_tar_reader_read(&reader, &small, buffer, 1, &count), -1);
    check_size("state rejected read count", count, 0);
    check_int("state original continues",
              neverc_tar_reader_read(&reader, &header, buffer, 1, &count), 0);
    check_int("state original byte", buffer[0], body[100]);

    check_int("state original skips rest",
              neverc_tar_reader_next(&reader, &header), 1);
    check_str("state second name", header.name, "second");
    check_int("state copy reaches second",
              neverc_tar_reader_next(&copy, &header), 1);
    check_str("state copy second name", header.name, "second");
    check_int("state second read",
              neverc_tar_reader_read(&copy, &header, buffer, 8, &count), 0);
    check_size("state second count", count, 1);
    check_int("state second byte", buffer[0], 'z');
    check_int("state exhausted entry",
              neverc_tar_reader_read(&copy, &header, buffer, 8, &count), 0);
    check_size("state exhausted count", count, 0);
    check_int("state copy end", neverc_tar_reader_next(&copy, &header), 0);
    check_int("state original end",
              neverc_tar_reader_next(&reader, &header), 0);
    check_int("state read after end",
              neverc_tar_reader_read(&reader, &header, buffer, 8, &count), 0);
    check_size("state read after end count", count, 0);

    /* Failures leave the cursor untouched. */
    neverc_tar_reader_init(&reader, writer.data, writer.len);
    check_int("state reread first",
              neverc_tar_reader_next(&reader, &header), 1);
    neverc_tar_reader_t before = reader;
    neverc_tar_header_v2_t v2;
    uint8_t *mutable_data = writer.data;
    mutable_data[NEVERC_TAR_BLOCK_SIZE * 3U] ^= 1U;
    check_int("state corrupt second header",
              neverc_tar_reader_next_v2(&reader, &v2), -1);
    check_int("state failure keeps data", reader.data == before.data, 1);
    check_size("state failure keeps len", reader.len, before.len);
    check_size("state failure keeps pos", reader.pos, before.pos);
    mutable_data[NEVERC_TAR_BLOCK_SIZE * 3U] ^= 1U;
    check_int("state retry after repair",
              neverc_tar_reader_next_v2(&reader, &v2), 1);
    check_str("state retry name", v2.name, "second");
    neverc_tar_writer_free(&writer);
}

/* Growable archive builder for pax/GNU fixtures. */
typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} test_archive_t;

static uint8_t *test_archive_grow(test_archive_t *archive, size_t extra) {
    if (archive->len + extra > archive->cap) {
        size_t next = archive->cap ? archive->cap : 4096U;
        while (next < archive->len + extra) next *= 2U;
        uint8_t *grown = (uint8_t *)realloc(archive->data, next);
        if (!grown) {
            printf("  FAIL: out of memory\n");
            exit(1);
        }
        archive->data = grown;
        archive->cap = next;
    }
    uint8_t *at = archive->data + archive->len;
    memset(at, 0, extra);
    archive->len += extra;
    return at;
}

static void test_archive_header(test_archive_t *archive, const char *name,
                                int typeflag, uint64_t size,
                                const char *linkname) {
    test_fill_header(test_archive_grow(archive, NEVERC_TAR_BLOCK_SIZE), name,
                     typeflag, size, linkname);
}

static void test_archive_body(test_archive_t *archive, const void *body,
                              size_t length) {
    size_t padded = (length + NEVERC_TAR_BLOCK_SIZE - 1U) /
                    NEVERC_TAR_BLOCK_SIZE * NEVERC_TAR_BLOCK_SIZE;
    uint8_t *at = test_archive_grow(archive, padded);
    if (length > 0) memcpy(at, body, length);
}

/* A metadata block ('x', 'g', 'L', 'K') followed by its payload. */
static void test_archive_meta(test_archive_t *archive, int typeflag,
                              const void *payload, size_t length) {
    test_archive_header(archive,
                        typeflag == 'L' || typeflag == 'K'
                            ? "././@LongLink" : "PaxHeaders.0/entry",
                        typeflag, length, NULL);
    test_archive_body(archive, payload, length);
}

static void test_archive_end(test_archive_t *archive) {
    (void)test_archive_grow(archive, NEVERC_TAR_BLOCK_SIZE * 2U);
}

static void test_archive_free(test_archive_t *archive) {
    free(archive->data);
    memset(archive, 0, sizeof(*archive));
}

/* Appends one "%d key=value\n" record whose length counts itself. */
static void test_pax_record(char *records, const char *key,
                            const char *value) {
    size_t body = strlen(key) + strlen(value) + 3U;
    size_t digits = 1;
    for (size_t n = body + 1U; n >= 10U; n /= 10U) digits++;
    size_t total = body + digits;
    char check[32];
    if ((size_t)snprintf(check, sizeof(check), "%zu", total) != digits)
        total++;
    size_t at = strlen(records);
    sprintf(records + at, "%zu %s=%s\n", total, key, value);
}

static void test_pax_member(test_archive_t *archive, const char *records,
                            const char *name, int typeflag, uint64_t size,
                            const char *linkname, const char *body) {
    test_archive_meta(archive, 'x', records, strlen(records));
    test_archive_header(archive, name, typeflag, size, linkname);
    if (body) test_archive_body(archive, body, strlen(body));
}

static int test_read_v3_entry(test_archive_t *archive,
                              neverc_tar_header_v3_t *header) {
    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, archive->data, archive->len);
    return neverc_tar_reader_next_v3(&reader, header);
}

static neverc_tar_header_v3_t test_v3_header;

static void test_pax_extended_records(void) {
    printf("[pax extended records]\n");
    char long_name[400], long_link[300], long_user[120];
    strcpy(long_name, "dir/");
    memset(long_name + 4, 'p', 300);
    long_name[304] = '\0';
    memset(long_link, 't', 200);
    long_link[200] = '\0';
    memset(long_user, 'u', 100);
    long_user[100] = '\0';
    char records[2048] = "";
    test_pax_record(records, "path", long_name);
    test_pax_record(records, "linkpath", long_link);
    test_pax_record(records, "uid", "-5");
    test_pax_record(records, "gid", "+77");
    test_pax_record(records, "uname", long_user);
    test_pax_record(records, "gname", "group");
    test_pax_record(records, "mtime", "-1.25");
    test_pax_record(records, "atime", "123.9999999999");
    test_pax_record(records, "ctime", "5.");
    test_pax_record(records, "comment", "ignored\nvalue");
    test_pax_record(records, "VENDOR.key", "x");

    test_archive_t archive = {0};
    test_pax_member(&archive, records, "short", NEVERC_TAR_SYM, 0, "t", NULL);
    test_archive_header(&archive, "next", NEVERC_TAR_REG, 2, NULL);
    test_archive_body(&archive, "hi", 2);
    test_archive_end(&archive);

    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, archive.data, archive.len);
    neverc_tar_header_t legacy;
    neverc_tar_header_v2_t v2;
    check_int("legacy rejects long pax path",
              neverc_tar_reader_next(&reader, &legacy), -1);
    check_int("v2 rejects long pax path",
              neverc_tar_reader_next_v2(&reader, &v2), -1);
    check_int("failed versions do not advance",
              reader.data == archive.data, 1);
    neverc_tar_header_v3_t *header = &test_v3_header;
    int result = neverc_tar_reader_next_v3(&reader, header);
    check_int("v3 reads pax entry", result, 1);
    if (result == 1) {
        check_str("pax path", header->name, long_name);
        check_str("pax linkpath", header->linkname, long_link);
        check_int("pax symlink type", header->typeflag, NEVERC_TAR_SYM);
        check_int("pax uid", (int)header->uid, -5);
        check_int("pax gid", (int)header->gid, 77);
        check_str("pax uname", header->uname, long_user);
        check_str("pax gname", header->gname, "group");
        check_int("pax negative mtime floor", (int)header->mtime, -2);
        check_int("pax negative mtime nsec", header->mtime_nsec, 750000000);
        check_int("pax atime", (int)header->atime, 123);
        check_int("pax atime truncates to nanoseconds", header->atime_nsec,
                  999999999);
        check_int("pax ctime", (int)header->ctime, 5);
        check_int("pax ctime empty fraction", header->ctime_nsec, 0);
    }
    check_int("entry after pax", neverc_tar_reader_next(&reader, &legacy), 1);
    check_str("entry after pax name", legacy.name, "next");
    check_int("pax archive end", neverc_tar_reader_next(&reader, &legacy), 0);
    test_archive_free(&archive);

    /* An "atime=1.000000001" keeps the ninth fraction digit. */
    records[0] = '\0';
    test_pax_record(records, "atime", "1.000000001");
    test_pax_member(&archive, records, "f", NEVERC_TAR_REG, 0, NULL, NULL);
    test_archive_end(&archive);
    check_int("ninth fraction digit entry",
              test_read_v3_entry(&archive, header), 1);
    check_int("ninth fraction digit", header->atime_nsec, 1);
    test_archive_free(&archive);
}

static void test_pax_size_and_precedence(void) {
    printf("[pax size and precedence]\n");
    neverc_tar_header_v3_t *header = &test_v3_header;
    char records[512] = "";

    /* A pax size replaces the ustar size field (here 0). */
    test_archive_t archive = {0};
    test_pax_record(records, "size", "3");
    test_pax_member(&archive, records, "sized", NEVERC_TAR_REG, 0, NULL,
                    "abc");
    test_archive_header(&archive, "after", NEVERC_TAR_REG, 1, NULL);
    test_archive_body(&archive, "z", 1);
    test_archive_end(&archive);
    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, archive.data, archive.len);
    int result = neverc_tar_reader_next_v3(&reader, header);
    check_int("pax size entry", result, 1);
    uint8_t body[8] = {0};
    size_t count = 0;
    if (result == 1) {
        check_int("pax size value", (int)header->size, 3);
        check_int("pax size partial read",
                  neverc_tar_reader_read_v3(&reader, header, body, 2, &count),
                  0);
        check_size("pax size partial count", count, 2);
        check_int("pax size partial bytes", memcmp(body, "ab", 2), 0);
    }
    check_int("skip rest of pax-sized entry",
              neverc_tar_reader_next_v3(&reader, header), 1);
    check_str("entry after pax-sized entry", header->name, "after");
    check_int("read after pax-sized entry",
              neverc_tar_reader_read_v3(&reader, header, body, 8, &count), 0);
    check_size("after pax-sized entry count", count, 1);
    test_archive_free(&archive);

    /* Duplicate keys: the last value wins and is the only one validated;
     * an empty value keeps the header field. Only the last 'x' applies. */
    records[0] = '\0';
    test_pax_record(records, "size", "abc");
    test_pax_record(records, "size", "2");
    test_pax_record(records, "path", "replaced");
    test_pax_record(records, "path", "");
    test_pax_record(records, "uname", "first");
    test_archive_meta(&archive, 'x', "11 path=ab\n", 11);
    test_pax_member(&archive, records, "orig", NEVERC_TAR_REG, 0, NULL, "xy");
    test_archive_end(&archive);
    result = test_read_v3_entry(&archive, header);
    check_int("duplicate pax keys", result, 1);
    if (result == 1) {
        check_int("last duplicate size wins", (int)header->size, 2);
        check_str("empty pax path keeps header name", header->name, "orig");
        check_str("pax uname", header->uname, "first");
    }
    test_archive_free(&archive);

    /* A GNU long name overrides the pax path. */
    records[0] = '\0';
    test_pax_record(records, "path", "from-pax");
    test_archive_meta(&archive, 'x', records, strlen(records));
    test_archive_meta(&archive, 'L', "from-gnu\0junk", 13);
    test_archive_header(&archive, "from-ustar", NEVERC_TAR_REG, 0, NULL);
    test_archive_end(&archive);
    result = test_read_v3_entry(&archive, header);
    check_int("gnu long name over pax", result, 1);
    if (result == 1)
        check_str("gnu long name wins", header->name, "from-gnu");
    test_archive_free(&archive);

    /* Size is ignored for header-only types even when negative. */
    records[0] = '\0';
    test_pax_record(records, "size", "-1");
    test_pax_member(&archive, records, "dir/", NEVERC_TAR_DIR, 0, NULL, NULL);
    test_archive_end(&archive);
    result = test_read_v3_entry(&archive, header);
    check_int("negative pax size on directory", result, 1);
    if (result == 1) check_int("directory size", (int)header->size, 0);
    test_archive_free(&archive);
    test_pax_member(&archive, records, "file", NEVERC_TAR_REG, 0, NULL, NULL);
    test_archive_end(&archive);
    check_int("reject negative pax size on file",
              test_read_v3_entry(&archive, header), -1);
    test_archive_free(&archive);
    /* '0' with a trailing slash reads as a directory here, but Go treats it
     * as a file, so a negative size is still invalid; NUL plus a slash is a
     * directory in both. */
    test_pax_member(&archive, records, "dir/", NEVERC_TAR_REG, 0, NULL, NULL);
    test_archive_end(&archive);
    check_int("reject negative pax size on '0' with slash",
              test_read_v3_entry(&archive, header), -1);
    test_archive_free(&archive);
    test_pax_member(&archive, records, "dir/", 0, 0, NULL, NULL);
    test_archive_end(&archive);
    check_int("negative pax size on NUL with slash",
              test_read_v3_entry(&archive, header), 1);
    test_archive_free(&archive);
}

static void test_pax_malformed_records(void) {
    printf("[pax malformed records]\n");
    static const struct {
        const char *label;
        const char *payload;
        size_t length;
        int expected;
    } cases[] = {
        {"valid record", "11 path=ab\n", 11, 1},
        {"plus sign in length", "+11 path=ab\n", 12, -1},
        {"plus sign counted in length", "+12 path=ab\n", 12, 1},
        {"no space", "11path=ab\n", 10, -1},
        {"non-digit length", "1x path=ab\n", 11, -1},
        {"empty length", " path=ab\n", 9, -1},
        {"length below five", "4 a=\n", 5, -1},
        {"length beyond payload", "12 path=ab", 11, -1},
        {"length not reaching newline", "10 path=ab\n", 11, -1},
        {"missing newline", "11 path=abc", 11, -1},
        {"missing equals", "10 pathab\n", 10, -1},
        {"empty key", "9 =value\n", 9, -1},
        {"nul in path value", "12 path=a\0b\n", 12, -1},
        {"nul in key", "11 ke\0y=ab\n", 11, -1},
        {"nul in other value", "14 comment=\0b\n", 14, 1},
        {"leading zeros in length", "011 path=ab\n", 12, -1},
        {"zero-padded exact length", "012 path=ab\n", 12, 1},
        {"bad uid", "11 uid=12x\n", 11, -1},
        {"overflowing gid", "27 gid=9223372036854775808\n", 27, -1},
        {"bad mtime fraction", "13 mtime=1.x\n", 13, -1},
        {"mtime without seconds", "12 mtime=.5\n", 12, -1},
        {"two dots in mtime", "15 mtime=1.5.5\n", 15, -1},
        /* Go wraps this to a garbage time; its floor is unrepresentable. */
        {"unrepresentable negative time",
         "32 atime=-9223372036854775808.5\n", 32, -1},
        {"most negative whole time", "30 atime=-9223372036854775808\n", 30, 1},
        {"bad size", "10 size=x\n", 10, -1},
        {"sparse numbytes first", "25 GNU.sparse.numbytes=1\n", 25, -1},
        {"sparse offset comma", "25 GNU.sparse.offset=1,2\n", 25, -1},
    };
    neverc_tar_header_v3_t *header = &test_v3_header;
    for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        test_archive_t archive = {0};
        test_archive_meta(&archive, 'x', cases[i].payload, cases[i].length);
        test_archive_header(&archive, "file", NEVERC_TAR_REG, 0, NULL);
        test_archive_end(&archive);
        check_int(cases[i].label, test_read_v3_entry(&archive, header),
                  cases[i].expected);
        test_archive_free(&archive);
    }

    /* A 'g' payload follows the same syntax rules. */
    test_archive_t archive = {0};
    test_archive_meta(&archive, 'g', "10 pathab\n", 10);
    test_archive_header(&archive, "file", NEVERC_TAR_REG, 0, NULL);
    test_archive_end(&archive);
    check_int("reject malformed global record",
              test_read_v3_entry(&archive, header), -1);
    test_archive_free(&archive);

    /* A truncated payload fails instead of reading past the archive. */
    test_archive_header(&archive, "PaxHeaders.0/x", 'x', 600, NULL);
    test_archive_body(&archive, "11 path=ab\n", 11);
    check_int("reject truncated pax payload",
              test_read_v3_entry(&archive, header), -1);
    test_archive_free(&archive);
}

static void test_gnu_long_names(void) {
    printf("[gnu long names]\n");
    char long_name[320], long_link[220];
    memcpy(long_name, "long/", 5);
    memset(long_name + 5, 'n', 300);
    long_name[305] = '\0';
    memcpy(long_link, "long/", 5);
    memset(long_link + 5, 'k', 150);
    long_link[155] = '\0';

    test_archive_t archive = {0};
    test_archive_meta(&archive, 'L', long_name, strlen(long_name) + 1U);
    test_archive_meta(&archive, 'K', long_link, strlen(long_link));
    test_archive_header(&archive, "truncated-name", NEVERC_TAR_SYM, 0,
                        "truncated-link");
    memcpy(archive.data + archive.len - NEVERC_TAR_BLOCK_SIZE + 257,
           "ustar  \0", 8);
    test_finish_header(archive.data + archive.len - NEVERC_TAR_BLOCK_SIZE);
    test_archive_header(&archive, "after", NEVERC_TAR_REG, 1, NULL);
    test_archive_body(&archive, "q", 1);
    test_archive_end(&archive);

    neverc_tar_header_v3_t *header = &test_v3_header;
    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, archive.data, archive.len);
    int result = neverc_tar_reader_next_v3(&reader, header);
    check_int("gnu long name entry", result, 1);
    if (result == 1) {
        check_str("gnu long name", header->name, long_name);
        check_str("gnu long link without nul", header->linkname, long_link);
    }
    check_int("gnu long names do not leak",
              neverc_tar_reader_next_v3(&reader, header), 1);
    check_str("gnu next entry", header->name, "after");
    check_str("gnu next entry link", header->linkname, "");
    test_archive_free(&archive);
}

static void test_pax_global_headers(void) {
    printf("[pax global headers]\n");
    char records[256] = "";
    test_pax_record(records, "comment", "commit-id");
    test_pax_record(records, "path", "not-applied");

    test_archive_t archive = {0};
    test_archive_meta(&archive, 'g', records, strlen(records));
    test_archive_header(&archive, "first", NEVERC_TAR_REG, 1, NULL);
    test_archive_body(&archive, "1", 1);
    /* As in Go, a global header drops metadata pending from an 'x'. */
    records[0] = '\0';
    test_pax_record(records, "path", "dropped");
    test_archive_meta(&archive, 'x', records, strlen(records));
    test_archive_meta(&archive, 'g', "", 0);
    test_archive_header(&archive, "second", NEVERC_TAR_REG, 0, NULL);
    test_archive_end(&archive);

    neverc_tar_reader_t reader;
    neverc_tar_header_t header;
    neverc_tar_reader_init(&reader, archive.data, archive.len);
    check_int("global header skipped", neverc_tar_reader_next(&reader, &header),
              1);
    check_str("global path not applied", header.name, "first");
    check_int("global after pax", neverc_tar_reader_next(&reader, &header), 1);
    check_str("global drops pending pax", header.name, "second");
    check_int("global archive end", neverc_tar_reader_next(&reader, &header),
              0);
    test_archive_free(&archive);

    /* Metadata directly before the end blocks ends the archive. */
    test_archive_header(&archive, "only", NEVERC_TAR_REG, 0, NULL);
    test_archive_meta(&archive, 'x', "11 path=ab\n", 11);
    test_archive_meta(&archive, 'L', "x", 1);
    test_archive_end(&archive);
    neverc_tar_reader_init(&reader, archive.data, archive.len);
    check_int("entry before dangling metadata",
              neverc_tar_reader_next(&reader, &header), 1);
    check_int("dangling metadata ends archive",
              neverc_tar_reader_next(&reader, &header), 0);
    check_int("dangling metadata end is stable",
              neverc_tar_reader_next(&reader, &header), 0);
    test_archive_free(&archive);
}

static void test_special_payload_limit(void) {
    printf("[special payload limit]\n");
    neverc_tar_header_v3_t *header = &test_v3_header;
    for (int over = 0; over <= 1; over++) {
        size_t total = (size_t)NEVERC_TAR_SPECIAL_MAX + (size_t)over;
        char *payload = (char *)malloc(total + 1U);
        if (!payload) {
            check_int("payload allocation", 0, 1);
            return;
        }
        int prefix = sprintf(payload, "%zu comment=", total);
        memset(payload + prefix, 'v', total - (size_t)prefix - 1U);
        payload[total - 1U] = '\n';
        test_archive_t archive = {0};
        test_archive_meta(&archive, 'x', payload, total);
        test_archive_header(&archive, "file", NEVERC_TAR_REG, 0, NULL);
        test_archive_end(&archive);
        check_int(over ? "reject payload over limit"
                       : "accept payload at limit",
                  test_read_v3_entry(&archive, header), over ? -1 : 1);
        test_archive_free(&archive);
        free(payload);
    }
}

static void test_base256_numbers(void) {
    printf("[base-256 numbers]\n");
    neverc_tar_header_v3_t *header = &test_v3_header;
    test_archive_t archive = {0};
    test_archive_header(&archive, "big", NEVERC_TAR_REG, 0, NULL);
    uint8_t *block = archive.data;
    /* size 3, uid 2^40, mtime -2 */
    memset(block + 124, 0, 12);
    block[124] = 0x80;
    block[135] = 3;
    memset(block + 108, 0, 8);
    block[108] = 0x80;
    block[110] = 0x01;
    memset(block + 136, 0xFF, 12);
    block[147] = 0xFE;
    test_finish_header(block);
    test_archive_body(&archive, "abc", 3);
    test_archive_end(&archive);
    int result = test_read_v3_entry(&archive, header);
    check_int("base-256 entry", result, 1);
    if (result == 1) {
        check_int("base-256 size", (int)header->size, 3);
        check_int("base-256 uid", header->uid == ((int64_t)1 << 40), 1);
        check_int("base-256 negative mtime", (int)header->mtime, -2);
    }

    /* A value needing more than 63 bits is invalid. */
    memset(block + 136, 0, 12);
    block[136] = 0x80;
    block[139] = 0x80;
    test_finish_header(block);
    check_int("reject base-256 overflow",
              test_read_v3_entry(&archive, header), -1);

    /* A negative size is invalid for a file but ignored for a symlink. */
    memset(block + 136, 0, 12);
    block[136] = 0x80;
    memset(block + 124, 0xFF, 12);
    test_finish_header(block);
    check_int("reject negative file size",
              test_read_v3_entry(&archive, header), -1);
    block[156] = NEVERC_TAR_SYM;
    memcpy(block + 157, "target", 6);
    test_finish_header(block);
    result = test_read_v3_entry(&archive, header);
    check_int("negative size on symlink", result, 1);
    if (result == 1) check_int("symlink size", (int)header->size, 0);
    test_archive_free(&archive);
}

static void test_sparse_entries_rejected(void) {
    printf("[sparse entries rejected]\n");
    static const char *const sparse_records[][2] = {
        {"GNU.sparse.major", "1"},
        {"GNU.sparse.map", "0,1"},
        {"GNU.sparse.offset", "0"},
    };
    neverc_tar_header_v3_t *header = &test_v3_header;
    for (size_t i = 0; i < 3; i++) {
        char records[256] = "";
        test_pax_record(records, sparse_records[i][0], sparse_records[i][1]);
        if (i == 0) test_pax_record(records, "GNU.sparse.minor", "0");
        test_archive_t archive = {0};
        test_pax_member(&archive, records, "sparse", NEVERC_TAR_REG, 1, NULL,
                        "s");
        test_archive_end(&archive);
        check_int("reject pax sparse entry",
                  test_read_v3_entry(&archive, header), -1);
        test_archive_free(&archive);
    }

    /* An unknown sparse version is an ordinary file, as in Go. */
    char records[256] = "";
    test_pax_record(records, "GNU.sparse.major", "2");
    test_pax_record(records, "GNU.sparse.map", "0,1");
    test_archive_t archive = {0};
    test_pax_member(&archive, records, "plain", NEVERC_TAR_REG, 1, NULL, "p");
    test_archive_end(&archive);
    check_int("unknown sparse version is a file",
              test_read_v3_entry(&archive, header), 1);
    test_archive_free(&archive);

    test_archive_header(&archive, "oldsparse", 'S', 0, NULL);
    test_archive_end(&archive);
    check_int("reject gnu sparse typeflag",
              test_read_v3_entry(&archive, header), -1);
    test_archive_free(&archive);
}

static void test_v3_capacities(void) {
    printf("[v3 capacities]\n");
    neverc_tar_header_v3_t *header = &test_v3_header;
    char *name = (char *)malloc(NEVERC_TAR_V3_NAME_SIZE + 1U);
    char *records = (char *)malloc(NEVERC_TAR_V3_NAME_SIZE + 64U);
    if (!name || !records) {
        free(name);
        free(records);
        check_int("capacity allocation", 0, 1);
        return;
    }
    for (int extra = 0; extra <= 1; extra++) {
        size_t length = NEVERC_TAR_V3_NAME_SIZE - 1U + (size_t)extra;
        for (size_t i = 0; i < length; i++)
            name[i] = (char)(i % 50U == 49U ? '/' : 'n');
        name[length] = '\0';
        records[0] = '\0';
        test_pax_record(records, "path", name);
        test_archive_t archive = {0};
        test_pax_member(&archive, records, "x", NEVERC_TAR_REG, 0, NULL, NULL);
        test_archive_end(&archive);
        int result = test_read_v3_entry(&archive, header);
        check_int(extra ? "reject name over v3 capacity"
                        : "accept name at v3 capacity",
                  result, extra ? -1 : 1);
        if (result == 1)
            check_size("v3 name length", strlen(header->name), length);
        test_archive_free(&archive);
    }
    for (int extra = 0; extra <= 1; extra++) {
        size_t length = NEVERC_TAR_V3_OWNER_SIZE - 1U + (size_t)extra;
        memset(name, 'o', length);
        name[length] = '\0';
        records[0] = '\0';
        test_pax_record(records, "gname", name);
        test_archive_t archive = {0};
        test_pax_member(&archive, records, "x", NEVERC_TAR_REG, 0, NULL, NULL);
        test_archive_end(&archive);
        check_int(extra ? "reject owner over v3 capacity"
                        : "accept owner at v3 capacity",
                  test_read_v3_entry(&archive, header), extra ? -1 : 1);
        test_archive_free(&archive);
    }
    free(name);
    free(records);
}

static void test_writer_pax_records(void) {
    printf("[writer pax records]\n");
    neverc_tar_header_v3_t *header = (neverc_tar_header_v3_t *)calloc(
        1, sizeof(neverc_tar_header_v3_t));
    if (!header) {
        check_int("writer header allocation", 0, 1);
        return;
    }
    strcpy(header->name, "dir/");
    memset(header->name + 4, 'n', 300);
    strcpy(header->linkname, "target/");
    memset(header->linkname + 7, 't', 150);
    memset(header->uname, 'u', 40);
    strcpy(header->gname, "gr\xc3\xbcp");
    header->typeflag = NEVERC_TAR_SYM;
    header->mode = 0777;
    header->uid = (int64_t)1 << 30;
    header->gid = -2;
    header->mtime = -2;
    header->mtime_nsec = 750000000;
    header->atime = 1600000000;
    header->atime_nsec = 5;
    header->ctime = -7;

    neverc_tar_writer_t writer;
    neverc_tar_writer_init(&writer);
    check_int("write pax header",
              neverc_tar_writer_write_header_v3(&writer, header), 0);
    check_int("close pax archive", neverc_tar_writer_close(&writer), 0);
    check_int("pax block first", writer.data[156], 'x');

    /* Records are sorted by key, as Go writes them. */
    char expected[2048] = "";
    char name_value[400], link_value[200], user_value[64];
    strcpy(name_value, header->name);
    strcpy(link_value, header->linkname);
    strcpy(user_value, header->uname);
    test_pax_record(expected, "atime", "1600000000.000000005");
    test_pax_record(expected, "ctime", "-7");
    test_pax_record(expected, "gid", "-2");
    test_pax_record(expected, "gname", "gr\xc3\xbcp");
    test_pax_record(expected, "linkpath", link_value);
    test_pax_record(expected, "mtime", "-1.25");
    test_pax_record(expected, "path", name_value);
    test_pax_record(expected, "uid", "1073741824");
    test_pax_record(expected, "uname", user_value);
    size_t expected_length = strlen(expected);
    check_int("pax payload records",
              memcmp(writer.data + NEVERC_TAR_BLOCK_SIZE, expected,
                     expected_length) == 0, 1);
    char size_field[12];
    test_write_octal((uint8_t *)size_field, 12, expected_length);
    check_int("pax payload size field",
              memcmp(writer.data + 124, size_field, 12) == 0, 1);
    /* Named like Go's: path.Join(dir, "PaxHeaders.0", file), 100 bytes. */
    char block_name[100];
    memcpy(block_name, "dir/PaxHeaders.0/", 17);
    memset(block_name + 17, 'n', sizeof(block_name) - 17U);
    check_int("pax block name",
              memcmp(writer.data, block_name, sizeof(block_name)) == 0, 1);

    size_t header_offset =
        NEVERC_TAR_BLOCK_SIZE +
        (expected_length + NEVERC_TAR_BLOCK_SIZE - 1U) /
            NEVERC_TAR_BLOCK_SIZE * NEVERC_TAR_BLOCK_SIZE;
    const uint8_t *main_block = writer.data + header_offset;
    check_int("main header type", main_block[156], NEVERC_TAR_SYM);
    check_int("main header truncated name",
              memcmp(main_block, name_value, 100) == 0, 1);
    check_int("main header gid field zero",
              memcmp(main_block + 116, "0000000", 8) == 0, 1);
    check_int("main header mtime field zero",
              memcmp(main_block + 136, "00000000000", 12) == 0, 1);
    check_int("main header gname is ascii only",
              memcmp(main_block + 297, "grp", 4) == 0, 1);

    neverc_tar_header_v3_t *decoded = &test_v3_header;
    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, writer.data, writer.len);
    int result = neverc_tar_reader_next_v3(&reader, decoded);
    check_int("read pax header", result, 1);
    if (result == 1) {
        check_str("roundtrip name", decoded->name, name_value);
        check_str("roundtrip link", decoded->linkname, link_value);
        check_str("roundtrip uname", decoded->uname, user_value);
        check_str("roundtrip gname", decoded->gname, "gr\xc3\xbcp");
        check_int("roundtrip uid", decoded->uid == header->uid, 1);
        check_int("roundtrip gid", (int)decoded->gid, -2);
        check_int("roundtrip mtime", (int)decoded->mtime, -2);
        check_int("roundtrip mtime nsec", decoded->mtime_nsec, 750000000);
        check_int("roundtrip atime", decoded->atime == 1600000000, 1);
        check_int("roundtrip atime nsec", decoded->atime_nsec, 5);
        check_int("roundtrip ctime", (int)decoded->ctime, -7);
        check_int("roundtrip ctime nsec", decoded->ctime_nsec, 0);
        check_int("roundtrip mode", (int)decoded->mode, 0777);
    }
    check_int("pax archive end", neverc_tar_reader_next_v3(&reader, decoded),
              0);
    neverc_tar_writer_free(&writer);
    free(header);
}

static void test_writer_chooses_ustar_or_pax(void) {
    printf("[writer chooses ustar or pax]\n");
    neverc_tar_writer_t writer;
    neverc_tar_header_t header;

    /* Fields that fit stay plain ustar, splitting a long ASCII name. */
    neverc_tar_writer_init(&writer);
    memset(&header, 0, sizeof(header));
    memset(header.name, 'p', 120);
    header.name[120] = '/';
    memcpy(header.name + 121, "file", 5);
    header.typeflag = NEVERC_TAR_REG;
    header.mtime = 1700000000;
    check_int("splittable name header",
              neverc_tar_writer_write_header(&writer, &header), 0);
    check_int("splittable name stays ustar", writer.data[156], NEVERC_TAR_REG);
    check_int("splittable name prefix", writer.data[345], 'p');
    neverc_tar_writer_free(&writer);

    /* A short name that is not ASCII needs a pax path. */
    neverc_tar_writer_init(&writer);
    memset(&header, 0, sizeof(header));
    strcpy(header.name, "dir/./na\xc3\xafve.txt");
    header.typeflag = NEVERC_TAR_REG;
    check_int("non-ascii name header",
              neverc_tar_writer_write_header(&writer, &header), 0);
    check_int("non-ascii name uses pax", writer.data[156], 'x');
    check_str("non-ascii pax block name", (const char *)writer.data,
              "dir/PaxHeaders.0/nave.txt");
    check_int("non-ascii name close", neverc_tar_writer_close(&writer), 0);
    neverc_tar_reader_t reader;
    neverc_tar_header_t decoded;
    neverc_tar_reader_init(&reader, writer.data, writer.len);
    check_int("non-ascii name read", neverc_tar_reader_next(&reader, &decoded),
              1);
    check_str("non-ascii name roundtrip", decoded.name, header.name);
    neverc_tar_writer_free(&writer);

    /* Negative and beyond-octal times move to pax records. */
    static const int64_t times[] = {-1, INT64_C(8589934592), INT64_MIN};
    for (size_t i = 0; i < sizeof(times) / sizeof(times[0]); i++) {
        neverc_tar_writer_init(&writer);
        memset(&header, 0, sizeof(header));
        strcpy(header.name, "timed");
        header.typeflag = NEVERC_TAR_REG;
        header.mtime = times[i];
        check_int("wide mtime header",
                  neverc_tar_writer_write_header(&writer, &header), 0);
        check_int("wide mtime uses pax", writer.data[156], 'x');
        check_int("wide mtime close", neverc_tar_writer_close(&writer), 0);
        neverc_tar_reader_init(&reader, writer.data, writer.len);
        check_int("wide mtime read", neverc_tar_reader_next(&reader, &decoded),
                  1);
        check_int("wide mtime roundtrip", decoded.mtime == times[i], 1);
        neverc_tar_writer_free(&writer);
    }

#if SIZE_MAX > UINT32_MAX
    /* A size beyond the 11 octal digits of ustar becomes a pax record; only
     * the header is inspected, the 8 GiB body is never written. */
    neverc_tar_writer_init(&writer);
    memset(&header, 0, sizeof(header));
    strcpy(header.name, "huge.bin");
    header.typeflag = NEVERC_TAR_REG;
    header.size = INT64_C(8589934592);
    check_int("huge size header",
              neverc_tar_writer_write_header(&writer, &header), 0);
    check_int("huge size uses pax", writer.data[156], 'x');
    check_int("huge size record",
              memcmp(writer.data + NEVERC_TAR_BLOCK_SIZE,
                     "19 size=8589934592\n", 19) == 0, 1);
    check_int("huge size field zero",
              memcmp(writer.data + NEVERC_TAR_BLOCK_SIZE * 2U + 124,
                     "00000000000", 12) == 0, 1);
    check_int("huge size entry stays open", neverc_tar_writer_close(&writer),
              -1);
    neverc_tar_writer_free(&writer);
#endif

    /* A field truncated in favor of a pax record must not end in '/'. */
    neverc_tar_header_v3_t *v3 = &test_v3_header;
    memset(v3, 0, sizeof(*v3));
    memset(v3->name, 'a', 99);
    v3->name[99] = '/';
    memset(v3->name + 100, 'b', 50);
    v3->name[150] = '/';
    v3->typeflag = NEVERC_TAR_DIR;
    v3->uid = (int64_t)1 << 30;
    neverc_tar_writer_init(&writer);
    check_int("truncated directory header",
              neverc_tar_writer_write_header_v3(&writer, v3), 0);
    size_t main_offset = NEVERC_TAR_BLOCK_SIZE * 2U;
    check_int("truncated name drops trailing slash",
              writer.data[main_offset + 99], 0);
    check_int("truncated name keeps prefix",
              writer.data[main_offset + 98], 'a');
    neverc_tar_writer_free(&writer);

    /* Invalid nanoseconds and pax-only typeflags are rejected. */
    neverc_tar_writer_init(&writer);
    memset(v3, 0, sizeof(*v3));
    strcpy(v3->name, "file");
    v3->typeflag = NEVERC_TAR_REG;
    v3->mtime_nsec = 1000000000;
    check_int("reject mtime nanoseconds overflow",
              neverc_tar_writer_write_header_v3(&writer, v3), -1);
    v3->mtime_nsec = 0;
    v3->atime_nsec = -1;
    check_int("reject negative atime nanoseconds",
              neverc_tar_writer_write_header_v3(&writer, v3), -1);
    v3->atime_nsec = 0;
    v3->typeflag = 'x';
    check_int("reject pax typeflag in v3",
              neverc_tar_writer_write_header_v3(&writer, v3), -1);
    v3->typeflag = NEVERC_TAR_REG;
    v3->mode = 010000000;
    check_int("reject mode beyond 21 bits",
              neverc_tar_writer_write_header_v3(&writer, v3), -1);
    check_size("rejected headers write nothing", writer.len, 0);
    neverc_tar_writer_free(&writer);
}

int main(void) {
    printf("=== NeverC Archive/Tar Module Tests ===\n\n");
    test_write_read_roundtrip();
    test_dot_component_paths();
    test_empty_tar();
    test_invalid_lengths();
    test_legacy_rejects_full_width_linkname();
    test_incremental_io_and_state();
    test_ustar_metadata_and_long_name();
    test_malformed_headers();
    test_reject_unsafe_paths();
    test_failed_reads_clear_headers();
    test_gnu_magic_ignores_prefix();
    test_pax_linkdata_hardlink();
    test_header_only_and_typeflags();
    test_octal_bytes_after_nul();
    test_star_prefix_width();
    test_v7_header_has_no_owner_names();
    test_device_and_time_fields_are_numeric();
    test_cursor_iteration();
    test_cursor_state_is_self_contained();
    test_pax_extended_records();
    test_pax_size_and_precedence();
    test_pax_malformed_records();
    test_gnu_long_names();
    test_pax_global_headers();
    test_special_payload_limit();
    test_base256_numbers();
    test_sparse_entries_rejected();
    test_v3_capacities();
    test_writer_pax_records();
    test_writer_chooses_ustar_or_pax();
    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed > 0 ? 1 : 0;
}
