// Native DIV/IDIV result and fault oracle for verify_native_division.py.
#define _GNU_SOURCE
#include <inttypes.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <ucontext.h>

typedef void (*operation)(uint64_t, uint64_t, uint64_t, uint64_t *, uint64_t *,
                          unsigned char *);
#define DIVISION_CASE(Name, Bits, Signed, Results)                             \
  extern void Name(uint64_t, uint64_t, uint64_t, uint64_t *, uint64_t *,       \
                   unsigned char *);
#include "division_cases.inc"
#undef DIVISION_CASE
static const struct {
  const char *name;
  operation function;
  unsigned bits, is_signed, results;
} cases[] = {
#define DIVISION_CASE(Name, Bits, Signed, Results)                             \
  {#Name, Name, Bits, Signed, Results},
#include "division_cases.inc"
#undef DIVISION_CASE
};

static sigjmp_buf escape;
static volatile sig_atomic_t active;
static volatile uint64_t fault_ax, fault_dx;
static volatile uintptr_t fault_pc;

static void fault(int signal, siginfo_t *info, void *context) {
  if (signal != SIGFPE || !active || info->si_code != FPE_INTDIV)
    _Exit(125);
  const ucontext_t *state = context;
#if UINTPTR_MAX == UINT64_MAX
  fault_ax = state->uc_mcontext.gregs[REG_RAX];
  fault_dx = state->uc_mcontext.gregs[REG_RDX];
  fault_pc = state->uc_mcontext.gregs[REG_RIP];
#else
  fault_ax = (uint32_t)state->uc_mcontext.gregs[REG_EAX];
  fault_dx = (uint32_t)state->uc_mcontext.gregs[REG_EDX];
  fault_pc = (uint32_t)state->uc_mcontext.gregs[REG_EIP];
#endif
  active = 0;
  siglongjmp(escape, 1);
}

static int original(unsigned bits, unsigned sign, uint64_t low, uint64_t high,
                    uint64_t divisor, uint64_t *quotient, uint64_t *remainder) {
  if (sigsetjmp(escape, 1))
    return 1;
  active = 1;
  switch (bits) {
  case 8: {
    uint16_t ax = (uint8_t)low | (uint16_t)((uint8_t)high << 8);
    uint8_t d = divisor;
    if (sign)
      __asm__ volatile("idivb %1" : "+a"(ax) : "c"(d) : "cc");
    else
      __asm__ volatile("divb %1" : "+a"(ax) : "c"(d) : "cc");
    *quotient = (uint8_t)ax;
    *remainder = (uint8_t)(ax >> 8);
    break;
  }
#define ORIGINAL(Width, Type, Suffix)                                          \
  case Width: {                                                                \
    Type a = low, d = high, c = divisor;                                       \
    if (sign)                                                                  \
      __asm__ volatile("idiv" Suffix " %2"                                     \
                       : "+a"(a), "+d"(d)                                      \
                       : "c"(c)                                                \
                       : "cc");                                                \
    else                                                                       \
      __asm__ volatile("div" Suffix " %2" : "+a"(a), "+d"(d) : "c"(c) : "cc"); \
    *quotient = a;                                                             \
    *remainder = d;                                                            \
    break;                                                                     \
  }
    ORIGINAL(16, uint16_t, "w")
    ORIGINAL(32, uint32_t, "l")
#if UINTPTR_MAX == UINT64_MAX
    ORIGINAL(64, uint64_t, "q")
#endif
#undef ORIGINAL
  default:
    _Exit(126);
  }
  active = 0;
  return 0;
}

static int generated(operation function, uint64_t low, uint64_t high,
                     uint64_t divisor, uint64_t *q, uint64_t *r,
                     unsigned char *marker) {
  if (sigsetjmp(escape, 1))
    return 1;
  active = 1;
  function(low, high, divisor, q, r, marker);
  active = 0;
  return 0;
}

static int valid_fault(unsigned bits, unsigned sign, uint64_t low,
                       uint64_t high) {
  const uint64_t mask = UINT64_MAX >> (64 - bits);
  const unsigned char *pc = (const unsigned char *)fault_pc;
  if (*pc == 0x66)
    ++pc;
#if UINTPTR_MAX == UINT64_MAX
  if ((*pc & 0xf0) == 0x40)
    ++pc;
#endif
  if (pc[0] != (bits == 8 ? 0xf6 : 0xf7) ||
      ((pc[1] >> 3) & 7) != (sign ? 7 : 6) || (pc[1] & 0xc0) != 0xc0)
    return 0;
  if (bits == 8)
    return (fault_ax & 0xffff) == ((low & 255) | ((high & 255) << 8));
  return (fault_ax & mask) == (low & mask) &&
         (fault_dx & mask) == (high & mask);
}

int main(void) {
  struct sigaction action = {0};
  action.sa_sigaction = fault;
  action.sa_flags = SA_SIGINFO;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGFPE, &action, NULL))
    return 2;
  uint64_t comparisons = 0, faults = 0;
  for (unsigned test = 0; test != sizeof(cases) / sizeof(cases[0]); ++test) {
    const unsigned bits = cases[test].bits, sign = cases[test].is_signed;
    const uint64_t mask = UINT64_MAX >> (64 - bits), middle = UINT64_C(1)
                                                              << (bits - 1);
    const uint64_t values[] = {
        0,      1,          2,          3,
        7,      13,         mask,       mask - 1,
        middle, middle - 1, middle + 1, UINT64_C(0x876543210abcdef0) & mask};
    for (unsigned a = 0; a != sizeof(values) / sizeof(values[0]); ++a)
      for (unsigned d = 0; d != sizeof(values) / sizeof(values[0]); ++d)
        for (unsigned c = 0; c != sizeof(values) / sizeof(values[0]); ++c) {
          const uint64_t untouched_q = UINT64_C(0xaabbccdd12345678);
          const uint64_t untouched_r = UINT64_C(0x0123456789abcdef);
          uint64_t q = untouched_q, r = untouched_r;
          const int expected_fault =
              original(bits, sign, values[a], values[d], values[c], &q, &r);
          if (expected_fault && !valid_fault(bits, sign, values[a], values[d]))
            return 3;
          uint64_t actual_q = untouched_q, actual_r = untouched_r;
          unsigned char marker = 0x91;
          const int actual_fault =
              generated(cases[test].function, values[a], values[d], values[c],
                        &actual_q, &actual_r, &marker);
          if (actual_fault != expected_fault ||
              (actual_fault &&
               !valid_fault(bits, sign, values[a], values[d])) ||
              marker != (expected_fault ? 17 : 29) ||
              actual_q != (expected_fault || !(cases[test].results & 1)
                               ? untouched_q
                               : q) ||
              actual_r != (expected_fault || !(cases[test].results & 2)
                               ? untouched_r
                               : r)) {
            fprintf(stderr,
                    "%s low=%" PRIx64 " high=%" PRIx64 " divisor=%" PRIx64
                    " fault=%d/%d marker=%u q=%" PRIx64 "/%" PRIx64
                    " r=%" PRIx64 "/%" PRIx64 "\n",
                    cases[test].name, values[a], values[d], values[c],
                    actual_fault, expected_fault, marker, actual_q, q, actual_r,
                    r);
            return 1;
          }
          ++comparisons;
          faults += expected_fault;
        }
  }
  printf("{\"comparisons\":%" PRIu64 ",\"faults\":%" PRIu64
         ",\"functions\":%zu}\n",
         comparisons, faults, sizeof(cases) / sizeof(cases[0]));
  return 0;
}
