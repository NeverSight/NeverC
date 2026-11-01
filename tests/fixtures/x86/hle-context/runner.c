/* SPDX-License-Identifier: AGPL-3.0-only */
#define _GNU_SOURCE
#include "cases.h"
#include <cpuid.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

double live_values[6], live_result[3];
static sigjmp_buf fault;
static volatile sig_atomic_t fault_signal;
static void handler(int signal, siginfo_t *info, void *context) {
  (void)info;
  (void)context;
  fault_signal = signal;
  siglongjmp(fault, 1);
}
static uint64_t random64(uint64_t *s) {
  *s ^= *s << 13;
  *s ^= *s >> 7;
  *s ^= *s << 17;
  return *s;
}
static uint64_t read64(const void *p) {
  uint64_t v;
  memcpy(&v, p, 8);
  return v;
}
static void write64(void *p, uint64_t v) { memcpy(p, &v, 8); }
static void fail(unsigned form, unsigned seed, const char *what) {
  fprintf(stderr, "form=%u seed=%u: %s\n", form, seed, what);
  exit(1);
}
int main(void) {
  struct sigaction action = {.sa_sigaction = handler, .sa_flags = SA_SIGINFO};
  sigemptyset(&action.sa_mask);
  sigaction(SIGSEGV, &action, 0);
  sigaction(SIGBUS, &action, 0);
  sigaction(SIGILL, &action, 0);
  unsigned cpua, cpub, cpuc, cpud, hle = 0;
  if (__get_cpuid_count(7, 0, &cpua, &cpub, &cpuc, &cpud))
    hle = (cpub >> 4) & 1;
  unsigned pairs = 0, faults = 0;
  void *bad = mmap(0, 4096, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (bad == MAP_FAILED)
    return 1;
  for (unsigned form = 0; form < sizeof(cases) / sizeof(cases[0]); ++form) {
    const struct test_case *t = &cases[form];
    for (unsigned seed = 0; seed < 256; ++seed) {
      _Alignas(64) unsigned char original[512], candidate[512];
      unsigned char o[96], c[96];
      uint64_t r = 0x123456789abcdef0ull + seed + form * 971;
      for (unsigned i = 0; i < sizeof(original); i += 8)
        write64(original + i, random64(&r));
      for (unsigned i = 0; i < sizeof(o); i += 8)
        write64(o + i, random64(&r));
      uint64_t flags = 0x202 | (random64(&r) & 0x8d5);
      write64(o + 57, flags);
      if (t->op >= 3 && t->op <= 5) {
        /* Signed register indexes exercise preceding/following memory words. */
        int64_t index = (int)(seed % (t->bytes * 32)) - (int)(t->bytes * 16);
        write64(o + 11, index);
      }
      if (t->op == 6 && (seed & 1)) {
        uint64_t mask = ~0ull >> (64 - t->bytes * 8);
        write64(o + 3,
                (read64(o + 3) & ~mask) | (read64(original + 128) & mask));
      }
      if (t->op == 7 && (seed & 1)) {
        uint64_t value = read64(original + 128);
        write64(o + 3,
                (read64(o + 3) & 0xffffffff00000000ull) | (uint32_t)value);
        write64(o + 19,
                (read64(o + 19) & 0xffffffff00000000ull) | (value >> 32));
      }
      memcpy(candidate, original, sizeof(original));
      memcpy(c, o, sizeof(o));
      fault_signal = 0;
      if (sigsetjmp(fault, 1))
        fail(form, seed, "unexpected native fault");
      t->original(original + 128, o + 3, o + 57);
      uint64_t expected = read64(c + 3) ^ (read64(c + 11) * 13) ^
                          (read64(c + 19) + 71) ^ (read64(c + 27) >> 7);
      for (unsigned v = 0; v < 6; ++v)
        live_values[v] = (double)(seed + v * 19 + 1) * 0.25;
      double fp[3] = {live_values[0] + live_values[1],
                      live_values[2] - live_values[3],
                      live_values[4] * live_values[5]};
      if (t->candidate(candidate + 128, c + 3, c + 57) != expected ||
          memcmp(fp, live_result, sizeof(fp)))
        fail(form, seed, "live integer or FP registers");
      /* Undefined flags are architectural choices, never an equality oracle. */
      uint64_t defined = ~(uint64_t)t->undefined_flags;
      if ((read64(o + 57) ^ read64(c + 57)) & defined)
        fail(form, seed, "defined flags");
      write64(o + 57, (read64(o + 57) & defined) | (read64(c + 57) & ~defined));
      if (memcmp(o, c, sizeof(o)))
        fail(form, seed, "GPR outputs or context canaries");
      if (memcmp(original, candidate, sizeof(original)))
        fail(form, seed, "memory");
      ++pairs;
    }
    for (unsigned version = 0; version < 2; ++version) {
      unsigned char state[96], before[96];
      memset(state, 0x39, sizeof(state));
      if (t->op >= 3 && t->op <= 5)
        write64(state + 11, 0);
#ifdef HLE_SOURCE
      write64(state + 57, 0x202);
#endif
      memcpy(before, state, sizeof(state));
      fault_signal = 0;
      if (!sigsetjmp(fault, 1)) {
        if (version)
          t->candidate(bad, state + 3, state + 57);
        else
          t->original(bad, state + 3, state + 57);
        fail(form, version, "missing memory fault");
      }
      if (fault_signal != SIGSEGV || memcmp(state, before, sizeof(state)))
        fail(form, version, "fault published context or changed signal");
      ++faults;
    }
  }
  munmap(bad, 4096);
  printf("{\"pairs\":%u,\"fault_before_publication\":%u,\"hardware_hle_"
         "available\":%u,"
         "\"physical_transaction_commit_qualified\":false}\n",
         pairs, faults, hle);
  return 0;
}
