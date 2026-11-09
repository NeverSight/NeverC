/* Native encodings run under Intel SDE. This does not claim physical RTM. */
#include "fixture.h"
#include <cpuid.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc != 2 && argc != 3)
    return 2;
  const int full = strcmp(argv[1], "full") == 0;
  const int forced = strncmp(argv[1], "sde-abort-", 10) == 0;
  if (!full && !forced)
    return 2;
  const uint32_t reason = forced ? (uint32_t)strtoul(argv[1] + 10, 0, 0) : 0;
  if (forced && reason != 0 && reason != 2 && reason != 6 && reason != 8)
    return 2;
  unsigned a, b, c, d;
  const int advertised =
      __get_cpuid_count(7, 0, &a, &b, &c, &d) && (b & (1u << 11));
  /* SDE abort mode emulates XBEGIN but clears CPUID.RTM. Only the explicit
   * SDE-only mode bypasses discovery; ordinary full execution still requires
   * it. NeverD's provider itself must continue to decline when discovery fails.
   */
  if (!advertised && !forced) {
    puts("{\"rtm_available\":false,\"pairs\":0}");
    return 77;
  }
  unsigned pairs = 0;
  for (unsigned code = 0; code != 256; ++code)
    for (unsigned seed = 0; seed != 32; ++seed) {
      const uint64_t values[] = {0x123456789abcdef0ull + seed * 37,
                                 0xfedcba9876543210ull - seed * 71,
                                 0x8000000100000001ull ^ (seed * 113ull),
                                 0x7fffffff00000007ull + seed * 193};
      const uint64_t original[] = {0x0123456789abcdefull,
                                   seed + 0xaaaaaaaa55555555ull,
                                   0xfedcba9876543210ull};
      unsigned long long left[3], right[3];
      memcpy(left, original, sizeof(left));
      memcpy(right, original, sizeof(right));
      struct result x = {0}, y = {0};
#define RUN(F, O, M)                                                           \
  F(&O, M + 1, values[0], values[1], values[2], values[3], seed * 0.25,        \
    (seed + 1) * 0.125, (seed + 2) * 0.5, (seed + 3) * 0.25,                   \
    (seed + 4) * 0.0625, (seed + 5) * 0.125)
      RUN(candidate[code], x, left);
      RUN(reference[code], y, right);
#undef RUN
      if (argc == 3) {
        if (strcmp(argv[2], "status") == 0)
          x.status ^= 1;
        else if (strcmp(argv[2], "status-high") == 0)
          x.status ^= 1ull << 32;
        else if (strcmp(argv[2], "integer") == 0)
          x.i0 ^= 1;
        else if (strcmp(argv[2], "previous-memory") == 0)
          x.i1 ^= 1;
        else if (strcmp(argv[2], "completed-memory") == 0)
          x.i2 ^= 1;
        else if (strcmp(argv[2], "floating") == 0)
          x.fp += 1;
        else if (strcmp(argv[2], "canary") == 0)
          left[0] ^= 1;
        else
          return 2;
      }
      const uint64_t status = full ? (code << 24) | 1u : reason;
      if (x.status != status || y.status != status ||
          memcmp(&x, &y, sizeof(x)) || memcmp(left, right, sizeof(left)) ||
          left[0] != original[0] || left[2] != original[2] ||
          x.i1 != original[1] ||
          x.i2 != (values[0] ^ values[1] ^ values[2] ^ values[3])) {
        fprintf(stderr,
                "code=%u seed=%u expected=%llx candidate=%llx reference=%llx\n",
                code, seed, (unsigned long long)status, x.status, y.status);
        return 1;
      }
      ++pairs;
    }
  printf("{\"rtm_advertised\":%s,\"pairs\":%u}\n",
         advertised ? "true" : "false", pairs);
  return 0;
}
