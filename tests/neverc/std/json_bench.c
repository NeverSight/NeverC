/*
 * Scaling demonstration for JSON parsing.
 *
 * Object keys: parse_object previously inserted each pair with
 * neverc_json_object_set, whose linear duplicate-key scan made parsing an n-key
 * object O(n^2). With the local O(1) key index, parsing is linear: time-per-key
 * stays roughly flat as the object grows (it grew linearly with n before).
 *
 * Strings: parse_string previously used a fixed 64 KiB stack buffer and copied
 * one byte at a time, so it OUTRIGHT REJECTED any string longer than ~64 KiB and
 * was slow on the rest. It now grows on demand and bulk-copies escape-free runs
 * with memchr+memcpy, so large strings parse at near-memcpy speed (low ns/byte).
 *
 * Build standalone:
 *   cc -O2 -I std/include json_bench.c std/src/encoding/json/json.c \
 *      std/src/strconv/format_float.c std/src/strconv/parse_float.c -o json_bench
 */
#include "neverc/std/encoding/json.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static volatile size_t sink;

/* OLD marshal_string: one bounds-checked store per byte (reproduced verbatim).
 * Writes a JSON-escaped quoted string into dst, returning the length or -1. */
static int old_marshal_string(char *dst, size_t cap, size_t pos, const char *s) {
#define OW1(ch)  do { if (pos >= cap) return -1; dst[pos++] = (ch); } while (0)
#define OWN(p,n) do { if (pos + (n) > cap) return -1; memcpy(dst + pos, (p), (n)); pos += (n); } while (0)
    OW1('"');
    for (const char *p = s; *p; p++) {
        switch (*p) {
            case '"':  OWN("\\\"", 2); break;
            case '\\': OWN("\\\\", 2); break;
            case '\b': OWN("\\b", 2);  break;
            case '\f': OWN("\\f", 2);  break;
            case '\n': OWN("\\n", 2);  break;
            case '\r': OWN("\\r", 2);  break;
            case '\t': OWN("\\t", 2);  break;
            default:
                if ((unsigned char)*p < 0x20) {
                    char esc[6];
                    esc[0]='\\'; esc[1]='u'; esc[2]='0'; esc[3]='0';
                    esc[4]="0123456789abcdef"[(*p>>4)&0xF];
                    esc[5]="0123456789abcdef"[*p&0xF];
                    OWN(esc, 6);
                } else OW1(*p);
        }
    }
    OW1('"');
    return (int)pos;
#undef OW1
#undef OWN
}

/* Marshal a single big string value: the library escapes by bulk-copying
 * escape-free runs; the old path stored one byte at a time. */
static void bench_marshal(void) {
    printf("\n=== JSON string marshal: bulk-copy (new) vs byte-at-a-time (old) ===\n");
    printf("%-26s  %10s  %10s  %8s\n", "case", "old", "new", "speedup");

    struct { const char *label; double esc_frac; } cases[] = {
        {"escape-free",   0.0},
        {"2% escapes",    0.02},
        {"20% escapes",   0.20},
    };
    size_t N = 262144;
    char *raw = (char *)malloc(N + 1);
    char *out = (char *)malloc(N * 6 + 16);

    for (int c = 0; c < 3; c++) {
        srand(7);
        for (size_t i = 0; i < N; i++) {
            int esc = ((double)rand() / RAND_MAX) < cases[c].esc_frac;
            raw[i] = esc ? "\"\\\n\t"[rand() & 3] : (char)('a' + (i % 26));
        }
        raw[N] = '\0';
        neverc_json_value_t *v = neverc_json_new_string(raw);

        int iters = 400; 
        double t0 = now_sec();
        for (int i = 0; i < iters; i++) sink = (size_t)old_marshal_string(out, N * 6 + 16, 0, raw);
        double t_old = now_sec() - t0;
        t0 = now_sec();
        for (int i = 0; i < iters; i++) sink = (size_t)neverc_json_marshal(v, out, N * 6 + 16, NULL);
        double t_new = now_sec() - t0;

        printf("%-26s  %8.2f ms  %8.2f ms  %6.2fx\n",
               cases[c].label, t_old * 1000, t_new * 1000, t_old / t_new);
        neverc_json_free(v);
    }
    free(raw); free(out);
}

int main(void) {
    printf("=== JSON object parse scaling (optimized) ===\n");
    printf("%-10s  %10s  %12s\n", "keys", "ms", "ns/key");

    int sizes[] = {100, 1000, 10000, 50000, 200000};
    int ns = (int)(sizeof(sizes) / sizeof(sizes[0]));

    for (int s = 0; s < ns; s++) {
        int N = sizes[s];
        size_t cap = (size_t)N * 28 + 8;
        char *buf = (char *)malloc(cap);
        size_t w = 0;
        buf[w++] = '{';
        for (int i = 0; i < N; i++) {
            if (i) buf[w++] = ',';
            w += (size_t)snprintf(buf + w, cap - w, "\"key%d\":%d", i, i);
        }
        buf[w++] = '}';
        buf[w] = '\0';

        int iters = (int)(20000000 / N); if (iters < 2) iters = 2;
        double t0 = now_sec();
        for (int it = 0; it < iters; it++) {
            neverc_json_value_t *v = neverc_json_parse(buf, w);
            sink = (size_t)v;
            neverc_json_free(v);
        }
        double dt = (now_sec() - t0) / iters;
        printf("%-10d  %10.3f  %12.2f\n", N, dt * 1000, dt * 1e9 / N);
        free(buf);
    }

    /* String parsing: one giant escape-free string. The old fixed-buffer parser
     * could not handle the >64 KiB sizes at all; the new one bulk-copies via
     * memchr+memcpy, so ns/byte stays low and flat. */
    printf("\n=== JSON string parse scaling (escape-free) ===\n");
    printf("%-10s  %10s  %12s\n", "bytes", "ms", "ns/byte");
    size_t ssizes[] = {1024, 65536, 262144, 1048576};
    int sns = (int)(sizeof(ssizes) / sizeof(ssizes[0]));
    for (int s = 0; s < sns; s++) {
        size_t N = ssizes[s];
        char *buf = (char *)malloc(N + 4);
        size_t w = 0;
        buf[w++] = '"';
        for (size_t i = 0; i < N; i++) buf[w++] = (char)('a' + (i % 26));
        buf[w++] = '"';
        int iters = (int)(50000000 / N); if (iters < 3) iters = 3;
        double t0 = now_sec();
        for (int it = 0; it < iters; it++) {
            neverc_json_value_t *v = neverc_json_parse(buf, w);
            sink = (size_t)v;
            neverc_json_free(v);
        }
        double dt = (now_sec() - t0) / iters;
        printf("%-10zu  %10.3f  %12.3f\n", N, dt * 1000, dt * 1e9 / (double)N);
        free(buf);
    }

    bench_marshal();

    printf("\n=== Done (sink=%zu) ===\n", sink);
    return 0;
}
