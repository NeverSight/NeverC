/* APPNOTE 4.4.21: 0xFFFF in the EOCD entry counts only redirects to ZIP64
 * when a ZIP64 locator actually precedes the EOCD. An archive holding exactly
 * 65535 entries encodes that count exactly and must stay readable. */
#include "neverc/std/archive/zip.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ENTRY_COUNT 65535U
#define LOCAL_SIZE 31U   /* 30-byte header + 1-byte name */
#define CENTRAL_SIZE 47U /* 46-byte header + 1-byte name */
#define LOCATOR_COMMENT 20U

static int tests_run = 0, tests_passed = 0, tests_failed = 0;

static void check_int(const char *name, int got, int expected) {
    tests_run++;
    if (got == expected) {
        tests_passed++;
    } else {
        tests_failed++;
        printf("  FAIL %s: got %d want %d\n", name, got, expected);
    }
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

static uint16_t get16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Builds ENTRY_COUNT empty stored entries named "f". With with_locator the
 * last central record carries a comment whose first bytes are the ZIP64
 * locator signature, so the byte before the EOCD really does look like one. */
static uint8_t *build_archive(int with_locator, size_t *out_len) {
    size_t comment = with_locator ? LOCATOR_COMMENT : 0U;
    size_t central_bytes = (size_t)ENTRY_COUNT * CENTRAL_SIZE + comment;
    size_t cd_offset = (size_t)ENTRY_COUNT * LOCAL_SIZE;
    size_t total = cd_offset + central_bytes + 22U;
    uint8_t *buf = (uint8_t *)calloc(total, 1U);
    if (!buf) return NULL;

    for (unsigned i = 0; i < ENTRY_COUNT; i++) {
        uint8_t *p = buf + (size_t)i * LOCAL_SIZE;
        put32(p, 0x04034b50U);
        put16(p + 4, 20);
        put16(p + 26, 1);
        p[30] = 'f';
    }

    uint8_t *cd = buf + cd_offset;
    for (unsigned i = 0; i < ENTRY_COUNT; i++) {
        uint8_t *p = cd + (size_t)i * CENTRAL_SIZE;
        put32(p, 0x02014b50U);
        put16(p + 4, 20);
        put16(p + 6, 20);
        put16(p + 28, 1);
        if (with_locator && i == ENTRY_COUNT - 1U)
            put16(p + 32, (uint16_t)LOCATOR_COMMENT);
        put32(p + 42, (uint32_t)((size_t)i * LOCAL_SIZE));
        p[46] = 'f';
    }
    if (with_locator)
        put32(cd + central_bytes - LOCATOR_COMMENT, 0x07064b50U);

    uint8_t *eocd = buf + cd_offset + central_bytes;
    put32(eocd, 0x06054b50U);
    put16(eocd + 8, (uint16_t)ENTRY_COUNT);
    put16(eocd + 10, (uint16_t)ENTRY_COUNT);
    put32(eocd + 12, (uint32_t)central_bytes);
    put32(eocd + 16, (uint32_t)cd_offset);

    *out_len = total;
    return buf;
}

int main(void) {
    printf("=== NeverC ZIP entry-count limit ===\n\n");

    size_t len = 0;
    uint8_t *archive = build_archive(0, &len);
    if (!archive) {
        printf("  FAIL: archive allocation\n");
        return 1;
    }
    neverc_zip_reader_t reader;
    check_int("accept 65535 entries",
              neverc_zip_reader_init(&reader, archive, len), 0);
    check_int("entry count", neverc_zip_reader_count(&reader),
              (int)ENTRY_COUNT);
    {
        const neverc_zip_file_header_t *first =
            neverc_zip_reader_file(&reader, 0);
        const neverc_zip_file_header_t *last =
            neverc_zip_reader_file(&reader, (int)ENTRY_COUNT - 1);
        check_int("first entry named f",
                  first != NULL && strcmp(first->name, "f") == 0, 1);
        check_int("last entry named f",
                  last != NULL && strcmp(last->name, "f") == 0, 1);
    }
    neverc_zip_reader_free(&reader);
    free(archive);

    archive = build_archive(1, &len);
    if (!archive) {
        printf("  FAIL: locator archive allocation\n");
        return 1;
    }
    check_int("reject 0xffff count behind a zip64 locator",
              neverc_zip_reader_init(&reader, archive, len), -1);
    neverc_zip_reader_free(&reader);
    free(archive);

    neverc_zip_writer_t writer;
    neverc_zip_writer_init(&writer);
    unsigned added = 0;
    for (; added < ENTRY_COUNT; added++) {
        if (neverc_zip_writer_add(&writer, "f", NULL, 0) != 0) break;
    }
    check_int("writer accepts 65535 entries", (int)added, (int)ENTRY_COUNT);
    if (added == ENTRY_COUNT) {
        check_int("writer rejects entry 65536",
                  neverc_zip_writer_add(&writer, "f", NULL, 0), -1);
        int close_result = neverc_zip_writer_close(&writer);
        check_int("writer closes 65535-entry classic archive", close_result,
                  0);
        check_int("writer archive has complete EOCD",
                  close_result != 0 || writer.len >= 42U, 1);
        if (close_result == 0 && writer.len >= 42U) {
            const uint8_t *eocd = writer.data + writer.len - 22U;
            check_int("writer EOCD signature",
                      get32(eocd) == UINT32_C(0x06054b50), 1);
            check_int("writer EOCD disk entry count",
                      get16(eocd + 8) == UINT16_MAX, 1);
            check_int("writer EOCD total entry count",
                      get16(eocd + 10) == UINT16_MAX, 1);
            check_int("writer omits ZIP64 locator",
                      get32(eocd - 20) != UINT32_C(0x07064b50), 1);
        }
    }
    neverc_zip_writer_free(&writer);

    printf("\n=== Results: %d/%d passed ===\n", tests_passed, tests_run);
    if (tests_failed == 0) puts("passed");
    return tests_failed == 0 ? 0 : 1;
}
