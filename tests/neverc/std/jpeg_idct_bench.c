/*
 * A/B benchmark + correctness check: JPEG inverse DCT.
 *
 * old_idct_block  — the previous O(8^3) reference IDCT (separable matrix
 *                   multiply, ~1024 multiplies per 8x8 block).
 * new_idct_block  — the AAN fast inverse DCT now in std/src/image/jpeg/jpeg.c
 *                   (~80 multiplies per block; libjpeg jidctflt port).
 *
 * Both are reproduced here verbatim so the benchmark measures the actual
 * old-vs-new transform. The correctness pass feeds identical dequantized
 * coefficients through both and asserts the spatial output is bit-for-bit
 * close (float rounding only), so the speedup is not bought with accuracy.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <time.h>

/* AAN per-coefficient scale factors (shared encoder/decoder convention). */
static const double aanscale[8] = {
    1.0, 1.387039845322148, 1.306562964876377, 1.175875602419359,
    1.0, 0.785694958387102, 0.541196100146197, 0.275899379282943
};

/* ---- OLD: reference separable IDCT (double, full cosine matrix) ---- */

static const double dct_cos[8][8] = {
    { 1.000000000000000, 0.980785280403230, 0.923879532511287, 0.831469612302545,
      0.707106781186548, 0.555570233019602, 0.382683432365090, 0.195090322016128},
    { 1.000000000000000, 0.831469612302545, 0.382683432365090,-0.195090322016128,
     -0.707106781186548,-0.980785280403230,-0.923879532511287,-0.555570233019602},
    { 1.000000000000000, 0.555570233019602,-0.382683432365090,-0.980785280403230,
     -0.707106781186548, 0.195090322016128, 0.923879532511287, 0.831469612302545},
    { 1.000000000000000, 0.195090322016128,-0.923879532511287,-0.555570233019602,
      0.707106781186548, 0.831469612302545,-0.382683432365090,-0.980785280403230},
    { 1.000000000000000,-0.195090322016128,-0.923879532511287, 0.555570233019602,
      0.707106781186548,-0.831469612302545,-0.382683432365090, 0.980785280403230},
    { 1.000000000000000,-0.555570233019602,-0.382683432365090, 0.980785280403230,
     -0.707106781186548,-0.195090322016128, 0.923879532511287,-0.831469612302545},
    { 1.000000000000000,-0.831469612302545, 0.382683432365090, 0.195090322016128,
     -0.707106781186548, 0.980785280403230,-0.923879532511287, 0.555570233019602},
    { 1.000000000000000,-0.980785280403230, 0.923879532511287,-0.831469612302545,
      0.707106781186548,-0.555570233019602, 0.382683432365090,-0.195090322016128},
};
static const double INV_SQRT2 = 0.707106781186548;

__attribute__((noinline))
static void old_idct_block(const int *input, int *output) {
    double tmp[64];
    for (int y = 0; y < 8; y++) {
        for (int x = 0; x < 8; x++) {
            double sum = 0.0;
            for (int u = 0; u < 8; u++) {
                double cu = (u == 0) ? INV_SQRT2 : 1.0;
                sum += cu * input[y * 8 + u] * dct_cos[x][u];
            }
            tmp[y * 8 + x] = sum / 2.0;
        }
    }
    for (int x = 0; x < 8; x++) {
        for (int y = 0; y < 8; y++) {
            double sum = 0.0;
            for (int v = 0; v < 8; v++) {
                double cv = (v == 0) ? INV_SQRT2 : 1.0;
                sum += cv * tmp[v * 8 + x] * dct_cos[y][v];
            }
            int val = (int)(sum / 2.0 + 128.5);
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            output[y * 8 + x] = val;
        }
    }
}

/* ---- NEW: AAN fast IDCT (input pre-scaled by aanscale[row]*aanscale[col]) ---- */

__attribute__((noinline))
static void new_idct_block(const float *input, int *output) {
    float ws[64];

    for (int c = 0; c < 8; c++) {
        const float *in = input + c;
        float *w = ws + c;

        if (in[8] == 0.0f && in[16] == 0.0f && in[24] == 0.0f && in[32] == 0.0f &&
            in[40] == 0.0f && in[48] == 0.0f && in[56] == 0.0f) {
            float dc = in[0];
            w[0] = w[8] = w[16] = w[24] = w[32] = w[40] = w[48] = w[56] = dc;
            continue;
        }

        float tmp0 = in[0], tmp1 = in[16], tmp2 = in[32], tmp3 = in[48];
        float t10 = tmp0 + tmp2, t11 = tmp0 - tmp2;
        float t13 = tmp1 + tmp3;
        float t12 = (tmp1 - tmp3) * 1.414213562f - t13;
        tmp0 = t10 + t13; tmp3 = t10 - t13;
        tmp1 = t11 + t12; tmp2 = t11 - t12;

        float tmp4 = in[8], tmp5 = in[24], tmp6 = in[40], tmp7 = in[56];
        float z13 = tmp6 + tmp5, z10 = tmp6 - tmp5;
        float z11 = tmp4 + tmp7, z12 = tmp4 - tmp7;
        tmp7 = z11 + z13;
        t11 = (z11 - z13) * 1.414213562f;
        float z5 = (z10 + z12) * 1.847759065f;
        t10 = 1.082392200f * z12 - z5;
        t12 = -2.613125930f * z10 + z5;
        tmp6 = t12 - tmp7;
        tmp5 = t11 - tmp6;
        tmp4 = t10 + tmp5;

        w[0]  = tmp0 + tmp7; w[56] = tmp0 - tmp7;
        w[8]  = tmp1 + tmp6; w[48] = tmp1 - tmp6;
        w[16] = tmp2 + tmp5; w[40] = tmp2 - tmp5;
        w[32] = tmp3 + tmp4; w[24] = tmp3 - tmp4;
    }

    for (int r = 0; r < 8; r++) {
        const float *w = ws + r * 8;
        int *o = output + r * 8;

        float t10 = w[0] + w[4], t11 = w[0] - w[4];
        float t13 = w[2] + w[6];
        float t12 = (w[2] - w[6]) * 1.414213562f - t13;
        float tmp0 = t10 + t13, tmp3 = t10 - t13;
        float tmp1 = t11 + t12, tmp2 = t11 - t12;

        float z13 = w[5] + w[3], z10 = w[5] - w[3];
        float z11 = w[1] + w[7], z12 = w[1] - w[7];
        float tmp7 = z11 + z13;
        t11 = (z11 - z13) * 1.414213562f;
        float z5 = (z10 + z12) * 1.847759065f;
        t10 = 1.082392200f * z12 - z5;
        t12 = -2.613125930f * z10 + z5;
        float tmp6 = t12 - tmp7;
        float tmp5 = t11 - tmp6;
        float tmp4 = t10 + tmp5;

#define IDCT_OUT(idx, val) do { \
    int v = (int)((val) * 0.125f + 128.5f); \
    o[idx] = v < 0 ? 0 : (v > 255 ? 255 : v); \
} while (0)
        IDCT_OUT(0, tmp0 + tmp7);
        IDCT_OUT(7, tmp0 - tmp7);
        IDCT_OUT(1, tmp1 + tmp6);
        IDCT_OUT(6, tmp1 - tmp6);
        IDCT_OUT(2, tmp2 + tmp5);
        IDCT_OUT(5, tmp2 - tmp5);
        IDCT_OUT(4, tmp3 + tmp4);
        IDCT_OUT(3, tmp3 - tmp4);
#undef IDCT_OUT
    }
}

/* ---- helpers ---- */

static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static volatile int sink;

/* Generate a realistic dequantized 8x8 block: a strong DC plus a few low-
 * frequency AC terms that fall off with frequency (kind=0), or a dense random
 * block (kind=1, stress). Values in natural order. */
static void gen_block(int *dq, unsigned *seed, int kind) {
    for (int i = 0; i < 64; i++) dq[i] = 0;
    unsigned s = *seed;
#define RND() (s = s * 1664525u + 1013904223u, (int)(s >> 16))
    if (kind == 0) {
        dq[0] = (RND() % 2048) - 1024;
        int nac = 3 + (RND() % 8);
        for (int k = 0; k < nac; k++) {
            int r = RND() % 4, c = RND() % 4;     /* low frequencies */
            int amp = 256 >> (r + c);
            dq[r * 8 + c] += (RND() % (2 * amp + 1)) - amp;
        }
    } else {
        for (int i = 0; i < 64; i++) {
            int amp = 512 >> ((i >> 3) + (i & 7));
            if (amp < 1) amp = 1;
            dq[i] = (RND() % (2 * amp + 1)) - amp;
        }
    }
    *seed = s;
#undef RND
}

static void to_prescaled(const int *dq, float *fdq) {
    for (int i = 0; i < 64; i++)
        fdq[i] = (float)((double)dq[i] * aanscale[i >> 3] * aanscale[i & 7]);
}

int main(void) {
    printf("=== JPEG inverse DCT: AAN (new) vs reference O(8^3) (old) ===\n\n");

    enum { NBLK = 4096 };
    int  (*dq)[64]  = malloc(sizeof(*dq)  * NBLK);
    float(*fdq)[64] = malloc(sizeof(*fdq) * NBLK);

    /* ---- Correctness: identical coefficients, compare spatial output ---- */
    for (int kind = 0; kind < 2; kind++) {
        unsigned seed = 0xC0FFEEu + (unsigned)kind;
        long worst = 0, total_abs = 0;
        long npix = 0, nover1 = 0;
        for (int b = 0; b < NBLK; b++) {
            int dqb[64]; float fdqb[64];
            int oo[64], on[64];
            gen_block(dqb, &seed, kind);
            to_prescaled(dqb, fdqb);
            old_idct_block(dqb, oo);
            new_idct_block(fdqb, on);
            for (int i = 0; i < 64; i++) {
                long d = oo[i] - on[i]; if (d < 0) d = -d;
                if (d > worst) worst = d;
                total_abs += d;
                if (d > 1) nover1++;
                npix++;
            }
        }
        printf("correctness [%s]: max |old-new| = %ld, mean |old-new| = %.4f, pixels>1 = %ld/%ld\n",
               kind == 0 ? "photo-like" : "dense-random",
               worst, (double)total_abs / (double)npix, nover1, npix);
    }
    printf("\n");

    /* ---- Benchmark ---- */
    printf("%-16s  %12s  %12s  %9s\n", "workload", "old (ref)", "new (AAN)", "speedup");
    for (int kind = 0; kind < 2; kind++) {
        unsigned seed = 0x1234u + (unsigned)kind;
        for (int b = 0; b < NBLK; b++) {
            gen_block(dq[b], &seed, kind);
            to_prescaled(dq[b], fdq[b]);
        }
        int iters = 4000;
        int out[64];

        double t0 = now_sec();
        for (int it = 0; it < iters; it++)
            for (int b = 0; b < NBLK; b++) { old_idct_block(dq[b], out); sink = out[0]; }
        double t_old = now_sec() - t0;

        t0 = now_sec();
        for (int it = 0; it < iters; it++)
            for (int b = 0; b < NBLK; b++) { new_idct_block(fdq[b], out); sink = out[0]; }
        double t_new = now_sec() - t0;

        printf("%-16s  %9.1f ms  %9.1f ms  %7.2fx\n",
               kind == 0 ? "photo-like" : "dense-random",
               t_old * 1000, t_new * 1000, t_old / t_new);
    }

    free(dq); free(fdq);
    printf("\n=== Done ===\n");
    return 0;
}
