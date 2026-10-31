/* Native compiler or actual NeverD source against independent original bytes.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <stddef.h>
#include <stdint.h>

void original_hyperv(void *, void *, void *);
#ifndef EXTERNAL_NATIVE
void native_hyperv(void *entry, void *gpr, void *vectors) {
  __builtin_ia32_hyperv_call(entry, gpr, vectors, 64);
}
#else
void native_hyperv(void *, void *, void *);
#endif
void *memcpy(void *d, const void *s, size_t n) {
  volatile unsigned char *a = d;
  const volatile unsigned char *b = s;
  for (size_t i = 0; i < n; ++i)
    a[i] = b[i];
  return d;
}
void *memset(void *d, int c, size_t n) {
  volatile unsigned char *a = d;
  for (size_t i = 0; i < n; ++i)
    a[i] = c;
  return d;
}
int memcmp(const void *a, const void *b, size_t n) {
  const volatile unsigned char *x = a, *y = b;
  for (size_t i = 0; i < n; ++i)
    if (x[i] != y[i])
      return x[i] < y[i] ? -1 : 1;
  return 0;
}
__attribute__((noreturn)) void abort(void) {
  __asm__ volatile("mov $1,%%eax;out %%al,$0xf4;hlt" ::: "rax", "memory");
  __builtin_unreachable();
}
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      abort();                                                                 \
  } while (0)
__attribute__((naked, section(".start"))) void _start(void) {
  __asm__ volatile("call main; out %al,$0xf4; hlt");
}

int main(void) {
  for (unsigned mode = 0; mode < 3; ++mode)
    for (unsigned seed = 0; seed < 128; ++seed) {
      uint64_t g[16], wantg[16], originalg[16];
      unsigned char v[32][64], wantv[32][64], originalv[32][64];
      for (unsigned i = 0; i < 16; ++i)
        g[i] = 0x123456789abcdef0ull + seed * 71 + i * 13;
      for (unsigned i = 0; i < sizeof(v); ++i)
        ((unsigned char *)v)[i] = (unsigned char)(seed + i * 7);
      g[1] = mode == 1 ? 0x1005c : (mode == 2 ? 0x100008001ull : 0x8001);
      memcpy(wantg, g, sizeof(g));
      memcpy(wantv, v, sizeof(v));
      memcpy(originalg, g, sizeof(g));
      memcpy(originalv, v, sizeof(v));
      uint64_t checksum = g[1] ^ g[2] ^ g[8];
      for (unsigned i = 0; i < 6; ++i)
        for (unsigned j = 0; j < 2; ++j) {
          uint64_t word;
          memcpy(&word, v[i] + j * 8, 8);
          checksum ^= word;
        }
      wantg[0] = checksum;
      if (mode == 2)
        wantg[1] += 1ull << 48;
      if (mode == 1) {
        wantg[2] ^= 0xaabbccdd12345678ull;
        wantg[8] += 0x123456789abcdef0ull;
        for (unsigned i = 0; i < 6; ++i)
          for (unsigned j = 0; j < 16; ++j)
            wantv[i][j] ^= i * 16 + j;
      }
      *(volatile uint64_t *)0x180000 = 0;
      original_hyperv((void *)0x62000000, originalg, originalv);
      CHECK(!memcmp(originalg, wantg, sizeof(g)) &&
            !memcmp(originalv, wantv, sizeof(v)));
      CHECK(*(volatile uint64_t *)0x180000 == checksum);
      *(volatile uint64_t *)0x180000 = 0;
#ifdef NEVERD_HYPERV_CONTINUATION
      neverd_x64_state_v6 s = {0};
      memcpy(s.GPR, g, sizeof(g));
      memcpy(s.ZMM, v, sizeof(v));
      s.PC = 0x1000 + 3 * (seed % 2);
      s.RFlags = 0x246;
      s.MXCSR = 0x1f80;
      s.CR2 = seed;
      s.CS = (neverd_x64_segment){0, UINT32_MAX, 8, 0xa09b};
      s.SS = (neverd_x64_segment){0, UINT32_MAX, 16, 0xc093};
      neverd_x64_state_v6 expected = s;
      memcpy(expected.GPR, wantg, sizeof(g));
      memcpy(expected.ZMM, wantv, sizeof(v));
      expected.PC += 3;
      uint64_t budget = 1;
      CHECK(neverd_x64_continue_v6(&s, &budget) == 9 && !budget);
      CHECK(!memcmp(&s, &expected, sizeof(s)));
#else
      native_hyperv((void *)0x62000000, g, v);
      CHECK(!memcmp(g, wantg, sizeof(g)) && !memcmp(v, wantv, sizeof(v)));
#endif
      CHECK(*(volatile uint64_t *)0x180000 == checksum);
    }
  return 0;
}
