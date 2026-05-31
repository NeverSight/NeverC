#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <zlib.h>

static void demo_trig(void) {
  printf("--- trigonometry ---\n");
  for (int deg = 0; deg <= 360; deg += 45) {
    double rad = deg * M_PI / 180.0;
    printf("  %3d°: sin=% .6f  cos=% .6f  tan=% .6f\n",
           deg, sin(rad), cos(rad),
           (deg % 180 == 90) ? INFINITY : tan(rad));
  }
}

static void demo_special(void) {
  printf("\n--- special functions ---\n");

  printf("  e       = %.15f\n", exp(1.0));
  printf("  pi      = %.15f\n", acos(-1.0));
  printf("  ln(2)   = %.15f\n", log(2.0));
  printf("  log10(1000) = %.6f\n", log10(1000.0));
  printf("  gamma(5)    = %.6f  (4! = 24)\n", tgamma(5.0));
  printf("  erf(1)      = %.6f\n", erf(1.0));
  printf("  cbrt(27)    = %.6f\n", cbrt(27.0));
  printf("  hypot(3,4)  = %.6f\n", hypot(3.0, 4.0));
}

static void demo_zlib(void) {
  printf("\n--- zlib compression ---\n");

  const char *input = "NeverC cross-compiles C code to Linux from any host. "
                       "This string will be compressed with zlib and then "
                       "decompressed to verify the roundtrip works correctly. "
                       "NeverC bundles the Linux sysroot so no system headers "
                       "or libraries are needed on the build host.";

  uLong src_len = (uLong)strlen(input) + 1;
  uLong comp_len = compressBound(src_len);
  Bytef *compressed = (Bytef *)malloc(comp_len);
  Bytef *decompressed = (Bytef *)malloc(src_len);

  if (!compressed || !decompressed) {
    fprintf(stderr, "malloc failed\n");
    free(compressed);
    free(decompressed);
    return;
  }

  int rc = compress2(compressed, &comp_len, (const Bytef *)input, src_len, Z_BEST_COMPRESSION);
  if (rc != Z_OK) {
    fprintf(stderr, "compress2 failed: %d\n", rc);
    free(compressed);
    free(decompressed);
    return;
  }

  printf("  original    = %lu bytes\n", (unsigned long)src_len);
  printf("  compressed  = %lu bytes (%.1f%%)\n",
         (unsigned long)comp_len,
         100.0 * (double)comp_len / (double)src_len);

  uLong decomp_len = src_len;
  rc = uncompress(decompressed, &decomp_len, compressed, comp_len);
  if (rc != Z_OK) {
    fprintf(stderr, "uncompress failed: %d\n", rc);
    free(compressed);
    free(decompressed);
    return;
  }

  int match = (decomp_len == src_len) &&
              (memcmp(input, decompressed, src_len) == 0);
  printf("  decompressed= %lu bytes, match=%s\n",
         (unsigned long)decomp_len, match ? "YES" : "NO");

  uLong crc = crc32(0L, Z_NULL, 0);
  crc = crc32(crc, (const Bytef *)input, src_len);
  printf("  crc32       = 0x%08lX\n", (unsigned long)crc);
  printf("  zlib version= %s\n", zlibVersion());

  free(compressed);
  free(decompressed);
}

int main(void) {
  printf("NeverC Linux math + zlib demo\n");
  printf("=============================\n\n");

  demo_trig();
  demo_special();
  demo_zlib();

  printf("\nDone.\n");
  return 0;
}
