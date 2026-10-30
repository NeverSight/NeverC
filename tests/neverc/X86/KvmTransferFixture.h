#ifndef NEVERC_TESTS_X86_KVMTRANSFERFIXTURE_H
#define NEVERC_TESTS_X86_KVMTRANSFERFIXTURE_H

// Native system-return qualification. Run compiler output and independent
// original bytes on separate KVM vCPUs; never transfer privilege on the host.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

enum {
  RAM_SIZE = 2 * 1024 * 1024,
  CODE_BASE = 0x10000,
  ORACLE_PC = 0x1000,
  TARGET_PC = 0x1800,
  MARKER = 0x8000,
  STACK_TOP = 0x1f0008
};
static const char *current_function;
static unsigned long long comparisons;
#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      fprintf(stderr, "%s case %llu, line %d: %s (errno %d)\n",                \
              current_function ? current_function : "setup", comparisons,      \
              __LINE__, #expression, errno);                                   \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

struct cpu {
  int fd;
  struct kvm_run *run;
  struct kvm_sregs initial;
};

static void msr(int cpu, unsigned index, uint64_t value) {
  struct kvm_msrs *request =
      calloc(1, sizeof(*request) + sizeof(struct kvm_msr_entry));
  CHECK(request);
  request->nmsrs = 1;
  request->entries[0].index = index;
  request->entries[0].data = value;
  CHECK(ioctl(cpu, KVM_SET_MSRS, request) == 1);
  free(request);
}

static void breakpoint(struct cpu *cpu, uint64_t pc) {
  struct kvm_guest_debug debug = {.control = KVM_GUESTDBG_ENABLE |
                                             KVM_GUESTDBG_USE_HW_BP};
  debug.arch.debugreg[0] = pc;
  debug.arch.debugreg[1] = pc + 1;
  debug.arch.debugreg[7] = 0x405;
  CHECK(ioctl(cpu->fd, KVM_SET_GUEST_DEBUG, &debug) == 0);
}

static struct kvm_regs run_to(struct cpu *cpu, uint64_t pc, int resume_flag) {
  breakpoint(cpu, pc);
  CHECK(ioctl(cpu->fd, KVM_RUN, 0) == 0);
  struct kvm_regs registers;
  CHECK(ioctl(cpu->fd, KVM_GET_REGS, &registers) == 0);
  if (cpu->run->exit_reason != KVM_EXIT_DEBUG ||
      (registers.rip != pc && !(resume_flag && registers.rip == pc + 1))) {
    fprintf(stderr, "exit %u, pc %#llx, expected %#llx, flags %#llx\n",
            cpu->run->exit_reason, registers.rip, (unsigned long long)pc,
            registers.rflags);
    CHECK(0);
  }
  // RF suppresses the first execution breakpoint. A single NOP preserves all
  // other represented state, so reconstruct the immediate post-transfer RF.
  if (registers.rip == pc + 1) {
    registers.rip = pc;
    registers.rflags |= 0x10000;
  }
  return registers;
}

static struct kvm_segment segment(unsigned selector, int code) {
  return (struct kvm_segment){.limit = UINT32_MAX,
                              .selector = selector,
                              .type = code ? 11 : 3,
                              .present = 1,
                              .db = !code,
                              .s = 1,
                              .l = code,
                              .g = 1};
}

static struct cpu create_cpu(int vm, int id, int run_size,
                             struct kvm_cpuid2 *cpuid, int protected_mode) {
  struct cpu result = {.fd = ioctl(vm, KVM_CREATE_VCPU, id)};
  CHECK(result.fd >= 0);
  CHECK(ioctl(result.fd, KVM_SET_CPUID2, cpuid) == 0);
  result.run =
      mmap(NULL, run_size, PROT_READ | PROT_WRITE, MAP_SHARED, result.fd, 0);
  CHECK(result.run != MAP_FAILED);
  CHECK(ioctl(result.fd, KVM_GET_SREGS, &result.initial) == 0);
  result.initial.cr0 = 0x80000011;
  result.initial.cr3 = 0x2000;
  result.initial.cr4 = protected_mode ? 0x10 : 0x20;
  result.initial.efer = protected_mode ? 0 : 0x501;
  result.initial.cs = segment(8, 1);
  result.initial.cs.l = !protected_mode;
  result.initial.cs.db = protected_mode;
  result.initial.ss = result.initial.ds = result.initial.es = segment(16, 0);
  result.initial.fs = result.initial.gs = segment(16, 0);
  result.initial.gdt.base = 0x5000;
  result.initial.gdt.limit = 0xffff;
  result.initial.ldt.unusable = 1;
  CHECK(ioctl(result.fd, KVM_SET_SREGS, &result.initial) == 0);
  CHECK(ioctl(result.fd, KVM_GET_SREGS, &result.initial) == 0);
  return result;
}

static void reset_cpu(struct cpu *cpu, unsigned selector) {
  CHECK(ioctl(cpu->fd, KVM_SET_SREGS, &cpu->initial) == 0);
  msr(cpu->fd, 0xc0000081, (uint64_t)selector << 48);
  msr(cpu->fd, 0x174, selector);
  struct kvm_vcpu_events events = {0};
  CHECK(ioctl(cpu->fd, KVM_GET_VCPU_EVENTS, &events) == 0);
  CHECK(!events.interrupt.shadow && !events.nmi.masked &&
        !events.exception.pending && !events.exception.injected);
}

#endif
