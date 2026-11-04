// Native bit-string CF, memory, signed-index and page-fault oracle.
#define _GNU_SOURCE
#include <inttypes.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

typedef unsigned char (*operation)(void *, int64_t, unsigned char *);
#define BIT_CASE(Name, Width, Action, Used)                                    \
  extern unsigned char Name(void *, int64_t, unsigned char *);
#include "bit_cases.inc"
#undef BIT_CASE
static const struct {
  const char *name;
  operation function;
  unsigned width, action, used;
} cases[] = {
#define BIT_CASE(Name, Width, Action, Used) {#Name, Name, Width, Action, Used},
#include "bit_cases.inc"
#undef BIT_CASE
};

static sigjmp_buf escape;
static volatile sig_atomic_t active, signal_number, signal_code;
static volatile uintptr_t fault_address;
static volatile unsigned fault_error, carry;
static volatile unsigned char marker;

static void fault(int number, siginfo_t *info, void *context) {
  if (!active || (number != SIGSEGV && number != SIGBUS))
    _Exit(125);
  const ucontext_t *state = context;
  signal_number = number;
  signal_code = info->si_code;
  fault_address = (uintptr_t)info->si_addr;
  fault_error = state->uc_mcontext.gregs[REG_ERR];
  active = 0;
  siglongjmp(escape, 1);
}

static unsigned char original(unsigned width, unsigned action, void *base,
                              int64_t index) {
  unsigned char result;
#define ORIGINAL(Width, Type, Suffix)                                          \
  case Width: {                                                                \
    Type bit = index;                                                          \
    switch (action) {                                                          \
    case 0:                                                                    \
      __asm__ volatile("bt" Suffix " %2,(%1)"                                  \
                       : "=@ccc"(result)                                       \
                       : "r"(base), "r"(bit)                                   \
                       : "memory");                                            \
      break;                                                                   \
    case 1:                                                                    \
      __asm__ volatile("bts" Suffix " %2,(%1)"                                 \
                       : "=@ccc"(result)                                       \
                       : "r"(base), "r"(bit)                                   \
                       : "memory");                                            \
      break;                                                                   \
    case 2:                                                                    \
      __asm__ volatile("btr" Suffix " %2,(%1)"                                 \
                       : "=@ccc"(result)                                       \
                       : "r"(base), "r"(bit)                                   \
                       : "memory");                                            \
      break;                                                                   \
    default:                                                                   \
      __asm__ volatile("btc" Suffix " %2,(%1)"                                 \
                       : "=@ccc"(result)                                       \
                       : "r"(base), "r"(bit)                                   \
                       : "memory");                                            \
      break;                                                                   \
    }                                                                          \
    break;                                                                     \
  }
  switch (width) {
    ORIGINAL(16, int16_t, "w")
    ORIGINAL(32, int32_t, "l")
#if UINTPTR_MAX == UINT64_MAX
    ORIGINAL(64, int64_t, "q")
#endif
  default:
    abort();
  }
#undef ORIGINAL
  return result;
}

struct outcome {
  int signal, code;
  uintptr_t address;
  unsigned error, cf, marker;
};

static struct outcome run(unsigned which, int compiled, void *base,
                          int64_t index) {
  signal_number = signal_code = fault_address = fault_error = carry = marker =
      0;
  if (!sigsetjmp(escape, 1)) {
    active = 1;
    if (compiled) {
      carry = cases[which].function(base, index, (unsigned char *)&marker);
    } else {
      marker = 17;
      unsigned char result =
          original(cases[which].width, cases[which].action, base, index);
      carry = cases[which].used ? result : 0;
      marker = 29;
    }
    active = 0;
  }
  return (struct outcome){signal_number, signal_code, fault_address,
                          fault_error,   carry,       marker};
}

static unsigned char *memory, *initial, *native;
static size_t page, size;
static unsigned long comparisons, faults;

static void compare(unsigned which, size_t offset, int64_t index,
                    unsigned mode) {
  if (mprotect(memory, size, PROT_READ | PROT_WRITE))
    abort();
  memcpy(memory, initial, size);
  if (mode && mprotect(memory + page, page, mode == 2 ? PROT_READ : PROT_NONE))
    abort();
  void *base = (void *)((uintptr_t)memory + offset);
  struct outcome a = run(which, 0, base, index);
  if (mprotect(memory, size, PROT_READ | PROT_WRITE))
    abort();
  memcpy(native, memory, size);
  memcpy(memory, initial, size);
  if (mode && mprotect(memory + page, page, mode == 2 ? PROT_READ : PROT_NONE))
    abort();
  struct outcome b = run(which, 1, base, index);
  if (mprotect(memory, size, PROT_READ | PROT_WRITE))
    abort();
  if (a.signal != b.signal || a.code != b.code || a.address != b.address ||
      a.error != b.error || a.cf != b.cf || a.marker != b.marker ||
      memcmp(native, memory, size)) {
    fprintf(stderr,
            "%s offset=%zu index=%" PRId64 " mode=%u: native=%d/%d/%" PRIxPTR
            "/%x/%u/%u compiled=%d/%d/%" PRIxPTR "/%x/%u/%u memory=%d\n",
            cases[which].name, offset, index, mode, a.signal, a.code, a.address,
            a.error, a.cf, a.marker, b.signal, b.code, b.address, b.error, b.cf,
            b.marker, memcmp(native, memory, size));
    exit(1);
  }
  ++comparisons;
  faults += !!a.signal;
}

int main(void) {
  page = sysconf(_SC_PAGESIZE);
  size = page * 3;
  memory =
      mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  initial = malloc(size);
  native = malloc(size);
  if (memory == MAP_FAILED || !initial || !native)
    return 125;
  struct sigaction handler = {.sa_sigaction = fault, .sa_flags = SA_SIGINFO};
  sigemptyset(&handler.sa_mask);
  if (sigaction(SIGSEGV, &handler, 0) || sigaction(SIGBUS, &handler, 0))
    return 125;
  for (unsigned pattern = 0; pattern != 4; ++pattern) {
    for (size_t i = 0; i != size; ++i)
      initial[i] = pattern == 0   ? 0
                   : pattern == 1 ? 255
                   : pattern == 2 ? (i * 37 + 19) & 255
                                  : (i & 1 ? 0x55 : 0xaa);
    for (unsigned which = 0; which != sizeof(cases) / sizeof(cases[0]);
         ++which) {
      for (unsigned alignment = 0; alignment != 8; ++alignment)
        for (int64_t index = -257; index <= 257; ++index)
          compare(which, page + 512 + alignment, index, 0);
      const int64_t extremes[] = {INT16_MIN, INT16_MAX, INT32_MIN, INT32_MAX,
                                  INT64_MIN, INT64_MAX, -32769,    32768};
      for (unsigned n = 0; n != sizeof(extremes) / sizeof(extremes[0]); ++n) {
        const int64_t index = cases[which].width == 16   ? (int16_t)extremes[n]
                              : cases[which].width == 32 ? (int32_t)extremes[n]
                                                         : extremes[n];
        const int64_t word =
            index / cases[which].width - (index % cases[which].width < 0);
        const size_t offset =
            page + 512 - (uint64_t)word * (cases[which].width / 8);
        compare(which, offset, extremes[n], 0);
      }
      for (unsigned alignment = 0; alignment != cases[which].width / 8;
           ++alignment) {
        compare(which, page - cases[which].width / 8 + 1 + alignment, 0, 1);
        compare(which, page + 32 + alignment, -1, 2);
        compare(which, page - 16 + alignment, 128, 1);
        compare(which, 2 * page + alignment, -1, 1);
      }
    }
  }
  printf("{\"comparisons\":%lu,\"faults\":%lu,\"functions\":%zu}\n",
         comparisons, faults, sizeof(cases) / sizeof(cases[0]));
  munmap(memory, size);
  free(initial);
  free(native);
  return 0;
}
