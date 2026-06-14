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

static void *worker(void *arg) {
    long id = (long)arg;
    neverc_crc32_table_t t_ieee, t_cast;
    neverc_crc32_make_table(NEVERC_CRC32_IEEE, t_ieee);
    neverc_crc32_make_table(NEVERC_CRC32_CASTAGNOLI, t_cast);
    neverc_crc64_table_t t_iso, t_ecma;
    neverc_crc64_make_table(NEVERC_CRC64_ISO, t_iso);
    neverc_crc64_make_table(NEVERC_CRC64_ECMA, t_ecma);

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
    exp_ieee = neverc_crc32_checksum(t_ieee, (const uint8_t *)MSG, MSGLEN);
    exp_cast = neverc_crc32_checksum(t_cast, (const uint8_t *)MSG, MSGLEN);
    exp_iso  = neverc_crc64_checksum(t_iso, (const uint8_t *)MSG, MSGLEN);
    exp_ecma = neverc_crc64_checksum(t_ecma, (const uint8_t *)MSG, MSGLEN);

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
