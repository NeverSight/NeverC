// Native protected descriptor-state instructions with explicit
// address-size/segment inputs. CS.base places the image above the fixture
// tables while its functions and conditional branches retain
// 16-bit-representable offsets.
#include "KvmProtectedFaultFixture.h"

enum {
  FAULTS = (1u << 11) | (1u << 13) | (1u << 14) | (1u << 17),
  STATE_RETURN = 0xf000,
  STATE_ORACLE = 0xf100
};

enum {
  VERR,
  VERW,
  SLDT,
  STR,
  SMSW,
  LLDT,
  LTR,
  LMSW,
  SGDT,
  SIDT,
  LGDT,
  LIDT,
  INVLPG,
  SGDT16,
  SIDT16,
  LGDT16,
  LIDT16
};

static int selector_input(unsigned op) {
  return op == VERR || op == VERW || op == LLDT || op == LTR;
}

static int table_operand(unsigned op) {
  return (op >= SGDT && op <= LIDT) || op >= SGDT16;
}

static unsigned segment_index(unsigned prefix) {
  switch (prefix) {
  case 0:
  case 0x3e:
    return 3;
  case 0x26:
    return 0;
  case 0x2e:
    return 1;
  case 0x36:
    return 2;
  case 0x64:
    return 4;
  case 0x65:
    return 5;
  default:
    CHECK(0);
    return 0;
  }
}

static void descriptors(uint8_t *ram, int code16) {
  uint64_t values[] = {0,
                       UINT64_C(0x00cf9b000000ffff),
                       UINT64_C(0x00cf93000000ffff),
                       UINT64_C(0x00cffb000000ffff),
                       UINT64_C(0x00cff3000000ffff),
                       UINT64_C(0x008f9b000000ffff),
                       UINT64_C(0x00cf92000000ffff),
                       UINT64_C(0x00cf90000000ffff),
                       UINT64_C(0x00cf12000000ffff),
                       UINT64_C(0x00cf9a000000ffff),
                       UINT64_C(0x00cf98000000ffff),
                       UINT64_C(0x00cff2000000ffff),
                       UINT64_C(0x00cf920100001234),
                       UINT64_C(0x000092000000ffff),
                       UINT64_C(0x0000890000000067),
                       UINT64_C(0x008ffb000000ffff),
                       UINT64_C(0x00008200500000ff),
                       UINT64_C(0x00008c0000081234),
                       UINT64_C(0x0000090000000067),
                       UINT64_C(0x00008b0000000067),
                       UINT64_C(0x00000200500000ff)};
  values[5] |= UINT64_C(1) << 32;
  values[15] |= UINT64_C(1) << 32;
  if (!code16) {
    values[5] |= UINT64_C(1) << 54;
    values[15] |= UINT64_C(1) << 54;
  }
  memcpy(ram + 0x5000, values, sizeof(values));
  memcpy(ram + 0x6000, values + 6, sizeof(*values));
}

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
  ram[TARGET_PC] = ram[TARGET_PC + 1] = 0x90;
  FILE *image = fopen(argv[1], "rb");
  CHECK(image);
  const size_t bytes = fread(ram + CODE_BASE, 1, 0x80000, image);
  CHECK(bytes && bytes < 0xc000 && !ferror(image));
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
  candidate.initial.gdt.limit = oracle.initial.gdt.limit = 0x1fff;
  candidate.initial.cr0 |= 0x50000; // WP and AM.
  oracle.initial.cr0 |= 0x50000;
  uint8_t *before = malloc(RAM_SIZE), *after = malloc(RAM_SIZE);
  CHECK(before && after);
  FILE *manifest = fopen(argv[2], "r");
  CHECK(manifest);

  char name[128];
  unsigned operation, address_bits, prefix, used, base;
  unsigned long long entry, instruction, next;
  unsigned functions = 0;
  uint64_t faults[18] = {0}, returns = 0, success = 0, rejected = 0;
  const uint16_t selectors[] = {0,  3,   16,  35,  56,  64,  72,     80,
                                91, 112, 128, 144, 152, 160, 0x1000, 0x2000};
  while (fscanf(manifest, "%127s %llx %llx %llx %u %u %u %u %u", name, &entry,
                &instruction, &next, &operation, &address_bits, &prefix, &used,
                &base) == 9) {
    current_function = name;
    CHECK(entry >= CODE_BASE && instruction >= entry && next > instruction &&
          next < CODE_BASE + bytes && operation <= LIDT16 && used <= 1);
    CHECK(address_bits == 16 || address_bits == 32);
    CHECK(address_bits == 16 ? base == 6 || base == 7 : base < 4);
    const unsigned segment_number = segment_index(prefix);
    ++functions;
    uint8_t original[8];
    unsigned length = 0;
    if (prefix)
      original[length++] = prefix;
    if ((address_bits == 16) != code16)
      original[length++] = 0x67;
    if (table_operand(operation) && (operation >= SGDT16) != code16)
      original[length++] = 0x66;
    const unsigned groups[] = {4, 5, 0, 1, 4, 2, 3, 6, 0,
                               1, 2, 3, 7, 0, 1, 2, 3};
    const unsigned second =
        operation <= STR || operation == LLDT || operation == LTR ? 0 : 1;
    original[length++] = 0x0f;
    original[length++] = second;
    original[length++] =
        (groups[operation] << 3) | (address_bits == 16 ? base - 2 : base);
    CHECK(instruction + length == next);
    CHECK(memcmp(ram + instruction, original, length) == 0);
    memcpy(ram + CODE_BASE + STATE_ORACLE, original, length);
    for (unsigned privilege = 0; privilege < 2; ++privilege)
      for (unsigned si = 0; si < (selector_input(operation) ? 16u : 4u); ++si)
        for (unsigned flags = 0; flags < 3; ++flags)
          for (unsigned access = 0; access < 8; ++access) {
            const unsigned cpl = privilege ? 3 : 0;
            const unsigned cs = (cpl ? 120 : 40) | cpl;
            const unsigned ds = (cpl ? 32 : 16) | cpl;
            candidate.initial.cs = oracle.initial.cs = segment(cs, 1);
            candidate.initial.cs.base = oracle.initial.cs.base = CODE_BASE;
            candidate.initial.cs.l = oracle.initial.cs.l = 0;
            candidate.initial.cs.db = oracle.initial.cs.db = !code16;
            candidate.initial.cs.dpl = oracle.initial.cs.dpl = cpl;
            struct kvm_segment data = segment(ds, 0);
            data.dpl = cpl;
            candidate.initial.ss = candidate.initial.ds = candidate.initial.es =
                data;
            candidate.initial.fs = candidate.initial.gs = data;
            candidate.initial.fs.base = 0x40000;
            candidate.initial.gs.base = 0x50000;
            oracle.initial.ss = oracle.initial.ds = oracle.initial.es = data;
            oracle.initial.fs = candidate.initial.fs;
            oracle.initial.gs = candidate.initial.gs;
            candidate.initial.cr3 = oracle.initial.cr3 =
                (comparisons & 1) ? 0x2000 : 0x4000;
            candidate.initial.cr4 = oracle.initial.cr4 =
                0x10 | (flags == 2 ? 0x800 : 0);
            reset_cpu(&candidate, 8);
            reset_cpu(&oracle, 8);
            descriptors(ram, code16);
            uint32_t *pt = (uint32_t *)(ram + 0x3000);
            pt[5] = 0x50e7;
            pt[6] = 0x60e7;
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
            const unsigned delta = access == 1                  ? 1
                                   : access == 2 || access == 3 ? 0xfff
                                                                : 0;
            const uint32_t offset =
                (address_bits == 16 ? 0xa5a5c000 : 0x9c000) + delta;
            const uint32_t effective =
                address_bits == 16 ? (uint16_t)offset : offset;
            const uint32_t pointer =
                effective + (segment_number == 1   ? CODE_BASE
                             : segment_number == 4 ? 0x40000
                             : segment_number == 5 ? 0x50000
                                                   : 0);
            const uint32_t first = pointer & ~4095u;
            pt[first >> 12] = first + 7;
            pt[(first >> 12) + 1] = first + 0x1007;
            const uint16_t scalar[] = {0, 1, 0xe, 0xffff};
            const uint16_t limits[] = {0x1234, 0xffff, 0, 0x7ff};
            const uint32_t bases[] = {0x00123456, 0xabcdef12, 0xfffffff0, 0};
            const uint16_t word =
                selector_input(operation) ? selectors[si] : scalar[si];
            memcpy(ram + pointer, &word, 2);
            if (table_operand(operation)) {
              memcpy(ram + pointer, limits + si, 2);
              memcpy(ram + pointer + 2, bases + si, 4);
            }
            if (access == 3)
              pt[(first >> 12) + 1] &= ~1u;
            if (access == 4)
              pt[first >> 12] &= ~1u;
            if (access == 5)
              pt[first >> 12] &= ~2u;
            if (access == 6) {
              pt[5] &= ~2u;
              pt[6] &= ~2u;
            }
            if (access == 7)
              pt[6] &= ~1u;
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
                                       .rip = entry - CODE_BASE,
                                       .rflags = flags ? 0x438d7 : 0x3002};
            const uint32_t arguments[] = {STATE_RETURN, offset, MARKER};
            memcpy(ram + initial.rsp, arguments, sizeof(arguments));
            CHECK(ioctl(candidate.fd, KVM_SET_REGS, &initial) == 0);
            struct kvm_regs boundary = run_to_linear(
                &candidate, instruction - CODE_BASE, instruction, 0);
            CHECK(ram[MARKER] == 17);
            CHECK((gpr(boundary, base) &
                   (address_bits == 16 ? UINT16_MAX : UINT32_MAX)) ==
                  effective);
            struct kvm_sregs special, actual_special, expected_special;
            struct kvm_vcpu_events events = {0}, actual_events = {0},
                                   expected_events = {0};
            CHECK(ioctl(candidate.fd, KVM_GET_SREGS, &special) == 0);
            CHECK(ioctl(candidate.fd, KVM_GET_VCPU_EVENTS, &events) == 0);
            CHECK(ioctl(oracle.fd, KVM_SET_SREGS, &special) == 0);
            CHECK(ioctl(oracle.fd, KVM_SET_VCPU_EVENTS, &events) == 0);
            memcpy(before, ram, RAM_SIZE);
            unsigned actual_vector, expected_vector;
            struct kvm_regs actual = run_instruction_to_linear(
                &candidate, next - CODE_BASE, next, &actual_vector, FAULTS);
            CHECK(ioctl(candidate.fd, KVM_GET_SREGS, &actual_special) == 0);
            CHECK(ioctl(candidate.fd, KVM_GET_VCPU_EVENTS, &actual_events) ==
                  0);
            memcpy(after, ram, RAM_SIZE);
            memcpy(ram, before, RAM_SIZE);
            boundary.rip = STATE_ORACLE;
            CHECK(ioctl(oracle.fd, KVM_SET_REGS, &boundary) == 0);
            struct kvm_regs expected = run_instruction_to_linear(
                &oracle, STATE_ORACLE + length,
                CODE_BASE + STATE_ORACLE + length, &expected_vector, FAULTS);
            CHECK(ioctl(oracle.fd, KVM_GET_SREGS, &expected_special) == 0);
            CHECK(ioctl(oracle.fd, KVM_GET_VCPU_EVENTS, &expected_events) == 0);
            CHECK(actual_vector == expected_vector);
            if (actual_vector) {
              CHECK(actual.rsp == boundary.rsp - 16 &&
                    expected.rsp == actual.rsp);
              CHECK(*(uint32_t *)(after + actual.rsp + 4) ==
                    instruction - CODE_BASE);
              CHECK(*(uint32_t *)(ram + expected.rsp + 4) == STATE_ORACLE);
              *(uint32_t *)(ram + expected.rsp + 4) = instruction - CODE_BASE;
              ++faults[actual_vector];
            } else {
              expected.rip = next - CODE_BASE;
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
            if (!actual_vector) {
              const unsigned zf = (actual.rflags >> 6) & 1;
              const uint32_t want = operation <= VERW && used ? zf : 42;
              const struct kvm_regs returned = run_to_linear(
                  &candidate, STATE_RETURN, CODE_BASE + STATE_RETURN, 0);
              const uint32_t result = returned.rax;
              CHECK(result == want);
              CHECK(ram[MARKER] == 29 && returned.rsp == initial.rsp + 4 &&
                    returned.rbx == initial.rbx &&
                    returned.rbp == initial.rbp &&
                    returned.rsi == initial.rsi && returned.rdi == initial.rdi);
              ++returns;
              if (operation <= VERW) {
                if (zf)
                  ++success;
                else
                  ++rejected;
              }
            } else {
              CHECK(ram[MARKER] == 17);
            }
            ++comparisons;
          }
  }
  CHECK(feof(manifest) && fclose(manifest) == 0 && functions == 266);
  printf("{\"functions\":%u,\"complete_state_pairs\":%llu,\"returns\":%llu,"
         "\"success\":%llu,\"rejected\":%llu,\"np\":%llu,\"gp\":%llu,\"pf\":%"
         "llu,\"ac\":%llu}\n",
         functions, comparisons, (unsigned long long)returns,
         (unsigned long long)success, (unsigned long long)rejected,
         (unsigned long long)faults[11], (unsigned long long)faults[13],
         (unsigned long long)faults[14], (unsigned long long)faults[17]);
  return 0;
}
