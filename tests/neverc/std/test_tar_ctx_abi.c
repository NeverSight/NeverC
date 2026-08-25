#include "neverc/std/archive/tar.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char name[256];
    int64_t size;
    uint32_t mode;
    int64_t mtime;
    int typeflag;
    char linkname[100];
    char uname[32];
    char gname[32];
} v3389_tar_header_t;

typedef struct {
    const uint8_t *data;
    size_t len;
    size_t pos;
} v3389_tar_reader_t;

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} v3389_tar_writer_t;

#define ABI_FIELD(current, legacy, field)                                  \
    _Static_assert(offsetof(current, field) == offsetof(legacy, field),     \
                   #current "." #field " v3389 offset changed")

_Static_assert(sizeof(neverc_tar_header_t) == sizeof(v3389_tar_header_t),
               "neverc_tar_header_t v3389 size changed");
_Static_assert(_Alignof(neverc_tar_header_t) ==
                   _Alignof(v3389_tar_header_t),
               "neverc_tar_header_t v3389 alignment changed");
ABI_FIELD(neverc_tar_header_t, v3389_tar_header_t, name);
ABI_FIELD(neverc_tar_header_t, v3389_tar_header_t, size);
ABI_FIELD(neverc_tar_header_t, v3389_tar_header_t, mode);
ABI_FIELD(neverc_tar_header_t, v3389_tar_header_t, mtime);
ABI_FIELD(neverc_tar_header_t, v3389_tar_header_t, typeflag);
ABI_FIELD(neverc_tar_header_t, v3389_tar_header_t, linkname);
ABI_FIELD(neverc_tar_header_t, v3389_tar_header_t, uname);
ABI_FIELD(neverc_tar_header_t, v3389_tar_header_t, gname);

_Static_assert(sizeof(neverc_tar_reader_t) == sizeof(v3389_tar_reader_t),
               "neverc_tar_reader_t v3389 size changed");
_Static_assert(_Alignof(neverc_tar_reader_t) ==
                   _Alignof(v3389_tar_reader_t),
               "neverc_tar_reader_t v3389 alignment changed");
ABI_FIELD(neverc_tar_reader_t, v3389_tar_reader_t, data);
ABI_FIELD(neverc_tar_reader_t, v3389_tar_reader_t, len);
ABI_FIELD(neverc_tar_reader_t, v3389_tar_reader_t, pos);

_Static_assert(sizeof(neverc_tar_writer_t) == sizeof(v3389_tar_writer_t),
               "neverc_tar_writer_t v3389 size changed");
_Static_assert(_Alignof(neverc_tar_writer_t) ==
                   _Alignof(v3389_tar_writer_t),
               "neverc_tar_writer_t v3389 alignment changed");
ABI_FIELD(neverc_tar_writer_t, v3389_tar_writer_t, data);
ABI_FIELD(neverc_tar_writer_t, v3389_tar_writer_t, len);
ABI_FIELD(neverc_tar_writer_t, v3389_tar_writer_t, cap);

#undef ABI_FIELD

static int failures;

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n", __LINE__,    \
                    #condition);                                             \
            failures++;                                                      \
        }                                                                    \
    } while (0)

static int canary_ok(const uint8_t *canary, size_t size) {
    for (size_t i = 0; i < size; i++)
        if (canary[i] != 0xa5) return 0;
    return 1;
}

static void fill_repeated_path(char *name, size_t prefix, size_t suffix) {
    memset(name, 'p', prefix);
    name[prefix] = '/';
    memset(name + prefix + 1U, 'n', suffix);
    name[prefix + 1U + suffix] = '\0';
}

static void test_legacy_capacity_and_writer_trailer(void) {
    struct {
        neverc_tar_writer_t value;
        uint8_t canary[32];
    } writer;
    struct {
        neverc_tar_header_t value;
        uint8_t canary[32];
    } header;
    uint8_t body[5000];

    memset(&writer, 0, sizeof(writer));
    memset(writer.canary, 0xa5, sizeof(writer.canary));
    memset(&header, 0, sizeof(header));
    memset(header.canary, 0xa5, sizeof(header.canary));
    memset(body, 0x5a, sizeof(body));
    fill_repeated_path(header.value.name, 154, 100); /* 255 bytes */
    memset(header.value.linkname, 'l', 99);
    header.value.linkname[99] = '\0';
    memset(header.value.uname, 'u', 31);
    header.value.uname[31] = '\0';
    memset(header.value.gname, 'g', 31);
    header.value.gname[31] = '\0';
    header.value.size = (int64_t)sizeof(body);
    header.value.mode = 0644;
    header.value.typeflag = NEVERC_TAR_REG;

    neverc_tar_writer_init(&writer.value);
    CHECK(neverc_tar_writer_write_header(&writer.value, &header.value) == 0);
    CHECK(neverc_tar_writer_write(&writer.value, body, 1234) == 0);
    CHECK(neverc_tar_writer_close(&writer.value) == -1);
    CHECK(neverc_tar_writer_write(
              &writer.value, body + 1234, sizeof(body) - 1234) == 0);
    CHECK(neverc_tar_writer_close(&writer.value) == 0);
    CHECK(neverc_tar_writer_close(&writer.value) == 0);
    CHECK(writer.value.cap > 4096);
    CHECK(canary_ok(writer.canary, sizeof(writer.canary)));
    CHECK(canary_ok(header.canary, sizeof(header.canary)));
    neverc_tar_reader_t reader;
    neverc_tar_header_t decoded = {0};
    neverc_tar_reader_init(&reader, writer.value.data, writer.value.len);
    CHECK(neverc_tar_reader_next(&reader, &decoded) == 1);
    CHECK(strlen(decoded.name) == 255);
    CHECK(strlen(decoded.linkname) == 99);
    CHECK(strlen(decoded.uname) == 31);
    CHECK(strlen(decoded.gname) == 31);
    neverc_tar_writer_free(&writer.value);
    CHECK(canary_ok(writer.canary, sizeof(writer.canary)));
}

static void test_reader_replay_and_canaries(void) {
    neverc_tar_writer_t writer;
    neverc_tar_header_t first = {0};
    neverc_tar_header_t second = {0};
    uint8_t first_body[700];
    memset(first_body, 'a', sizeof(first_body));
    strcpy(first.name, "first");
    first.size = (int64_t)sizeof(first_body);
    first.mode = 0600;
    first.typeflag = NEVERC_TAR_REG;
    strcpy(second.name, "second");
    second.mode = 0644;
    second.typeflag = NEVERC_TAR_REG;

    neverc_tar_writer_init(&writer);
    CHECK(neverc_tar_writer_write_header(&writer, &first) == 0);
    CHECK(neverc_tar_writer_write(&writer, first_body, sizeof(first_body)) == 0);
    CHECK(neverc_tar_writer_write_header(&writer, &second) == 0);
    CHECK(neverc_tar_writer_close(&writer) == 0);

    struct {
        neverc_tar_reader_t value;
        uint8_t canary[32];
    } reader;
    struct {
        neverc_tar_header_t value;
        uint8_t canary[32];
    } decoded;
    memset(&reader, 0, sizeof(reader));
    memset(reader.canary, 0xa5, sizeof(reader.canary));
    memset(&decoded, 0, sizeof(decoded));
    memset(decoded.canary, 0xa5, sizeof(decoded.canary));
    neverc_tar_reader_init(&reader.value, writer.data, writer.len);
    CHECK(neverc_tar_reader_next(&reader.value, &decoded.value) == 1);
    uint8_t chunk[37];
    size_t count = 0;
    CHECK(neverc_tar_reader_read(
              &reader.value, &decoded.value, chunk, sizeof(chunk), &count) == 0);
    CHECK(count == sizeof(chunk) && chunk[0] == 'a');
    CHECK(neverc_tar_reader_next(&reader.value, &decoded.value) == 1);
    CHECK(strcmp(decoded.value.name, "second") == 0);
    CHECK(neverc_tar_reader_next(&reader.value, &decoded.value) == 0);
    CHECK(neverc_tar_reader_next(&reader.value, &decoded.value) == 0);
    CHECK(canary_ok(reader.canary, sizeof(reader.canary)));
    CHECK(canary_ok(decoded.canary, sizeof(decoded.canary)));
    neverc_tar_writer_free(&writer);
}

static void test_v2_full_width_boundary(void) {
    neverc_tar_writer_t writer;
    neverc_tar_header_v2_t full = {0};
    neverc_tar_header_v2_t decoded = {0};
    fill_repeated_path(full.name, 155, 100); /* 256 bytes */
    memset(full.linkname, 'l', 100);
    full.linkname[100] = '\0';
    memset(full.uname, 'u', 32);
    full.uname[32] = '\0';
    memset(full.gname, 'g', 32);
    full.gname[32] = '\0';
    full.size = 3;
    full.mode = 0644;
    full.typeflag = NEVERC_TAR_REG;

    neverc_tar_writer_init(&writer);
    CHECK(neverc_tar_writer_write_header_v2(&writer, &full) == 0);
    CHECK(neverc_tar_writer_write(
              &writer, (const uint8_t *)"abc", 3) == 0);
    CHECK(neverc_tar_writer_close(&writer) == 0);

    struct {
        neverc_tar_header_t value;
        uint8_t canary[32];
    } legacy = {{0}, {0}};
    memset(legacy.canary, 0xa5, sizeof(legacy.canary));
    neverc_tar_reader_t reader;
    neverc_tar_reader_init(&reader, writer.data, writer.len);
    CHECK(neverc_tar_reader_next(&reader, &legacy.value) == -1);
    CHECK(canary_ok(legacy.canary, sizeof(legacy.canary)));

    neverc_tar_reader_init(&reader, writer.data, writer.len);
    CHECK(neverc_tar_reader_next_v2(&reader, &decoded) == 1);
    CHECK(strlen(decoded.name) == 256);
    CHECK(strlen(decoded.linkname) == 100);
    CHECK(strlen(decoded.uname) == 32);
    CHECK(strlen(decoded.gname) == 32);
    CHECK(strcmp(decoded.name, full.name) == 0);
    CHECK(strcmp(decoded.linkname, full.linkname) == 0);
    uint8_t body[4] = {0};
    size_t count = 0;
    CHECK(neverc_tar_reader_read_v2(
              &reader, &decoded, body, sizeof(body), &count) == 0);
    CHECK(count == 3 && memcmp(body, "abc", 3) == 0);
    neverc_tar_writer_free(&writer);

    neverc_tar_header_t unterminated;
    memset(&unterminated, 'x', sizeof(unterminated));
    unterminated.size = 0;
    unterminated.mode = 0644;
    unterminated.mtime = 0;
    unterminated.typeflag = NEVERC_TAR_REG;
    neverc_tar_writer_init(&writer);
    CHECK(neverc_tar_writer_write_header(&writer, &unterminated) == -1);
    neverc_tar_writer_free(&writer);
}

int main(void) {
    test_legacy_capacity_and_writer_trailer();
    test_reader_replay_and_canaries();
    test_v2_full_width_boundary();
    if (failures == 0) puts("passed");
    return failures == 0 ? 0 : 1;
}
