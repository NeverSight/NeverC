// Compare protected code16/code32 division with independent original bytes.
#include "KvmTransferFixture.h"

enum { IDT_BASE = 0x6000, FAULT_PC = 0x1900, Q_OUT = 0x8100, R_OUT = 0x8110 };

static uint64_t reg_value(struct kvm_regs r, unsigned index, unsigned bits) {
  const uint64_t values[] = {r.rax, r.rcx, r.rdx, r.rbx,
                             r.rsp, r.rbp, r.rsi, r.rdi};
  CHECK(index < 8);
  if (bits == 8 && index >= 4)
    return (values[index - 4] >> 8) & 255;
  return values[index] & (UINT64_MAX >> (64 - bits));
}

static struct kvm_regs divide_to(struct cpu *cpu, uint64_t next) {
  struct kvm_guest_debug debug = {.control = KVM_GUESTDBG_ENABLE |
                                             KVM_GUESTDBG_USE_HW_BP};
  debug.arch.debugreg[0] = next;
  debug.arch.debugreg[1] = FAULT_PC;
  debug.arch.debugreg[7] = 0x405;
  CHECK(ioctl(cpu->fd, KVM_SET_GUEST_DEBUG, &debug) == 0);
  CHECK(ioctl(cpu->fd, KVM_RUN, 0) == 0);
  struct kvm_regs result;
  CHECK(ioctl(cpu->fd, KVM_GET_REGS, &result) == 0);
  if (cpu->run->exit_reason != KVM_EXIT_DEBUG ||
      (result.rip != next && result.rip != FAULT_PC)) {
    fprintf(stderr, "exit %u, rip %#llx, expected %#llx or #DE\n",
            cpu->run->exit_reason, result.rip, (unsigned long long)next);
    CHECK(0);
  }
  return result;
}

static uint64_t load64(const uint8_t *ram, unsigned address) {
  uint64_t result;
  memcpy(&result, ram + address, sizeof(result));
  return result;
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
  // One flat, already accessed/dirty 4 MiB mapping. This oracle owns division
  // and #DE; it does not claim operand-memory fault or page-walk coverage.
  ((uint32_t *)(ram + 0x2000))[0] = 0xe7;
  const uint64_t descriptors[] = {
      0, UINT64_C(0x00cf9b000000ffff), UINT64_C(0x00cf93000000ffff), 0,
      0, UINT64_C(0x008f9b000000ffff)};
  memcpy(ram + 0x5000, descriptors, sizeof(descriptors));
  const uint64_t gate = UINT64_C(0x00008e0000080000) | FAULT_PC;
  memcpy(ram + IDT_BASE, &gate, sizeof(gate));
  ram[TARGET_PC] = ram[TARGET_PC + 1] = 0x90;
  ram[FAULT_PC] = ram[FAULT_PC + 1] = 0x90;
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
  candidate.initial.idt.limit = oracle.initial.idt.limit = 7;
  if (code16) {
    candidate.initial.cs.selector = oracle.initial.cs.selector = 40;
    candidate.initial.cs.db = oracle.initial.cs.db = 0;
  }
  uint8_t *before = malloc(RAM_SIZE), *after = malloc(RAM_SIZE);
  CHECK(before && after);
  FILE *manifest = fopen(argv[2], "r");
  CHECK(manifest);
  char name[96];
  unsigned bits, sign, results, operand;
  unsigned long long entry, instruction;
  unsigned functions = 0;
  uint64_t faults = 0;
  while (fscanf(manifest, "%95s %llx %llx %u %u %u %u", name, &entry,
                &instruction, &bits, &sign, &results, &operand) == 7) {
    current_function = name;
    CHECK(entry >= CODE_BASE && instruction >= entry &&
          instruction < CODE_BASE + bytes);
    CHECK((bits == 8 || bits == 16 || bits == 32) && sign <= 1 &&
          results <= 3 && operand < 8);
    ++functions;
    uint8_t original[3];
    unsigned length = 0;
    if ((bits == 16 && !code16) || (bits == 32 && code16))
      original[length++] = 0x66;
    original[length++] = bits == 8 ? 0xf6 : 0xf7;
    original[length++] = 0xc0 | ((sign ? 7 : 6) << 3) | operand;
    CHECK(memcmp(ram + instruction, original, length) == 0);
    memcpy(ram + ORACLE_PC, original, length);
    const uint64_t mask = UINT64_MAX >> (64 - bits);
    const uint64_t middle = UINT64_C(1) << (bits - 1);
    const uint64_t values[] = {
        0,      1,          2,          3,
        7,      13,         mask,       mask - 1,
        middle, middle - 1, middle + 1, UINT64_C(0x876543210abcdef0) & mask};
    for (unsigned li = 0; li < 12; ++li)
      for (unsigned hi = 0; hi < 12; ++hi)
        for (unsigned di = 0; di < 12; ++di) {
          reset_cpu(&candidate, 8);
          reset_cpu(&oracle, 8);
          memset(ram + 0x1e0000, 0x5a, 0x20000);
          const uint64_t untouched_q = UINT64_C(0xaabbccdd12345678);
          const uint64_t untouched_r = UINT64_C(0x0123456789abcdef);
          memcpy(ram + Q_OUT, &untouched_q, sizeof(untouched_q));
          memcpy(ram + R_OUT, &untouched_r, sizeof(untouched_r));
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
                                     .rflags = (li & 1) ? 0x8d7 : 2};
          const uint32_t arguments[] = {
              TARGET_PC,  values[li], 0xaaaaaaaa, values[hi], 0xbbbbbbbb,
              values[di], 0xcccccccc, Q_OUT,      R_OUT,      MARKER};
          memcpy(ram + initial.rsp, arguments, sizeof(arguments));
          CHECK(ioctl(candidate.fd, KVM_SET_REGS, &initial) == 0);
          struct kvm_regs boundary = run_to(&candidate, instruction, 0);
          CHECK(ram[MARKER] == 17 && load64(ram, Q_OUT) == untouched_q &&
                load64(ram, R_OUT) == untouched_r);
          if (bits == 8)
            CHECK((boundary.rax & 0xffff) == (values[li] | (values[hi] << 8)));
          else
            CHECK((boundary.rax & mask) == values[li] &&
                  (boundary.rdx & mask) == values[hi]);
          CHECK(reg_value(boundary, operand, bits) == values[di]);
          struct kvm_sregs special, actual_special, expected_special;
          struct kvm_vcpu_events events = {0}, actual_events = {0},
                                 expected_events = {0};
          CHECK(ioctl(candidate.fd, KVM_GET_SREGS, &special) == 0);
          CHECK(ioctl(candidate.fd, KVM_GET_VCPU_EVENTS, &events) == 0);
          CHECK(ioctl(oracle.fd, KVM_SET_SREGS, &special) == 0);
          CHECK(ioctl(oracle.fd, KVM_SET_VCPU_EVENTS, &events) == 0);
          memcpy(before, ram, RAM_SIZE);
          struct kvm_regs actual = divide_to(&candidate, instruction + length);
          CHECK(ioctl(candidate.fd, KVM_GET_SREGS, &actual_special) == 0);
          CHECK(ioctl(candidate.fd, KVM_GET_VCPU_EVENTS, &actual_events) == 0);
          memcpy(after, ram, RAM_SIZE);
          memcpy(ram, before, RAM_SIZE);
          boundary.rip = ORACLE_PC;
          CHECK(ioctl(oracle.fd, KVM_SET_REGS, &boundary) == 0);
          struct kvm_regs expected = divide_to(&oracle, ORACLE_PC + length);
          CHECK(ioctl(oracle.fd, KVM_GET_SREGS, &expected_special) == 0);
          CHECK(ioctl(oracle.fd, KVM_GET_VCPU_EVENTS, &expected_events) == 0);
          const int faulted = actual.rip == FAULT_PC;
          CHECK(faulted == (expected.rip == FAULT_PC));
          if (faulted) {
            CHECK(actual.rsp == boundary.rsp - 12 &&
                  expected.rsp == actual.rsp);
            CHECK(*(uint32_t *)(after + actual.rsp) == instruction);
            CHECK(*(uint32_t *)(ram + expected.rsp) == ORACLE_PC);
            // Authenticate each original restart PC before normalizing the
            // only intentional difference in the independent #DE frames.
            *(uint32_t *)(ram + expected.rsp) = instruction;
            CHECK(actual.rax == boundary.rax && actual.rdx == boundary.rdx);
          } else {
            expected.rip = instruction + length;
          }
          CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
          CHECK(memcmp(&actual_special, &expected_special,
                       sizeof(actual_special)) == 0);
          CHECK(memcmp(&actual_events, &expected_events,
                       sizeof(actual_events)) == 0);
          CHECK(memcmp(after, ram, RAM_SIZE) == 0);
          memcpy(ram, after, RAM_SIZE);
          if (faulted) {
            CHECK(ram[MARKER] == 17 && load64(ram, Q_OUT) == untouched_q &&
                  load64(ram, R_OUT) == untouched_r);
            ++faults;
          } else {
            const uint64_t quotient = actual.rax & mask;
            const uint64_t remainder =
                bits == 8 ? (actual.rax >> 8) & 255 : actual.rdx & mask;
            const struct kvm_regs returned = run_to(&candidate, TARGET_PC, 0);
            CHECK(returned.rsp == initial.rsp + 4 &&
                  returned.rbx == initial.rbx && returned.rbp == initial.rbp &&
                  returned.rsi == initial.rsi && returned.rdi == initial.rdi);
            CHECK(ram[MARKER] == 29);
            CHECK(load64(ram, Q_OUT) == (results & 1 ? quotient : untouched_q));
            CHECK(load64(ram, R_OUT) ==
                  (results & 2 ? remainder : untouched_r));
          }
          ++comparisons;
        }
  }
  CHECK(feof(manifest) && fclose(manifest) == 0 && functions == 24);
  printf("{\"functions\":%u,\"complete_state_pairs\":%llu,\"faults\":%llu}\n",
         functions, comparisons, (unsigned long long)faults);
  return 0;
}
