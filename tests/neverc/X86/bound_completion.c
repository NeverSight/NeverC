// Independent original-byte witness for compiler-produced legacy BOUND.
#define _GNU_SOURCE
#include <asm/ldt.h>
#include <inttypes.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <ucontext.h>
#include <unistd.h>

typedef uint32_t (*Probe)(uint32_t, uintptr_t, volatile unsigned char *);
struct Case {
  Probe compiled, original;
  unsigned width, bits, prefix, dead;
  const char *name;
};
#include "bound_cases.inc"

static sigjmp_buf Resume;
static uint16_t OriginalFS, ActiveFS;
static volatile sig_atomic_t Trap, Error, AtBound;
static volatile uintptr_t FaultAddress;
static volatile unsigned char Marker;
struct Result {
  uint32_t value;
  int trap, error, bound, marker;
  uintptr_t address;
};

__attribute__((used, noinline)) static void fault_body(int sig, siginfo_t *info,
                                                       void *raw) {
  (void)sig;
  (void)info;
  __asm__ volatile("movw %0,%%fs" ::"rm"(OriginalFS) : "memory");
  ucontext_t *u = raw;
  Trap = u->uc_mcontext.gregs[REG_TRAPNO];
  Error = u->uc_mcontext.gregs[REG_ERR];
  FaultAddress = u->uc_mcontext.cr2;
  const unsigned char *pc = (const void *)u->uc_mcontext.gregs[REG_EIP];
  unsigned n = 0;
  while (n < 14 &&
         (pc[n] == 0x26 || pc[n] == 0x2e || pc[n] == 0x36 || pc[n] == 0x3e ||
          pc[n] == 0x64 || pc[n] == 0x65 || pc[n] == 0x66 || pc[n] == 0x67))
    ++n;
  AtBound = pc[n] == 0x62;
  siglongjmp(Resume, 1);
}
__attribute__((naked)) static void fault(int sig, siginfo_t *info, void *raw) {
  __asm__ volatile("pushl $0x202; popfl; jmp fault_body");
}
static struct Result invoke(Probe p, uint32_t index, uintptr_t address) {
  Trap = Error = AtBound = 0;
  FaultAddress = 0;
  Marker = 0;
  uint32_t value = 0;
  if (!sigsetjmp(Resume, 1)) {
    __asm__ volatile("movw %0,%%fs" ::"rm"(ActiveFS) : "memory");
    value = p(index, address, &Marker);
    __asm__ volatile("movw %0,%%fs" ::"rm"(OriginalFS) : "memory");
  }
  return (struct Result){value, Trap, Error, AtBound, Marker, FaultAddress};
}
static uint64_t Comparisons;
static void compare(const struct Case *c, uint32_t index, uintptr_t address,
                    unsigned scenario) {
  struct Result expected = invoke(c->original, index, address);
  struct Result actual = invoke(c->compiled, index, address);
  if (actual.trap != expected.trap || actual.error != expected.error ||
      actual.bound != expected.bound || actual.marker != expected.marker ||
      (actual.trap == 14 && actual.address != expected.address) ||
      (!actual.trap && actual.value != (c->dead ? 42u : expected.value))) {
    fprintf(stderr,
            "%s scenario=%u index=%08" PRIx32 " address=%08" PRIxPTR
            " expected=%d/%d/%d/%d/%08" PRIx32 " actual=%d/%d/%d/%d/%08" PRIx32
            "\n",
            c->name, scenario, index, address, expected.trap, expected.error,
            expected.bound, expected.marker, expected.value, actual.trap,
            actual.error, actual.bound, actual.marker, actual.value);
    exit(1);
  }
  ++Comparisons;
}
int main(void) {
  struct sigaction a = {.sa_sigaction = fault, .sa_flags = SA_SIGINFO};
  if (sigaction(SIGSEGV, &a, 0) || sigaction(SIGBUS, &a, 0) ||
      sigaction(SIGILL, &a, 0))
    return 2;
  __asm__ volatile("movw %%fs,%0" : "=rm"(OriginalFS));
  unsigned char *mem = mmap(0, 0x40000, PROT_READ | PROT_WRITE,
                            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (mem == MAP_FAILED)
    return 2;
  const uint32_t indices[] = {0,          1,          4,          5,
                              6,          0xfffffffa, 0xfffffffb, 0xfffffffc,
                              0x7fff,     0x8000,     0xffff,     0xffff8000,
                              0x7fffffff, 0x80000000, 0xffffffff};
  for (unsigned cidx = 0; cidx != sizeof(Cases) / sizeof(Cases[0]); ++cidx) {
    const struct Case *c = &Cases[cidx];
    for (unsigned scenario = 0; scenario != 10; ++scenario) {
      if (scenario >= 6 && !c->prefix)
        continue;
      if (mprotect(mem, 0x40000, PROT_READ | PROT_WRITE))
        return 2;
      memset(mem, 0, 0x40000);
      unsigned offset = scenario == 2   ? 0x2000
                        : scenario == 3 ? 0x2000 - c->width
                        : scenario >= 8 ? 0x10000 - c->width
                        : scenario == 4 ? 0x1001
                                        : 0x1000;
      uint32_t low = -4, high = 4;
      memcpy(mem + offset, &low, c->width);
      memcpy(mem + offset + c->width, &high, c->width);
      if (scenario >= 8) {
        high = -100;
        memcpy(mem, &high, c->width);
      }
      ActiveFS = OriginalFS;
      if (c->prefix) {
        ActiveFS = 15;
        struct user_desc d = {.entry_number = 1,
                              .base_addr = (uintptr_t)mem,
                              .limit = scenario == 6   ? offset + c->width - 1
                                       : scenario == 7 ? offset + c->width - 2
                                       : scenario == 9 ? 0xffff
                                                       : 0x3ffff,
                              .seg_32bit = 1,
                              .contents = 0,
                              .read_exec_only = 1,
                              .useable = 1};
        if (syscall(SYS_modify_ldt, 1, &d, sizeof(d)))
          return 2;
      }
      if (scenario == 1 && mprotect(mem, 0x40000, PROT_READ))
        return 2;
      if ((scenario == 2 || scenario == 3) &&
          mprotect(mem + 0x2000, 4096, PROT_NONE))
        return 2;
      uintptr_t address = c->prefix ? offset : (uintptr_t)(mem + offset);
      if (c->bits == 16)
        address |= 0xabcd0000u;
      for (unsigned i = 0; i != sizeof(indices) / sizeof(indices[0]); ++i)
        compare(c, indices[i], address, scenario);
      if (scenario == 5) {
        low = c->width == 2 ? 0xffff8000u : 0x80000000u;
        high = c->width == 2 ? 0x7fffu : 0x7fffffffu;
        memcpy(mem + offset, &low, c->width);
        memcpy(mem + offset + c->width, &high, c->width);
        // Exhaust every word index, including transport of nonzero high bits.
        for (uint32_t i = 0; i != 65536; ++i)
          compare(c, 0xa5a50000u | i, address, scenario);
      }
    }
  }
  printf("{\"comparisons\":%" PRIu64 ",\"cases\":%zu}\n", Comparisons,
         sizeof(Cases) / sizeof(Cases[0]));
  return 0;
}
