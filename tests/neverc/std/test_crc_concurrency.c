#include "neverc/std/hash/crc32.h"
#include "neverc/std/hash/crc64.h"
#include <pthread.h>
#include <stdio.h>
#include <stdint.h>

/* Regression: slicing-8 used a shared mutable cache; concurrent update/checksum
 * with different polynomials produced wrong checksums. Exercises len >= 64. */
static const char MSG[] =
    "The quick brown fox jumps over the lazy dog -- repeated to exceed "
    "sixty-four bytes so the slicing-by-8 code path is exercised here.";
static const size_t MSGLEN = sizeof(MSG) - 1;

#define ITERS 100000
#define NTHREAD 8

static uint32_t exp_ieee, exp_cast;
static uint64_t exp_iso, exp_ecma;
static int start_flag;

/* Each worker also owns a distinct polynomial, so the threads race to publish
 * slicing-8 cache slots and, with more tables than slots, some of them take
 * the full-cache paths (byte loop for MSG, private tables for LONGMSG). */
static uint8_t LONGMSG[1024];
static uint32_t exp_own32[NTHREAD], exp_own32_long[NTHREAD];
static uint64_t exp_own64[NTHREAD], exp_own64_long[NTHREAD];

static uint32_t own_poly32(long id) {
    return 0x80000001u ^ (0x9E3779B9u * (uint32_t)(id + 1));
}

static uint64_t own_poly64(long id) {
    return 0x8000000000000001ULL ^ (0x9E3779B97F4A7C15ULL * (uint64_t)(id + 1));
}

/* Chunks of 32 stay on the byte path (len < 64), so this must not publish
 * the process-lifetime slicing-8 cache. Expected values are therefore
 * computed without warming the tables the workers will race to init. */
static uint32_t crc32_chunked(const neverc_crc32_table_t table,
                              const void *data, size_t len) {
    const uint8_t *p = (const uint8_t *)data;
    uint32_t crc = 0;
    while (len > 0) {
        size_t n = len > 32 ? 32 : len;
        crc = neverc_crc32_update(crc, table, p, n);
        p += n;
        len -= n;
    }
    return crc;
}

static uint64_t crc64_chunked(const neverc_crc64_table_t table,
                              const uint8_t *data, size_t len) {
    uint64_t crc = 0;
    while (len > 0) {
        size_t n = len > 32 ? 32 : len;
        crc = neverc_crc64_update(crc, table, data, n);
        data += n;
        len -= n;
    }
    return crc;
}

static void *worker(void *arg) {
    long id = (long)arg;
    neverc_crc32_table_t t_ieee, t_cast;
    neverc_crc32_make_table(NEVERC_CRC32_IEEE, t_ieee);
    neverc_crc32_make_table(NEVERC_CRC32_CASTAGNOLI, t_cast);
    neverc_crc64_table_t t_iso, t_ecma;
    neverc_crc64_make_table(NEVERC_CRC64_ISO, t_iso);
    neverc_crc64_make_table(NEVERC_CRC64_ECMA, t_ecma);
    neverc_crc32_table_t t_own32;
    neverc_crc64_table_t t_own64;
    neverc_crc32_make_table(own_poly32(id), t_own32);
    neverc_crc64_make_table(own_poly64(id), t_own64);

    while (!__atomic_load_n(&start_flag, __ATOMIC_ACQUIRE)) { }

    long ok = 1;
    for (int i = 0; i < ITERS; i++) {
        if (id & 1) {
            if (neverc_crc32_checksum(t_ieee, (const uint8_t *)MSG, MSGLEN) != exp_ieee) { ok = 0; break; }
            if (neverc_crc64_checksum(t_iso, (const uint8_t *)MSG, MSGLEN) != exp_iso) { ok = 0; break; }
        } else {
            if (neverc_crc32_checksum(t_cast, (const uint8_t *)MSG, MSGLEN) != exp_cast) { ok = 0; break; }
            if (neverc_crc64_checksum(t_ecma, (const uint8_t *)MSG, MSGLEN) != exp_ecma) { ok = 0; break; }
        }
        if ((i & 7) == 0)
            if (neverc_crc32_ieee(MSG, MSGLEN) != exp_ieee) { ok = 0; break; }
        if ((i & 15) == 0) {
            if (neverc_crc32_checksum(t_own32, MSG, MSGLEN) != exp_own32[id] ||
                neverc_crc32_checksum(t_own32, LONGMSG, sizeof LONGMSG) != exp_own32_long[id] ||
                neverc_crc64_checksum(t_own64, (const uint8_t *)MSG, MSGLEN) != exp_own64[id] ||
                neverc_crc64_checksum(t_own64, LONGMSG, sizeof LONGMSG) != exp_own64_long[id]) {
                ok = 0;
                break;
            }
        }
    }
    return (void *)ok;
}

int main(void) {
    neverc_crc32_table_t t_ieee, t_cast;
    neverc_crc32_make_table(NEVERC_CRC32_IEEE, t_ieee);
    neverc_crc32_make_table(NEVERC_CRC32_CASTAGNOLI, t_cast);
    neverc_crc64_table_t t_iso, t_ecma;
    neverc_crc64_make_table(NEVERC_CRC64_ISO, t_iso);
    neverc_crc64_make_table(NEVERC_CRC64_ECMA, t_ecma);
    exp_ieee = crc32_chunked(t_ieee, MSG, MSGLEN);
    exp_cast = crc32_chunked(t_cast, MSG, MSGLEN);
    exp_iso  = crc64_chunked(t_iso, (const uint8_t *)MSG, MSGLEN);
    exp_ecma = crc64_chunked(t_ecma, (const uint8_t *)MSG, MSGLEN);
    for (size_t i = 0; i < sizeof LONGMSG; i++)
        LONGMSG[i] = (uint8_t)(i * 131u + 7u);
    for (long id = 0; id < NTHREAD; id++) {
        neverc_crc32_table_t t32;
        neverc_crc64_table_t t64;
        neverc_crc32_make_table(own_poly32(id), t32);
        neverc_crc64_make_table(own_poly64(id), t64);
        exp_own32[id] = crc32_chunked(t32, MSG, MSGLEN);
        exp_own32_long[id] = crc32_chunked(t32, LONGMSG, sizeof LONGMSG);
        exp_own64[id] = crc64_chunked(t64, (const uint8_t *)MSG, MSGLEN);
        exp_own64_long[id] = crc64_chunked(t64, LONGMSG, sizeof LONGMSG);
    }

    pthread_t th[NTHREAD];
    for (long i = 0; i < NTHREAD; i++)
        pthread_create(&th[i], NULL, worker, (void *)i);
    __atomic_store_n(&start_flag, 1, __ATOMIC_RELEASE);

    long all_ok = 1;
    for (int i = 0; i < NTHREAD; i++) {
        void *r;
        pthread_join(th[i], &r);
        if (!(long)r) all_ok = 0;
    }
    printf("crc concurrency (%d threads x %d iters): %s\n",
           NTHREAD, ITERS, all_ok ? "passed" : "FAILED");
    return all_ok ? 0 : 1;
}
