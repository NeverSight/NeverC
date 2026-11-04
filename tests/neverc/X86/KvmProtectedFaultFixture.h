#ifndef NEVERC_TESTS_X86_KVMPROTECTEDFAULTFIXTURE_H
#define NEVERC_TESTS_X86_KVMPROTECTEDFAULTFIXTURE_H

#include "KvmTransferFixture.h"

enum { IDT_BASE = 0x7000, FAULT_BASE = 0xa000 };

static uint64_t gpr(struct kvm_regs r, unsigned index) {
  const uint64_t values[] = {r.rax, r.rcx, r.rdx, r.rbx,
                             r.rsp, r.rbp, r.rsi, r.rdi};
  CHECK(index < 8);
  return values[index];
}

static void clear_inactive_exception_record(struct kvm_vcpu_events *events) {
  // KVM_GET_VCPU_EVENTS copies the last exception's payload even after both
  // active flags clear. Hardware-delivered and KVM-emulated faults can leave
  // different stale records. Reject active exceptions before excluding that
  // inactive payload; all other event state still participates in equality.
  CHECK(!events->exception.injected && !events->exception.pending);
  memset(&events->exception, 0, sizeof(events->exception));
  events->exception_has_payload = 0;
  events->exception_payload = 0;
}

// CS-relative completion can have a different linear breakpoint address.
// Admitted fault handlers still execute in the fixture's flat code segments.
static struct kvm_regs run_instruction_to_linear(struct cpu *cpu, uint64_t next,
                                                 uint64_t linear_next,
                                                 unsigned *vector,
                                                 unsigned faults) {
  breakpoint(cpu, linear_next);
  CHECK(ioctl(cpu->fd, KVM_RUN, 0) == 0);
  struct kvm_regs result;
  CHECK(ioctl(cpu->fd, KVM_GET_REGS, &result) == 0);
  *vector = 0;
  if (cpu->run->exit_reason == KVM_EXIT_DEBUG && result.rip == next)
    return result;
  // A two-byte OUT in each original fault handler exposes the vector without
  // modifying GPRs, flags, caches or RAM. Both sides execute this same handler.
  if (cpu->run->exit_reason == KVM_EXIT_IO &&
      cpu->run->io.direction == KVM_EXIT_IO_OUT && cpu->run->io.port == 0x80 &&
      cpu->run->io.size == 1 && cpu->run->io.count == 1) {
    const uint64_t pc = result.rip;
    if (pc >= FAULT_BASE && pc < FAULT_BASE + 32 * 16 && (pc & 15) == 0) {
      *vector = (pc - FAULT_BASE) / 16;
      CHECK(faults & (1u << *vector));
      // KVM_EXIT_IO precedes completion of the OUT. Reenter KVM and stop
      // immediately after it before reading or replacing architectural state.
      return run_to(cpu, pc + 2, 0);
    }
  }
  fprintf(stderr, "exit %u, rip %#llx, expected %#llx or admitted fault\n",
          cpu->run->exit_reason, result.rip, (unsigned long long)next);
  CHECK(0);
  return result;
}

static inline struct kvm_regs run_instruction_to(struct cpu *cpu, uint64_t next,
                                                 unsigned *vector,
                                                 unsigned faults) {
  return run_instruction_to_linear(cpu, next, next, vector, faults);
}

#endif
