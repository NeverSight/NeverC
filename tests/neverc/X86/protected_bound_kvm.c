// Compare compiler-produced BOUND with independent bytes and fault frames.
#include "KvmProtectedFaultFixture.h"

enum {
  CELL_BASE = 0x90000,
  FAULTS = (1u << 5) | (1u << 13) | (1u << 14) | (1u << 17),
  MEMORY_CASES = 10
};

int main(int argc, char **argv) {
  CHECK(argc == 4 && (!strcmp(argv[3], "16") || !strcmp(argv[3], "32")));
  const int code16 = !strcmp(argv[3], "16");
  const int kvm = open("/dev/kvm", O_RDWR | O_CLOEXEC);
  if (kvm < 0) {
    fprintf(stderr, "KVM unavailable: errno %d\n", errno);
    return 77;
  }
  CHECK(ioctl(kvm, KVM_GET_API_VERSION, 0) == KVM_API_VERSION);
  const int vm = ioctl(kvm, KVM_CREATE_VM, 0);
  CHECK(vm >= 0);
  uint8_t *ram = mmap(NULL, RAM_SIZE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(ram != MAP_FAILED);
  struct kvm_userspace_memory_region region = {
      .memory_size = RAM_SIZE, .userspace_addr = (uintptr_t)ram};
  CHECK(ioctl(vm, KVM_SET_USER_MEMORY_REGION, &region) == 0);
  ((uint32_t *)(ram + 0x2000))[0] = 0x3027;
  ((uint32_t *)(ram + 0x4000))[0] = 0x3027;
  for (unsigned i = 0; i < RAM_SIZE / 4096; ++i)
    ((uint32_t *)(ram + 0x3000))[i] = i * 4096 + 0xe7;
  const uint64_t descriptors[] = {0,
                                  UINT64_C(0x00cf9b000000ffff),
                                  UINT64_C(0x00cf93000000ffff),
                                  UINT64_C(0x00cffb000000ffff),
                                  UINT64_C(0x00cff3000000ffff),
                                  UINT64_C(0x008f9b000000ffff),
                                  UINT64_C(0x008ffb000000ffff)};
  memcpy(ram + 0x5000, descriptors, sizeof(descriptors));
  ram[TARGET_PC] = ram[TARGET_PC + 1] = 0x90;
  FILE *image = fopen(argv[1], "rb");
  CHECK(image);
  const size_t bytes = fread(ram + CODE_BASE, 1, 0x80000, image);
  CHECK(bytes && bytes < 0x80000 && !ferror(image));
  CHECK(fclose(image) == 0);
  struct kvm_cpuid2 *cpuid =
      calloc(1, sizeof(*cpuid) + 256 * sizeof(struct kvm_cpuid_entry2));
  CHECK(cpuid);
  cpuid->nent = 256;
  CHECK(ioctl(kvm, KVM_GET_SUPPORTED_CPUID, cpuid) == 0);
  const int run_size = ioctl(kvm, KVM_GET_VCPU_MMAP_SIZE, 0);
  CHECK(run_size > 0);
  struct cpu candidate = create_cpu(vm, 0, run_size, cpuid, 1);
  struct cpu oracle = create_cpu(vm, 1, run_size, cpuid, 1);
  candidate.initial.idt.base = oracle.initial.idt.base = IDT_BASE;
  candidate.initial.idt.limit = oracle.initial.idt.limit = 2047;
  // CR0.WP makes read-only pages meaningful at CPL0; CR0.AM admits #AC at CPL3.
  candidate.initial.cr0 |= 0x50000;
  oracle.initial.cr0 |= 0x50000;
  uint8_t *before = malloc(RAM_SIZE), *after = malloc(RAM_SIZE);
  CHECK(before && after);
  FILE *manifest = fopen(argv[2], "r");
  CHECK(manifest);
  char name[96];

  unsigned width, bits, prefix, dead, index_register, base_register;
  unsigned long long entry, instruction;
  unsigned functions = 0;
  uint64_t faults[18] = {0}, returns = 0, completions = 0;
  const uint32_t indices[] = {0xfffffffb, 0xfffffffc, 0,          4,         5,
                              0xa5a5ffff, 0xa5a50000, 0x7fffffff, 0x80000000};
  while (fscanf(manifest, "%95s %llx %llx %u %u %u %u %u %u", name, &entry,
                &instruction, &width, &bits, &prefix, &dead, &index_register,
                &base_register) == 9) {
    current_function = name;
    CHECK(entry >= CODE_BASE && instruction >= entry &&
          instruction < CODE_BASE + bytes);
    CHECK((width == 2 || width == 4) && (bits == 16 || bits == 32));
    CHECK((prefix == 0 || prefix == 0x64) && dead < 2 && index_register < 8 &&
          base_register < 8);
    CHECK(bits == 16
              ? (base_register == 3 || base_register == 6 || base_register == 7)
              : base_register != 4 && base_register != 5);
    ++functions;
    // Encode from the declared operand contract, independently of emitted
    // bytes.
    uint8_t original[5];
    unsigned length = 0;
    if (prefix)
      original[length++] = prefix;
    if ((width == 2) != code16)
      original[length++] = 0x66;
    if ((bits == 16) != code16)
      original[length++] = 0x67;
    original[length++] = 0x62;
    original[length++] =
        index_register << 3 | (bits == 16 ? (base_register == 3   ? 7
                                             : base_register == 6 ? 4
                                                                  : 5)
                                          : base_register);
    memcpy(ram + ORACLE_PC, original, length);
    for (unsigned privilege = 0; privilege < 2; ++privilege)
      for (unsigned pi = 0; pi < sizeof(indices) / sizeof(indices[0]); ++pi)
        for (unsigned flags = 0; flags < 2; ++flags)
          for (unsigned access = 0; access < MEMORY_CASES; ++access) {
            const unsigned cpl = privilege ? 3 : 0;
            const unsigned cs =
                (code16 ? (cpl ? 48 : 40) : (cpl ? 24 : 8)) | cpl;
            const unsigned ds = (cpl ? 32 : 16) | cpl;
            candidate.initial.cs = oracle.initial.cs = segment(cs, 1);
            candidate.initial.cs.l = oracle.initial.cs.l = 0;
            candidate.initial.cs.db = oracle.initial.cs.db = !code16;
            candidate.initial.cs.dpl = oracle.initial.cs.dpl = cpl;
            struct kvm_segment data = segment(ds, 0);
            data.dpl = cpl;
            candidate.initial.ss = candidate.initial.ds = candidate.initial.es =
                data;
            candidate.initial.fs = candidate.initial.gs = data;
            oracle.initial.ss = oracle.initial.ds = oracle.initial.es = data;
            oracle.initial.fs = oracle.initial.gs = data;
            candidate.initial.cr3 = oracle.initial.cr3 =
                (comparisons & 1) ? 0x2000 : 0x4000;
            reset_cpu(&candidate, 8);
            reset_cpu(&oracle, 8);
            for (unsigned vector = 0; vector < 18; ++vector) {
              if (!(FAULTS & (1u << vector)))
                continue;
              const unsigned pc = FAULT_BASE + vector * 16;
              const uint64_t gate = UINT64_C(0x00008e0000000000) |
                                    ((uint64_t)((cpl ? 24 : 8) | cpl) << 16) |
                                    pc;
              memcpy(ram + IDT_BASE + vector * 8, &gate, sizeof(gate));
              ram[pc] = 0xe6;
              ram[pc + 1] = 0x80;
            }
            uint32_t *pt = (uint32_t *)(ram + 0x3000);
            for (unsigned page = 0x90; page <= 0xa0; ++page)
              pt[page] = page * 4096 + 7;
            const unsigned offset = access == 1 ? 0x1001
                                    : access == 3 || access == 5
                                        ? 0x2000 - width
                                    : access >= 8 ? 0x10000 - width
                                                  : 0x1000;
            const unsigned linear = CELL_BASE + offset;
            int32_t low = -4, high = 4, wrap_high = -100;
            memcpy(ram + linear, &low, width);
            memcpy(ram + linear + width, &high, width);
            memcpy(ram + CELL_BASE, &wrap_high, width);
            if (access == 2)
              pt[linear / 4096] &= ~1u;
            if (access == 3)
              pt[0x92] &= ~1u;
            if (access == 4)
              pt[linear / 4096] &= ~2u;
            const unsigned pointer =
                prefix ? offset | (bits == 16 ? 0xabcd0000u : 0) : linear;
            memset(ram + 0x1e0000, 0x5a, 0x20000);
            ram[MARKER] = 0x91;
            struct kvm_regs initial = {.rax = 0x11223344,
                                       .rbx = 0x55667788,
                                       .rcx = 0x12345678,
                                       .rdx = 0x76543210,
                                       .rsi = 0xaabbccdd,
                                       .rdi = 0x98765432,
                                       .rsp = STACK_TOP + 4,
                                       .rbp = 0x10203040,
                                       .rip = entry,
                                       .rflags = flags ? 0x438d7 : 0x3002};
            const uint32_t arguments[] = {TARGET_PC, indices[pi], pointer,
                                          MARKER};
            memcpy(ram + initial.rsp, arguments, sizeof(arguments));
            CHECK(ioctl(candidate.fd, KVM_SET_REGS, &initial) == 0);
            struct kvm_regs boundary = run_to(&candidate, instruction, 0);
            const uint32_t mask = width == 2 ? 0xffff : UINT32_MAX;
            CHECK(ram[MARKER] == 17 && (gpr(boundary, index_register) & mask) ==
                                           (indices[pi] & mask));
            CHECK((gpr(boundary, base_register) &
                   (bits == 16 ? 0xffff : UINT32_MAX)) ==
                  (pointer & (bits == 16 ? 0xffff : UINT32_MAX)));
            struct kvm_sregs special, actual_special, expected_special;
            struct kvm_vcpu_events events = {0}, actual_events = {0},
                                   expected_events = {0};
            CHECK(ioctl(candidate.fd, KVM_GET_SREGS, &special) == 0);
            struct kvm_segment *operand = prefix ? &special.fs : &special.ds;
            if (prefix)
              operand->base = CELL_BASE;
            if (access == 5) {
              operand->g = 0;
              operand->limit = (prefix ? offset : linear) + width - 1;
            }
            if (access == 6)
              operand->type = 1;
            if (access == 7) {
              operand->selector = 0;
              operand->unusable = 1;
            }
            if (access == 9) {
              operand->g = 0;
              operand->limit = 0xffff;
            }
            CHECK(ioctl(candidate.fd, KVM_SET_SREGS, &special) == 0);
            CHECK(ioctl(candidate.fd, KVM_GET_VCPU_EVENTS, &events) == 0);
            CHECK(ioctl(oracle.fd, KVM_SET_SREGS, &special) == 0);
            CHECK(ioctl(oracle.fd, KVM_SET_VCPU_EVENTS, &events) == 0);
            memcpy(before, ram, RAM_SIZE);
            unsigned actual_vector, expected_vector;
            struct kvm_regs actual = run_instruction_to(
                &candidate, instruction + length, &actual_vector, FAULTS);
            CHECK(ioctl(candidate.fd, KVM_GET_SREGS, &actual_special) == 0);
            CHECK(ioctl(candidate.fd, KVM_GET_VCPU_EVENTS, &actual_events) ==
                  0);
            memcpy(after, ram, RAM_SIZE);
            memcpy(ram, before, RAM_SIZE);
            boundary.rip = ORACLE_PC;
            CHECK(ioctl(oracle.fd, KVM_SET_REGS, &boundary) == 0);
            struct kvm_regs expected = run_instruction_to(
                &oracle, ORACLE_PC + length, &expected_vector, FAULTS);
            CHECK(ioctl(oracle.fd, KVM_GET_SREGS, &expected_special) == 0);
            CHECK(ioctl(oracle.fd, KVM_GET_VCPU_EVENTS, &expected_events) == 0);
            CHECK(actual_vector == expected_vector);
            if (actual_vector) {
              const unsigned error_bytes = actual_vector == 5 ? 0 : 4;
              CHECK(actual.rsp == boundary.rsp - 12 - error_bytes &&
                    expected.rsp == actual.rsp);
              CHECK(*(uint32_t *)(after + actual.rsp + error_bytes) ==
                    instruction);
              CHECK(*(uint32_t *)(ram + expected.rsp + error_bytes) ==
                    ORACLE_PC);
              *(uint32_t *)(ram + expected.rsp + error_bytes) = instruction;
              ++faults[actual_vector];
            } else {
              expected.rip = instruction + length;
              CHECK(actual.rflags == boundary.rflags);
              ++completions;
            }
            CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
            CHECK(memcmp(&actual_special, &expected_special,
                         sizeof(actual_special)) == 0);
            clear_inactive_exception_record(&actual_events);
            clear_inactive_exception_record(&expected_events);
            CHECK(memcmp(&actual_events, &expected_events,
                         sizeof(actual_events)) == 0);
            CHECK(memcmp(after, ram, RAM_SIZE) == 0);
            memcpy(ram, after, RAM_SIZE);
            // FS variations do not change the compiler's DS/SS ABI.
            if (!actual_vector && (prefix || access < 5 || access == 8)) {
              const struct kvm_regs returned = run_to(&candidate, TARGET_PC, 0);
              CHECK(ram[MARKER] == 29 && returned.rsp == initial.rsp + 4 &&
                    returned.rbx == initial.rbx &&
                    returned.rbp == initial.rbp &&
                    returned.rsi == initial.rsi && returned.rdi == initial.rdi);
              CHECK((uint32_t)returned.rax == (dead ? 42u : indices[pi]));
              ++returns;
            } else
              CHECK(ram[MARKER] == 17);
            ++comparisons;
          }
  }
  CHECK(feof(manifest) && fclose(manifest) == 0 && functions == 12);
  printf(
      "{\"functions\":%u,\"complete_state_pairs\":%llu,\"returns\":%llu,"
      "\"completions\":%llu,\"br\":%llu,\"gp\":%llu,\"pf\":%llu,\"ac\":%llu}\n",
      functions, comparisons, (unsigned long long)returns,
      (unsigned long long)completions, (unsigned long long)faults[5],
      (unsigned long long)faults[13], (unsigned long long)faults[14],
      (unsigned long long)faults[17]);
  return 0;
}
