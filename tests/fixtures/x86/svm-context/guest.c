/* Original NeverC SVM guest regression. SPDX-License-Identifier: AGPL-3.0-only
 */

typedef unsigned long long U64;
typedef unsigned U32;
typedef unsigned short U16;
typedef unsigned char U8;
void *memset(void *p, int value, __SIZE_TYPE__ size) {
  volatile U8 *out = p;
  for (__SIZE_TYPE__ i = 0; i < size; ++i)
    out[i] = (U8)value;
  return p;
}
struct Context {
  U64 vmcb, guest, host, mask, gpr[14];
};
#ifndef VMRUN64
#define VMRUN64(context) __builtin_ia32_vmrun_context(context, 64)
#endif
static U8 vmcb[4096] __attribute__((aligned(4096)));
static U8 hsave[4096] __attribute__((aligned(4096)));
static U8 input[4096] __attribute__((aligned(64)));
static U8 scratch[4096] __attribute__((aligned(64)));
static U8 host_input[4096] __attribute__((aligned(64)));
static U8 host_output[4096] __attribute__((aligned(64)));
static U8 guest_stack[4096] __attribute__((aligned(16)));
extern void guest_vmmcall(void), guest_hlt(void), guest_rcx(void);
extern U64 ida_vmrun_rcx_result(U64);
#define Q(p, o) (*(U64 *)((U8 *)(p) + (o)))
#define D(p, o) (*(U32 *)((U8 *)(p) + (o)))
#define W(p, o) (*(U16 *)((U8 *)(p) + (o)))
static void msr(U32 index, U64 value) {
  __asm__ volatile("wrmsr"
                   :
                   : "c"(index), "a"((U32)value), "d"((U32)(value >> 32))
                   : "memory");
}
static void put(U8 c) { __asm__ volatile("outb %0, $0xe9" : : "a"(c)); }
static U32 fail(U32 code, U64 value) {
  put('F');
  put(':');
  for (int i = 7; i >= 0; --i)
    put("0123456789abcdef"[(code >> (i * 4)) & 15]);
  put(':');
  for (int i = 15; i >= 0; --i)
    put("0123456789abcdef"[(value >> (i * 4)) & 15]);
  put('\n');
  return 32;
}
static void segment(unsigned offset, unsigned selector, unsigned attributes) {
  W(vmcb, offset) = selector;
  W(vmcb, offset + 2) = attributes;
  D(vmcb, offset + 4) = 0xffffffff;
  Q(vmcb, offset + 8) = 0;
}
static void init_vmcb(void (*pc)(void)) {
  for (unsigned i = 0; i < sizeof(vmcb); ++i)
    vmcb[i] = 0;
  D(vmcb, 8) = 0xffffffff;
  D(vmcb, 12) = 1U << 24; /* HLT */
  D(vmcb, 16) = 3;        /* VMRUN must be intercepted; VMMCALL exits. */
  D(vmcb, 0x58) = 1;
  segment(0x400, 16, 0xc93);
  segment(0x410, 8, 0xa9b);
  segment(0x420, 16, 0xc93);
  segment(0x430, 16, 0xc93);
  Q(vmcb, 0x4d0) = 0x1500;
  U64 value;
  __asm__ volatile("mov %%cr4,%0" : "=r"(value));
  Q(vmcb, 0x548) = value;
  __asm__ volatile("mov %%cr3,%0" : "=r"(value));
  Q(vmcb, 0x550) = value;
  __asm__ volatile("mov %%cr0,%0" : "=r"(value));
  Q(vmcb, 0x558) = value;
  Q(vmcb, 0x560) = 0x400;
  Q(vmcb, 0x568) = 0xffff0ff0;
  Q(vmcb, 0x570) = 2;
  Q(vmcb, 0x578) = (U64)pc;
  Q(vmcb, 0x5d8) = (U64)(guest_stack + sizeof(guest_stack) - 16);
  Q(vmcb, 0x5f8) = 0x12345678abcdef00ULL;
}
static void seed(U8 *state, U64 value) {
  for (unsigned i = 0; i < 4096; ++i)
    state[i] = 0;
  W(state, 0) = 0x37f;
  D(state, 24) = 0x1f80;
  Q(state, 512) = 7;
  for (unsigned r = 0; r < 16; ++r) {
    Q(state, 160 + 16 * r) = value + 4 * r;
    Q(state, 168 + 16 * r) = value + 4 * r + 1;
    Q(state, 576 + 16 * r) = value + 4 * r + 2;
    Q(state, 584 + 16 * r) = value + 4 * r + 3;
  }
}
__attribute__((noinline)) static U64 invoke64(struct Context *p, unsigned n) {
  volatile U64 local[32];
  volatile U8 dynamic[n + 32];
  for (unsigned i = 0; i < 32; ++i)
    local[i] = 0xabc000 + i + n;
  dynamic[n] = 0x5a;
  VMRUN64(p);
  U64 result = dynamic[n];
  for (unsigned i = 0; i < 32; ++i)
    result += local[i];
  return result;
}
__attribute__((noinline)) static U64 invoke32(struct Context *p, unsigned n) {
  volatile U64 local[32];
  volatile U8 dynamic[n + 32];
  for (unsigned i = 0; i < 32; ++i)
    local[i] = 0xabc000 + i + n;
  dynamic[n] = 0x5a;
  __builtin_ia32_vmrun_context(p, 32);
  U64 result = dynamic[n];
  for (unsigned i = 0; i < 32; ++i)
    result += local[i];
  return result;
}
unsigned probe(void) {
  U32 a = 0x80000001, b, c = 0, d;
  __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "+c"(c), "=d"(d));
  if (!(c & 4))
    return fail(1, c);
  msr(0xc0000080, 0x1500);
  msr(0xc0010117, (U64)hsave);
  for (unsigned iteration = 0; iteration < 8; ++iteration) {
    for (unsigned width = 0; width < 2; ++width)
      for (unsigned exit_kind = 0; exit_kind < 3; ++exit_kind) {
        init_vmcb(exit_kind ? guest_hlt : guest_vmmcall);
        const unsigned invalid = exit_kind == 2;
        if (invalid)
          D(vmcb, 0x58) = 0; /* ASID zero yields VMEXIT_INVALID. */
        U64 base = 0x123456789abcd000ULL + iteration * 101;
        seed(input, base);
        seed(host_input, 0xfedcba9876543000ULL + iteration);
        for (unsigned i = 0; i < 4096; ++i)
          scratch[i] = host_output[i] = 0;
        struct Context context = {(U64)vmcb, (U64)input, (U64)scratch, 7, {0}};
        if (width)
          context.vmcb |= 0xfedcba9800000000ULL;
        for (unsigned i = 0; i < 14; ++i)
          context.gpr[i] = base + i * 37;
        __asm__ volatile("xrstor64 (%0)"
                         :
                         : "r"(host_input), "a"(7), "d"(0)
                         : "memory");
        U64 result = width ? invoke32(&context, iteration * 13)
                           : invoke64(&context, iteration * 13);
        __asm__ volatile("xsave64 (%0)"
                         :
                         : "r"(host_output), "a"(7), "d"(0)
                         : "memory");
        if (result != 0x5a + 32 * (0xabc000ULL + iteration * 13) + 496)
          return fail(2, result);
        if (Q(vmcb, 0x70) != (invalid ? ~0ULL : exit_kind ? 0x78ULL : 0x81ULL))
          return fail(3, Q(vmcb, 0x70));
        for (unsigned i = 0; i < 14; ++i) {
          U64 prior = base + i * 37;
#ifdef NEVERD_RCXSOURCE
          if (width == 0 && i == 1)
            prior = 0x1234;
#endif
          if (context.gpr[i] != prior + (invalid ? 0 : i + 1))
            return fail(0x100 + i, context.gpr[i]);
        }
        /* Invalid VMCBs can exit before guest state is installed. Do not assert
         * an executed guest's save-area RAX/RSP for that early exit. */
        if (!invalid && Q(vmcb, 0x5f8) != 0x12345678abcdef07ULL)
          return fail(4, Q(vmcb, 0x5f8));
        if (!invalid &&
            Q(vmcb, 0x5d8) != (U64)(guest_stack + sizeof(guest_stack) - 32))
          return fail(5, Q(vmcb, 0x5d8));
        for (unsigned r = 0; r < 16; ++r)
          for (unsigned lane = 0; lane < 4; ++lane) {
            unsigned offset = (lane < 2 ? 160 : 576) + 16 * r + (lane % 2) * 8;
            if (Q(input, offset) !=
                base + 4 * r + lane + (invalid ? 0 : 0x1234))
              return fail(0x200 + r * 4 + lane, Q(input, offset));
            if (Q(host_input, offset) != Q(host_output, offset))
              return fail(0x300 + r * 4 + lane, Q(host_output, offset));
          }
        if (invalid) {
          if (W(input, 0) != 0x37f || W(input, 2) != 0 || input[4] != 0)
            return fail(6, Q(input, 0));
        } else if (W(input, 0) != 0x37f || (W(input, 2) & 0x3800) != 0x3800 ||
                   input[4] != 0x80 || Q(input, 32) != 0x8000000000000000ULL ||
                   W(input, 40) != 0x3fff)
          return fail(6, Q(input, 0));
        if (W(host_output, 0) != 0x37f || W(host_output, 2) != 0 ||
            host_output[4] != 0 || D(host_output, 24) != 0x1f80)
          return fail(7, Q(host_output, 0));
      }
  }
  /* Execute the identical RCX-writing guest through the generated interface,
   * then through the original host bytes analyzed by IDA. */
  init_vmcb(guest_rcx);
  seed(input, 0);
  struct Context context = {(U64)vmcb, (U64)input, (U64)scratch, 7, {0}};
  context.gpr[1] = 0x1234;
#ifdef NEVERD_RCXSOURCE
  U64 projected = vmrun_rcx_result((void *)&context);
  if (projected != 0x5678)
    return fail(9, projected);
#else
  VMRUN64(&context);
#endif
  if (context.gpr[1] != 0x5678)
    return fail(10, context.gpr[1]);
  init_vmcb(guest_rcx);
  U64 actual = ida_vmrun_rcx_result((U64)vmcb);
  if (actual != 0x5678)
    return fail(8, actual);
  put('I');
  put('D');
  put('A');
  put(':');
  put('5');
  put('6');
  put('7');
  put('8');
  put('\n');
  put('P');
  put('A');
  put('S');
  put('S');
  put('\n');
  return 16;
}
