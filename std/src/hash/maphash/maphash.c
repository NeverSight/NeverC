/*
 * NeverC hash/maphash — fast non-cryptographic hash for hash tables.
 * Mirrors Go hash/maphash.
 *
 * Uses wyhash final v3 internally — passes SMHasher, excellent distribution,
 * and very fast on modern hardware.
 */

#include "neverc/std/hash/maphash.h"
#include "neverc/std/_platform.h"
#include "../_wyhash_final3.h"
#include <string.h>

#ifndef NCI_MAPHASH_RANDOM
#define NCI_MAPHASH_RANDOM neverc_platform_random
#endif

/* ---- seed generation ---- */

uint64_t neverc_maphash_make_seed(void) {
    uint64_t seed = 0;
    do {
        if (NCI_MAPHASH_RANDOM((unsigned char *)&seed, sizeof(seed)) != 0)
            return 0;
    } while (seed == 0);
    return seed;
}

/* ---- streaming API ---- */

void neverc_maphash_init(neverc_maphash_t *h, uint64_t seed) {
    if (!h) return;
    h->seed = seed;
    h->state = h->seed;
    h->n = 0;
    h->used = 0;
}

void neverc_maphash_reset(neverc_maphash_t *h) {
    if (!h) return;
    h->state = h->seed;
    h->n = 0;
    h->used = 0;
}

static void maphash_flush(neverc_maphash_t *h) {
    h->state = nci_wyhash_final3(h->buf, (size_t)h->n, h->state);
    h->n = 0;
}

size_t neverc_maphash_write_byte(neverc_maphash_t *h, uint8_t b) {
    if (!h) return 0;
    if (h->n == NEVERC_MAPHASH_BUF_SIZE) maphash_flush(h);
    h->buf[h->n++] = b;
    h->used = 1;
    return 1;
}

size_t neverc_maphash_write(neverc_maphash_t *h, const void *data, size_t len) {
    if (!h) return 0;
    if (len == 0) return 0;
    if (!data) return 0;
    const uint8_t *p = (const uint8_t *)data;
    size_t remaining = len;
    h->used = 1;

    if (h->n > 0) {
        size_t space = (size_t)(NEVERC_MAPHASH_BUF_SIZE - h->n);
        size_t k = remaining < space ? remaining : space;
        memcpy(h->buf + h->n, p, k);
        h->n += (int)k;
        if (h->n < NEVERC_MAPHASH_BUF_SIZE) return len;
        p += k;
        remaining -= k;
        maphash_flush(h);
    }

    while (remaining > NEVERC_MAPHASH_BUF_SIZE) {
        h->state = nci_wyhash_final3(p, NEVERC_MAPHASH_BUF_SIZE, h->state);
        p += NEVERC_MAPHASH_BUF_SIZE;
        remaining -= NEVERC_MAPHASH_BUF_SIZE;
    }
    memcpy(h->buf, p, remaining);
    h->n = (int)remaining;
    return len;
}

size_t neverc_maphash_write_string(neverc_maphash_t *h, const char *s) {
    if (!s) return 0;
    return neverc_maphash_write(h, s, strlen(s));
}

uint64_t neverc_maphash_sum64(const neverc_maphash_t *h) {
    if (!h) return 0;
    /* Empty input still mixes the seed so the digest does not leak it.
     * After a full-buffer flush, n==0 but used==1: state already holds the mix. */
    if (!h->used)
        return nci_wyhash_final3(NULL, 0, h->seed);
    if (h->n == 0) return h->state;
    return nci_wyhash_final3(h->buf, (size_t)h->n, h->state);
}

/* ---- one-shot convenience ---- */

uint64_t neverc_maphash_bytes(uint64_t seed, const void *data, size_t len) {
    if (!data) len = 0;
    const uint8_t *p = (const uint8_t *)data;
    uint64_t state = seed;
    while (len > NEVERC_MAPHASH_BUF_SIZE) {
        state = nci_wyhash_final3(p, NEVERC_MAPHASH_BUF_SIZE, state);
        p += NEVERC_MAPHASH_BUF_SIZE;
        len -= NEVERC_MAPHASH_BUF_SIZE;
    }
    return nci_wyhash_final3(p, len, state);
}

uint64_t neverc_maphash_string(uint64_t seed, const char *s) {
    return neverc_maphash_bytes(seed, s, s ? strlen(s) : 0);
}
