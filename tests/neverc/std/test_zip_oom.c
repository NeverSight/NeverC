#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "neverc/std/hash/crc32.h"

static size_t allocation_count;
static size_t fail_at;
static size_t crc_bytes_read;

static uint32_t measured_crc32_ieee(const uint8_t *data, size_t len) {
    if (len > SIZE_MAX - crc_bytes_read)
        crc_bytes_read = SIZE_MAX;
    else
        crc_bytes_read += len;
    return neverc_crc32_ieee(data, len);
}

static int allocation_fails(void) {
    allocation_count++;
    return fail_at != 0 && allocation_count == fail_at;
}

static void *controlled_malloc(size_t size) {
    return allocation_fails() ? NULL : malloc(size);
}

static void *controlled_realloc(void *ptr, size_t size) {
    return allocation_fails() ? NULL : realloc(ptr, size);
}

#define malloc controlled_malloc
#define realloc controlled_realloc
#define neverc_crc32_ieee measured_crc32_ieee
#include "../../../std/src/archive/zip/zip.c"
#undef neverc_crc32_ieee
#undef malloc
#undef realloc

#define CHECK(condition)                                                     \
    do {                                                                     \
        if (!(condition)) {                                                  \
            fprintf(stderr, "check failed at line %d: %s\n",              \
                    __LINE__, #condition);                                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

static void reset_allocator(size_t failure) {
    allocation_count = 0;
    fail_at = failure;
}

static uint32_t test_read32(const uint8_t *p) {
    return (uint32_t)p[0] |
           ((uint32_t)p[1] << 8U) |
           ((uint32_t)p[2] << 16U) |
           ((uint32_t)p[3] << 24U);
}

static void test_write16(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
}

static void test_write32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8U);
    p[2] = (uint8_t)(value >> 16U);
    p[3] = (uint8_t)(value >> 24U);
}

int main(void) {
    for (size_t failure = 1; failure <= 3; failure++) {
        reset_allocator(failure);
        neverc_zip_writer_t w;
        neverc_zip_writer_init(&w);
        int result = neverc_zip_writer_add(
            &w, "x", (const uint8_t *)"x", 1);
        CHECK(result == -1 || (result == 0 && w.nentries == 1));
        neverc_zip_writer_free(&w);
    }

    reset_allocator(0);
    neverc_zip_writer_t w;
    neverc_zip_writer_init(&w);
    uint8_t *large = (uint8_t *)malloc(8192);
    CHECK(large != NULL);
    memset(large, 'x', 8192);
    fail_at = allocation_count + 1;
    CHECK(neverc_zip_writer_add(&w, "large", large, 8192) == -1);
    CHECK(w.nentries == 0);
    fail_at = 0;
    free(large);
    neverc_zip_writer_free(&w);

    for (size_t relative_failure = 1; relative_failure <= 2;
         relative_failure++) {
        reset_allocator(0);
        neverc_zip_writer_init(&w);
        for (int i = 0; i < 16; i++)
            CHECK(neverc_zip_writer_add(&w, "x", (const uint8_t *)"x", 1) == 0);
        fail_at = allocation_count + relative_failure;
        CHECK(neverc_zip_writer_add(&w, "overflow",
                                    (const uint8_t *)"x", 1) == -1);
        CHECK(w.nentries == 16);
        fail_at = 0;
        neverc_zip_writer_free(&w);
    }

    reset_allocator(0);
    neverc_zip_writer_init(&w);
    CHECK(neverc_zip_writer_add(
              &w, "reader", (const uint8_t *)"data", 4) == 0);
    CHECK(neverc_zip_writer_close(&w) == 0);
    for (size_t failure = 1; failure <= 2; failure++) {
        reset_allocator(failure);
        neverc_zip_reader_t r;
        CHECK(neverc_zip_reader_init(&r, w.data, w.len) == -1);
        CHECK(allocation_count >= failure);
        neverc_zip_reader_free(&r);
    }
    fail_at = 0;
    neverc_zip_writer_free(&w);

    reset_allocator(0);
    neverc_zip_writer_init(&w);
    CHECK(neverc_zip_writer_add(&w, "a", (const uint8_t *)"x", 1) == 0);
    CHECK(neverc_zip_writer_add(&w, "b", (const uint8_t *)"y", 1) == 0);
    CHECK(neverc_zip_writer_close(&w) == 0);
    for (size_t failure = 1; failure <= 3; failure++) {
        reset_allocator(failure);
        neverc_zip_reader_t r;
        CHECK(neverc_zip_reader_init(&r, w.data, w.len) == -1);
        CHECK(allocation_count >= failure);
        neverc_zip_reader_free(&r);
    }
    fail_at = 0;
    neverc_zip_writer_free(&w);

    /* A central directory may repeat one valid local range many times.  The
     * archive is invalid regardless of the payload bytes, so overlap
     * rejection must run before CRC work; otherwise a small directory can
     * amplify hashing of one large payload once per duplicate entry. */
    reset_allocator(0);
    neverc_zip_writer_init(&w);
    enum { duplicate_entries = 64, payload_size = 4096 };
    uint8_t payload[payload_size];
    memset(payload, 'z', sizeof(payload));
    CHECK(neverc_zip_writer_add(&w, "z", payload, sizeof(payload)) == 0);
    CHECK(neverc_zip_writer_close(&w) == 0);
    CHECK(w.len >= 22U);
    size_t eocd = w.len - 22U;
    uint32_t central_offset = test_read32(w.data + eocd + 16U);
    uint32_t central_size = test_read32(w.data + eocd + 12U);
    CHECK(central_size > 0U);
    CHECK((uint64_t)central_offset + central_size <= eocd);
    size_t duplicate_size =
        (size_t)central_offset + (size_t)central_size * duplicate_entries + 22U;
    uint8_t *duplicate = (uint8_t *)malloc(duplicate_size);
    CHECK(duplicate != NULL);
    memcpy(duplicate, w.data, central_offset);
    for (size_t i = 0; i < duplicate_entries; i++) {
        memcpy(duplicate + central_offset + i * central_size,
               w.data + central_offset, central_size);
    }
    size_t duplicate_eocd = duplicate_size - 22U;
    memcpy(duplicate + duplicate_eocd, w.data + eocd, 22U);
    test_write16(duplicate + duplicate_eocd + 8U, duplicate_entries);
    test_write16(duplicate + duplicate_eocd + 10U, duplicate_entries);
    test_write32(duplicate + duplicate_eocd + 12U,
                 central_size * duplicate_entries);

    crc_bytes_read = 0;
    neverc_zip_reader_t duplicate_reader;
    CHECK(neverc_zip_reader_init(
              &duplicate_reader, duplicate, duplicate_size) == -1);
    if (crc_bytes_read != 0)
        fprintf(stderr, "overlapping ZIP hashed %zu payload bytes\n",
                crc_bytes_read);
    CHECK(crc_bytes_read == 0);
    neverc_zip_reader_free(&duplicate_reader);
    free(duplicate);
    neverc_zip_writer_free(&w);

    puts("passed");
    return 0;
}
