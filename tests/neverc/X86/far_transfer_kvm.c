// Native far CALL/JMP compiler transport and returning far-entry ABI checks.
#include "KvmTransferFixture.h"

enum {
  IMAGE = 0x9000,
  POINTER = 0x8100,
  VALUES = 0x8200,
  DONE = 0x1700,
  DESTINATION = 0x1800,
  NEW_STACK = 0x1d0000,
  TSS = 0x6000
};

static void descriptor(uint8_t *ram, unsigned selector, unsigned mode,
                       unsigned cpl) {
  uint64_t value = UINT64_C(0x000f90000000ffff) | ((uint64_t)cpl << 45);
  value |= mode ? UINT64_C(0xb0000000000) : UINT64_C(0x30000000000);
  value |= mode == 64   ? UINT64_C(0xa0000000000000)
           : mode == 16 ? 0
                        : UINT64_C(0xc0000000000000);
  memcpy(ram + 0x5000 + selector, &value, 8);
}

static uint64_t reg_value(struct kvm_regs r, unsigned index) {
  const uint64_t values[] = {r.rax, r.rcx, r.rdx, r.rbx, r.rsp, r.rbp,
                             r.rsi, r.rdi, r.r8,  r.r9,  r.r10, r.r11,
                             r.r12, r.r13, r.r14, r.r15};
  CHECK(index < 16);
  return values[index];
}

static unsigned native_bytes(uint8_t *bytes, unsigned mode, unsigned bits,
                             int call, int immediate, unsigned base,
                             unsigned address_bits) {
  unsigned size = 0;
  if (!immediate && (mode == 16 || (mode == 64 && address_bits == 32)))
    bytes[size++] = 0x67;
  if ((bits == 16 && mode != 16) || (bits == 32 && mode == 16))
    bytes[size++] = 0x66;
  if (bits == 64 || base >= 8)
    bytes[size++] = 0x40 | (bits == 64 ? 8 : 0) | (base >= 8);
  if (immediate) {
    bytes[size++] = call ? 0x9a : 0xea;
    const uint64_t offset = DESTINATION;
    memcpy(bytes + size, &offset, bits / 8);
    size += bits / 8;
    bytes[size++] = 48;
    bytes[size++] = 0;
  } else {
    bytes[size++] = 0xff;
    bytes[size++] =
        (call ? 0x18 : 0x28) | (base & 7) | ((base & 7) == 5 ? 0x40 : 0);
    if ((base & 7) == 4)
      bytes[size++] = 0x24;
    if ((base & 7) == 5)
      bytes[size++] = 0;
  }
  return size;
}

static void callee(uint8_t *ram, unsigned mode, unsigned return_bits,
                   unsigned result_bits) {
  uint8_t *p = ram + DESTINATION;
  // Destroy every integer caller-saved register. Returning wrappers must retain
  // values live across the far call and must not use the red zone as a leaf.
  const unsigned regs64[] = {1, 2, 6, 7, 8, 9, 10, 11};
  const unsigned regs32[] = {1, 2};
  const unsigned *regs = mode == 64 ? regs64 : regs32;
  for (unsigned i = 0; i < (mode == 64 ? 8u : 2u); ++i) {
    if (regs[i] >= 8)
      *p++ = 0x41;
    *p++ = 0xb8 + (regs[i] & 7);
    const uint32_t poison = 0xc1a0bb00 + i;
    memcpy(p, &poison, 4);
    p += 4;
  }
  // All Linux SysV vector registers are caller-saved.
  for (unsigned i = 0; i < (mode == 64 ? 16u : 8u); ++i) {
    *p++ = 0x66;
    if (i >= 8)
      *p++ = 0x45;
    *p++ = 0x0f;
    *p++ = 0xef;
    *p++ = 0xc0 | ((i & 7) << 3) | (i & 7);
  }
  // A memory mutation distinguishes loads preserved across the call from
  // incorrectly delayed or CSE'd loads after a supposedly pure operation.
  *p++ = 0xc7;
  *p++ = 0x04;
  *p++ = 0x25;
  const uint32_t address = VALUES, poison = 0xdeadbeef;
  memcpy(p, &address, 4);
  p += 4;
  memcpy(p, &poison, 4);
  p += 4;
  if (result_bits == 64)
    *p++ = 0x48;
  *p++ = 0xb8;
  const uint64_t result = result_bits == 64   ? UINT64_C(0x1234567889123456)
                          : result_bits == 32 ? 0x89123456
                                              : 0x3456;
  memcpy(p, &result, result_bits == 64 ? 8 : 4);
  p += result_bits == 64 ? 8 : 4;
  if (return_bits == 16)
    *p++ = 0x66;
  else if (return_bits == 64)
    *p++ = 0x48;
  *p++ = 0xcb;
}

int main(int argc, char **argv) {
  CHECK(argc == 4);
  const unsigned mode = strtoul(argv[3], NULL, 10);
  CHECK(mode == 16 || mode == 32 || mode == 64);
  const unsigned word = mode == 64 ? 8 : 4;
  const int kvm = open("/dev/kvm", O_RDWR | O_CLOEXEC);
  if (kvm < 0)
    return 77;
  CHECK(ioctl(kvm, KVM_GET_API_VERSION, 0) == KVM_API_VERSION);
  const int vm = ioctl(kvm, KVM_CREATE_VM, 0);
  CHECK(vm >= 0);
  const int xsave_size = ioctl(vm, KVM_CHECK_EXTENSION, KVM_CAP_XSAVE2);
  CHECK(xsave_size >= 0);
  CHECK(ioctl(vm, KVM_CHECK_EXTENSION, KVM_CAP_XSAVE) > 0);
  void *boundary_xsave =
      calloc(1, xsave_size ? (size_t)xsave_size : sizeof(struct kvm_xsave));
  CHECK(boundary_xsave);
  uint8_t *ram = mmap(NULL, RAM_SIZE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  CHECK(ram != MAP_FAILED);
  struct kvm_userspace_memory_region region = {
      .memory_size = RAM_SIZE, .userspace_addr = (uintptr_t)ram};
  CHECK(ioctl(vm, KVM_SET_USER_MEMORY_REGION, &region) == 0);
  ((uint64_t *)(ram + 0x2000))[0] = 0x3027;
  ((uint64_t *)(ram + 0x3000))[0] = 0x4027;
  ((uint64_t *)(ram + 0x4000))[0] = 0xe7;
  if (mode != 64) {
    memset(ram + 0x2000, 0, 4096);
    ((uint32_t *)(ram + 0x2000))[0] = 0xe7;
  }
  struct kvm_cpuid2 *cpuid =
      calloc(1, sizeof(*cpuid) + 256 * sizeof(struct kvm_cpuid_entry2));
  CHECK(cpuid);
  cpuid->nent = 256;
  CHECK(ioctl(kvm, KVM_GET_SUPPORTED_CPUID, cpuid) == 0);
  const int run_size = ioctl(kvm, KVM_GET_VCPU_MMAP_SIZE, 0);
  CHECK(run_size > 0);
  struct cpu candidate = create_cpu(vm, 0, run_size, cpuid, mode != 64);
  struct cpu oracle = create_cpu(vm, 1, run_size, cpuid, mode != 64);
  if (mode == 16)
    candidate.initial.cs.db = oracle.initial.cs.db = 0;
  candidate.initial.cr4 |= 0x600;
  oracle.initial.cr4 |= 0x600;
  FILE *image = fopen(argv[1], "rb");
  CHECK(image);
  const size_t bytes = fread(ram + IMAGE, 1, 0x6000, image);
  CHECK(bytes && bytes < 0x6000 && !ferror(image));
  CHECK(fclose(image) == 0);
  FILE *manifest = fopen(argv[2], "r");
  CHECK(manifest);
  char name[96];
  unsigned bits, call, immediate, base, length, address_bits, functions = 0,
                                                              returned = 0;
  unsigned long long entry, instruction;
  uint8_t *before = malloc(RAM_SIZE), *after = malloc(RAM_SIZE);
  CHECK(before && after);
  while (fscanf(manifest, "%95s %llx %llx %u %u %u %u %u %u", name, &entry,
                &instruction, &bits, &call, &immediate, &base, &length,
                &address_bits) == 9) {
    current_function = name;
    CHECK(entry >= IMAGE && instruction >= entry &&
          instruction + length <= IMAGE + bytes);
    CHECK(bits == 16 || bits == 32 || (mode == 64 && bits == 64));
    uint8_t original[16];
    CHECK(native_bytes(original, mode, bits, call, immediate, base,
                       address_bits) == length);
    CHECK(memcmp(ram + instruction, original, length) == 0);
    ++functions;
    for (unsigned gate = 0; gate < (mode != 64 && !immediate ? 3u : 1u); ++gate)
      for (unsigned cpl = 0; cpl <= 3; cpl += 3)
        for (unsigned size = 1; size <= 257; size += 128) {
          // Gate parameter count is zero: the source ABI passes no arguments.
          // Both 16/32-bit gates are tested independently of instruction width.
          const unsigned gate_bits = gate == 1 ? 16 : 32;
          if (gate && !call && cpl)
            continue; // JMP cannot change CPL inward.
          descriptor(ram, 8, mode, cpl);
          descriptor(ram, 16, 0, cpl);
          descriptor(ram, 48, mode == 64 ? 64 : 32, gate ? 0 : cpl);
          descriptor(ram, 56, 0, 0);
          const uint64_t gate_value =
              UINT64_C(0x0000e40000300000) |
              (gate_bits == 32 ? UINT64_C(0x80000000000) : 0) | DESTINATION;
          memcpy(ram + 0x5050, &gate_value, 8);
          const uint64_t tss_desc =
              UINT64_C(0x00008b0000000067) | ((uint64_t)TSS << 16);
          memcpy(ram + 0x5060, &tss_desc, 8);
          const uint32_t inner_sp = NEW_STACK;
          const uint16_t inner_ss = 56;
          memcpy(ram + TSS + 4, &inner_sp, 4);
          memcpy(ram + TSS + 8, &inner_ss, 2);
          reset_cpu(&candidate, 8);
          reset_cpu(&oracle, 8);
          struct kvm_sregs special = candidate.initial;
          special.cs.selector = 8 | cpl;
          special.cs.dpl = cpl;
          special.ss.selector = special.ds.selector = special.es.selector =
              special.fs.selector = special.gs.selector = 16 | cpl;
          special.ss.dpl = special.ds.dpl = special.es.dpl = special.fs.dpl =
              special.gs.dpl = cpl;
          special.tr = (struct kvm_segment){.base = TSS,
                                            .limit = 103,
                                            .selector = 96,
                                            .type = 11,
                                            .present = 1};
          CHECK(ioctl(candidate.fd, KVM_SET_SREGS, &special) == 0);
          struct kvm_vcpu_events events = {0};
          CHECK(ioctl(candidate.fd, KVM_SET_VCPU_EVENTS, &events) == 0);
          const uint64_t target = DESTINATION;
          const uint16_t selector = gate ? 80 : 48 | cpl;
          memcpy(ram + POINTER, &target, bits / 8);
          memcpy(ram + POINTER + bits / 8, &selector, 2);
          for (unsigned i = 0; i < 8; ++i) {
            const uint64_t value = 0x100 + i * 17;
            memcpy(ram + VALUES + i * (word), &value, word);
          }
          memcpy(ram + VALUES + 128, &(double){3.5}, 8);
          memset(ram + 0x6400, 0x5a, 0x1c00);
          const uint64_t stack = mode == 64 ? 0x7c08 : 0x7c0c;
          memcpy(ram + stack, &(uint64_t){DONE}, word);
          struct kvm_regs initial = {.rip = entry,
                                     .rsp = stack,
                                     .rflags = 2,
                                     .rbx = 0x11223344,
                                     .rbp = 0x55667788,
                                     .r12 = 0x12345678,
                                     .r13 = 0x23456789,
                                     .r14 = 0x3456789a,
                                     .r15 = 0x456789ab,
                                     .rdi = POINTER,
                                     .rsi = call ? VALUES : NEW_STACK,
                                     .rdx = size};
          if (mode != 64) {
            uint32_t *args = (uint32_t *)(ram + stack + 4);
            args[0] = POINTER;
            args[1] = call ? VALUES : NEW_STACK;
            args[2] = size;
            initial.rsi = 0x33445566;
            initial.rdi = 0x778899aa;
          }
          CHECK(ioctl(candidate.fd, KVM_SET_REGS, &initial) == 0);
          struct kvm_regs boundary = run_to(&candidate, instruction, 0);
          if (!immediate) {
            const uint64_t pointer = address_bits == 32
                                         ? (uint32_t)reg_value(boundary, base)
                                         : reg_value(boundary, base);
            if (strstr(name, "_local")) {
              CHECK(base != 4 && pointer >= 0x6400 &&
                    pointer + bits / 8 + 2 < 0x8000);
              CHECK(memcmp(ram + pointer, ram + POINTER, bits / 8 + 2) == 0);
            } else
              CHECK(pointer == POINTER);
          }
          if (!call)
            CHECK(boundary.rsp == NEW_STACK);
          CHECK(ioctl(candidate.fd, KVM_GET_SREGS, &special) == 0);
          CHECK(ioctl(candidate.fd, KVM_GET_VCPU_EVENTS, &events) == 0);
          struct kvm_fpu boundary_fpu = {0};
          CHECK(ioctl(candidate.fd, KVM_GET_FPU, &boundary_fpu) == 0);
          CHECK(ioctl(candidate.fd, xsave_size ? KVM_GET_XSAVE2 : KVM_GET_XSAVE,
                      boundary_xsave) == 0);
          memcpy(before, ram, RAM_SIZE);
          struct kvm_regs actual = run_to(&candidate, DESTINATION, 0);
          struct kvm_sregs actual_s, expected_s;
          CHECK(ioctl(candidate.fd, KVM_GET_SREGS, &actual_s) == 0);
          struct kvm_fpu actual_fpu = {0}, expected_fpu = {0};
          struct kvm_vcpu_events actual_events = {0}, expected_events = {0};
          CHECK(ioctl(candidate.fd, KVM_GET_FPU, &actual_fpu) == 0);
          CHECK(ioctl(candidate.fd, KVM_GET_VCPU_EVENTS, &actual_events) == 0);
          memcpy(after, ram, RAM_SIZE);
          memcpy(ram, before, RAM_SIZE);
          // Execute independently encoded bytes at the same PC: a far CALL's
          // pushed return IP is itself an architectural output.
          memcpy(ram + instruction, original, length);
          CHECK(ioctl(oracle.fd, KVM_SET_SREGS, &special) == 0);
          CHECK(ioctl(oracle.fd, KVM_SET_REGS, &boundary) == 0);
          CHECK(ioctl(oracle.fd, KVM_SET_VCPU_EVENTS, &events) == 0);
          // Transport the component-presence bitmap as well as legacy values.
          // A fresh oracle vCPU must materialize the candidate's live XMM bank.
          CHECK(ioctl(oracle.fd, KVM_SET_XSAVE, boundary_xsave) == 0);
          struct kvm_regs expected = run_to(&oracle, DESTINATION, 0);
          CHECK(ioctl(oracle.fd, KVM_GET_FPU, &expected_fpu) == 0);
          CHECK(ioctl(oracle.fd, KVM_GET_VCPU_EVENTS, &expected_events) == 0);
          if (memcmp(&actual_fpu, &expected_fpu, sizeof(actual_fpu))) {
            const uint8_t *a = (const uint8_t *)&actual_fpu;
            const uint8_t *b = (const uint8_t *)&expected_fpu;
            const uint8_t *input = (const uint8_t *)&boundary_fpu;
            for (unsigned i = 0; i < sizeof(actual_fpu); ++i)
              if (a[i] != b[i])
                fprintf(
                    stderr,
                    "FPU byte %u: boundary=%02x actual=%02x expected=%02x\n", i,
                    input[i], a[i], b[i]);
          }
          CHECK(memcmp(&actual_fpu, &expected_fpu, sizeof(actual_fpu)) == 0);
          CHECK(memcmp(&actual_events, &expected_events,
                       sizeof(actual_events)) == 0);
          CHECK(ioctl(oracle.fd, KVM_GET_SREGS, &expected_s) == 0);
          CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
          CHECK(memcmp(&actual_s, &expected_s, sizeof(actual_s)) == 0);
          CHECK(memcmp(after, ram, RAM_SIZE) == 0);
          CHECK(actual_s.cs.selector == (48u | (gate ? 0u : cpl)));
          ++comparisons;
          if (call) {
            callee(ram, mode, gate ? gate_bits : bits, bits);
            const struct kvm_regs done = run_to(&candidate, DONE, 0);
            const uint64_t expected_result =
                (bits == 64   ? UINT64_C(0x1234567889123456)
                 : bits == 32 ? UINT64_C(0x89123456)
                              : 0x3456) +
                8 * 0x100 + 17 * 28 + size + (mode == 64 ? 7 : 0);
            CHECK((mode == 64 ? done.rax : (uint32_t)done.rax) ==
                  (mode == 64 ? expected_result : (uint32_t)expected_result));
            CHECK(done.rsp == stack + word);
            CHECK(done.rbx == initial.rbx && done.rbp == initial.rbp);
            if (mode != 64)
              CHECK(done.rsi == initial.rsi && done.rdi == initial.rdi);
            else
              CHECK(done.r12 == initial.r12 && done.r13 == initial.r13 &&
                    done.r14 == initial.r14 && done.r15 == initial.r15);
            CHECK(*(uint32_t *)(ram + VALUES) == 0xdeadbeef);
            ++returned;
          }
        }
  }
  CHECK(feof(manifest) && functions > 0);
  CHECK(fclose(manifest) == 0);
  printf("{\"functions\":%u,\"complete_state_pairs\":%llu,\"returned_abi_"
         "checks\":%u}\n",
         functions, comparisons, returned);
  return 0;
}
