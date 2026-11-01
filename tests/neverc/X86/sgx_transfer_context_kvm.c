// Qualify terminal SGX input transport without claiming enclave execution.
// Stop before ENCLU and compare xstate with independent original XRSTOR bytes.
#include "KvmTransferFixture.h"
#include "NativeXStateFixture.h"

static void bank(uint8_t *ram, unsigned bits, const uint64_t values[18]) {
  for (unsigned i = 0; i < (bits == 64 ? 18u : 10u); ++i) {
    if (bits == 64)
      ((uint64_t *)(ram + 0x80000))[i] = values[i];
    else
      ((uint32_t *)(ram + 0x80000))[i] = (uint32_t)values[i];
  }
}

int main(int argc, char **argv) {
  CHECK(argc == 4 || (argc == 5 && !strcmp(argv[4], "projected")));
  const int projected = argc == 5;
  const int x32 = !strcmp(argv[3], "x32");
  const unsigned bits = x32 ? 64 : (unsigned)strtoul(argv[3], NULL, 10);
  CHECK(bits == 32 || bits == 64);
  const int kvm = open("/dev/kvm", O_RDWR | O_CLOEXEC);
  if (kvm < 0 || ioctl(kvm, KVM_CHECK_EXTENSION, KVM_CAP_XSAVE) <= 0 ||
      ioctl(kvm, KVM_CHECK_EXTENSION, KVM_CAP_XCRS) <= 0) {
    fprintf(stderr, "KVM XSAVE and XCR state are required\n");
    return 77;
  }
  const int vm = ioctl(kvm, KVM_CREATE_VM, 0);
  CHECK(vm >= 0);
  uint8_t *ram = mmap(NULL, RAM_SIZE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(ram != MAP_FAILED);
  struct kvm_userspace_memory_region region = {
      .memory_size = RAM_SIZE, .userspace_addr = (uintptr_t)ram};
  CHECK(ioctl(vm, KVM_SET_USER_MEMORY_REGION, &region) == 0);
  if (bits == 64) {
    ((uint64_t *)(ram + 0x2000))[0] = 0x3027;
    ((uint64_t *)(ram + 0x3000))[0] = 0x4027;
    ((uint64_t *)(ram + 0x4000))[0] = 0xe7;
  } else {
    ((uint32_t *)(ram + 0x2000))[0] = 0xe7;
  }
  // XRSTOR64 [RDI] / XRSTOR [EDI], followed by the ENCLU boundary.
  const uint8_t reference64[] = {0x48, 0x0f, 0xae, 0x2f, 0x0f, 0x01, 0xd7};
  const uint8_t reference32[] = {0x0f, 0xae, 0x2f, 0x0f, 0x01, 0xd7};
  enum { XSTATE_REFERENCE_PC = 0x6000, ORIGINAL_ENCLU_PC = 0x1000 };
  memcpy(ram + ORIGINAL_ENCLU_PC, "\x0f\x01\xd7", 3);
  memcpy(ram + XSTATE_REFERENCE_PC, bits == 64 ? reference64 : reference32,
         bits == 64 ? sizeof(reference64) : sizeof(reference32));
  FILE *image = fopen(argv[1], "rb");
  CHECK(image);
  const size_t bytes = fread(ram + CODE_BASE, 1, 0x60000, image);
  CHECK(bytes && bytes < 0x60000 && !ferror(image));
  CHECK(fclose(image) == 0);
  struct kvm_cpuid2 *cpuid =
      calloc(1, sizeof(*cpuid) + 256 * sizeof(struct kvm_cpuid_entry2));
  CHECK(cpuid);
  cpuid->nent = 256;
  CHECK(ioctl(kvm, KVM_GET_SUPPORTED_CPUID, cpuid) == 0);
  int run_size = ioctl(kvm, KVM_GET_VCPU_MMAP_SIZE, 0);
  CHECK(run_size > 0);
  struct cpu candidate = create_cpu(vm, 0, run_size, cpuid, bits == 32);
  struct cpu oracle = create_cpu(vm, 1, run_size, cpuid, bits == 32);
  for (unsigned i = 0; i < 2; ++i) {
    struct cpu *cpu = i ? &oracle : &candidate;
    cpu->initial.cr0 |= 0x40000; // Exercise AC=1 with native alignment checking.
    cpu->initial.cr4 |= 0x40600;
    cpu->initial.cr2 = 0x1234567800010000ULL;
    cpu->initial.cs.selector |= 3;
    cpu->initial.cs.dpl = 3;
    struct kvm_segment *segments[] = {&cpu->initial.ss, &cpu->initial.ds,
                                      &cpu->initial.es, &cpu->initial.fs,
                                      &cpu->initial.gs};
    for (unsigned j = 0; j < 5; ++j) {
      segments[j]->selector |= 3;
      segments[j]->dpl = 3;
    }
    CHECK(ioctl(cpu->fd, KVM_SET_SREGS, &cpu->initial) == 0);
    CHECK(ioctl(cpu->fd, KVM_GET_SREGS, &cpu->initial) == 0);
    struct kvm_xcrs xcrs = {.nr_xcrs = 1};
    xcrs.xcrs[0].value = 7;
    CHECK(ioctl(cpu->fd, KVM_SET_XCRS, &xcrs) == 0);
  }
  FILE *manifest = fopen(argv[2], "r");
  CHECK(manifest);
  char name[96];
  unsigned long long entry, instruction, rejection;
  const uint64_t flags[] = {0x202, 0xad7, 0x602, 0x240202, 0x240ed7, 0x200ad7};
  const uint64_t stacks[] = {0x180008, 0x170007, 0x1234567880000000ULL,
                             0xffff800000100001ULL};
  const unsigned sizes[] = {0, 1, 31, 256};
  const unsigned count = bits == 64 ? 16 : 8;
  uint8_t *before = malloc(RAM_SIZE);
  CHECK(before);
  unsigned functions = 0, refusals = 0;
  while (fscanf(manifest, "%95s %llx %llx %llx", name, &entry, &instruction,
                &rejection) == 4) {
    current_function = name;
    CHECK(entry >= CODE_BASE &&
          (instruction == ORIGINAL_ENCLU_PC ||
           (instruction >= entry && instruction + 3 <= CODE_BASE + bytes)));
    CHECK(!memcmp(ram + instruction, "\x0f\x01\xd7", 3));
    CHECK(rejection >= CODE_BASE && rejection < CODE_BASE + bytes &&
          !memcmp(ram + rejection, "\x0f\x0b", 2));
    ++functions;
    for (unsigned leaf = 2; leaf <= 4; ++leaf)
      for (unsigned fi = 0; fi != 6; ++fi)
        for (unsigned pi = 0; pi != 4; ++pi)
          for (unsigned zi = 0; zi != 4; ++zi)
            for (unsigned bv = 0; bv != 8; ++bv) {
              reset_cpu(&candidate, 8);
              reset_cpu(&oracle, 8);
              memset(ram + 0x180000, 0x5a, 0x80000);
              uint64_t values[18];
              for (unsigned i = 0; i < count; ++i)
                values[i] =
                    0x9123456780000000ULL ^ (0x0102030405060708ULL * (i + 1));
              values[0] = 0xcafebab000000000ULL | leaf;
              values[4] = stacks[pi];
              values[count] = flags[fi];
              values[count + 1] = 0x90000;
              bank(ram, bits, values);
              uint8_t *x = ram + 0x90000;
              memset(x, 0, 4096);
              *(uint16_t *)x = 0x37f | ((zi & 3) << 10);
              *(uint16_t *)(x + 2) = zi << 11;
              x[4] = 0xff;
              *(uint16_t *)(x + 6) = 0x5a1;
              *(uint64_t *)(x + 8) = 0x123456789abcULL;
              *(uint64_t *)(x + 16) = 0xffff800012345678ULL;
              *(uint32_t *)(x + 24) = 0x1f80 | ((zi & 3) << 13);
              *(uint64_t *)(x + 512) = bv;
              for (unsigned i = 0; i < 8; ++i) {
                *(uint64_t *)(x + 32 + i * 16) = 0x8000000012345678ULL + i;
                *(uint16_t *)(x + 40 + i * 16) = 0x3fff + i;
              }
              for (unsigned i = 0; i < 256; ++i) {
                x[160 + i] = (uint8_t)(i * 13 + fi + zi);
                x[576 + i] = (uint8_t)(i * 19 + pi + 17);
              }
              struct kvm_fpu poison = {.fcw = 0x37f, .mxcsr = 0x1f80};
              CHECK(ioctl(candidate.fd, KVM_SET_FPU, &poison) == 0);
              CHECK(ioctl(oracle.fd, KVM_SET_FPU, &poison) == 0);
              struct kvm_regs initial = {.rip = entry,
                                         .rsp = STACK_TOP,
                                         .rdi = 0x80000,
                                         .rsi = sizes[zi],
                                         .rdx = MARKER,
                                         .rflags = 0x202};
              // x32 psABI requires pointer arguments to be zero-extended to 64
              // bits. The explicit destination GPR bank above still exercises
              // nonzero high halves, including RDI, after native transport.
              if (bits == 32) {
                initial.rsp += 4;
                uint32_t *args = (void *)(ram + initial.rsp);
                args[0] = 0xdddddddd;
                args[1] = 0x80000;
                args[2] = sizes[zi];
                args[3] = 0;
                args[4] = MARKER;
              }
              *(uint32_t *)(ram + MARKER) = 0;
              memcpy(before, ram, RAM_SIZE);
              CHECK(ioctl(candidate.fd, KVM_SET_REGS, &initial) == 0);
              struct kvm_regs actual = run_to(&candidate, instruction, 0);
              const uint64_t gpr[] = {
                  actual.rax, actual.rcx, actual.rdx, actual.rbx,
                  actual.rsp, actual.rbp, actual.rsi, actual.rdi,
                  actual.r8,  actual.r9,  actual.r10, actual.r11,
                  actual.r12, actual.r13, actual.r14, actual.r15};
              for (unsigned i = 0; i < count; ++i)
                CHECK(bits == 64 ? gpr[i] == values[i]
                                 : (uint32_t)gpr[i] == (uint32_t)values[i]);
              CHECK(actual.rflags == flags[fi]);
              struct kvm_sregs special;
              CHECK(ioctl(candidate.fd, KVM_GET_SREGS, &special) == 0);
              CHECK(!memcmp(&special, &candidate.initial, sizeof(special)));
              CHECK(*(uint32_t *)(ram + MARKER) == 17);
              *(uint32_t *)(before + MARKER) = 17;
              uint8_t original_image[832];
              memcpy(original_image, before + 0x90000, sizeof(original_image));
              if (projected) {
                // Only the explicit projection's state and imported SIMD
                // image may change outside compiler storage. x87 remains
                // byte-identical, and the oracle consumes the original image.
                *(uint64_t *)(before + 0x90000 + 512) = bv | 6;
                if (!(bv & 2))
                  memset(before + 0x90000 + 160, 0, 256);
                if (!(bv & 4))
                  memset(before + 0x90000 + 576, 0, 256);
                memcpy(before + 0x95000, ram + 0x95000, 2464);
              }
              CHECK(!memcmp(before, ram, 0x180000));
              memcpy(ram + 0x90000, original_image, sizeof(original_image));
              struct kvm_regs reference = {.rip = XSTATE_REFERENCE_PC,
                                           .rsp = STACK_TOP,
                                           .rax = 7,
                                           .rdx = 0,
                                           .rdi = 0x90000,
                                           .rflags = 0x202};
              CHECK(ioctl(oracle.fd, KVM_SET_REGS, &reference) == 0);
              run_to(&oracle, XSTATE_REFERENCE_PC + (bits == 64 ? 4 : 3), 0);
              struct kvm_xsave actual_xstate = {0}, expected_xstate = {0};
              CHECK(ioctl(candidate.fd, KVM_GET_XSAVE, &actual_xstate) == 0);
              CHECK(ioctl(oracle.fd, KVM_GET_XSAVE, &expected_xstate) == 0);
              canonical_xsave7(&actual_xstate);
              canonical_xsave7(&expected_xstate);
              CHECK(!memcmp(&actual_xstate, &expected_xstate,
                            sizeof(actual_xstate)));
              ++comparisons;
            }
    for (unsigned bad = 0; bad != 6; ++bad) {
      static const uint32_t leaves[] = {0, 1, 5, 11, 0xffffffff, 0x80000002};
      reset_cpu(&candidate, 8);
      uint64_t values[18] = {0};
      values[0] = leaves[bad];
      values[count] = 0x202;
      values[count + 1] = 1;
      bank(ram, bits, values);
      const unsigned context_bytes = (count + 2) * (bits / 8);
      uint8_t context_before[144];
      memcpy(context_before, ram + 0x80000, context_bytes);
      struct kvm_regs initial = {.rip = entry,
                                 .rsp = STACK_TOP,
                                 .rdi = 0x80000,
                                 .rdx = MARKER,
                                 .rflags = 0x202};
      if (bits == 32) {
        initial.rsp += 4;
        uint32_t *args = (void *)(ram + initial.rsp);
        args[0] = 0xdddddddd;
        args[1] = 0x80000;
        args[2] = args[3] = 0;
        args[4] = MARKER;
      }
      CHECK(ioctl(candidate.fd, KVM_SET_REGS, &initial) == 0);
      run_to(&candidate, rejection, 0);
      if (projected)
        CHECK(*(uint32_t *)(ram + MARKER) == 5);
      CHECK(!memcmp(ram + 0x80000, context_before, context_bytes));
      ++refusals;
    }
  }
  CHECK(feof(manifest) && functions == 4);
  CHECK(fclose(manifest) == 0);
  printf("{\"functions\":%u,\"input_state_observations\":%llu,\"invalid_leaves_"
         "rejected\":%u,"
         "\"code_bits\":%u,\"enclave_instructions_executed\":0}\n",
         functions, comparisons, refusals, bits);
  return 0;
}
