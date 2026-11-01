// KVM-mediated SMM qualification: SMI-created save image, compiled
// RSM, and independent original-byte RSM on a second vCPU in SMM.
#include "KvmTransferFixture.h"
#include "NativeXStateFixture.h"

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
  cpu->run->cr8 = cpu->initial.cr8;
  CHECK(ioctl(cpu->fd, KVM_GET_VCPU_EVENTS, &events) == 0 && events.smi.smm);
}

static uint64_t get_kernel_gs(int fd) {
  struct kvm_msrs *m = calloc(1, sizeof(*m) + sizeof(struct kvm_msr_entry));
  CHECK(m);
  m->nmsrs = 1;
  m->entries[0].index = 0xc0000102;
  CHECK(ioctl(fd, KVM_GET_MSRS, m) == 1);
  const uint64_t value = m->entries[0].data;
  free(m);
  return value;
}

int main(int argc, char **argv) {
  CHECK((argc == 4 || argc == 5) && !strcmp(argv[3], "64"));
  const int projection = argc == 5 && !strcmp(argv[4], "projection");
  const int context = argc == 5 && (!strcmp(argv[4], "context") || projection);
  CHECK(argc == 4 || context);
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
  if (context) {
    CHECK(ioctl(kvm, KVM_CHECK_EXTENSION, KVM_CAP_XSAVE) > 0);
    CHECK(ioctl(kvm, KVM_CHECK_EXTENSION, KVM_CAP_XCRS) > 0);
    for (unsigned i = 0; i < 2; ++i) {
      struct cpu *cpu = i ? &oracle : &candidate;
      cpu->initial.cr4 |= 0x40600; // OSXSAVE, OSFXSR, OSXMMEXCPT.
      CHECK(ioctl(cpu->fd, KVM_SET_SREGS, &cpu->initial) == 0);
      cpu->run->cr8 = cpu->initial.cr8;
      struct kvm_xcrs xcrs = {.nr_xcrs = 1};
      xcrs.xcrs[0].xcr = 0;
      xcrs.xcrs[0].value = 7;
      CHECK(ioctl(cpu->fd, KVM_SET_XCRS, &xcrs) == 0);
    }
    // Original XRSTOR64 [RDI]; RSM. The oracle inputs are installed by KVM,
    // independently from the compiled context-memory transport.
    const uint8_t original[] = {0x48, 0x0f, 0xae, 0x2f, 0x0f, 0xaa};
    memcpy(smram + ORACLE_PC, original, sizeof(original));
  }
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
          (projection || instruction >= entry) && instruction >= CODE_BASE &&
          instruction + 2 <= CODE_BASE + bytes);
    CHECK(smram[instruction] == 0x0f && smram[instruction + 1] == 0xaa);
    ++functions;
    for (unsigned fi = 0; fi < sizeof(flags) / sizeof(*flags); ++fi)
      for (unsigned pi = 0; pi < sizeof(stacks) / sizeof(*stacks); ++pi)
        for (unsigned zi = 0; zi < sizeof(sizes) / sizeof(*sizes); ++zi)
          for (unsigned bv = 0; bv < (context ? 8u : 1u); ++bv) {
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
            const uint64_t cr2 = 0x1234567800001000ULL + fi * 17 + zi;
            const uint64_t cr8 = (pi * 4 + zi) & 15;
            const uint64_t kernel_gs = 0xffff800000500000ULL + zi * 17;
            uint8_t original_xstate[832];
            if (context) {
              uint8_t *xstate = smram + 0xa0000;
              memset(xstate, 0, 4096);
              *(uint16_t *)(xstate + 0) = 0x37f | ((zi & 3) << 10);
              *(uint16_t *)(xstate + 2) = (zi & 7) << 11;
              xstate[4] = 0xff;
              *(uint16_t *)(xstate + 6) = 0x5a1;
              *(uint64_t *)(xstate + 8) = 0x123456789abcULL;
              *(uint64_t *)(xstate + 16) = 0xffff800012345678ULL;
              *(uint32_t *)(xstate + 24) = 0x1f80 | ((zi & 3) << 13);
              *(uint64_t *)(xstate + 512) = bv;
              for (unsigned i = 0; i != 8; ++i) {
                *(uint64_t *)(xstate + 32 + i * 16) = 0x8000000012345678ULL + i;
                *(uint16_t *)(xstate + 40 + i * 16) = 0x3fff + i;
              }
              for (unsigned i = 0; i != 256; ++i) {
                xstate[160 + i] = (uint8_t)(i * 13 + zi + fi);
                xstate[576 + i] = (uint8_t)(i * 19 + pi + 17);
              }
              memcpy(original_xstate, xstate, sizeof(original_xstate));
              const uint64_t inputs[] = {0xa0000, 7, cr2, cr8, kernel_gs};
              memcpy(smram + 0x90000, inputs, sizeof(inputs));
              // Start with unrelated live state so a missing XRSTOR is visible.
              struct kvm_fpu poison = {.fcw = 0x37f, .mxcsr = 0x1f80};
              CHECK(ioctl(candidate.fd, KVM_SET_FPU, &poison) == 0);
              CHECK(ioctl(oracle.fd, KVM_SET_FPU, &poison) == 0);
              msr(candidate.fd, 0xc0000102, 0x123000);
            }
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
            if (context) {
              CHECK(special.cr2 == cr2 && special.cr8 == cr8);
              msr(oracle.fd, 0xc0000102, kernel_gs);
            }
            CHECK(ioctl(oracle.fd, KVM_SET_SREGS, &special) == 0);
            // With a userspace IRQ chip KVM_RUN imports CR8 from the run page.
            // SET_SREGS alone does not update that next-run input.
            oracle.run->cr8 = special.cr8;
            memcpy(before, memory, 2 * RAM_SIZE);
            struct kvm_regs actual =
                run_to(&candidate, TARGET_PC, !!(flags[fi] & 0x10000));
            struct kvm_sregs actual_special, expected_special;
            CHECK(ioctl(candidate.fd, KVM_GET_SREGS, &actual_special) == 0);
            memcpy(after, memory, 2 * RAM_SIZE);
            memcpy(memory, before, 2 * RAM_SIZE);
            boundary.rip = ORACLE_PC;
            if (context) {
              boundary.rdi = 0xa0000;
              boundary.rax = 7;
              boundary.rdx = 0;
            }
            CHECK(ioctl(oracle.fd, KVM_SET_REGS, &boundary) == 0);
            if (projection) {
              // The source projection rewrites its image from explicit SIMD
              // cells. Restore the independently seeded input for the oracle's
              // XRSTOR so a faulty import cannot become the expected result.
              memcpy(smram + 0xa0000, original_xstate, sizeof(original_xstate));
              run_to(&oracle, ORACLE_PC + 4, 0);
              memcpy(smram + 0xa0000, before + RAM_SIZE + 0xa0000,
                     sizeof(original_xstate));
            }
            struct kvm_regs expected =
                run_to(&oracle, TARGET_PC, !!(flags[fi] & 0x10000));
            CHECK(ioctl(oracle.fd, KVM_GET_SREGS, &expected_special) == 0);
            CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
            CHECK(memcmp(&actual, &saved, sizeof(actual)) == 0);
            if (memcmp(&actual_special, &expected_special,
                       sizeof(actual_special))) {
              const uint64_t *a = (const void *)&actual_special;
              const uint64_t *e = (const void *)&expected_special;
              for (unsigned i = 0; i < sizeof(actual_special) / 8; ++i)
                if (a[i] != e[i])
                  fprintf(stderr, "sreg byte %u: actual %#llx expected %#llx\n",
                          i * 8, (unsigned long long)a[i],
                          (unsigned long long)e[i]);
            }
            CHECK(memcmp(&actual_special, &expected_special,
                         sizeof(actual_special)) == 0);
            struct kvm_sregs expected_restore = candidate.initial;
            if (context) {
              expected_restore.cr2 = cr2;
              expected_restore.cr8 = cr8;
              struct kvm_xsave actual_xstate = {0}, expected_xstate = {0};
              CHECK(ioctl(candidate.fd, KVM_GET_XSAVE, &actual_xstate) == 0);
              CHECK(ioctl(oracle.fd, KVM_GET_XSAVE, &expected_xstate) == 0);
              canonical_xsave7(&actual_xstate);
              canonical_xsave7(&expected_xstate);
              if (memcmp(&actual_xstate, &expected_xstate,
                         sizeof(actual_xstate))) {
                const uint64_t *a = (const void *)&actual_xstate;
                const uint64_t *e = (const void *)&expected_xstate;
                for (unsigned i = 0; i < sizeof(actual_xstate) / 8; ++i)
                  if (a[i] != e[i])
                    fprintf(stderr,
                            "xsave byte %u: actual %#llx expected %#llx (input "
                            "bv %u)\n",
                            i * 8, (unsigned long long)a[i],
                            (unsigned long long)e[i], bv);
              }
              CHECK(memcmp(&actual_xstate, &expected_xstate,
                           sizeof(actual_xstate)) == 0);
              CHECK(get_kernel_gs(candidate.fd) == kernel_gs);
              CHECK(get_kernel_gs(oracle.fd) == kernel_gs);
            }
            CHECK(memcmp(&actual_special, &expected_restore,
                         sizeof(actual_special)) == 0);
            CHECK(memcmp(after, memory, 2 * RAM_SIZE) == 0);
            struct kvm_vcpu_events actual_events = {0}, expected_events = {0};
            CHECK(ioctl(candidate.fd, KVM_GET_VCPU_EVENTS, &actual_events) ==
                  0);
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
