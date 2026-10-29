// Native Linux x64 comparison with independently encoded far-pointer loads.
// The invocation window restores TLS and SS before returning to host C code.
#define _GNU_SOURCE
#include <asm/ldt.h>
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/ptrace.h>
#include <sys/syscall.h>
#include <sys/user.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

typedef uint64_t (*operation)(const void *, unsigned *);
struct observation {
  uint64_t value, fsbase, gsbase;
  uint16_t fs, gs, ss, padding;
};
#define DECLARE(name)                                                          \
  extern uint64_t original_##name(const void *, unsigned *);                   \
  extern uint64_t generated_##name(const void *, unsigned *)
DECLARE(lfs16);
DECLARE(lfs32);
DECLARE(lfs64);
DECLARE(lgs16);
DECLARE(lgs32);
DECLARE(lgs64);
DECLARE(lss16);
DECLARE(lss32);
DECLARE(lss64);
#undef DECLARE
extern void invoke(operation, const void *, struct observation *, unsigned *);
extern void trace_invoke(operation, const void *, struct observation *,
                         unsigned *);
// clang-format off
#define ORIGINAL(name, bytes, extend) \
  ".globl original_" #name "\noriginal_" #name ":\n" \
  "movl $17,(%rsi)\n.byte " bytes "\n" extend "movl $23,(%rsi)\nret\n"
__asm__(
    ".text\n"
    ORIGINAL(lfs16, "0x66,0x0f,0xb4,0x07", "movzwl %ax,%eax\n")
    ORIGINAL(lfs32, "0x0f,0xb4,0x07", "")
    ORIGINAL(lfs64, "0x48,0x0f,0xb4,0x07", "")
    ORIGINAL(lgs16, "0x66,0x0f,0xb5,0x07", "movzwl %ax,%eax\n")
    ORIGINAL(lgs32, "0x0f,0xb5,0x07", "")
    ORIGINAL(lgs64, "0x48,0x0f,0xb5,0x07", "")
    ORIGINAL(lss16, "0x66,0x0f,0xb2,0x07", "movzwl %ax,%eax\n")
    ORIGINAL(lss32, "0x0f,0xb2,0x07", "")
    ORIGINAL(lss64, "0x48,0x0f,0xb2,0x07", "")
    ".globl invoke\ninvoke:\n"
    "push %rbx\npush %r12\npush %r13\nsub $32,%rsp\n"
    "mov %rdx,%rbx\nmov %rdi,%r11\nmov %rsi,%rdi\nmov %rcx,%rsi\n"
    "rdfsbase %r12\nrdgsbase %r13\n"
    "mov %fs,16(%rsp)\nmov %gs,18(%rsp)\nmov %ss,20(%rsp)\n"
    "call *%r11\nmov %rax,0(%rbx)\n"
    "rdfsbase %rax\nmov %rax,8(%rbx)\nrdgsbase %rax\nmov %rax,16(%rbx)\n"
    "mov %fs,24(%rbx)\nmov %gs,26(%rbx)\nmov %ss,28(%rbx)\n"
    "mov 16(%rsp),%ax\nmov %ax,%fs\nwrfsbase %r12\n"
    "mov 18(%rsp),%ax\nmov %ax,%gs\nwrgsbase %r13\n"
    "mov 20(%rsp),%ax\nmov %ax,%ss\n"
    "add $32,%rsp\npop %r13\npop %r12\npop %rbx\nret\n");
#undef ORIGINAL
// clang-format on
// Single-step only faulting children. A trap immediately before the far load
// records the real allocated destination and the complete incoming GPR bank.
__asm__(".text\n.globl trace_invoke\ntrace_invoke:\n"
        "pushfq\norq $0x100,(%rsp)\npopfq\njmp invoke\n");

static uint16_t initial_fs, initial_gs, initial_ss;
static uintptr_t initial_fsbase, initial_gsbase;
static uintptr_t fault_begin;
static unsigned fault_kind, fault_trap, fault_error;
static unsigned *fault_marker;
static uintptr_t fault_address;
static greg_t before_gprs[REG_RSP + 1], before_flags;
static unsigned saw_instruction;
static unsigned is_far_load(uintptr_t pc) {
  if (pc < fault_begin || pc >= fault_begin + 192)
    return 0;
  const uint8_t *code = (const uint8_t *)pc;
  unsigned bits = 32;
  if (*code == 0x66) {
    bits = 16;
    ++code;
  }
  if ((*code & 0xf0) == 0x40) {
    if (*code & 8)
      bits = 64;
    ++code;
  }
  return bits == (unsigned[]){16, 32, 64}[fault_kind % 3] && code[0] == 0x0f &&
         code[1] == (uint8_t[]){0xb4, 0xb5, 0xb2}[fault_kind / 3] &&
         code[2] >> 6 != 3;
}
static void on_fault(int number, siginfo_t *info, void *opaque) {
  (void)info;
  const ucontext_t *ctx = opaque;
  const uintptr_t pc = ctx->uc_mcontext.gregs[REG_RIP];
  if (number == SIGTRAP) {
    if (is_far_load(pc)) {
      for (unsigned r = 0; r <= REG_RSP; ++r)
        before_gprs[r] = ctx->uc_mcontext.gregs[r];
      before_flags = ctx->uc_mcontext.gregs[REG_EFL];
      saw_instruction = 1;
    }
    return;
  }
  uintptr_t fsbase, gsbase;
  uint16_t fs, gs, ss;
  __asm__ volatile(
      "rdfsbase %0; rdgsbase %1; mov %%fs,%2; mov %%gs,%3; mov %%ss,%4"
      : "=r"(fsbase), "=r"(gsbase), "=r"(fs), "=r"(gs), "=r"(ss));
  if (!is_far_load(pc) || !saw_instruction || *fault_marker != 17 ||
      ctx->uc_mcontext.gregs[REG_TRAPNO] != fault_trap ||
      (fault_trap != 14 && ctx->uc_mcontext.gregs[REG_ERR] != fault_error) ||
      (fault_trap == 14 && ctx->uc_mcontext.gregs[REG_CR2] != fault_address) ||
      fs != initial_fs || gs != initial_gs || ss != initial_ss ||
      fsbase != initial_fsbase || gsbase != initial_gsbase)
    _exit(90);
  for (unsigned r = 0; r <= REG_RSP; ++r)
    if (before_gprs[r] != ctx->uc_mcontext.gregs[r])
      _exit(91);
  if ((before_flags ^ ctx->uc_mcontext.gregs[REG_EFL]) & 0xcd5)
    _exit(94);
  _exit(0);
}

static void setup_descriptor(unsigned index, unsigned contents,
                             unsigned readonly, unsigned absent) {
  struct user_desc d = {.entry_number = index,
                        .base_addr = 0x12345000,
                        .limit = 0xfffff,
                        .seg_32bit = 1,
                        .contents = contents,
                        .read_exec_only = readonly,
                        .limit_in_pages = 1,
                        .seg_not_present = absent,
                        .useable = 1};
  if (syscall(SYS_modify_ldt, 1, &d, sizeof d)) {
    perror("modify_ldt");
    exit(77);
  }
}

static int fault_case(operation fn, unsigned kind, const void *p, unsigned trap,
                      unsigned error) {
  pid_t child = fork();
  if (child < 0)
    return 1;
  if (!child) {
    unsigned marker = 0;
    fault_begin = (uintptr_t)fn;
    fault_kind = kind;
    fault_trap = trap;
    fault_error = error;
    fault_marker = &marker;
    fault_address = ((uintptr_t)p + (unsigned[]){2, 4, 8}[kind % 3] + 1) &
                    ~((uintptr_t)sysconf(_SC_PAGESIZE) - 1);
    struct sigaction a = {.sa_sigaction = on_fault, .sa_flags = SA_SIGINFO};
    sigemptyset(&a.sa_mask);
    if (sigaction(SIGSEGV, &a, 0) || sigaction(SIGBUS, &a, 0) ||
        sigaction(SIGTRAP, &a, 0))
      _exit(92);
    struct observation out = {0};
    trace_invoke(fn, p, &out, &marker);
    _exit(93);
  }
  int status;
  if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
      WEXITSTATUS(status)) {
    fprintf(stderr, "fault mismatch kind=%u trap=%u error=%u status=%d\n", kind,
            trap, error, status);
    return 1;
  }
  return 0;
}

static void image(uint8_t *p, unsigned bytes, uint64_t value,
                  unsigned selector) {
  memcpy(p, &value, bytes);
  p[bytes] = selector;
  p[bytes + 1] = selector >> 8;
}

static uint64_t gpr(const struct user_regs_struct *r, unsigned index) {
  const uint64_t values[] = {r->rax, r->rcx, r->rdx, r->rbx, r->rsp, r->rbp,
                             r->rsi, r->rdi, r->r8,  r->r9,  r->r10, r->r11,
                             r->r12, r->r13, r->r14, r->r15};
  return values[index];
}

static int debugger_stop(pid_t child, int *live) {
  int status;
  if (waitpid(child, &status, 0) != child)
    return 0;
  *live = WIFSTOPPED(status);
  return *live && WSTOPSIG(status) == SIGTRAP;
}

// Linux signal/interrupt return can clear null selectors 1..3. Stop at the
// instruction's #DB boundary and read the kernel's saved register bank before
// returning to userspace. A signal handler cannot do this: Linux's x64 signal
// frame fills its FS/GS fields with zero. The child is never resumed past the
// observation, so this checks the instruction transition, not its continuation.
static void stopped_load(operation fn, unsigned kind, const uint8_t *buffer,
                         unsigned offset, struct observation *out) {
  unsigned marker = 0;
  pid_t child = fork();
  if (child < 0) {
    perror("fork");
    exit(9);
  }
  if (!child) {
    if (ptrace(PTRACE_TRACEME, 0, 0, 0))
      _exit(77);
    __asm__ volatile("int3");
    invoke(fn, buffer + offset, out, &marker);
    _exit(93);
  }
  fault_begin = (uintptr_t)fn;
  fault_kind = kind;
  int live = 1, passed = 0;
  // Bound the debugger walk through the invocation wrapper and function entry.
  const unsigned max_steps = 256;
  for (unsigned step = 0; step < max_steps; ++step) {
    struct user_regs_struct before, after;
    if (!debugger_stop(child, &live) ||
        ptrace(PTRACE_GETREGS, child, 0, &before))
      break;
    if (!is_far_load(before.rip)) {
      if (ptrace(PTRACE_SINGLESTEP, child, 0, 0))
        break;
      continue;
    }
    siginfo_t event;
    errno = 0;
    if ((unsigned)ptrace(PTRACE_PEEKDATA, child, &marker, 0) != 17 || errno ||
        ptrace(PTRACE_SINGLESTEP, child, 0, 0) ||
        !debugger_stop(child, &live) ||
        ptrace(PTRACE_GETSIGINFO, child, 0, &event) ||
        event.si_code != TRAP_TRACE || ptrace(PTRACE_GETREGS, child, 0, &after))
      break;
    const uint8_t *code = (const uint8_t *)before.rip;
    if (*code == 0x66)
      ++code;
    unsigned rex = (*code & 0xf0) == 0x40 ? *code++ : 0;
    unsigned dest = ((code[2] >> 3) & 7) | ((rex & 4) << 1);
    unsigned bits = (unsigned[]){16, 32, 64}[kind % 3];
    uint64_t mask = UINT64_MAX >> (64 - bits);
    *out = (struct observation){.value = gpr(&after, dest) & mask,
                                .fsbase = after.fs_base,
                                .gsbase = after.gs_base,
                                .fs = after.fs,
                                .gs = after.gs,
                                .ss = after.ss};
    passed = !((before.eflags ^ after.eflags) & 0xcd5) &&
             (bits != 16 ||
              (gpr(&before, dest) & ~mask) == (gpr(&after, dest) & ~mask)) &&
             (bits != 32 || !(gpr(&after, dest) >> 32));
    for (unsigned r = 0; r < 16; ++r)
      if (r != dest && gpr(&before, r) != gpr(&after, r))
        passed = 0;
    for (unsigned b = 0; b < 64; b += sizeof(long)) {
      errno = 0;
      long observed = ptrace(PTRACE_PEEKDATA, child, buffer + b, 0);
      if (errno || memcmp(&observed, buffer + b, sizeof observed))
        passed = 0;
    }
    break;
  }
  if (live) {
    int status;
    kill(child, SIGKILL);
    waitpid(child, &status, 0);
  }
  if (!passed) {
    fprintf(stderr, "stopped instruction mismatch kind=%u offset=%u\n", kind,
            offset);
    exit(10);
  }
}

#define LIST(p)                                                                \
  p##lfs16, p##lfs32, p##lfs64, p##lgs16, p##lgs32, p##lgs64, p##lss16,        \
      p##lss32, p##lss64
int main(void) {
  // Linux HWCAP2_FSGSBASE proves the restoration instructions are OS-enabled.
  if (!(getauxval(AT_HWCAP2) & 2))
    return 77;
  __asm__ volatile(
      "rdfsbase %0; rdgsbase %1; mov %%fs,%2; mov %%gs,%3; mov %%ss,%4"
      : "=r"(initial_fsbase), "=r"(initial_gsbase), "=r"(initial_fs),
        "=r"(initial_gs), "=r"(initial_ss));
  setup_descriptor(0, 0, 0, 0); // Writable data.
  setup_descriptor(1, 0, 0, 1); // Not present.
  setup_descriptor(2, 0, 1, 0); // Read-only data.
  setup_descriptor(3, 2, 1, 0); // Execute-only code.
  setup_descriptor(4, 2, 0, 0); // Readable code.
  const operation fns[2][9] = {{LIST(original_)}, {LIST(generated_)}};
  unsigned observations = 0, stopped = 0, boundaries = 0, faults = 0;
  const uint64_t values[] = {0, 1, 0x80000000, UINT64_MAX,
                             UINT64_C(0x123456789abcdef0)};
  const uint16_t selectors[] = {0, 1, 2, 3, initial_ss, 7, 0x17, 0x27};
  for (unsigned kind = 0; kind < 9; ++kind) {
    unsigned bytes = (unsigned[]){2, 4, 8}[kind % 3];
    for (unsigned s = 0; s < sizeof selectors / sizeof *selectors; ++s) {
      if (kind >= 6 && s != 4 && s != 5)
        continue;
      for (unsigned offset = 0; offset < 16; ++offset)
        for (unsigned v = 0; v < sizeof values / sizeof *values; ++v) {
          uint8_t buffer[64], before[64];
          memset(buffer, 0xa5, sizeof buffer);
          image(buffer + offset, bytes, values[v], selectors[s]);
          memcpy(before, buffer, sizeof buffer);
          struct observation result[2] = {{0}, {0}};
          for (unsigned impl = 0; impl < 2; ++impl) {
            unsigned marker = 0;
            if (s >= 1 && s <= 3) {
              stopped_load(fns[impl][kind], kind, buffer, offset,
                           &result[impl]);
            } else {
              invoke(fns[impl][kind], buffer + offset, &result[impl], &marker);
              if (marker != 23 || memcmp(buffer, before, sizeof buffer))
                return 1;
            }
            uint64_t expected = values[v] & (UINT64_MAX >> (64 - bytes * 8));
            const uint16_t actual_selector = kind < 3   ? result[impl].fs
                                             : kind < 6 ? result[impl].gs
                                                        : result[impl].ss;
            if (result[impl].value != expected ||
                actual_selector != selectors[s]) {
              fprintf(
                  stderr,
                  "state mismatch kind=%u impl=%u offset=%u "
                  "value=%llx expected=%llx selector=%x expected=%x\n",
                  kind, impl, offset, (unsigned long long)result[impl].value,
                  (unsigned long long)expected, actual_selector, selectors[s]);
              return 2;
            }
            if ((s == 5 || s == 6 || s == 7) && kind < 6 &&
                (kind < 3 ? result[impl].fsbase : result[impl].gsbase) !=
                    0x12345000)
              return 3;
          }
          if (memcmp(result, result + 1, sizeof *result))
            return 4;
          printf("{\"kind\":%u,\"selector\":%u,\"offset\":%u,\"value\":%llu}\n",
                 kind, selectors[s], offset,
                 (unsigned long long)result[0].value);
          ++observations;
          stopped += s >= 1 && s <= 3;
        }
    }
    const size_t page = sysconf(_SC_PAGESIZE);
    uint8_t *p = mmap(0, page * 2, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED || mprotect(p + page, page, PROT_NONE))
      return 5;
    for (unsigned impl = 0; impl < 2; ++impl) {
      uint8_t *operand = p + page - bytes - 2;
      image(operand, bytes, UINT64_MAX, initial_ss);
      struct observation result = {0};
      unsigned marker = 0;
      invoke(fns[impl][kind], operand, &result, &marker);
      if (marker != 23 || result.value != (UINT64_MAX >> (64 - bytes * 8)))
        return 6;
      ++boundaries;
      for (unsigned missing = 1; missing <= bytes + 2; ++missing) {
        if (fault_case(fns[impl][kind], kind, operand + missing, 14, 0))
          return 7;
        ++faults;
      }
      const unsigned invalid[] = {0xffff, 0x0f, 0x1f, 0, 0x17};
      for (unsigned i = 0; i < sizeof invalid / sizeof *invalid; ++i) {
        if (kind < 6 && i >= 3)
          continue;
        unsigned trap = i == 1 ? (kind < 6 ? 11 : 12) : 13;
        uint8_t bad[10];
        image(bad, bytes, UINT64_MAX, invalid[i]);
        if (fault_case(fns[impl][kind], kind, bad, trap, invalid[i] & ~3u))
          return 8;
        ++faults;
      }
    }
    munmap(p, page * 2);
  }
  printf("{\"state_observations\":%u,\"stopped_observations\":%u,"
         "\"boundary_observations\":%u,\"fault_"
         "observations\":%u}\n",
         observations, stopped, boundaries, faults);
  return 0;
}
