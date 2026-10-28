#!/usr/bin/env python3
"""Compare native x87 status/reset/wait operations with Linux x64 byte oracles.

Exercise compiler-owned x87/MMX values, exact two-byte stores, and real faults.
Child signal handlers inspect fault instructions and saved state, then exit.
"""

from x87_native_qualification import run_qualification


HARNESS = r'''
#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

typedef unsigned short (*operation)(void *, unsigned *);
#define DECLARE(name) extern unsigned short original_##name(void *, unsigned *), \
    generated_##name(void *, unsigned *);
DECLARE(read) DECLARE(store) DECLARE(clear) DECLARE(init) DECLARE(wait)
extern unsigned short invoke(operation, void *, const void *, void *, unsigned *);
extern long double live_x87(volatile long double *, volatile long double *);
extern unsigned long long live_mmx(unsigned long long, unsigned long long);
__asm__(
    ".text\n.globl original_read\noriginal_read:\n"
    "movl $17,(%rsi)\n.byte 0xdf,0xe0\nmovl $23,(%rsi)\nret\n"
    ".globl original_store\noriginal_store:\n"
    "movl $17,(%rsi)\n.byte 0xdd,0x3f\nmovl $23,(%rsi)\nxorl %eax,%eax\nret\n"
    ".globl original_clear\noriginal_clear:\n"
    "movl $17,(%rsi)\n.byte 0xdb,0xe2\nmovl $23,(%rsi)\nxorl %eax,%eax\nret\n"
    ".globl original_init\noriginal_init:\n"
    "movl $17,(%rsi)\n.byte 0xdb,0xe3\nmovl $23,(%rsi)\nxorl %eax,%eax\nret\n"
    ".globl original_wait\noriginal_wait:\n"
    "movl $17,(%rsi)\n.byte 0x9b\nmovl $23,(%rsi)\nxorl %eax,%eax\nret\n"
    ".globl invoke\ninvoke:\n"
    "push %rbx\npush %r12\npush %r13\nsub $512,%rsp\n"
    "mov %rdx,%rbx\nmov %rcx,%r12\nmov %rdi,%r13\nmov %rsi,%rdi\n"
    "mov %r8,%rsi\nfxsave64 (%rsp)\nfxrstor64 (%rbx)\ncall *%r13\n"
    "fxsave64 (%r12)\nfxrstor64 (%rsp)\n"
    "add $512,%rsp\npop %r13\npop %r12\npop %rbx\nret\n");

static void word(uint8_t *p, uint16_t value) {
  p[0] = value; p[1] = value >> 8;
}
static uint16_t read_word(const uint8_t *p) {
  return p[0] | (uint16_t)p[1] << 8;
}
static uintptr_t expected_begin;
static unsigned expected_pending, expected_signal, expected_kind;
static unsigned *fault_marker;
static void on_fault(int number, siginfo_t *info, void *opaque) {
  (void)info;
  const ucontext_t *context = opaque;
  const uintptr_t pc = context->uc_mcontext.gregs[REG_RIP];
  const unsigned trap = context->uc_mcontext.gregs[REG_TRAPNO];
  if (number != expected_signal || trap != (number == SIGFPE ? 16u : 14u) ||
      pc < expected_begin || pc >= expected_begin + 128 || *fault_marker != 17)
    _exit(90);
  const unsigned char *instruction = (const unsigned char *)pc;
  if ((*instruction & 0xf0) == 0x40) ++instruction;
  if ((expected_kind == 4 ? instruction[0] != 0x9b :
       instruction[0] != 0xdd || (instruction[1] >> 6) == 3 ||
       ((instruction[1] >> 3) & 7) != 7) ||
      context->uc_mcontext.fpregs->cwd != (expected_pending ? 0x37e : 0x37f) ||
      (context->uc_mcontext.fpregs->swd & 0x81) != (expected_pending ? 0x81 : 0))
    _exit(91);
  _exit(0);
}

static int fault_case(operation fn, unsigned pending, unsigned bad, unsigned kind,
                      void *inaccessible) {
  const pid_t child = fork();
  if (child < 0) return 1;
  if (child == 0) {
    unsigned marker = 0;
    expected_begin = (uintptr_t)fn;
    expected_kind = kind;
    expected_pending = pending;
    expected_signal = pending && kind == 4 ? SIGFPE : bad ? SIGSEGV : 0;
    fault_marker = &marker;
    struct sigaction action = {.sa_sigaction = on_fault, .sa_flags = SA_SIGINFO};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGFPE, &action, 0) || sigaction(SIGSEGV, &action, 0)) _exit(92);
    _Alignas(16) uint8_t before[512] = {0}, after[512] = {0};
    uint16_t value = 0xa5a5;
    word(before, pending ? 0x37e : 0x37f);
    word(before + 2, pending ? 0x81 : 0);
    word(before + 24, 0x1f80);
    const uint16_t result = invoke(fn, bad ? inaccessible : &value,
                                   before, after, &marker);
    if (expected_signal || marker != 23 ||
        (kind == 0 && (result & 0x81) != (pending ? 0x81 : 0)) ||
        (kind == 1 && (value & 0x81) != (pending ? 0x81 : 0)) ||
        ((kind == 2 || kind == 3) && (read_word(after + 2) & 0x80ff))) _exit(93);
    _exit(0);
  }
  int status = 0;
  return waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
         WEXITSTATUS(status) != 0;
}

int main(void) {
  const operation functions[2][5] = {
      {original_read, original_store, original_clear, original_init, original_wait},
      {generated_read, generated_store, generated_clear, generated_init, generated_wait}};
  unsigned observations = 0, faults = 0, fault_cases = 0, boundaries = 0;
  for (unsigned top = 0; top < 8; ++top)
    for (unsigned tags = 0; tags < 3; ++tags)
      for (unsigned pc = 0; pc < 4; ++pc) {
        if (pc == 1) continue;
        for (unsigned rc = 0; rc < 4; ++rc)
          for (unsigned sticky = 0; sticky < 2; ++sticky)
            for (unsigned kind = 0; kind < 5; ++kind) {
              _Alignas(16) uint8_t before[512] = {0}, after[2][512] = {{0}};
              uint8_t buffer[2][8];
              uint16_t result[2];
              memset(buffer, 0xa5, sizeof(buffer));
              word(before, 0x7f | pc << 8 | rc << 10);
              word(before + 2, 0x4700 | top << 11 | (sticky ? 0x65 : 0));
              before[4] = (uint8_t[]){0,0x55,0xff}[tags];
              word(before + 6, 0x123); word(before + 8, 0x2345);
              word(before + 16, 0x3456); word(before + 24, 0x1f80);
              for (unsigned reg = 0; reg < 8; ++reg) {
                before[32 + reg * 16] = reg + 1;
                before[39 + reg * 16] = 0x80;
                word(before + 40 + reg * 16, 0x3fff);
              }
              for (unsigned implementation = 0; implementation < 2; ++implementation) {
                unsigned marker = 0;
                uint8_t *state = after[implementation];
                result[implementation] = invoke(functions[implementation][kind],
                    buffer[implementation] + 3, before, state, &marker);
                const uint16_t defined_sw = read_word(state + 2) & 0xb8ff;
                if (marker != 23 || read_word(state) != (kind == 3 ? 0x37f : read_word(before)) ||
                    defined_sw != (kind == 3 ? 0 : kind == 2 ? top << 11 :
                                              (read_word(before + 2) & 0xb8ff)) ||
                    state[4] != (kind == 3 ? 0 : before[4])) return 2;
                if ((kind == 0 && result[implementation] != read_word(before + 2)) ||
                    (kind != 0 && result[implementation]) ||
                    (kind == 1 && read_word(buffer[implementation] + 3) !=
                                  read_word(before + 2))) return 3;
                for (unsigned index = 0; index < 8; ++index)
                  if ((kind != 1 || (index != 3 && index != 4)) &&
                      buffer[implementation][index] != 0xa5) return 4;
                for (unsigned reg = 0; reg < 8; ++reg) {
                  const unsigned source = kind == 3 ? (reg + 8 - top) % 8 : reg;
                  if (memcmp(before + 32 + source * 16, state + 32 + reg * 16, 10)) return 5;
                }
                printf("{\"kind\":%u,\"generated\":%u,\"top\":%u,\"tags\":%u,"
                       "\"pc\":%u,\"rc\":%u,\"sticky\":%u,\"sw_defined\":%u}\n",
                       kind, implementation, top, before[4], pc, rc, sticky, defined_sw);
                // All non-init operations leave condition-code results undefined.
                if (kind != 3) state[3] &= 0xb8;
                ++observations;
              }
              if (memcmp(after[0], after[1], 24) ||
                  memcmp(buffer[0], buffer[1], 8) || result[0] != result[1]) return 6;
            }
      }
  const long pagesize = sysconf(_SC_PAGESIZE);
  if (pagesize <= 0) return 7;
  uint8_t *pages = mmap(0, pagesize * 2, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (pages == MAP_FAILED || mprotect(pages + pagesize, pagesize, PROT_NONE)) return 8;
  for (unsigned implementation = 0; implementation < 2; ++implementation) {
    _Alignas(16) uint8_t before[512] = {0}, after[512] = {0};
    word(before, 0x37f); word(before + 2, 0x2825); word(before + 24, 0x1f80);
    pages[pagesize - 3] = 0xa5;
    unsigned marker = 0;
    invoke(functions[implementation][1], pages + pagesize - 2, before, after, &marker);
    if (marker != 23 || read_word(pages + pagesize - 2) != 0x2825 ||
        pages[pagesize - 3] != 0xa5) return 9;
    printf("{\"boundary_case\":true,\"generated\":%u}\n", implementation);
    ++boundaries;
    for (unsigned pending = 0; pending < 2; ++pending)
      for (unsigned kind = 0; kind < 5; ++kind)
        for (unsigned bad = 0; bad < (kind == 1 ? 3u : 1u); ++bad) {
          if (fault_case(functions[implementation][kind], pending, bad, kind,
                          pages + pagesize - (bad == 2))) {
            fprintf(stderr, "fault mismatch: generated=%u pending=%u bad=%u kind=%u\n",
                    implementation, pending, bad, kind);
            return 10;
          }
          const unsigned signal = pending && kind == 4 ? SIGFPE : bad ? SIGSEGV : 0;
          printf("{\"fault_case\":true,\"generated\":%u,\"pending\":%u,"
                 "\"kind\":%u,\"bad_operand\":%u,\"signal\":%u}\n",
                 implementation, pending, kind, bad, signal);
          faults += !!signal;
          ++fault_cases;
        }
  }
  unsigned live_cases = 0;
  for (unsigned i = 0; i < 256; ++i) {
    volatile long double a = (long double)((int)i - 128) / 4;
    volatile long double b = (long double)(i + 1) / 8;
    const long double expected = a + b;
    if (live_x87(&a, &b) != expected) return 11;
    const uint64_t x = UINT64_C(0x01030507090b0d0f) * i;
    const uint64_t y = UINT64_C(0xff12ef34df56cf78) ^ i;
    uint64_t sum = 0;
    for (unsigned byte = 0; byte < 8; ++byte)
      sum |= (((x >> (byte * 8)) + (y >> (byte * 8))) & 255) << (byte * 8);
    const uint64_t actual = live_mmx(x, y);
    _Alignas(16) uint8_t state[512];
    __asm__ volatile("fxsave64 %0" : "=m"(state));
    if (actual != (sum ^ x) || state[4]) return 12;
    printf("{\"live_values\":true,\"case\":%u}\n", i);
    ++live_cases;
  }
  munmap(pages, pagesize * 2);
  printf("{\"state_observations\":%u,\"fault_observations\":%u,"
         "\"fault_and_nonfault_cases\":%u,\"boundary_observations\":%u,"
         "\"live_value_cases\":%u}\n", observations, faults, fault_cases, boundaries, live_cases);
  return 0;
}
'''


LIVE_IR = r'''
define x86_fp80 @live_x87(ptr %in, ptr %other) {
  %frame = alloca [128 x i8], align 64
  store volatile i8 43, ptr %frame
  %a = load volatile x86_fp80, ptr %in, align 1
  call void @llvm.x86.fninit()
  %b = load volatile x86_fp80, ptr %other, align 1
  %r = fadd x86_fp80 %a, %b
  %canary = load volatile i8, ptr %frame
  %ok = icmp eq i8 %canary, 43
  br i1 %ok, label %good, label %bad
good:
  ret x86_fp80 %r
bad:
  call void @llvm.trap()
  unreachable
}
define i64 @live_mmx(i64 %a, i64 %b) #0 {
  %x = bitcast i64 %a to <1 x i64>
  %y = bitcast i64 %b to <1 x i64>
  %sum = call <1 x i64> @llvm.x86.mmx.padd.b(<1 x i64> %x, <1 x i64> %y)
  call void @llvm.x86.fninit()
  %mixed = call <1 x i64> @llvm.x86.mmx.pxor(<1 x i64> %sum, <1 x i64> %x)
  %bits = bitcast <1 x i64> %mixed to i64
  call void @llvm.x86.fninit()
  ret i64 %bits
}
declare <1 x i64> @llvm.x86.mmx.padd.b(<1 x i64>, <1 x i64>)
declare <1 x i64> @llvm.x86.mmx.pxor(<1 x i64>, <1 x i64>)
declare void @llvm.trap()
attributes #0 = { "target-features"="+mmx" }
'''

LIVE_C = r'''
long double live_x87(volatile long double *in, volatile long double *other) {
  _Alignas(64) volatile unsigned char frame[128];
  frame[0] = 43;
  long double a = *in;
  __builtin_ia32_fninit();
  long double r = a + *other;
  if (frame[0] != 43) __builtin_trap();
  return r;
}
typedef char bytes8 __attribute__((vector_size(8)));
typedef long long word64 __attribute__((vector_size(8)));
unsigned long long live_mmx(unsigned long long a, unsigned long long b) {
  union { unsigned long long bits; bytes8 bytes; word64 word; } x = {.bits=a}, y = {.bits=b};
  bytes8 sum = __builtin_ia32_paddb(x.bytes, y.bytes);
  __builtin_ia32_fninit();
  word64 mixed = __builtin_ia32_pxor((word64)sum, x.word);
  union { unsigned long long bits; word64 word; } out = {.word=mixed};
  __builtin_ia32_fninit();
  return out.bits;
}
'''


def source(ir):
    result = []
    for name, native in (("read", "fnstsw"), ("store", "fnstsw.mem"),
                         ("clear", "fnclex"), ("init", "fninit"), ("wait", "fwait")):
        returns = name == "read"
        pointer = name == "store"
        if ir:
            result.append(
                f"declare {'i16' if returns else 'void'} @llvm.x86.{native}"
                f"({'ptr' if pointer else ''})\n"
                f"define i16 @generated_{name}(ptr %p, ptr %marker) {{\n"
                "store i32 17, ptr %marker\n" + ("%r = " if returns else "") +
                f"call {'i16' if returns else 'void'} @llvm.x86.{native}"
                f"({'ptr %p' if pointer else ''})\nstore i32 23, ptr %marker\n"
                f"ret i16 {'%r' if returns else '0'}\n}}\n")
        else:
            result.append(
                f"unsigned short generated_{name}(void *p, unsigned *marker) {{\n"
                "*marker = 17; " + ("unsigned short r = " if returns else "") +
                f"__builtin_ia32_{native.replace('.', '_')}({'p' if pointer else ''});\n"
                f"*marker = 23; return {'r' if returns else '0'};\n}}\n")
    return "\n".join(result) + (LIVE_IR if ir else LIVE_C)


def main():
    run_qualification(
        __doc__, HARNESS, {"ir": source(True), "c": source(False)},
        {"state_observations": 5760, "fault_observations": 10,
         "fault_and_nonfault_cases": 28, "boundary_observations": 2,
         "live_value_cases": 256},
        6047, "independently authored DF E0, DD /7, DB E2, DB E3 and 9B opcodes", __file__)


if __name__ == "__main__":
    main()
