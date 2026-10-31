/* Original-byte KVM ABI oracle for the full LLVM fork's i386 codegen.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
#include <stdint.h>
uint32_t native_vmcall(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t native_vmmcall(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t original_vmcall(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
uint32_t original_vmmcall(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
__attribute__((noreturn)) void abort(void) {
  __asm__ volatile("mov $1,%%eax;hlt" ::: "eax", "memory");
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
      uint32_t n = 12, a = (seed + 1) << 12, b = seed + 1, c = seed % 32,
               d = 0xabcdef00 + seed * 19;
      uint32_t expected = n + 3 * a + 5 * b + 7 * c + 11 * d;
      volatile uint32_t *ram = (void *)0x180000;
      *ram = 0;
      CHECK((form ? original_vmmcall : original_vmcall)(n, a, b, c, d) ==
            expected);
      CHECK(*ram == (n ^ a ^ b ^ c ^ d));
      *ram = 0;
      CHECK((form ? native_vmmcall : native_vmcall)(n, a, b, c, d) == expected);
      CHECK(*ram == (n ^ a ^ b ^ c ^ d));
    }
  return 0;
}
