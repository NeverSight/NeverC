/* Native compiler or actual NeverD source against independent original bytes.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <stddef.h>
#include <stdint.h>
#ifndef EXTERNAL_NATIVE
#include <kvmhypercallintrin.h>
uint64_t native_vmcall(uint64_t n, uint64_t a, uint64_t b, uint64_t c,
                       uint64_t d) {
  return __kvm_vmcall(n, a, b, c, d);
}
uint64_t native_vmmcall(uint64_t n, uint64_t a, uint64_t b, uint64_t c,
                        uint64_t d) {
  return __kvm_vmmcall(n, a, b, c, d);
}
#else
uint64_t native_vmcall(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t native_vmmcall(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
#endif
uint64_t original_vmcall(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
uint64_t original_vmmcall(uint64_t, uint64_t, uint64_t, uint64_t, uint64_t);
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
  __asm__ volatile("mov $1,%%eax;hlt" ::: "rax", "memory");
  __builtin_unreachable();
}
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      abort();                                                                 \
  } while (0)
__attribute__((naked, section(".start"))) void _start(void) {
  __asm__ volatile("call main; hlt");
}
int main(void) {
  for (unsigned form = 0; form < 2; ++form)
    for (unsigned seed = 0; seed < 128; ++seed) {
      uint64_t n = 12, a = (uint64_t)(seed + 1) << 12, b = seed + 1,
               c = seed % 32, d = 0xabcdef0000000000ULL + seed * 19;
      uint64_t expected = n + 3 * a + 5 * b + 7 * c + 11 * d;
      volatile uint64_t *ram = (void *)0x180000;
      *ram = 0;
      CHECK((form ? original_vmmcall : original_vmcall)(n, a, b, c, d) ==
            expected);
      CHECK(*ram == (n ^ a ^ b ^ c ^ d));
      *ram = 0;
#ifdef NEVERD_KVM_CONTINUATION
      neverd_x64_state_v6 s = {0};
      for (unsigned i = 0; i < 16; ++i)
        s.GPR[i] = 0x1234000000000000ULL + i * 71 + seed;
      for (unsigned i = 0; i < sizeof(s.ZMM); ++i)
        ((uint8_t *)s.ZMM)[i] = (uint8_t)(i * 7 + seed);
      s.GPR[0] = n;
      s.GPR[3] = a;
      s.GPR[1] = b;
      s.GPR[2] = c;
      s.GPR[6] = d;
      s.PC = form ? 0x1006 : 0x1000;
      s.RFlags = 0x246;
      s.MXCSR = 0x1f80;
      s.CS = (neverd_x64_segment){0, UINT32_MAX, 8, 0xa09b};
      s.SS = (neverd_x64_segment){0, UINT32_MAX, 16, 0xc093};
      neverd_x64_state_v6 want = s;
      want.GPR[0] = expected;
      want.PC += 3;
      uint64_t budget = 1;
      CHECK(neverd_x64_continue_v6(&s, &budget) == 9 && !budget);
      CHECK(!memcmp(&s, &want, sizeof(s)));
#else
      CHECK((form ? native_vmmcall : native_vmcall)(n, a, b, c, d) == expected);
#endif
      CHECK(*ram == (n ^ a ^ b ^ c ^ d));
    }
  return 0;
}
