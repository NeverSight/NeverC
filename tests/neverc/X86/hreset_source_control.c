//===- hreset_source_control.c - Controlled HRESET input observation
//-------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//
// The verifier replaces an independently decoded HRESET with UD2 and four
// NOPs. Observe the actual EAX at that boundary, skip six bytes and check the
// compiled return. This does not execute or emulate prediction-history reset.
//===----------------------------------------------------------------------===//

#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <ucontext.h>
#include <unistd.h>

extern unsigned invoke_hreset(unsigned);
static volatile uint32_t expected_control;
static volatile sig_atomic_t calls, bad_control;

static void observe(int signal, siginfo_t *info, void *context) {
  (void)info;
  greg_t *g = ((ucontext_t *)context)->uc_mcontext.gregs;
#ifdef __x86_64__
  const unsigned ip_index = REG_RIP, ax_index = REG_RAX;
#else
  const unsigned ip_index = REG_EIP, ax_index = REG_EAX;
#endif
  const unsigned char *ip = (const unsigned char *)(uintptr_t)g[ip_index];
  if (signal != SIGILL || ip[0] != 0x0f || ip[1] != 0x0b || ip[2] != 0x90 ||
      ip[3] != 0x90 || ip[4] != 0x90 || ip[5] != 0x90)
    _exit(90);
  if ((uint32_t)g[ax_index] != expected_control)
    bad_control = 1;
  calls = calls + 1;
  g[ip_index] += 6;
}

int main(void) {
  struct sigaction action = {0};
  action.sa_sigaction = observe;
  action.sa_flags = SA_SIGINFO;
  if (sigaction(SIGILL, &action, 0))
    return 91;
  unsigned state = 0x12345678;
  const unsigned edges[] = {0, 1, 0x80000000u, 0xffffffffu};
  for (unsigned i = 0; i != 4096; ++i) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    const unsigned input = i < 4 ? edges[i] : state;
    expected_control = input;
    const unsigned result = invoke_hreset(input);
    if (bad_control || result != input || calls != (sig_atomic_t)(i + 1)) {
      fprintf(stderr, "wrong control or return at %u: control=%d\n", i,
              bad_control);
      return 1;
    }
  }
  puts("{\"controlled_inputs_and_returns\":4096}");
  return 0;
}
