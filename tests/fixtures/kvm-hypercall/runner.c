/* Isolated KVM hypercall ABI oracle. SPDX-License-Identifier: AGPL-3.0-only */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x)) {                                                                \
      fprintf(stderr, "KVM line %u: %s errno=%u\n", __LINE__, #x, errno);      \
      return 1;                                                                \
    }                                                                          \
  } while (0)

int main(int argc, char **argv) {
  CHECK(argc == 3);
  int bits = atoi(argv[2]);
  CHECK(bits == 32 || bits == 64);
  int kvm = open("/dev/kvm", O_RDWR | O_CLOEXEC);
  if (kvm < 0)
    return 77;
  CHECK(ioctl(kvm, KVM_GET_API_VERSION, 0) == KVM_API_VERSION);
  if (!(ioctl(kvm, KVM_CHECK_EXTENSION, KVM_CAP_EXIT_HYPERCALL) & (1 << 12)))
    return 77;
  int vm = ioctl(kvm, KVM_CREATE_VM, 0);
  CHECK(vm >= 0);
  struct kvm_enable_cap cap = {.cap = KVM_CAP_EXIT_HYPERCALL,
                               .args = {1ULL << 12}};
  CHECK(ioctl(vm, KVM_ENABLE_CAP, &cap) == 0);
  unsigned size = 4 * 1024 * 1024;
  uint8_t *mem =
      mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(mem != MAP_FAILED);
  struct kvm_userspace_memory_region region = {
      .slot = 0, .memory_size = size, .userspace_addr = (uintptr_t)mem};
  CHECK(ioctl(vm, KVM_SET_USER_MEMORY_REGION, &region) == 0);
  int cpu = ioctl(vm, KVM_CREATE_VCPU, 0);
  CHECK(cpu >= 0);
  unsigned capacity = 256;
  struct kvm_cpuid2 *cpuid =
      calloc(1, sizeof(*cpuid) + capacity * sizeof(struct kvm_cpuid_entry2));
  CHECK(cpuid);
  cpuid->nent = capacity;
  CHECK(ioctl(kvm, KVM_GET_SUPPORTED_CPUID, cpuid) == 0);
  CHECK(ioctl(cpu, KVM_SET_CPUID2, cpuid) == 0);
  free(cpuid);
  int run_size = ioctl(kvm, KVM_GET_VCPU_MMAP_SIZE, 0);
  CHECK(run_size > 0);
  struct kvm_run *run =
      mmap(0, run_size, PROT_READ | PROT_WRITE, MAP_SHARED, cpu, 0);
  CHECK(run != MAP_FAILED);
  *(uint64_t *)(mem + 0x2000) = 0x3003;
  *(uint64_t *)(mem + 0x3000) = 0x4003;
  *(uint64_t *)(mem + 0x4000) = 0x83;
  *(uint64_t *)(mem + 0x4008) = 0x200083;
  struct kvm_sregs sr;
  CHECK(ioctl(cpu, KVM_GET_SREGS, &sr) == 0);
  sr.cr0 = 0x80000033;
  sr.cr3 = 0x2000;
  sr.cr4 = 0x620;
  sr.efer = 0x500;
  sr.cs = (struct kvm_segment){.limit = UINT32_MAX,
                               .selector = 8,
                               .type = 11,
                               .present = 1,
                               .s = 1,
                               .g = 1,
                               .l = 1};
  sr.ds = sr.es = sr.ss = (struct kvm_segment){.limit = UINT32_MAX,
                                               .selector = 16,
                                               .type = 3,
                                               .present = 1,
                                               .s = 1,
                                               .g = 1,
                                               .db = 1};
  if (bits == 32) {
    sr.cr0 = 0x33;
    sr.cr3 = 0;
    sr.cr4 = 0x600;
    sr.efer = 0;
    sr.cs.l = 0;
    sr.cs.db = 1;
  }
  sr.ldt.unusable = 1;
  CHECK(ioctl(cpu, KVM_SET_SREGS, &sr) == 0);
  FILE *file = fopen(argv[1], "rb");
  CHECK(file);
  size_t bytes = fread(mem + 0x10000, 1, 0x100000, file);
  CHECK(bytes && bytes < 0x100000 && !ferror(file));
  CHECK(!fclose(file));
  struct kvm_regs r = {.rip = 0x10000, .rsp = 0x1f0000, .rflags = 2};
  CHECK(ioctl(cpu, KVM_SET_REGS, &r) == 0);
  unsigned calls = 0;
  for (;;) {
    CHECK(!ioctl(cpu, KVM_RUN, 0));
    CHECK(!ioctl(cpu, KVM_GET_REGS, &r));
    if (run->exit_reason != KVM_EXIT_HYPERCALL)
      break;
    CHECK(run->hypercall.nr == 12 && r.rax == 12);
    CHECK(run->hypercall.args[0] == r.rbx && run->hypercall.args[1] == r.rcx &&
          run->hypercall.args[2] == r.rdx);
    CHECK((r.rbx & 4095) == 0 && r.rbx < size && r.rcx > 0 && r.rcx < 129);
    // A userspace KVM service gives every input an independently observable
    // contribution, including RSI, while leaving all other CPU state alone.
    run->hypercall.ret = r.rax + 3 * r.rbx + 5 * r.rcx + 7 * r.rdx + 11 * r.rsi;
    if (bits == 64)
      *(uint64_t *)(mem + 0x180000) = r.rax ^ r.rbx ^ r.rcx ^ r.rdx ^ r.rsi;
    else
      *(uint32_t *)(mem + 0x180000) =
          (uint32_t)(r.rax ^ r.rbx ^ r.rcx ^ r.rdx ^ r.rsi);
    ++calls;
  }
  if (run->exit_reason != KVM_EXIT_HLT || r.rax) {
    fprintf(stderr, "exit=%u rip=%llx result=%llu calls=%u\n", run->exit_reason,
            (unsigned long long)r.rip, (unsigned long long)r.rax, calls);
    return 1;
  }
  printf("PASS calls=%u\n", calls);
  return 0;
}
