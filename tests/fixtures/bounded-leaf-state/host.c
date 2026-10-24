// Linux x64 CPL3 oracle for bounded SGX/SMX state transport. Compare complete
// fault contexts or normal results with independently encoded instructions.
#define _GNU_SOURCE
#include <dlfcn.h>
#include <inttypes.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ucontext.h>
#include <unistd.h>

typedef void (*Probe)(const uint64_t *, uint64_t *);
static sigjmp_buf Recovery;
static uint64_t FaultState[5];
static volatile sig_atomic_t FaultSignal;
static volatile sig_atomic_t NativeBoundary;
static int Enclu;

static uint64_t packFlags(uint64_t Flags) {
  return ((Flags & 0xd5) << 8) | ((Flags >> 11) & 1);
}

static void onFault(int Signal, siginfo_t *Info, void *Context) {
  (void)Info;
  const ucontext_t *C = Context;
  const greg_t *R = C->uc_mcontext.gregs;
  const unsigned char *IP = (const unsigned char *)(uintptr_t)R[REG_RIP];
  NativeBoundary = IP[0] == 0x0f &&
                   (Enclu ? (IP[1] == 0x01 && IP[2] == 0xd7) : IP[1] == 0x37);
  FaultState[0] = R[REG_RAX];
  FaultState[1] = R[REG_RBX];
  FaultState[2] = R[REG_RCX];
  FaultState[3] = R[REG_RDX];
  FaultState[4] = packFlags(R[REG_EFL]);
  FaultSignal = Signal;
  siglongjmp(Recovery, 1);
}

static int observe(Probe Function, const uint64_t *Input, uint64_t *Output) {
  FaultSignal = 0;
  NativeBoundary = 0;
  memset(Output, 0xa5, 5 * sizeof(*Output));
  if (!sigsetjmp(Recovery, 1))
    Function(Input, Output);
  if (FaultSignal) {
    if (!NativeBoundary) {
      fputs("fault occurred before the original instruction boundary\n",
            stderr);
      exit(2);
    }
    memcpy(Output, FaultState, sizeof(FaultState));
  }
  return FaultSignal;
}

int main(int Argc, char **Argv) {
  if (Argc != 4)
    return 2;
  Enclu = strcmp(Argv[2], "enclu") == 0;
  const uint64_t Leaf = strtoull(Argv[3], NULL, 0);
  void *Library = dlopen(Argv[1], RTLD_NOW | RTLD_LOCAL);
  if (!Library) {
    fprintf(stderr, "%s\n", dlerror());
    return 2;
  }
  Probe Reference = (Probe)dlsym(Library, "original");
  Probe Actual = (Probe)dlsym(Library, "probe");
  if (!Reference || !Actual)
    return 2;
  struct sigaction Action = {0};
  Action.sa_sigaction = onFault;
  Action.sa_flags = SA_SIGINFO;
  sigemptyset(&Action.sa_mask);
  if (sigaction(SIGILL, &Action, NULL) || sigaction(SIGSEGV, &Action, NULL) ||
      sigaction(SIGBUS, &Action, NULL))
    return 2;
  const uint64_t Seeds[][3] = {
      {0, 0, 0},
      {UINT64_C(0x0d0123456789abcde), UINT64_C(0x798ab6cdaaaaffff),
       UINT64_C(0x80000000000000c0)},
      {UINT64_C(0x0102030405060708), UINT64_C(0x8000000180000001),
       UINT64_C(0x00000000000a0009)}};
  const unsigned Bits[] = {8, 10, 12, 14, 15, 0};
  unsigned Faults = 0, Normal = 0;
  for (unsigned Seed = 0; Seed != 3; ++Seed)
    for (unsigned Flags = 0; Flags != 64; ++Flags) {
      uint64_t Packed = 0;
      for (unsigned I = 0; I != 6; ++I)
        Packed |= ((uint64_t)(Flags >> I) & 1) << Bits[I];
      uint64_t Input[] = {Leaf, Seeds[Seed][0], Seeds[Seed][1], Seeds[Seed][2],
                          Packed | (Flags & 1 ? 0x2afe : 0)};
      uint64_t Expected[5], Observed[5];
      const int ExpectedSignal = observe(Reference, Input, Expected);
      const int ActualSignal = observe(Actual, Input, Observed);
      // #UD has no instruction effects. This also independently checks the
      // reference's POPFQ restoration against the requested input status.
      if (ExpectedSignal == SIGILL &&
          (memcmp(Expected, Input, 4 * sizeof(*Input)) ||
           Expected[4] != Packed)) {
        fputs("reference #UD context differs from instruction inputs\n",
              stderr);
        return 1;
      }
      if (ExpectedSignal != ActualSignal ||
          memcmp(Expected, Observed, sizeof(Expected))) {
        fprintf(stderr, "seed %u flags %u: signals %d/%d\n", Seed, Flags,
                ExpectedSignal, ActualSignal);
        for (unsigned I = 0; I != 5; ++I)
          fprintf(stderr, "%u: %016" PRIx64 " / %016" PRIx64 "\n", I,
                  Expected[I], Observed[I]);
        return 1;
      }
      Faults += 2 * (ExpectedSignal != 0);
      Normal += 2 * (ExpectedSignal == 0);
    }
  printf("%u native boundaries: %u faults, %u normal completions\n",
         Faults + Normal, Faults, Normal);
  dlclose(Library);
  return 0;
}
