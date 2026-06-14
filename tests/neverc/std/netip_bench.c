/*
 * A/B benchmark + correctness check: net/netip address formatting.
 *
 *  - old_* — the previous library formatters, reproduced verbatim: every
 *      component (each IPv4 octet, each IPv6 hex group, every ':'/'::'/'%'
 *      separator, the port and prefix bits) went through its own snprintf
 *      call. snprintf re-parses the format string and runs locale machinery
 *      on each call, so an IPv6 address could cost a dozen-plus snprintf calls.
 *
 *  - neverc_netip_*_string (library) — the new formatters: a single pass with
 *      hand-written decimal/hex emitters into a stack buffer, then one copy out.
 *
 * The fast path is behavior-preserving, so the output is asserted byte-for-byte
 * identical to the old output across an exhaustive sweep (including every
 * IPv6 zero-run start/length, which drives the "::" selection) before timing.
 *
 * Build:
 *   cc -O2 -std=c11 -Wall -Wextra -I std/include -o /tmp/netip_bench \
 *      tests/neverc/std/netip_bench.c std/src/net/netip/netip.c
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#include "neverc/std/net/netip.h"

/* Timing iteration count; override (e.g. -DBENCH_ITERS=50000) for fast
 * sanitizer runs where only the correctness sweeps matter. */
#ifndef BENCH_ITERS
#define BENCH_ITERS 4000000
#endif

/* ============================================================
 * OLD formatters — verbatim reproduction of the previous library
 * ============================================================ */
static int old_addr_string(const neverc_netip_addr_t *addr, char *buf, size_t cap) {
    if (!addr || !buf || !addr->valid) return -1;
    if (addr->is_v4) {
        return snprintf(buf, cap, "%d.%d.%d.%d",
                        addr->addr[12], addr->addr[13], addr->addr[14], addr->addr[15]);
    }
    uint16_t groups[8];
    for (int i = 0; i < 8; i++)
        groups[i] = (uint16_t)((addr->addr[i*2] << 8) | addr->addr[i*2+1]);

    int best_start = -1, best_len = 0, cur_start = -1, cur_len = 0;
    for (int i = 0; i < 8; i++) {
        if (groups[i] == 0) {
            if (cur_start < 0) cur_start = i;
            cur_len++;
        } else {
            if (cur_len > best_len && cur_len >= 2) { best_start = cur_start; best_len = cur_len; }
            cur_start = -1; cur_len = 0;
        }
    }
    if (cur_len > best_len && cur_len >= 2) { best_start = cur_start; best_len = cur_len; }

    int pos = 0;
    for (int i = 0; i < 8; i++) {
        if (i == best_start) {
            pos += snprintf(buf + pos, cap - (size_t)pos, "::");
            i += best_len - 1;
            continue;
        }
        if (i > 0 && i != best_start + best_len) pos += snprintf(buf + pos, cap - (size_t)pos, ":");
        pos += snprintf(buf + pos, cap - (size_t)pos, "%x", groups[i]);
    }
    if (addr->zone[0]) pos += snprintf(buf + pos, cap - (size_t)pos, "%%%s", addr->zone);
    return pos;
}

static int old_addrport_string(const neverc_netip_addrport_t *ap, char *buf, size_t cap) {
    if (!ap || !buf) return -1;
    char addrbuf[128];
    old_addr_string(&ap->addr, addrbuf, sizeof(addrbuf));
    if (ap->addr.is_v4)
        return snprintf(buf, cap, "%s:%u", addrbuf, ap->port);
    else
        return snprintf(buf, cap, "[%s]:%u", addrbuf, ap->port);
}

static int old_prefix_string(const neverc_netip_prefix_t *pfx, char *buf, size_t cap) {
    if (!pfx || !buf || !pfx->valid) return -1;
    char addrbuf[128];
    old_addr_string(&pfx->addr, addrbuf, sizeof(addrbuf));
    return snprintf(buf, cap, "%s/%u", addrbuf, pfx->bits);
}

/* ============================================================
 * Helpers
 * ============================================================ */
static double now_sec(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}
static volatile int sink;

static uint64_t rng = 0x123456789abcdef0ULL;
static uint32_t xr(void) {
    rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
    return (uint32_t)(rng >> 32);
}

static void mk4(neverc_netip_addr_t *a, uint8_t b0, uint8_t b1, uint8_t b2, uint8_t b3) {
    memset(a, 0, sizeof(*a));
    a->is_v4 = 1; a->valid = 1;
    a->addr[10] = 0xff; a->addr[11] = 0xff;
    a->addr[12] = b0; a->addr[13] = b1; a->addr[14] = b2; a->addr[15] = b3;
}

static void mk6(neverc_netip_addr_t *a, const uint16_t g[8], const char *zone) {
    memset(a, 0, sizeof(*a));
    a->is_v4 = 0; a->valid = 1;
    for (int i = 0; i < 8; i++) {
        a->addr[i*2]   = (uint8_t)(g[i] >> 8);
        a->addr[i*2+1] = (uint8_t)(g[i] & 0xff);
    }
    if (zone) { strncpy(a->zone, zone, sizeof(a->zone) - 1); a->zone[sizeof(a->zone)-1] = '\0'; }
}

/* ============================================================
 * Correctness sweeps (must be byte-for-byte identical)
 * ============================================================ */
static int sweep_v4(void) {
    int mism = 0; char ob[64], nb[64];
    for (int i = 0; i < 600000; i++) {
        neverc_netip_addr_t a;
        mk4(&a, (uint8_t)xr(), (uint8_t)xr(), (uint8_t)xr(), (uint8_t)xr());
        int o = old_addr_string(&a, ob, sizeof ob);
        int n = neverc_netip_addr_string(&a, nb, sizeof nb);
        if (o != n || strcmp(ob, nb) != 0) {
            if (mism < 5) printf("  V4 MISMATCH old=\"%s\"(%d) new=\"%s\"(%d)\n", ob, o, nb, n);
            mism++;
        }
    }
    return mism;
}

/* Exhaustive over the "::"-driving zero-run shape: every (start,len) with the
 * run forced to zero and all other groups guaranteed non-zero. */
static int sweep_v6_runs(void) {
    int mism = 0; char ob[128], nb[128];
    for (int start = 0; start < 8; start++) {
        for (int len = 1; start + len <= 8; len++) {
            for (int trial = 0; trial < 3000; trial++) {
                uint16_t g[8];
                for (int i = 0; i < 8; i++) g[i] = (uint16_t)(xr() | 1u);
                for (int i = start; i < start + len; i++) g[i] = 0;
                neverc_netip_addr_t a; mk6(&a, g, NULL);
                int o = old_addr_string(&a, ob, sizeof ob);
                int n = neverc_netip_addr_string(&a, nb, sizeof nb);
                if (o != n || strcmp(ob, nb) != 0) {
                    if (mism < 5) printf("  V6RUN MISMATCH (start=%d len=%d) old=\"%s\" new=\"%s\"\n",
                                         start, len, ob, nb);
                    mism++;
                }
            }
        }
    }
    return mism;
}

static int sweep_v6_rand(void) {
    int mism = 0; char ob[128], nb[128];
    const char *zones[] = { NULL, "eth0", "1", "wlan0", "en0", "verylongzonename1234567890" };
    for (int i = 0; i < 600000; i++) {
        uint16_t g[8];
        for (int k = 0; k < 8; k++) g[k] = (uint16_t)xr();   /* incidental zeros allowed */
        const char *z = zones[xr() % (sizeof(zones)/sizeof(zones[0]))];
        neverc_netip_addr_t a; mk6(&a, g, z);
        int o = old_addr_string(&a, ob, sizeof ob);
        int n = neverc_netip_addr_string(&a, nb, sizeof nb);
        if (o != n || strcmp(ob, nb) != 0) {
            if (mism < 5) printf("  V6 MISMATCH old=\"%s\"(%d) new=\"%s\"(%d)\n", ob, o, nb, n);
            mism++;
        }
    }
    return mism;
}

static int sweep_addrport(void) {
    int mism = 0; char ob[160], nb[160];
    for (int i = 0; i < 400000; i++) {
        neverc_netip_addrport_t ap; memset(&ap, 0, sizeof ap);
        if (xr() & 1) {
            mk4(&ap.addr, (uint8_t)xr(), (uint8_t)xr(), (uint8_t)xr(), (uint8_t)xr());
        } else {
            uint16_t g[8];
            for (int k = 0; k < 8; k++) g[k] = (uint16_t)xr();
            mk6(&ap.addr, g, (xr() & 3) == 0 ? "eth0" : NULL);
        }
        ap.port = (uint16_t)xr();
        int o = old_addrport_string(&ap, ob, sizeof ob);
        int n = neverc_netip_addrport_string(&ap, nb, sizeof nb);
        if (o != n || strcmp(ob, nb) != 0) {
            if (mism < 5) printf("  ADDRPORT MISMATCH old=\"%s\"(%d) new=\"%s\"(%d)\n", ob, o, nb, n);
            mism++;
        }
    }
    return mism;
}

static int sweep_prefix(void) {
    int mism = 0; char ob[160], nb[160];
    for (int i = 0; i < 400000; i++) {
        neverc_netip_prefix_t pfx; memset(&pfx, 0, sizeof pfx);
        pfx.valid = 1;
        if (xr() & 1) {
            mk4(&pfx.addr, (uint8_t)xr(), (uint8_t)xr(), (uint8_t)xr(), (uint8_t)xr());
        } else {
            uint16_t g[8];
            for (int k = 0; k < 8; k++) g[k] = (uint16_t)xr();
            mk6(&pfx.addr, g, NULL);
        }
        pfx.bits = (uint8_t)xr();   /* full 0..255 range to exercise the emitter */
        int o = old_prefix_string(&pfx, ob, sizeof ob);
        int n = neverc_netip_prefix_string(&pfx, nb, sizeof nb);
        if (o != n || strcmp(ob, nb) != 0) {
            if (mism < 5) printf("  PREFIX MISMATCH old=\"%s\"(%d) new=\"%s\"(%d)\n", ob, o, nb, n);
            mism++;
        }
    }
    return mism;
}

/*
 * cap/truncation parity, measured against canonical snprintf("%s") semantics
 * (NOT against the old library formatter: the old IPv6 path computed
 * snprintf(buf+pos, cap - pos, ...), and once pos > cap the size_t subtraction
 * underflows, letting later writes run past `cap` — an out-of-bounds write the
 * new formatter eliminates). Here we verify the new formatter (a) reports the
 * full length, (b) truncates exactly like snprintf, and (c) never writes at or
 * beyond index `cap`.
 */
static int sweep_cap(void) {
    int mism = 0;
    neverc_netip_addr_t a;
    uint16_t g[8] = { 0x2001, 0x0db8, 0x85a3, 0, 0, 0x8a2e, 0x0370, 0x7334 };
    mk6(&a, g, NULL);

    char full[64];
    int flen = neverc_netip_addr_string(&a, full, sizeof full);

    for (size_t cap = 0; cap <= 40; cap++) {
        char ref[64], nb[64];
        memset(ref, 0x7f, sizeof ref); memset(nb, 0x7f, sizeof nb);
        int r = snprintf(ref, cap, "%s", full);
        int n = neverc_netip_addr_string(&a, nb, cap);
        if (r != n || n != flen) {
            if (mism < 6) printf("  CAP RET MISMATCH cap=%zu ref=%d new=%d flen=%d\n", cap, r, n, flen);
            mism++;
            continue;
        }
        if (memcmp(ref, nb, sizeof ref) != 0) {
            if (mism < 6) printf("  CAP BUF MISMATCH cap=%zu ref=\"%s\" new=\"%s\"\n", cap, ref, nb);
            mism++;
        }
    }
    return mism;
}

/* ============================================================
 * Timing
 * ============================================================ */
static void bench_addr(const char *label, const neverc_netip_addr_t *a) {
    char ob[128], nb[128];
    int o = old_addr_string(a, ob, sizeof ob);
    int n = neverc_netip_addr_string(a, nb, sizeof nb);
    if (o != n || strcmp(ob, nb) != 0) {
        printf("%-20s  CORRECTNESS FAIL (old=\"%s\" new=\"%s\")\n", label, ob, nb);
        return;
    }
    const int iters = BENCH_ITERS;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink = old_addr_string(a, ob, sizeof ob); }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink = neverc_netip_addr_string(a, nb, sizeof nb); }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-20s  %8.1f ms  %8.1f ms  %6.2fx   (\"%s\")\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, nb);
}

static void bench_addrport(const char *label, const neverc_netip_addrport_t *ap) {
    char ob[160], nb[160];
    int o = old_addrport_string(ap, ob, sizeof ob);
    int n = neverc_netip_addrport_string(ap, nb, sizeof nb);
    if (o != n || strcmp(ob, nb) != 0) {
        printf("%-20s  CORRECTNESS FAIL (old=\"%s\" new=\"%s\")\n", label, ob, nb);
        return;
    }
    const int iters = BENCH_ITERS;
    double t_old = 1e30, t_new = 1e30;
    for (int rep = 0; rep < 5; rep++) {
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink = old_addrport_string(ap, ob, sizeof ob); }
        double e = now_sec() - t0; if (e < t_old) t_old = e;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) { sink = neverc_netip_addrport_string(ap, nb, sizeof nb); }
        e = now_sec() - t0; if (e < t_new) t_new = e;
    }
    printf("%-20s  %8.1f ms  %8.1f ms  %6.2fx   (\"%s\")\n",
           label, t_old * 1000, t_new * 1000, t_old / t_new, nb);
}

int main(void) {
    printf("=== netip formatting: hand-written emitters (new) vs per-field snprintf (old) ===\n\n");

    printf("--- correctness sweeps ---\n");
    int total = 0, m;
    m = sweep_v4();       total += m; printf("  IPv4 random       : %s (%d mismatches)\n", m ? "FAIL" : "OK", m);
    m = sweep_v6_runs();  total += m; printf("  IPv6 zero-runs    : %s (%d mismatches)\n", m ? "FAIL" : "OK", m);
    m = sweep_v6_rand();  total += m; printf("  IPv6 random+zone  : %s (%d mismatches)\n", m ? "FAIL" : "OK", m);
    m = sweep_addrport(); total += m; printf("  AddrPort          : %s (%d mismatches)\n", m ? "FAIL" : "OK", m);
    m = sweep_prefix();   total += m; printf("  Prefix            : %s (%d mismatches)\n", m ? "FAIL" : "OK", m);
    m = sweep_cap();      total += m; printf("  cap/truncation    : %s (%d mismatches)\n", m ? "FAIL" : "OK", m);
    printf("  => %s\n\n", total ? "CORRECTNESS FAILED" : "all identical");

    printf("--- timing ---\n");
    printf("%-20s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    neverc_netip_addr_t a;
    mk4(&a, 192, 168, 1, 1);                                      bench_addr("ipv4", &a);
    mk4(&a, 255, 255, 255, 255);                                  bench_addr("ipv4_max", &a);

    uint16_t full[8] = { 0x2001, 0x0db8, 0x85a3, 0x1, 0x2, 0x3, 0x4, 0x5678 };
    mk6(&a, full, NULL);                                          bench_addr("ipv6_full", &a);

    uint16_t comp[8] = { 0x2001, 0x0db8, 0, 0, 0, 0, 0, 0x1 };
    mk6(&a, comp, NULL);                                          bench_addr("ipv6_compressed", &a);

    uint16_t lo[8] = { 0, 0, 0, 0, 0, 0, 0, 1 };
    mk6(&a, lo, NULL);                                            bench_addr("ipv6_loopback", &a);

    uint16_t zoned[8] = { 0xfe80, 0, 0, 0, 0, 0, 0, 0x1 };
    mk6(&a, zoned, "eth0");                                       bench_addr("ipv6_zone", &a);

    neverc_netip_addrport_t ap; memset(&ap, 0, sizeof ap);
    mk4(&ap.addr, 192, 168, 1, 1); ap.port = 8080;               bench_addrport("ipv4:port", &ap);
    mk6(&ap.addr, comp, NULL); ap.port = 443;                    bench_addrport("[ipv6]:port", &ap);

    printf("\n=== Done ===\n");
    return total ? 1 : 0;
}
