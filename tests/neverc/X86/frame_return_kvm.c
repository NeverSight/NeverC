// Compare compiled frame transfers with independently encoded original bytes.
#include "KvmTransferFixture.h"

static void descriptor(uint8_t *ram, unsigned selector, int bits, int cpl) {
  uint64_t value = UINT64_C(0x000090000000ffff) | ((uint64_t)cpl << 45);
  if (bits) {
    value |= UINT64_C(0x00000b0000000000);
    if (bits == 64)
      value |= UINT64_C(0x00a0000000000000);
    else if (bits == 32)
      value |= UINT64_C(0x00c0000000000000);
  } else {
    value |= UINT64_C(0x00c0030000000000);
  }
  if (bits != 16)
    value |= UINT64_C(0x000f000000000000);
  memcpy(ram + 0x5000 + (selector & ~7u), &value, sizeof(value));
}

static void word(uint8_t *ram, uint64_t frame, unsigned offset, unsigned bytes,
                 uint64_t value) {
  CHECK(bytes == 2 || bytes == 4 || bytes == 8);
  const unsigned physical = (frame + offset) & (RAM_SIZE - 1);
  CHECK(physical + bytes <= RAM_SIZE);
  memcpy(ram + physical, &value, bytes);
}

static void compare_transfer(struct cpu *candidate, struct cpu *oracle,
                             uint8_t *ram, uint8_t *before, uint8_t *after,
                             struct kvm_regs boundary, uint64_t target,
                             unsigned cpl, unsigned bits, int may_resume) {
  struct kvm_sregs boundary_special, actual_special, expected_special;
  CHECK(ioctl(candidate->fd, KVM_GET_SREGS, &boundary_special) == 0);
  struct kvm_vcpu_events events = {0};
  CHECK(ioctl(candidate->fd, KVM_GET_VCPU_EVENTS, &events) == 0);
  CHECK(ioctl(oracle->fd, KVM_SET_VCPU_EVENTS, &events) == 0);
  CHECK(ioctl(oracle->fd, KVM_SET_SREGS, &boundary_special) == 0);
  memcpy(before, ram, RAM_SIZE);
  struct kvm_regs actual = run_to(candidate, target, may_resume);
  CHECK(ioctl(candidate->fd, KVM_GET_SREGS, &actual_special) == 0);
  memcpy(after, ram, RAM_SIZE);
  memcpy(ram, before, RAM_SIZE);
  boundary.rip = ORACLE_PC;
  CHECK(ioctl(oracle->fd, KVM_SET_REGS, &boundary) == 0);
  struct kvm_regs expected = run_to(oracle, target, may_resume);
  CHECK(ioctl(oracle->fd, KVM_GET_SREGS, &expected_special) == 0);
  CHECK(memcmp(after, ram, RAM_SIZE) == 0);
  CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
  CHECK(memcmp(&actual_special, &expected_special, sizeof(actual_special)) ==
        0);
  CHECK(actual.rip == target);
  CHECK(actual_special.cs.dpl == cpl && actual_special.cs.l == (bits == 64) &&
        actual_special.cs.db == (bits == 32));
  struct kvm_vcpu_events actual_events = {0}, expected_events = {0};
  CHECK(ioctl(candidate->fd, KVM_GET_VCPU_EVENTS, &actual_events) == 0);
  CHECK(ioctl(oracle->fd, KVM_GET_VCPU_EVENTS, &expected_events) == 0);
  CHECK(memcmp(&actual_events, &expected_events, sizeof(actual_events)) == 0);
  ++comparisons;
}

int main(int argc, char **argv) {
  CHECK(argc == 4 && (!strcmp(argv[3], "32") || !strcmp(argv[3], "64")));
  const int protected_mode = !strcmp(argv[3], "32");
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
  ((uint64_t *)(ram + 0x2000))[0] = 0x3027;
  ((uint64_t *)(ram + 0x2000))[256] = 0x3027;
  ((uint64_t *)(ram + 0x3000))[0] = 0x4027;
  ((uint64_t *)(ram + 0x3000))[4] = 0x4027;
  ((uint64_t *)(ram + 0x4000))[0] = 0xe7;
  if (protected_mode) {
    memset(ram + 0x2000, 0, 0x1000);
    ((uint32_t *)(ram + 0x2000))[0] = 0xe7;
  }
  ram[TARGET_PC] = ram[TARGET_PC + 1] = 0x90;
  descriptor(ram, 8, protected_mode ? 32 : 64, 0);
  descriptor(ram, 16, 0, 0);
  descriptor(ram, 24, protected_mode ? 32 : 64, 3);
  descriptor(ram, 32, 0, 3);
  descriptor(ram, 40, 16, 0);
  descriptor(ram, 48, 32, 0);
  descriptor(ram, 56, 16, 3);
  descriptor(ram, 64, 32, 3);
  FILE *image = fopen(argv[1], "rb");
  CHECK(image);
  size_t bytes = fread(ram + CODE_BASE, 1, 0x80000, image);
  CHECK(bytes && bytes < 0x80000 && !ferror(image));
  CHECK(fclose(image) == 0);
  struct kvm_cpuid2 *cpuid =
      calloc(1, sizeof(*cpuid) + 256 * sizeof(struct kvm_cpuid_entry2));
  CHECK(cpuid);
  cpuid->nent = 256;
  CHECK(ioctl(kvm, KVM_GET_SUPPORTED_CPUID, cpuid) == 0);
  int run_size = ioctl(kvm, KVM_GET_VCPU_MMAP_SIZE, 0);
  CHECK(run_size > 0);
  struct cpu candidate = create_cpu(vm, 0, run_size, cpuid, protected_mode);
  struct cpu oracle = create_cpu(vm, 1, run_size, cpuid, protected_mode);
  uint8_t *before = malloc(RAM_SIZE), *after = malloc(RAM_SIZE);
  CHECK(before && after);
  FILE *manifest = fopen(argv[2], "r");
  CHECK(manifest);
  char name[96];
  unsigned kind, width, adjustment;
  unsigned long long entry, instruction;
  const unsigned sizes[] = {0, 1, 31, 256};
  const uint64_t flags[] = {2, 0x8d7, 0x602, 0x240002, 0x3002, 0x10002};
  unsigned functions = 0;
  while (fscanf(manifest, "%95s %llx %llx %u %u %u", name, &entry, &instruction,
                &kind, &width, &adjustment) == 6) {
    current_function = name;
    CHECK(kind <= 3 && entry >= CODE_BASE && instruction >= entry &&
          instruction < CODE_BASE + bytes);
    CHECK(width == 16 || width == 32 || (!protected_mode && width == 64));
    ++functions;
    uint8_t original[4];
    unsigned length = 0;
    if (kind == 3) {
      original[length++] = 0x0f;
      original[length++] = 0x34;
    } else {
      if (width == 16)
        original[length++] = 0x66;
      else if (width == 64)
        original[length++] = 0x48;
      original[length++] = kind == 0 ? 0xcf : kind == 1 ? 0xcb : 0xca;
      if (kind == 2) {
        original[length++] = adjustment;
        original[length++] = adjustment >> 8;
      }
    }
    CHECK(memcmp(ram + instruction, original, length) == 0);
    memcpy(ram + ORACLE_PC, original, length);
    for (unsigned cpl = 0; cpl <= 3; cpl += 3)
      for (unsigned fi = 0; fi < sizeof(flags) / sizeof(*flags); ++fi)
        for (unsigned pi = 0; pi < 4; ++pi)
          for (unsigned zi = 0; zi < 4; ++zi) {
            reset_cpu(&candidate, 8);
            reset_cpu(&oracle, 8);
            memset(ram + 0x180000, 0x5a, 0x80000);
            memset(ram + 0x90000, 0x96, 0x20000);
            *(uint32_t *)(ram + MARKER) = 0;
            const uint64_t alias = protected_mode ? 0
                                   : pi == 2      ? UINT64_C(0x100000000)
                                   : pi == 3      ? UINT64_C(0xffff800000000000)
                                                  : 0;
            const uint64_t frame = 0x90000 + (pi & 1) * 7 + alias;
            const uint64_t target = TARGET_PC + (width == 64 ? alias : 0);
            const uint64_t stack =
                width == 16 ? 0x7000 + zi
                            : 0x170000 + zi + (width == 64 ? alias : 0);
            unsigned cs = (width == 16   ? (cpl ? 56 : 40)
                           : width == 32 ? (cpl ? 64 : 48)
                                         : (cpl ? 24 : 8)) |
                          cpl;
            unsigned ss = (cpl ? 32 : 16) | cpl;
            const unsigned unit = width / 8;
            word(ram, frame, 0, unit, target);
            word(ram, frame, unit, unit, cs);
            unsigned offset = 2 * unit;
            if (kind == 0) {
              word(ram, frame, offset, unit, flags[fi]);
              offset += unit;
            } else if (kind == 2) {
              offset += adjustment;
            }
            word(ram, frame, offset, unit, stack);
            word(ram, frame, offset + unit, unit, ss);
            struct kvm_regs initial = {.rax = 0x1111222233334444ULL,
                                       .rbx = 0x5555666677778888ULL,
                                       .rcx = 0x123456789abcdef0ULL,
                                       .rdx = MARKER,
                                       .rsi = sizes[zi],
                                       .rdi = frame,
                                       .rsp = STACK_TOP,
                                       .rbp = 0x123456789abcdef0ULL,
                                       .r8 = 0xdeadbeef12345678ULL,
                                       .r9 = 0xabcdef9876543210ULL,
                                       .r10 = 0xabcdef9876543210ULL,
                                       .r11 = 0x76543210abcdef98ULL,
                                       .r12 = 0x12345678fedcba98ULL,
                                       .r13 = 0x87654321abcdef12ULL,
                                       .r14 = 0x55555555aaaaaaaaULL,
                                       .r15 = 0x9999999966666666ULL,
                                       .rip = entry,
                                       .rflags = 2 | (flags[fi] & 0x243ed5)};
            if (protected_mode) {
              initial.rsp += 4;
              uint32_t *arguments = (uint32_t *)(ram + initial.rsp);
              arguments[0] = 0xdddddddd;
              arguments[1] = frame;
              arguments[2] = sizes[zi];
              arguments[3] = 0;
              arguments[4] = MARKER;
            }
            if (kind == 3) {
              for (unsigned ci = 0; ci < 2; ++ci) {
                struct cpu *cpu = ci ? &oracle : &candidate;
                msr(cpu->fd, 0x175, stack);
                msr(cpu->fd, 0x176, target);
              }
              struct kvm_sregs source = candidate.initial;
              source.cs.selector = (cpl ? 24 : 8) | cpl;
              source.cs.dpl = cpl;
              source.ss.selector = (cpl ? 32 : 16) | cpl;
              source.ss.dpl = cpl;
              CHECK(ioctl(candidate.fd, KVM_SET_SREGS, &source) == 0);
            }
            CHECK(ioctl(candidate.fd, KVM_SET_REGS, &initial) == 0);
            struct kvm_regs boundary = run_to(&candidate, instruction, 0);
            CHECK(*(uint32_t *)(ram + MARKER) == 17);
            if (kind != 3)
              CHECK(boundary.rsp == frame);
            compare_transfer(&candidate, &oracle, ram, before, after, boundary,
                             target, kind == 3 ? 0 : cpl, width,
                             kind == 0   ? !!(flags[fi] & 0x10000)
                             : kind == 3 ? 0
                                         : !!(boundary.rflags & 0x10000));
          }
  }
  CHECK(feof(manifest) && functions == (protected_mode ? 28 : 40));
  CHECK(fclose(manifest) == 0);
  printf("{\"functions\":%u,\"complete_state_pairs\":%llu,"
         "\"mapped_bytes_per_observation\":%u}\n",
         functions, comparisons, RAM_SIZE);
  return 0;
}
