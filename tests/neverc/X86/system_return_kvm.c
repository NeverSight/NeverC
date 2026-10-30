// Native system-return qualification. Run compiler output and independent
// original bytes on separate KVM vCPUs; never transfer privilege on the host.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <linux/kvm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>

enum {
  RAM_SIZE = 2 * 1024 * 1024,
  CODE_BASE = 0x10000,
  ORACLE_PC = 0x1000,
  TARGET_PC = 0x1800,
  MARKER = 0x8000,
  STACK_TOP = 0x1f0008
};
static const char *current_function;
static unsigned long long comparisons;
#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!(expression)) {                                                       \
      fprintf(stderr, "%s case %llu, line %d: %s (errno %d)\n",                \
              current_function ? current_function : "setup", comparisons,      \
              __LINE__, #expression, errno);                                   \
      exit(1);                                                                 \
    }                                                                          \
  } while (0)

struct cpu {
  int fd;
  struct kvm_run *run;
  struct kvm_sregs initial;
};

static void msr(int cpu, unsigned index, uint64_t value) {
  struct kvm_msrs *request =
      calloc(1, sizeof(*request) + sizeof(struct kvm_msr_entry));
  CHECK(request);
  request->nmsrs = 1;
  request->entries[0].index = index;
  request->entries[0].data = value;
  CHECK(ioctl(cpu, KVM_SET_MSRS, request) == 1);
  free(request);
}

static void breakpoint(struct cpu *cpu, uint64_t pc) {
  struct kvm_guest_debug debug = {.control = KVM_GUESTDBG_ENABLE |
                                             KVM_GUESTDBG_USE_HW_BP};
  debug.arch.debugreg[0] = pc;
  debug.arch.debugreg[1] = pc + 1;
  debug.arch.debugreg[7] = 0x405;
  CHECK(ioctl(cpu->fd, KVM_SET_GUEST_DEBUG, &debug) == 0);
}

static struct kvm_regs run_to(struct cpu *cpu, uint64_t pc, int resume_flag) {
  breakpoint(cpu, pc);
  CHECK(ioctl(cpu->fd, KVM_RUN, 0) == 0);
  struct kvm_regs registers;
  CHECK(ioctl(cpu->fd, KVM_GET_REGS, &registers) == 0);
  if (cpu->run->exit_reason != KVM_EXIT_DEBUG ||
      (registers.rip != pc && !(resume_flag && registers.rip == pc + 1))) {
    fprintf(stderr, "exit %u, pc %#llx, expected %#llx, flags %#llx\n",
            cpu->run->exit_reason, registers.rip, (unsigned long long)pc,
            registers.rflags);
    CHECK(0);
  }
  // RF suppresses the first execution breakpoint. A single NOP preserves all
  // other represented state, so reconstruct the immediate post-transfer RF.
  if (registers.rip == pc + 1) {
    registers.rip = pc;
    registers.rflags |= 0x10000;
  }
  return registers;
}

static struct kvm_segment segment(unsigned selector, int code) {
  return (struct kvm_segment){.limit = UINT32_MAX,
                              .selector = selector,
                              .type = code ? 11 : 3,
                              .present = 1,
                              .db = !code,
                              .s = 1,
                              .l = code,
                              .g = 1};
}

static struct cpu create_cpu(int vm, int id, int run_size,
                             struct kvm_cpuid2 *cpuid, int protected_mode) {
  struct cpu result = {.fd = ioctl(vm, KVM_CREATE_VCPU, id)};
  CHECK(result.fd >= 0);
  CHECK(ioctl(result.fd, KVM_SET_CPUID2, cpuid) == 0);
  result.run =
      mmap(NULL, run_size, PROT_READ | PROT_WRITE, MAP_SHARED, result.fd, 0);
  CHECK(result.run != MAP_FAILED);
  CHECK(ioctl(result.fd, KVM_GET_SREGS, &result.initial) == 0);
  result.initial.cr0 = 0x80000011;
  result.initial.cr3 = 0x2000;
  result.initial.cr4 = protected_mode ? 0x10 : 0x20;
  result.initial.efer = protected_mode ? 0 : 0x501;
  result.initial.cs = segment(8, 1);
  result.initial.cs.l = !protected_mode;
  result.initial.cs.db = protected_mode;
  result.initial.ss = result.initial.ds = result.initial.es = segment(16, 0);
  result.initial.fs = result.initial.gs = segment(16, 0);
  result.initial.gdt.base = 0x5000;
  result.initial.gdt.limit = 0xffff;
  result.initial.ldt.unusable = 1;
  CHECK(ioctl(result.fd, KVM_SET_SREGS, &result.initial) == 0);
  CHECK(ioctl(result.fd, KVM_GET_SREGS, &result.initial) == 0);
  return result;
}

static void reset_cpu(struct cpu *cpu, unsigned selector) {
  CHECK(ioctl(cpu->fd, KVM_SET_SREGS, &cpu->initial) == 0);
  msr(cpu->fd, 0xc0000081, (uint64_t)selector << 48);
  msr(cpu->fd, 0x174, selector);
  struct kvm_vcpu_events events = {0};
  CHECK(ioctl(cpu->fd, KVM_GET_VCPU_EVENTS, &events) == 0);
  CHECK(!events.interrupt.shadow && !events.nmi.masked &&
        !events.exception.pending && !events.exception.injected);
}

static void check_segments(struct cpu *cpu, struct kvm_sregs *out,
                           unsigned selector, int sysret, int wide) {
  CHECK(ioctl(cpu->fd, KVM_GET_SREGS, out) == 0);
  struct kvm_sregs expected = cpu->initial;
  unsigned cs = (selector + (sysret ? (wide ? 16 : 0) : (wide ? 32 : 16))) | 3;
  unsigned ss = sysret ? ((selector + 8) | 3) : (cs + 8);
  expected.cs = segment((uint16_t)cs, 1);
  expected.cs.dpl = 3;
  expected.cs.l = wide;
  expected.cs.db = !wide;
  expected.ss = segment((uint16_t)ss, 0);
  expected.ss.dpl = 3;
  CHECK(memcmp(&expected, out, sizeof(expected)) == 0);
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
  // Identity map plus canonical 4-GiB and negative aliases of the same RAM.
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
  uint8_t *before_memory = malloc(RAM_SIZE);
  CHECK(before_memory);
  FILE *manifest = fopen(argv[2], "r");
  CHECK(manifest);
  char name[96];
  unsigned kind;
  unsigned long long entry, instruction;
  const unsigned selectors[] = {8, 16, 0x28, 0xffc0, 0xffe0};
  const uint64_t stacks[] = {0, 0x90007, 0x00007fffffff0001ULL,
                             0xffffffff80010000ULL};
  const unsigned sizes[] = {0, 1, 31, 256};
  uint64_t flags[68] = {2, 0x3c7fd7, UINT64_MAX, 0};
  for (unsigned bit = 0; bit < 64; ++bit)
    flags[bit + 4] = 2 | (UINT64_C(1) << bit);
  unsigned functions = 0;
  while (fscanf(manifest, "%95s %llx %llx %u", name, &entry, &instruction,
                &kind) == 4) {
    current_function = name;
    CHECK(kind < 4 && entry >= CODE_BASE && instruction >= entry &&
          instruction < CODE_BASE + bytes);
    CHECK(!protected_mode || kind == 2);
    ++functions;
    const int sysret = kind < 2, wide = kind & 1;
    const uint8_t native[] = {0x48, 0x0f, sysret ? 0x07 : 0x35};
    CHECK(memcmp(ram + instruction, native + !wide, 2 + wide) == 0);
    memcpy(ram + ORACLE_PC, native + !wide, 2 + wide);
    for (unsigned si = 0; si < sizeof(selectors) / sizeof(*selectors); ++si)
      for (unsigned fi = 0; fi < sizeof(flags) / sizeof(*flags); ++fi)
        for (unsigned pi = 0; pi < sizeof(stacks) / sizeof(*stacks); ++pi)
          for (unsigned zi = 0; zi < sizeof(sizes) / sizeof(*sizes); ++zi) {
            const uint64_t target =
                TARGET_PC + (wide ? (zi == 1   ? UINT64_C(0x100000000)
                                     : zi == 2 ? UINT64_C(0xffff800000000000)
                                               : 0)
                                  : 0);
            const uint64_t pc_arg =
                wide ? target : target | ((uint64_t)(zi * 0x3456789aU) << 32);
            reset_cpu(&candidate, selectors[si]);
            reset_cpu(&oracle, selectors[si]);
            memset(ram + 0x180000, 0x5a, 0x80000);
            *(uint32_t *)(ram + MARKER) = 0;
            struct kvm_regs initial = {.rax = 0x1111222233334444ULL,
                                       .rbx = 0x5555666677778888ULL,
                                       .rcx = sizes[zi],
                                       .rdx = stacks[pi],
                                       .rsi = flags[fi],
                                       .rdi = pc_arg,
                                       .rsp = STACK_TOP,
                                       .rbp = 0x123456789abcdef0ULL,
                                       .r8 = MARKER,
                                       .r9 = 0xdeadbeef12345678ULL,
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
              arguments[1] = pc_arg;
              arguments[2] = pc_arg >> 32;
              arguments[3] = flags[fi];
              arguments[4] = flags[fi] >> 32;
              arguments[5] = stacks[pi];
              arguments[6] = stacks[pi] >> 32;
              arguments[7] = sizes[zi];
              arguments[8] = 0;
              arguments[9] = MARKER;
            }
            CHECK(ioctl(candidate.fd, KVM_SET_REGS, &initial) == 0);
            struct kvm_regs boundary = run_to(&candidate, instruction, 0);
            CHECK(*(uint32_t *)(ram + MARKER) == 17);
            const uint64_t boundary_pc = sysret ? boundary.rcx : boundary.rdx;
            CHECK((wide ? boundary_pc : (uint32_t)boundary_pc) == target);
            if (sysret) {
              CHECK(boundary.rsp == stacks[pi]);
              CHECK((wide ? boundary.r11 : (uint32_t)boundary.r11) ==
                    (wide ? flags[fi] : (uint32_t)flags[fi]));
            } else {
              CHECK((wide ? boundary.rcx : (uint32_t)boundary.rcx) ==
                    (wide ? stacks[pi] : (uint32_t)stacks[pi]));
            }
            memcpy(before_memory, ram, RAM_SIZE);
            struct kvm_regs actual = run_to(
                &candidate, target, !sysret && (boundary.rflags & 0x10000));
            CHECK(memcmp(before_memory, ram, RAM_SIZE) == 0);
            struct kvm_sregs actual_special, oracle_special;
            check_segments(&candidate, &actual_special, selectors[si], sysret,
                           wide);
            struct kvm_regs original = boundary;
            original.rip = ORACLE_PC;
            CHECK(ioctl(oracle.fd, KVM_SET_REGS, &original) == 0);
            struct kvm_regs expected =
                run_to(&oracle, target, !sysret && (boundary.rflags & 0x10000));
            CHECK(memcmp(before_memory, ram, RAM_SIZE) == 0);
            check_segments(&oracle, &oracle_special, selectors[si], sysret,
                           wide);
            CHECK(memcmp(&actual, &expected, sizeof(actual)) == 0);
            CHECK(actual.rip == target);
            CHECK((wide ? actual.rsp : (uint32_t)actual.rsp) ==
                  (wide ? stacks[pi] : (uint32_t)stacks[pi]));
            CHECK(actual.rflags ==
                  (sysret ? ((flags[fi] & 0x3c7fd7) | 2) : boundary.rflags));
            ++comparisons;
          }
  }
  CHECK(feof(manifest) && functions == (protected_mode ? 4 : 16));
  CHECK(fclose(manifest) == 0);
  printf("{\"functions\":%u,\"complete_state_pairs\":%llu,"
         "\"mapped_bytes_per_observation\":%u}\n",
         functions, comparisons, RAM_SIZE);
  return 0;
}
