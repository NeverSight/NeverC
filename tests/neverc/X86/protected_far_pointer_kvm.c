// Native protected far-pointer loads, including hardware-owned selector faults.
#include "KvmProtectedFaultFixture.h"

enum { POINTER_BASE = 0x90000, FAULTS = (1u << 11) | (1u << 12) |
                                      (1u << 13) | (1u << 14) };

static void put_descriptor(uint8_t *ram, unsigned selector, uint64_t value) {
  memcpy(ram + 0x5000 + selector, &value, sizeof(value));
}

static void descriptors(uint8_t *ram) {
  put_descriptor(ram, 8, UINT64_C(0x00cf9b000000ffff));
  put_descriptor(ram, 16, UINT64_C(0x00cf93000000ffff));
  put_descriptor(ram, 40, UINT64_C(0x008f9b000000ffff));
  put_descriptor(ram, 48, UINT64_C(0x00cf92000000ffff));
  put_descriptor(ram, 56, UINT64_C(0x00cf90000000ffff));
  put_descriptor(ram, 64, UINT64_C(0x00cf12000000ffff));
  put_descriptor(ram, 72, UINT64_C(0x00cf9a000000ffff));
  put_descriptor(ram, 80, UINT64_C(0x00cf98000000ffff));
  put_descriptor(ram, 88, UINT64_C(0x00cff2000000ffff));
  put_descriptor(ram, 96, UINT64_C(0x00cf92010000ffff));
  put_descriptor(ram, 104, UINT64_C(0x000092000000ffff));
  put_descriptor(ram, 112, UINT64_C(0x0000890000000067));
  put_descriptor(ram, 0x1000, UINT64_C(0x00cf92000000ffff));
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
  descriptors(ram);
  for (unsigned vector = 11; vector <= 14; ++vector) {
    const unsigned pc = FAULT_BASE + vector * 16;
    const uint64_t gate = UINT64_C(0x00008e0000080000) | pc;
    memcpy(ram + IDT_BASE + vector * 8, &gate, sizeof(gate));
    ram[pc] = 0xe6;
    ram[pc + 1] = 0x80;
  }
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
  if (code16) {
    candidate.initial.cs.selector = oracle.initial.cs.selector = 40;
    candidate.initial.cs.db = oracle.initial.cs.db = 0;
  }
  uint8_t *before = malloc(RAM_SIZE), *after = malloc(RAM_SIZE);
  CHECK(before && after);
  FILE *manifest = fopen(argv[2], "r");
  CHECK(manifest);
  char name[96];
  unsigned kind, bits, used, base, destination;
  unsigned long long entry, instruction;
  unsigned functions = 0;
  uint64_t faults[15] = {0}, returns = 0;
  const unsigned selectors[] = {0,  1,  2,  3,  48, 49,  50,  51,     56,    64,
                                72, 80, 88, 91, 96, 104, 112, 0x1000, 0x2000};
  const uint32_t offsets[] = {0, 1, 0x8000, 0xffff, 0xa5a51234, UINT32_MAX};
  while (fscanf(manifest, "%95s %llx %llx %u %u %u %u %u", name, &entry,
                &instruction, &kind, &bits, &used, &base, &destination) == 8) {
    current_function = name;
    CHECK(entry >= CODE_BASE && instruction >= entry &&
          instruction < CODE_BASE + bytes && kind < 5 &&
          (bits == 16 || bits == 32) && used < 2 && base < 8 &&
          destination < 8);
    ++functions;
    uint8_t original[8];
    unsigned length = 0;
    if (code16)
      original[length++] = 0x67;
    if ((bits == 16 && !code16) || (bits == 32 && code16))
      original[length++] = 0x66;
    if (kind < 2) {
      original[length++] = kind ? 0xc4 : 0xc5;
    } else {
      original[length++] = 0x0f;
      original[length++] = kind == 2 ? 0xb2 : kind == 3 ? 0xb4 : 0xb5;
    }
    original[length++] = (base == 5 ? 0x40 : 0) | (destination << 3) | base;
    if (base == 4)
      original[length++] = 0x24;
    if (base == 5)
      original[length++] = 0;
    CHECK(memcmp(ram + instruction, original, length) == 0);
    memcpy(ram + ORACLE_PC, original, length);
    for (unsigned si = 0; si < sizeof(selectors) / sizeof(*selectors); ++si)
      for (unsigned memory = 0; memory < 7; ++memory)
        for (unsigned oi = 0; oi < sizeof(offsets) / sizeof(*offsets); ++oi) {
          // Change CR3 each case so modified pointer/descriptor PTEs cannot
          // reuse the previous case's cached translation.
          candidate.initial.cr3 = oracle.initial.cr3 =
              (comparisons & 1) ? 0x2000 : 0x4000;
          reset_cpu(&candidate, 8);
          reset_cpu(&oracle, 8);
          uint32_t *pt = (uint32_t *)(ram + 0x3000);
          pt[0x90] = POINTER_BASE + 7;
          pt[0x91] = POINTER_BASE + 0x1007;
          pt[6] = 0x60e7;
          descriptors(ram);
          memset(ram + 0x1e0000, 0x5a, 0x20000);
          ram[MARKER] = 0x91;
          const unsigned pointer =
              POINTER_BASE + (memory == 1                  ? 1
                              : memory == 2 || memory == 3 ? 0xffe
                                                           : 0);
          const uint16_t selector = selectors[si];
          memcpy(ram + pointer, &offsets[oi], bits / 8);
          memcpy(ram + pointer + bits / 8, &selector, sizeof(selector));
          if (memory == 3)
            pt[0x91] &= ~1u;
          else if (memory == 4)
            pt[0x90] &= ~1u;
          else if (memory == 5)
            pt[0x90] &= ~2u;
          else if (memory == 6)
            pt[6] &= ~1u;
          struct kvm_regs initial = {.rax = 0x11223344,
                                     .rbx = 0x55667788,
                                     .rcx = 0x12345678,
                                     .rdx = 0x76543210,
                                     .rsi = 0xaabbccdd,
                                     .rdi = 0x98765432,
                                     .rsp = STACK_TOP + 4,
                                     .rbp = 0x10203040,
                                     .rip = entry,
                                     .rflags = (oi & 1) ? 0x8d7 : 2};
          const uint32_t arguments[] = {TARGET_PC, pointer, MARKER};
          memcpy(ram + initial.rsp, arguments, sizeof(arguments));
          CHECK(ioctl(candidate.fd, KVM_SET_REGS, &initial) == 0);
          struct kvm_regs boundary = run_to(&candidate, instruction, 0);
          CHECK(ram[MARKER] == 17 && (uint32_t)gpr(boundary, base) == pointer);
          struct kvm_sregs special, actual_special, expected_special;
          struct kvm_vcpu_events events = {0}, actual_events = {0},
                                 expected_events = {0};
          CHECK(ioctl(candidate.fd, KVM_GET_SREGS, &special) == 0);
          CHECK(ioctl(candidate.fd, KVM_GET_VCPU_EVENTS, &events) == 0);
          CHECK(ioctl(oracle.fd, KVM_SET_SREGS, &special) == 0);
          CHECK(ioctl(oracle.fd, KVM_SET_VCPU_EVENTS, &events) == 0);
          memcpy(before, ram, RAM_SIZE);
          unsigned actual_vector, expected_vector;
          struct kvm_regs actual =
              run_instruction_to(&candidate, instruction + length, &actual_vector, FAULTS);
          CHECK(ioctl(candidate.fd, KVM_GET_SREGS, &actual_special) == 0);
          CHECK(ioctl(candidate.fd, KVM_GET_VCPU_EVENTS, &actual_events) == 0);
          memcpy(after, ram, RAM_SIZE);
          memcpy(ram, before, RAM_SIZE);
          boundary.rip = ORACLE_PC;
          CHECK(ioctl(oracle.fd, KVM_SET_REGS, &boundary) == 0);
          struct kvm_regs expected =
              run_instruction_to(&oracle, ORACLE_PC + length, &expected_vector, FAULTS);
          CHECK(ioctl(oracle.fd, KVM_GET_SREGS, &expected_special) == 0);
          CHECK(ioctl(oracle.fd, KVM_GET_VCPU_EVENTS, &expected_events) == 0);
          CHECK(actual_vector == expected_vector);
          if (actual_vector) {
            CHECK(actual.rsp == boundary.rsp - 16 &&
                  expected.rsp == actual.rsp);
            CHECK(*(uint32_t *)(after + actual.rsp + 4) == instruction);
            CHECK(*(uint32_t *)(ram + expected.rsp + 4) == ORACLE_PC);
            *(uint32_t *)(ram + expected.rsp + 4) = instruction;
            CHECK(gpr(actual, destination) == gpr(boundary, destination));
            ++faults[actual_vector];
          } else {
            expected.rip = instruction + length;
            const uint32_t mask = bits == 16 ? 0xffff : UINT32_MAX;
            CHECK((gpr(actual, destination) & mask) == (offsets[oi] & mask));
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
          // Only finish the compiler frame when the installed segment still
          // satisfies its ABI. Nonflat/null/read-only DS and word/nonflat SS
          // are qualified at the native boundary, not as callable C functions.
          const struct kvm_segment ds = actual_special.ds,
                                   ss = actual_special.ss;
          const int abi = !ds.unusable && ds.base == 0 &&
                          ds.limit == UINT32_MAX && (ds.type & 0xa) == 2 &&
                          ss.base == 0 && ss.db && ss.limit == UINT32_MAX;
          if (!actual_vector && abi) {
            const struct kvm_regs returned = run_to(&candidate, TARGET_PC, 0);
            CHECK(ram[MARKER] == 29 && returned.rsp == initial.rsp + 4 &&
                  returned.rbx == initial.rbx && returned.rbp == initial.rbp &&
                  returned.rsi == initial.rsi && returned.rdi == initial.rdi);
            const uint32_t result =
                used ? offsets[oi] & (bits == 16 ? 0xffff : UINT32_MAX) : 42;
            CHECK((uint32_t)returned.rax == result);
            ++returns;
          } else {
            CHECK(ram[MARKER] == 17);
          }
          ++comparisons;
        }
  }
  CHECK(feof(manifest) && fclose(manifest) == 0 && functions == 20);
  printf("{\"functions\":%u,\"complete_state_pairs\":%llu,\"returns\":%llu,"
         "\"np\":%llu,\"ss\":%llu,\"gp\":%llu,\"pf\":%llu}\n",
         functions, comparisons, (unsigned long long)returns,
         (unsigned long long)faults[11], (unsigned long long)faults[12],
         (unsigned long long)faults[13], (unsigned long long)faults[14]);
  return 0;
}
