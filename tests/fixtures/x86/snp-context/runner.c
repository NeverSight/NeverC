/* SPDX-License-Identifier: AGPL-3.0-only */
#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

typedef unsigned long long u64;
extern u64 candidate(unsigned, void *, void *, u64, const double *, double *);
#define DECL(n) extern void original_##n(void *, void *);
DECL(0)
DECL(1) DECL(2) DECL(3) DECL(4) DECL(5) DECL(6) DECL(7) DECL(8) DECL(9) DECL(10)
    DECL(11) static void (*const originals[])(void *, void *) = {
        original_0, original_1, original_2,  original_3,
        original_4, original_5, original_6,  original_7,
        original_8, original_9, original_10, original_11};
static volatile sig_atomic_t active_form, emulate = 1, calls;
static volatile u64 service_ram, observed[3];

static void fault(int signal, siginfo_t *info, void *context) {
  (void)info;
  ucontext_t *u = context;
  greg_t *r = u->uc_mcontext.gregs;
  const unsigned char *pc = (const unsigned char *)(uintptr_t)r[REG_RIP];
  unsigned n = 0, width = 64, segment = 0;
  if (pc[n] == 0x65) {
    segment = 0x65;
    ++n;
  }
  if (pc[n] == 0x67) {
    width = 32;
    ++n;
  }
  unsigned pre = pc[n++];
  if ((pre != 0xf2 && pre != 0xf3) || pc[n++] != 0x0f || pc[n++] != 1)
    _exit(120);
  unsigned opcode = pc[n++], op = 99;
  if (opcode == 0xff)
    op = pre == 0xf3 ? 0 : 1;
  if (opcode == 0xfe)
    op = pre == 0xf3 ? 2 : 3;
  if (opcode == 0xfd)
    op = pre == 0xf3 ? 4 : 5;
  if (opcode == 0xfc)
    op = pre == 0xf3 ? 7 : 6;
  if (opcode == 0xd9)
    op = 9;
  unsigned expected = active_form == 8    ? 1
                      : active_form == 11 ? 5
                      : active_form >= 9  ? 9
                                          : active_form;
  if (op != expected || (width == 32) != (active_form == 8) ||
      (segment == 0x65) != (active_form == 11))
    _exit(121);
  if (!emulate)
    _exit(64 + signal);
  u64 a = r[REG_RAX], b = r[REG_RCX], c = r[REG_RDX];
  observed[0] = a;
  observed[1] = observed[2] = 0;
  ++calls;
  if (op == 9) {
    service_ram = a ^ 0x3c3c3c3c3c3c3c3cull;
    r[REG_RIP] += n;
    return;
  }
  if (op != 0 && op != 4)
    observed[1] = b;
  if (op == 1 || op == 2 || op == 4)
    observed[2] = c;
  if (op == 1) {
    if (width == 32)
      a = (uint32_t)a;
    b = (uint32_t)b;
    c = (uint32_t)c;
  }
  u64 h = a;
  if (op != 0 && op != 4)
    h ^= b;
  if (op == 1 || op == 2 || op == 4)
    h ^= c;
  h ^= 0x13579bdf2468ace0ull + op;
  service_ram = h;
  if (op == 4) {
    r[REG_RAX] = (uint32_t)h;
    r[REG_RCX] = h ^ 0x1122334455667788ull;
    r[REG_RDX] = h + 71;
  } else if (op == 7) {
    r[REG_RAX] = a + 4096;
    r[REG_RCX] = b - 1;
  } else if (op != 6)
    r[REG_RAX] = (uint32_t)h;
  unsigned mask = op == 6 ? 1 : (op == 1 || op == 7 ? 0x8d5 : 0x8d4);
  r[REG_EFL] = (r[REG_EFL] & ~(u64)mask) | (h & mask);
  r[REG_RIP] += n;
}

static void expected(unsigned form, unsigned char *bytes, unsigned go,
                     unsigned fo, u64 *ram) {
  u64 g[3], f;
  memcpy(g, bytes + go, 24);
  memcpy(&f, bytes + fo, 8);
  if (form == 9 || form == 10) {
    *ram = g[0] ^ 0x3c3c3c3c3c3c3c3cull;
    return;
  }
  unsigned op = form == 8 ? 1 : form == 11 ? 5 : form;
  u64 a = g[0], b = g[1], c = g[2];
  if (op == 1) {
    b &= UINT32_MAX;
    c &= UINT32_MAX;
    if (form == 8)
      a &= UINT32_MAX;
  }
  u64 inputs[] = {a, a ^ b ^ c, a ^ b ^ c, a ^ b, a ^ c, a ^ b, a ^ b, a ^ b};
  u64 h = inputs[op] ^ (0x13579bdf2468ace0ull + op);
  *ram = h;
  switch (op) {
  case 4:
    g[1] = h ^ 0x1122334455667788ull;
    g[2] = h + 71;
  case 0:
  case 1:
  case 2:
  case 3:
  case 5:
    g[0] = (uint32_t)h;
    break;
  case 7:
    g[0] += 4096;
    --g[1];
    break;
  }
  const unsigned masks[] = {0x8d4, 0x8d5, 0x8d4, 0x8d4, 0x8d4, 0x8d4, 1, 0x8d5};
  f = (f & ~(u64)masks[op]) | (h & masks[op]);
  memcpy(bytes + go, g, 24);
  memcpy(bytes + fo, &f, 8);
}

int main(void) {
  struct rlimit limit = {0, 0};
  setrlimit(RLIMIT_CORE, &limit);
  struct sigaction action = {0};
  action.sa_sigaction = fault;
  action.sa_flags = SA_SIGINFO;
  sigemptyset(&action.sa_mask);
  if (sigaction(SIGILL, &action, 0) || sigaction(SIGSEGV, &action, 0))
    return 2;
  const double fp[] = {1.25, 2.5, 17.75, 6.25, 3.5, 8};
  double out[3];
  unsigned pairs = 0, faults = 0;
#ifdef SNP_SOURCE
  const unsigned forms = 11;
#else
  const unsigned forms = 12;
#endif
  for (unsigned form = 0; form < forms; ++form)
    for (unsigned seed = 0; seed < 256; ++seed) {
      unsigned char before[192], a[192], b[192], want[192];
      for (unsigned i = 0; i < 192; ++i)
        before[i] = (unsigned char)(seed + i * 17);
      unsigned go = 16 + (seed & 7), fo = 96 + ((seed >> 3) & 7);
      u64 inputs[] = {0xfedcba9876543210ull + seed * 971,
                      0x8000000098765432ull + seed * 19,
                      0x7fffffff87654321ull + seed * 71};
      u64 flags = 0x202 | (seed * 137 & 0x8d5);
#ifndef SNP_SOURCE
      flags |= 0xface000000000000ull;
#endif
      memcpy(before + go, inputs, 24);
      memcpy(before + fo, &flags, 8);
      memcpy(a, before, 192);
      memcpy(b, before, 192);
      memcpy(want, before, 192);
      u64 memory;
      expected(form, want, go, fo, &memory);
      active_form = form;
      calls = 0;
      service_ram = 0;
      originals[form](a + go, a + fo);
      if (calls != 1 || service_ram != memory || memcmp(a, want, 192)) {
        fprintf(stderr, "reference mismatch form=%u seed=%u\n", form, seed);
        return 3;
      }
      u64 seen[] = {observed[0], observed[1], observed[2]};
      calls = 0;
      service_ram = 0;
      u64 live = candidate(form, b + go, b + fo, seed, fp, out);
      u64 live_want = (seed + 7ull) ^ (seed * 13ull) ^
                      (seed ^ 0xfedcba9876543210ull) ^
                      (seed + 0x123456789abcdef0ull);
      if (calls != 1 || service_ram != memory || memcmp(b, want, 192) ||
          live != live_want || out[0] != 3.75 || out[1] != 11.5 ||
          out[2] != 28 || seen[0] != observed[0] || seen[1] != observed[1] ||
          seen[2] != observed[2]) {
        fprintf(stderr, "candidate mismatch form=%u seed=%u calls=%d\n", form,
                seed, calls);
        return 4;
      }
      ++pairs;
    }
  unsigned char *shared =
      mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
  if (shared == MAP_FAILED)
    return 5;
  int reference_signal = 0;
  for (unsigned form = 0; form < forms; ++form)
    for (unsigned which = 0; which < 2; ++which) {
      unsigned char before[192];
      memset(before, 0x7b, sizeof(before));
      u64 flags = 0x202;
      memcpy(before + 96, &flags, 8);
      memcpy(shared, before, 192);
      pid_t child = fork();
      if (child < 0)
        return 6;
      if (!child) {
        emulate = 0;
        active_form = form;
        if (which)
          candidate(form, shared + 16, shared + 96, 7, fp, out);
        else
          originals[form](shared + 16, shared + 96);
        _exit(122);
      }
      int status;
      if (waitpid(child, &status, 0) != child)
        return 7;
      if (!WIFEXITED(status) ||
          (WEXITSTATUS(status) != 64 + SIGILL &&
           WEXITSTATUS(status) != 64 + SIGSEGV) ||
          memcmp(before, shared, 192))
        return 8;
      if (which && WEXITSTATUS(status) != reference_signal)
        return 9;
      reference_signal = WEXITSTATUS(status);
      ++faults;
    }
  munmap(shared, 4096);
  printf("{\"pairs\":%u,\"native_instruction_faults\":%u,\"fault_before_"
         "publication\":%u,"
         "\"live_fp_cases\":%u,\"physical_snp_qualified\":false}\n",
         pairs, 2 * pairs, faults, pairs);
  return 0;
}
