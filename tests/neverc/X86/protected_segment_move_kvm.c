// Native protected segment MOV; LFENCE completes the MOV-SS debug shadow.
#include "KvmProtectedFaultFixture.h"

enum {
  POINTER_BASE = 0x90000,
  FAULTS = (1u << 11) | (1u << 12) | (1u << 13) | (1u << 14) | (1u << 17)
};

static struct kvm_segment selected(struct kvm_sregs s, unsigned index) {
  const struct kvm_segment segments[] = {s.es, s.cs, s.ss, s.ds, s.fs, s.gs};
  CHECK(index < 6);
  return segments[index];
}

static void descriptors(uint8_t *ram) {
  const uint64_t values[] = {0,
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
                             UINT64_C(0x00cf92010000ffff),
                             UINT64_C(0x000092000000ffff),
                             UINT64_C(0x0000890000000067),
                             UINT64_C(0x008ffb000000ffff)};
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
  candidate.initial.gdt.limit = oracle.initial.gdt.limit = 0x1fff;
  candidate.initial.cr0 |= 0x50000; // WP and AM.
  oracle.initial.cr0 |= 0x50000;
  uint8_t *before = malloc(RAM_SIZE), *after = malloc(RAM_SIZE);
  CHECK(before && after);
  FILE *manifest = fopen(argv[2], "r");
  CHECK(manifest);
  char name[96];
  unsigned kind, segment_index, used, operand;
  unsigned long long entry, instruction, fence;
  unsigned functions = 0;
  uint64_t faults[18] = {0}, returns = 0;
  const uint16_t selectors[] = {0,  1,  2,  3,  16, 32,  35,  48,     56,    64,
                                72, 80, 88, 91, 96, 104, 112, 0x1000, 0x2000};
  while (fscanf(manifest, "%95s %llx %llx %llx %u %u %u %u", name, &entry,
                &instruction, &fence, &kind, &segment_index, &used,
                &operand) == 8) {
    current_function = name;
    CHECK(entry >= CODE_BASE && instruction >= entry && fence > instruction &&
          fence + 3 < CODE_BASE + bytes && kind < 4 && segment_index < 6 &&
          (kind < 2 || segment_index != 1) && used < 2 && operand < 8);
    const int memory = kind & 1, write_segment = kind >= 2;
    CHECK(!memory || (operand != 4 && operand != 5));
    ++functions;
    uint8_t original[8];
    unsigned length = 0;
    if (memory && code16)
      original[length++] = 0x67;
    if (!memory && !code16)
      original[length++] = 0x66;
    original[length++] = write_segment ? 0x8e : 0x8c;
    original[length++] = (memory ? 0 : 0xc0) | (segment_index << 3) | operand;
    CHECK(instruction + length == fence);
    // An explicit, adjacent LFENCE has no represented register/flag/memory
    // changes and ends successful MOV SS inhibition before the debug stop.
    // Compare this same two-instruction span on both vCPUs. This does not
    // claim to measure interrupt/debug delivery inside that shadow.
    original[length++] = 0x0f;
    original[length++] = 0xae;
    original[length++] = 0xe8;
    CHECK(memcmp(ram + instruction, original, length) == 0);
    memcpy(ram + ORACLE_PC, original, length);
    for (unsigned privilege = 0; privilege < 2; ++privilege)
      for (unsigned si = 0; si < (write_segment ? 19 : 1); ++si)
        for (unsigned mi = 0; mi < (memory ? 9 : write_segment ? 3 : 1); ++mi)
          for (unsigned flags = 0; flags < 2; ++flags) {
            const unsigned access = !memory && mi ? mi + 5 : mi;
            const unsigned cpl = privilege ? 3 : 0;
            const unsigned cs =
                (code16 ? (cpl ? 120 : 40) : (cpl ? 24 : 8)) | cpl;
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
            uint32_t *pt = (uint32_t *)(ram + 0x3000);
            pt[0x90] = POINTER_BASE + 7;
            pt[0x91] = POINTER_BASE + 0x1007;
            pt[5] = 0x50e7;
            pt[6] = 0x60e7;
            descriptors(ram);
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
            const unsigned pointer =
                POINTER_BASE + (access == 1 || access == 8   ? 1
                                : access == 2 || access == 3 ? 0xfff
                                                             : 0);
            const uint16_t selector = selectors[si];
            memcpy(ram + pointer, &selector, sizeof(selector));
            if (access == 3)
              pt[0x91] &= ~1u;
            else if (access == 4)
              pt[0x90] &= ~1u;
            else if (access == 5)
              pt[0x90] &= ~2u;
            else if (access == 6 || access == 8)
              pt[6] &= ~1u;
            else if (access == 7)
              pt[5] &= ~2u;
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
            const uint32_t arguments[] = {TARGET_PC, 0xa5a50000 | selector,
                                          pointer, MARKER};
            memcpy(ram + initial.rsp, arguments, sizeof(arguments));
            CHECK(ioctl(candidate.fd, KVM_SET_REGS, &initial) == 0);
            struct kvm_regs boundary = run_to(&candidate, instruction, 0);
            CHECK(ram[MARKER] == 17);
            if (memory)
              CHECK((uint32_t)gpr(boundary, operand) == pointer);
            else if (write_segment)
              CHECK((gpr(boundary, operand) & 0xffff) == selector);
            struct kvm_sregs special, actual_special, expected_special;
            struct kvm_vcpu_events events = {0}, actual_events = {0},
                                   expected_events = {0};
            CHECK(ioctl(candidate.fd, KVM_GET_SREGS, &special) == 0);
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
              CHECK(actual.rsp == boundary.rsp - 16 &&
                    expected.rsp == actual.rsp);
              CHECK(*(uint32_t *)(after + actual.rsp + 4) == instruction);
              CHECK(*(uint32_t *)(ram + expected.rsp + 4) == ORACLE_PC);
              *(uint32_t *)(ram + expected.rsp + 4) = instruction;
              ++faults[actual_vector];
            } else {
              expected.rip = instruction + length;
              CHECK(actual.rflags == boundary.rflags);
              if (kind == 0)
                CHECK((gpr(actual, operand) & 0xffff) ==
                      selected(special, segment_index).selector);
              if (kind == 1) {
                uint16_t stored;
                memcpy(&stored, after + pointer, sizeof(stored));
                CHECK(stored == selected(special, segment_index).selector);
              }
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
            const struct kvm_segment ds_after = actual_special.ds,
                                     ss_after = actual_special.ss;
            const int abi = !ds_after.unusable && ds_after.base == 0 &&
                            ds_after.limit == UINT32_MAX &&
                            (ds_after.type & 0xa) == 2 && ss_after.base == 0 &&
                            ss_after.db && ss_after.limit == UINT32_MAX;
            if (!actual_vector && abi) {
              const struct kvm_regs returned = run_to(&candidate, TARGET_PC, 0);
              CHECK(ram[MARKER] == 29 && returned.rsp == initial.rsp + 4 &&
                    returned.rbx == initial.rbx &&
                    returned.rbp == initial.rbp &&
                    returned.rsi == initial.rsi && returned.rdi == initial.rdi);
              CHECK(returned.rax ==
                    (used ? selected(special, segment_index).selector : 42));
              ++returns;
            } else {
              CHECK(ram[MARKER] == 17);
            }
            ++comparisons;
          }
  }
  CHECK(feof(manifest) && fclose(manifest) == 0 && functions == 28);
  printf("{\"functions\":%u,\"complete_state_pairs\":%llu,\"returns\":%llu,"
         "\"np\":%llu,\"ss\":%llu,\"gp\":%llu,\"pf\":%llu,\"ac\":%llu}\n",
         functions, comparisons, (unsigned long long)returns,
         (unsigned long long)faults[11], (unsigned long long)faults[12],
         (unsigned long long)faults[13], (unsigned long long)faults[14],
         (unsigned long long)faults[17]);
  return 0;
}
