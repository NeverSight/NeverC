/* Isolated KVM Hyper-V page transport oracle. SPDX-License-Identifier:
 * AGPL-3.0-only */
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
  if (!ioctl(kvm, KVM_CHECK_EXTENSION, KVM_CAP_HYPERV_SYNIC))
    return 77;
  int vm = ioctl(kvm, KVM_CREATE_VM, 0);
  CHECK(vm >= 0);
  CHECK(ioctl(vm, KVM_CREATE_IRQCHIP, 0) == 0);

  unsigned size = 4 * 1024 * 1024;
  uint8_t *mem =
      mmap(0, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(mem != MAP_FAILED);
  struct kvm_userspace_memory_region region = {
      .slot = 0, .memory_size = size, .userspace_addr = (uintptr_t)mem};
  CHECK(ioctl(vm, KVM_SET_USER_MEMORY_REGION, &region) == 0);
  int cpu = ioctl(vm, KVM_CREATE_VCPU, 0);
  CHECK(cpu >= 0);
  struct kvm_enable_cap cap = {.cap = KVM_CAP_HYPERV_SYNIC};
  CHECK(ioctl(cpu, KVM_ENABLE_CAP, &cap) == 0);
  uint8_t *page =
      mmap(0, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(page != MAP_FAILED);
  region =
      (struct kvm_userspace_memory_region){.slot = 1,
                                           .guest_phys_addr = 0x62000000,
                                           .memory_size = 4096,
                                           .userspace_addr = (uintptr_t)page};
  CHECK(ioctl(vm, KVM_SET_USER_MEMORY_REGION, &region) == 0);

  unsigned capacity = 256;
  struct kvm_cpuid2 *cpuid =
      calloc(1, sizeof(*cpuid) + capacity * sizeof(struct kvm_cpuid_entry2));
  CHECK(cpuid);
  cpuid->nent = capacity;
  CHECK(ioctl(kvm, KVM_GET_SUPPORTED_CPUID, cpuid) == 0);

  for (unsigned leaf = 0; leaf < 2; ++leaf) {
    unsigned i = 0;
    for (; i < cpuid->nent; ++i)
      if (cpuid->entries[i].function == 0x40000000u + leaf)
        break;
    if (i == cpuid->nent)
      ++cpuid->nent;
    cpuid->entries[i] =
        (struct kvm_cpuid_entry2){.function = 0x40000000u + leaf};
    if (!leaf) {
      cpuid->entries[i].eax = 0x40000005;
      cpuid->entries[i].ebx = 0x7263694d;
      cpuid->entries[i].ecx = 0x666f736f;
      cpuid->entries[i].edx = 0x76482074;
    } else
      cpuid->entries[i].eax = 0x31237648;
  }
  CHECK(ioctl(cpu, KVM_SET_CPUID2, cpuid) == 0);
  struct kvm_msrs *msrs =
      calloc(1, sizeof(*msrs) + 2 * sizeof(struct kvm_msr_entry));
  CHECK(msrs);
  msrs->nmsrs = 2;
  msrs->entries[0] = (struct kvm_msr_entry){.index = 0x40000000,
                                            .data = 0x8100000000000001ULL};
  msrs->entries[1] =
      (struct kvm_msr_entry){.index = 0x40000001, .data = 0x62000001};
  CHECK(ioctl(cpu, KVM_SET_MSRS, msrs) == 2);
  free(msrs);
  CHECK(page[0] && page[3] == 0xc3);

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
  *(uint64_t *)(mem + 0x3008) = 0x5003;
  *(uint64_t *)(mem + 0x5000 + 272 * 8) = 0x62000083;

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

    if (run->exit_reason != KVM_EXIT_HYPERV ||
        run->hyperv.type != KVM_EXIT_HYPERV_HCALL)
      break;
    CHECK(run->hyperv.u.hcall.input == r.rcx &&
          run->hyperv.u.hcall.params[0] == r.rdx &&
          run->hyperv.u.hcall.params[1] == r.r8);
    struct kvm_fpu fpu;
    CHECK(ioctl(cpu, KVM_GET_FPU, &fpu) == 0);
    uint64_t checksum = r.rcx ^ r.rdx ^ r.r8;
    for (unsigned i = 0; i < 6; ++i)
      for (unsigned j = 0; j < 2; ++j) {
        uint64_t word;
        __builtin_memcpy(&word, &fpu.xmm[i][j * 8], 8);
        checksum ^= word;
      }
    run->hyperv.u.hcall.result = checksum;
    *(uint64_t *)(mem + 0x180000) = checksum;
    if (r.rcx & (0xfffull << 32))
      r.rcx += 1ull << 48;
    if (r.rcx & (1u << 16)) {
      r.rdx ^= 0xaabbccdd12345678ull;
      r.r8 += 0x123456789abcdef0ull;
      for (unsigned i = 0; i < 6; ++i)
        for (unsigned j = 0; j < 16; ++j)
          fpu.xmm[i][j] ^= i * 16 + j;
    }
    CHECK(ioctl(cpu, KVM_SET_REGS, &r) == 0);
    CHECK(ioctl(cpu, KVM_SET_FPU, &fpu) == 0);
    ++calls;
  }
  if (run->exit_reason != KVM_EXIT_IO || run->io.port != 0xf4 || r.rax) {
    fprintf(stderr, "exit=%u rip=%llx result=%llu calls=%u\n", run->exit_reason,
            (unsigned long long)r.rip, (unsigned long long)r.rax, calls);
    return 1;
  }
  printf("PASS calls=%u\n", calls);
  return 0;
}
