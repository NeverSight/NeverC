// KVM-mediated SMM qualification: SMI-created save image, compiled
// RSM, and independent original-byte RSM on a second vCPU in SMM.
#include "KvmTransferFixture.h"

static void enter_smm(struct cpu *cpu, struct kvm_regs saved) {
  CHECK(ioctl(cpu->fd, KVM_SET_REGS, &saved) == 0);
  breakpoint(cpu, 0x38000);
  CHECK(ioctl(cpu->fd, KVM_SMI, 0) == 0);
  CHECK(ioctl(cpu->fd, KVM_RUN, 0) == 0);
  struct kvm_regs at_entry;
  struct kvm_sregs special;
  struct kvm_vcpu_events events = {0};
  CHECK(ioctl(cpu->fd, KVM_GET_REGS, &at_entry) == 0);
  CHECK(ioctl(cpu->fd, KVM_GET_SREGS, &special) == 0);
  CHECK(ioctl(cpu->fd, KVM_GET_VCPU_EVENTS, &events) == 0);
  CHECK(cpu->run->exit_reason == KVM_EXIT_DEBUG &&
        at_entry.rip + special.cs.base == 0x38000 && events.smi.smm);
  // SMM remains active. Establish the execution mode needed by the compiled
  // C wrapper without changing the independent SMI-created save image.
  CHECK(ioctl(cpu->fd, KVM_SET_SREGS, &cpu->initial) == 0);
  CHECK(ioctl(cpu->fd, KVM_GET_VCPU_EVENTS, &events) == 0 && events.smi.smm);
}

int main(int argc, char **argv) {
  CHECK(argc == 4 && !strcmp(argv[3], "64"));
  const int kvm = open("/dev/kvm", O_RDWR | O_CLOEXEC);
  if (kvm < 0 || ioctl(kvm, KVM_CHECK_EXTENSION, KVM_CAP_X86_SMM) <= 0 ||
      ioctl(kvm, KVM_CHECK_EXTENSION, KVM_CAP_MULTI_ADDRESS_SPACE) < 2) {
    fprintf(stderr, "KVM SMM and two address spaces are required\n");
    return 77;
  }
  CHECK(ioctl(kvm, KVM_GET_API_VERSION, 0) == KVM_API_VERSION);
  const int vm = ioctl(kvm, KVM_CREATE_VM, 0);
  CHECK(vm >= 0);
  uint8_t *memory = mmap(NULL, 2 * RAM_SIZE, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(memory != MAP_FAILED);
  for (unsigned space = 0; space < 2; ++space) {
    uint8_t *ram = memory + space * RAM_SIZE;
    struct kvm_userspace_memory_region region = {.slot = space << 16,
                                                 .memory_size = RAM_SIZE,
                                                 .userspace_addr =
                                                     (uintptr_t)ram};
    CHECK(ioctl(vm, KVM_SET_USER_MEMORY_REGION, &region) == 0);
    ((uint64_t *)(ram + 0x2000))[0] = 0x3027;
    ((uint64_t *)(ram + 0x3000))[0] = 0x4027;
    ((uint64_t *)(ram + 0x4000))[0] = 0xe7;
    ram[TARGET_PC] = ram[TARGET_PC + 1] = 0x90;
  }
  uint8_t *smram = memory + RAM_SIZE;
  smram[ORACLE_PC] = 0x0f;
  smram[ORACLE_PC + 1] = 0xaa;
  smram[0x38000] = 0x90;
  FILE *image = fopen(argv[1], "rb");
  CHECK(image);
  // Keep the SMM entry and save-state area outside the generated image.
  size_t bytes = fread(smram + CODE_BASE, 1, 0x20000, image);
  CHECK(bytes && bytes < 0x20000 && !ferror(image));
  CHECK(fclose(image) == 0);
  struct kvm_cpuid2 *cpuid =
      calloc(1, sizeof(*cpuid) + 256 * sizeof(struct kvm_cpuid_entry2));
  CHECK(cpuid);
  cpuid->nent = 256;
  CHECK(ioctl(kvm, KVM_GET_SUPPORTED_CPUID, cpuid) == 0);
  int run_size = ioctl(kvm, KVM_GET_VCPU_MMAP_SIZE, 0);
  CHECK(run_size > 0);
  struct cpu candidate = create_cpu(vm, 0, run_size, cpuid, 0);
  struct cpu oracle = create_cpu(vm, 1, run_size, cpuid, 0);
  uint8_t *before = malloc(2 * RAM_SIZE), *after = malloc(2 * RAM_SIZE);
  CHECK(before && after);
  FILE *manifest = fopen(argv[2], "r");
  CHECK(manifest);
  char name[96];
  unsigned kind, width, adjustment;
  unsigned long long entry, instruction;
  const unsigned sizes[] = {0, 1, 31, 256};
  const uint64_t flags[] = {2, 0x8d7, 0x602, 0x240002, 0x3002, 0x10002};
  const uint64_t stacks[] = {0x180008, 0x170007, UINT64_C(0x7fffffff0001),
                             UINT64_C(0xffff800000100000)};
  unsigned functions = 0;
  while (fscanf(manifest, "%95s %llx %llx %u %u %u", name, &entry, &instruction,
                &kind, &width, &adjustment) == 6) {
    current_function = name;
    CHECK(kind == 4 && width == 64 && adjustment == 0 && entry >= CODE_BASE &&
          instruction >= entry && instruction + 2 <= CODE_BASE + bytes);
    CHECK(smram[instruction] == 0x0f && smram[instruction + 1] == 0xaa);
    ++functions;
    for (unsigned fi = 0; fi < sizeof(flags) / sizeof(*flags); ++fi)
      for (unsigned pi = 0; pi < sizeof(stacks) / sizeof(*stacks); ++pi)
        for (unsigned zi = 0; zi < sizeof(sizes) / sizeof(*sizes); ++zi) {
          reset_cpu(&candidate, 8);
          reset_cpu(&oracle, 8);
          memset(smram + 0x180000, 0x5a, 0x80000);
          *(uint32_t *)(smram + MARKER) = 0;
          struct kvm_regs saved = {.rax = 0x1111222233334444ULL,
                                   .rbx = 0x5555666677778888ULL,
                                   .rcx = 0x123456789abcdef0ULL,
                                   .rdx = 0x5678987656789876ULL,
                                   .rsi = 0xa1b2c3d4e5f60718ULL,
                                   .rdi = 0x18001,
                                   .rsp = stacks[pi],
                                   .rbp = 0x123456789abcdef0ULL,
                                   .r8 = 0xdeadbeef12345678ULL,
                                   .r9 = 0xabcdef9876543210ULL,
                                   .r10 = 0xabcdef9876543210ULL,
                                   .r11 = 0x76543210abcdef98ULL,
                                   .r12 = 0x12345678fedcba98ULL,
                                   .r13 = 0x87654321abcdef12ULL,
                                   .r14 = 0x55555555aaaaaaaaULL,
                                   .r15 = 0x9999999966666666ULL,
                                   .rip = TARGET_PC,
                                   .rflags = flags[fi]};
          enter_smm(&candidate, saved);
          enter_smm(&oracle, saved);
          struct kvm_regs initial = saved;
          initial.rip = entry;
          initial.rsp = STACK_TOP;
          initial.rdi = 0x90000;
          initial.rsi = sizes[zi];
          initial.rdx = MARKER;
          initial.rflags = 2;
          CHECK(ioctl(candidate.fd, KVM_SET_REGS, &initial) == 0);
          struct kvm_regs boundary = run_to(&candidate, instruction, 0);
          CHECK(*(uint32_t *)(smram + MARKER) == 17);
          struct kvm_sregs special;
          CHECK(ioctl(candidate.fd, KVM_GET_SREGS, &special) == 0);
          CHECK(ioctl(oracle.fd, KVM_SET_SREGS, &special) == 0);
          memcpy(before, memory, 2 * RAM_SIZE);
          struct kvm_regs actual =
              run_to(&candidate, TARGET_PC, !!(flags[fi] & 0x10000));
          struct kvm_sregs actual_special, expected_special;
          CHECK(ioctl(candidate.fd, KVM_GET_SREGS, &actual_special) == 0);
          memcpy(after, memory, 2 * RAM_SIZE);
          memcpy(memory, before, 2 * RAM_SIZE);
          boundary.rip = ORACLE_PC;
          CHECK(ioctl(oracle.fd, KVM_SET_REGS, &boundary) == 0);
          struct kvm_regs expected =
              run_to(&oracle, TARGET_PC, !!(flags[fi] & 0x10000));
          CHECK(ioctl(oracle.fd, KVM_GET_SREGS, &expected_special) == 0);
          CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
          CHECK(memcmp(&actual, &saved, sizeof(actual)) == 0);
          CHECK(memcmp(&actual_special, &expected_special,
                       sizeof(actual_special)) == 0);
          CHECK(memcmp(&actual_special, &candidate.initial,
                       sizeof(actual_special)) == 0);
          CHECK(memcmp(after, memory, 2 * RAM_SIZE) == 0);
          struct kvm_vcpu_events actual_events = {0}, expected_events = {0};
          CHECK(ioctl(candidate.fd, KVM_GET_VCPU_EVENTS, &actual_events) == 0);
          CHECK(ioctl(oracle.fd, KVM_GET_VCPU_EVENTS, &expected_events) == 0);
          CHECK(!actual_events.smi.smm && !expected_events.smi.smm);
          CHECK(memcmp(&actual_events, &expected_events,
                       sizeof(actual_events)) == 0);
          ++comparisons;
        }
  }
  CHECK(feof(manifest) && functions == 4);
  CHECK(fclose(manifest) == 0);
  printf("{\"functions\":%u,\"complete_state_pairs\":%llu,"
         "\"mapped_bytes_per_observation\":%u}\n",
         functions, comparisons, 2 * RAM_SIZE);
  return 0;
}
