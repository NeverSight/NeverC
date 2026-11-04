// Compare native ARPL with independent bytes, including original fault frames.
#include "KvmProtectedFaultFixture.h"

enum {
  CELL_BASE = 0x90000,
  FAULTS = (1u << 13) | (1u << 14) | (1u << 17),
  MEMORY_CASES = 12
};

static uint16_t word(const uint8_t *ram, unsigned address) {
  uint16_t value;
  memcpy(&value, ram + address, sizeof(value));
  return value;
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
  unsigned memory, alias, results, destination, source;
  unsigned long long entry, instruction;
  unsigned functions = 0;
  uint64_t faults[18] = {0}, returns = 0, changed = 0, unchanged = 0;
  const uint32_t patterns[] = {0x12340000, 0xabcd7ffc, 0x56788000, 0xdeadfffc};
  while (fscanf(manifest, "%95s %llx %llx %u %u %u %u %u", name, &entry,
                &instruction, &memory, &alias, &results, &destination,
                &source) == 8) {
    current_function = name;
    CHECK(entry >= CODE_BASE && instruction >= entry &&
          instruction < CODE_BASE + bytes && memory < 2 && alias < 2 &&
          results < 4 && destination < 8 && source < 8);
    // These pointer functions use DS, independently of their stack frames.
    CHECK(!memory || (destination != 4 && destination != 5));
    ++functions;
    uint8_t original[3];
    unsigned length = 0;
    if (memory && code16)
      original[length++] = 0x67;
    original[length++] = 0x63;
    original[length++] = (memory ? 0 : 0xc0) | (source << 3) | destination;
    CHECK(memcmp(ram + instruction, original, length) == 0);
    memcpy(ram + ORACLE_PC, original, length);
    for (unsigned privilege = 0; privilege < 2; ++privilege)
      for (unsigned pi = 0; pi < 4; ++pi)
        for (unsigned rpls = 0; rpls < 16; ++rpls)
          for (unsigned flags = 0; flags < 2; ++flags)
            for (unsigned access = 0; access < (memory ? MEMORY_CASES : 1);
                 ++access) {
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
              candidate.initial.ss = candidate.initial.ds =
                  candidate.initial.es = data;
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
              pt[0x90] = CELL_BASE + 7;
              pt[0x91] = CELL_BASE + 0x1007;
              pt[0x92] = CELL_BASE + 0x2007;
              const unsigned pointer =
                  CELL_BASE + (access == 1 ? 1
                               : access == 2 || access == 5 || access == 6
                                   ? 0xfff
                                   : 0);
              const unsigned linear = pointer + (access == 11 ? 0x1000 : 0);
              const uint32_t d = patterns[pi] | (rpls & 3);
              const uint32_t s =
                  alias ? d : (~patterns[pi] & ~3u) | (rpls >> 2);
              memcpy(ram + linear, &d, 2);
              if (access == 3)
                pt[0x90] &= ~2u;
              else if (access == 4)
                pt[0x90] &= ~1u;
              else if (access == 5)
                pt[0x91] &= ~1u;
              else if (access == 6)
                pt[0x91] &= ~2u;
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
              const uint32_t arguments[] = {TARGET_PC, d, s, pointer, MARKER};
              memcpy(ram + initial.rsp, arguments, sizeof(arguments));
              CHECK(ioctl(candidate.fd, KVM_SET_REGS, &initial) == 0);
              struct kvm_regs boundary = run_to(&candidate, instruction, 0);
              CHECK(ram[MARKER] == 17 &&
                    (gpr(boundary, source) & 0xffff) == (s & 0xffff));
              CHECK(memory ? (uint32_t)gpr(boundary, destination) == pointer
                           : (gpr(boundary, destination) & 0xffff) ==
                                 (d & 0xffff));
              struct kvm_sregs special, actual_special, expected_special;
              struct kvm_vcpu_events events = {0}, actual_events = {0},
                                     expected_events = {0};
              CHECK(ioctl(candidate.fd, KVM_GET_SREGS, &special) == 0);
              // Probe non-ABI segment states only at the native boundary.
              if (access == 7)
                special.ds.type = 1;
              else if (access == 8) {
                special.ds.limit = pointer;
                special.ds.g = 0;
              } else if (access == 9) {
                special.ds.selector = 0;
                special.ds.unusable = 1;
              } else if (access == 10) {
                special.ds.type = 7;
                special.ds.limit = pointer;
                special.ds.g = 0;
              } else if (access == 11) {
                special.ds.base = 0x1000;
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
              CHECK(ioctl(oracle.fd, KVM_GET_VCPU_EVENTS, &expected_events) ==
                    0);
              CHECK(actual_vector == expected_vector);
              const unsigned adjust = (d & 3) < (s & 3);
              const uint16_t result = adjust ? (d & ~3u) | (s & 3) : d;
              if (actual_vector) {
                CHECK(actual.rsp == boundary.rsp - 16 &&
                      expected.rsp == actual.rsp);
                CHECK(*(uint32_t *)(after + actual.rsp + 4) == instruction);
                CHECK(*(uint32_t *)(ram + expected.rsp + 4) == ORACLE_PC);
                *(uint32_t *)(ram + expected.rsp + 4) = instruction;
                CHECK(gpr(actual, source) == gpr(boundary, source));
                CHECK(word(after, linear) == (uint16_t)d);
                ++faults[actual_vector];
              } else {
                expected.rip = instruction + length;
                CHECK((actual.rflags & 0x40) == (adjust << 6));
                CHECK(((actual.rflags ^ boundary.rflags) & ~UINT64_C(0x40)) ==
                      0);
                CHECK((memory ? word(after, linear)
                              : gpr(actual, destination) & 0xffff) == result);
                if (adjust)
                  ++changed;
                else
                  ++unchanged;
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
              if (!actual_vector && access < 7) {
                const struct kvm_regs returned =
                    run_to(&candidate, TARGET_PC, 0);
                CHECK(ram[MARKER] == 29 && returned.rsp == initial.rsp + 4 &&
                      returned.rbx == initial.rbx &&
                      returned.rbp == initial.rbp &&
                      returned.rsi == initial.rsi &&
                      returned.rdi == initial.rdi);
                const uint32_t output = (results ? 0 : 42) |
                                        ((results & 1) ? result : 0) |
                                        ((results & 2) ? adjust << 16 : 0);
                CHECK((uint32_t)returned.rax == output);
                ++returns;
              } else {
                CHECK(ram[MARKER] == 17);
              }
              ++comparisons;
            }
  }
  CHECK(feof(manifest) && fclose(manifest) == 0 && functions == 8);
  printf("{\"functions\":%u,\"complete_state_pairs\":%llu,\"returns\":%llu,"
         "\"changed\":%llu,\"unchanged\":%llu,\"gp\":%llu,\"pf\":%llu,\"ac\":%"
         "llu}\n",
         functions, comparisons, (unsigned long long)returns,
         (unsigned long long)changed, (unsigned long long)unchanged,
         (unsigned long long)faults[13], (unsigned long long)faults[14],
         (unsigned long long)faults[17]);
  return 0;
}
