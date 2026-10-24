// Linux x86-64 host adapter for verify-rep-state.py. The compiler under test
// builds only the intrinsic wrappers; the host C compiler builds this oracle.
#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include <ucontext.h>
#include <unistd.h>

typedef void (*RepFunction)(const uint64_t *, uint64_t *);

// Raw arch_prctl keeps TLS/library calls outside an overridden FS window.
static long segment_base(long command, uintptr_t argument) {
  long result;
  __asm__ volatile("syscall"
                   : "=a"(result)
                   : "a"(158L), "D"(command), "S"(argument)
                   : "rcx", "r11", "memory");
  return result;
}

uint64_t rep_invoke(RepFunction function, const uint64_t *input,
                    uint64_t *output, unsigned segment, uintptr_t base) {
  uintptr_t previous = 0;
  const long get = segment == 0x64 ? 0x1003 : 0x1004;
  const long set = segment == 0x64 ? 0x1002 : 0x1001;
  if (segment &&
      (segment_base(get, (uintptr_t)&previous) || segment_base(set, base)))
    return UINT64_MAX;
  function(input, output);
  uint64_t flags;
  __asm__ volatile("pushfq; popq %0; cld" : "=r"(flags)::"cc", "memory");
  if (segment && segment_base(set, previous))
    __builtin_trap();
  return flags;
}

struct RepFault {
  uint64_t pc, size, page, page_size, controlled;
  uint64_t si, di, cx, ax, flags, dx, faults;
  uint64_t final_si, final_di, final_cx;
};
static struct RepFault *active_fault;

static void handle_fault(int signal, siginfo_t *info, void *raw) {
  (void)signal;
  (void)info;
  ucontext_t *context = raw;
  greg_t *gpr = context->uc_mcontext.gregs;
  struct RepFault *fault = active_fault;
  if (!fault || (uint64_t)gpr[REG_RIP] != fault->pc || fault->faults)
    _exit(121);
  fault->faults++;
  fault->si = gpr[REG_RSI];
  fault->di = gpr[REG_RDI];
  fault->cx = gpr[REG_RCX];
  fault->ax = gpr[REG_RAX];
  fault->flags = gpr[REG_EFL];
  fault->dx = gpr[REG_RDX];
  if (fault->controlled) {
    if (fault->controlled == 1)
      gpr[REG_RDI] = fault->final_di;
    else
      gpr[REG_RSI] = fault->final_si;
    gpr[REG_RCX] = fault->final_cx;
    gpr[REG_RIP] += fault->size;
  } else if (mprotect((void *)(uintptr_t)fault->page, fault->page_size,
                      PROT_READ | PROT_WRITE)) {
    _exit(122);
  }
}

uint64_t rep_fault_invoke(RepFunction function, const uint64_t *input,
                          uint64_t *output, struct RepFault *fault) {
  struct sigaction action, previous;
  memset(&action, 0, sizeof(action));
  action.sa_sigaction = handle_fault;
  action.sa_flags = SA_SIGINFO;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGSEGV, &action, &previous))
    return UINT64_MAX;
  active_fault = fault;
  const uint64_t flags = rep_invoke(function, input, output, 0, 0);
  active_fault = 0;
  if (sigaction(SIGSEGV, &previous, 0))
    __builtin_trap();
  return flags;
}
