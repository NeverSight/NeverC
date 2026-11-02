// Native MOV selector comparison against independently authored opcode bytes.
// The call window restores all mutable segment selectors and both TLS bases.
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
#include <sys/syscall.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

typedef uint64_t (*operation)(uint64_t, void *, unsigned *);
struct observation {
  uint64_t value, fsbase, gsbase;
  uint16_t segment[6];
};
extern void invoke(operation, uint64_t, void *, struct observation *,
                   unsigned *);
#define DECLARE(name)                                                          \
  extern uint64_t original_##name(uint64_t, void *, unsigned *);               \
  extern uint64_t generated_##name(uint64_t, void *, unsigned *)
DECLARE(read_es);
DECLARE(read_cs);
DECLARE(read_ss);
DECLARE(read_ds);
DECLARE(read_fs);
DECLARE(read_gs);
DECLARE(readmem_es);
DECLARE(readmem_cs);
DECLARE(readmem_ss);
DECLARE(readmem_ds);
DECLARE(readmem_fs);
DECLARE(readmem_gs);
DECLARE(write_es);
DECLARE(write_ss);
DECLARE(write_ds);
DECLARE(write_fs);
DECLARE(write_gs);
DECLARE(writemem_es);
DECLARE(writemem_ss);
DECLARE(writemem_ds);
DECLARE(writemem_fs);
DECLARE(writemem_gs);
#undef DECLARE
// clang-format off
#define ORIGINAL(name, bytes, result) \
  ".globl original_" #name "\noriginal_" #name ":\n" \
  "movl $17,(%rdx)\n.byte " bytes "\n" result "movl $23,(%rdx)\nret\n"
__asm__(".text\n"
 ORIGINAL(read_es, "0x66,0x8c,0xc0", "movzwl %ax,%eax\n")
 ORIGINAL(read_cs, "0x66,0x8c,0xc8", "movzwl %ax,%eax\n")
 ORIGINAL(read_ss, "0x66,0x8c,0xd0", "movzwl %ax,%eax\n")
 ORIGINAL(read_ds, "0x66,0x8c,0xd8", "movzwl %ax,%eax\n")
 ORIGINAL(read_fs, "0x66,0x8c,0xe0", "movzwl %ax,%eax\n")
 ORIGINAL(read_gs, "0x66,0x8c,0xe8", "movzwl %ax,%eax\n")
 ORIGINAL(readmem_es, "0x8c,0x06", "xorl %eax,%eax\n")
 ORIGINAL(readmem_cs, "0x8c,0x0e", "xorl %eax,%eax\n")
 ORIGINAL(readmem_ss, "0x8c,0x16", "xorl %eax,%eax\n")
 ORIGINAL(readmem_ds, "0x8c,0x1e", "xorl %eax,%eax\n")
 ORIGINAL(readmem_fs, "0x8c,0x26", "xorl %eax,%eax\n")
 ORIGINAL(readmem_gs, "0x8c,0x2e", "xorl %eax,%eax\n")
 ORIGINAL(write_es, "0x66,0x8e,0xc7", "xorl %eax,%eax\n")
 ORIGINAL(write_ss, "0x66,0x8e,0xd7", "xorl %eax,%eax\n")
 ORIGINAL(write_ds, "0x66,0x8e,0xdf", "xorl %eax,%eax\n")
 ORIGINAL(write_fs, "0x66,0x8e,0xe7", "xorl %eax,%eax\n")
 ORIGINAL(write_gs, "0x66,0x8e,0xef", "xorl %eax,%eax\n")
 ORIGINAL(writemem_es, "0x8e,0x06", "xorl %eax,%eax\n")
 ORIGINAL(writemem_ss, "0x8e,0x16", "xorl %eax,%eax\n")
 ORIGINAL(writemem_ds, "0x8e,0x1e", "xorl %eax,%eax\n")
 ORIGINAL(writemem_fs, "0x8e,0x26", "xorl %eax,%eax\n")
 ORIGINAL(writemem_gs, "0x8e,0x2e", "xorl %eax,%eax\n")
 ".globl invoke\ninvoke:\n"
 "push %rbx\npush %r12\npush %r13\nsub $32,%rsp\n"
 "mov %rcx,%rbx\nmov %rdi,%r11\nmov %rsi,%rdi\nmov %rdx,%rsi\nmov %r8,%rdx\n"
 "rdfsbase %r12\nrdgsbase %r13\n"
 "mov %fs,16(%rsp)\nmov %gs,18(%rsp)\nmov %ss,20(%rsp)\n"
 "mov %ds,22(%rsp)\nmov %es,24(%rsp)\n"
 "call *%r11\nmov %rax,0(%rbx)\n"
 "rdfsbase %rax\nmov %rax,8(%rbx)\nrdgsbase %rax\nmov %rax,16(%rbx)\n"
 "mov %es,24(%rbx)\nmov %cs,26(%rbx)\nmov %ss,28(%rbx)\n"
 "mov %ds,30(%rbx)\nmov %fs,32(%rbx)\nmov %gs,34(%rbx)\n"
 "mov 24(%rsp),%ax\nmov %ax,%es\nmov 22(%rsp),%ax\nmov %ax,%ds\n"
 "mov 20(%rsp),%ax\nmov %ax,%ss\n"
 "mov 16(%rsp),%ax\nmov %ax,%fs\nwrfsbase %r12\n"
 "mov 18(%rsp),%ax\nmov %ax,%gs\nwrgsbase %r13\n"
 "add $32,%rsp\npop %r13\npop %r12\npop %rbx\nret\n");
#undef ORIGINAL
// clang-format on

struct form {
  operation original, generated;
  unsigned kind, segment;
};
static const struct form forms[] = {
    {original_read_es, generated_read_es, 0, 0},
    {original_read_cs, generated_read_cs, 0, 1},
    {original_read_ss, generated_read_ss, 0, 2},
    {original_read_ds, generated_read_ds, 0, 3},
    {original_read_fs, generated_read_fs, 0, 4},
    {original_read_gs, generated_read_gs, 0, 5},
    {original_readmem_es, generated_readmem_es, 1, 0},
    {original_readmem_cs, generated_readmem_cs, 1, 1},
    {original_readmem_ss, generated_readmem_ss, 1, 2},
    {original_readmem_ds, generated_readmem_ds, 1, 3},
    {original_readmem_fs, generated_readmem_fs, 1, 4},
    {original_readmem_gs, generated_readmem_gs, 1, 5},
    {original_write_es, generated_write_es, 2, 0},
    {original_write_ss, generated_write_ss, 2, 2},
    {original_write_ds, generated_write_ds, 2, 3},
    {original_write_fs, generated_write_fs, 2, 4},
    {original_write_gs, generated_write_gs, 2, 5},
    {original_writemem_es, generated_writemem_es, 3, 0},
    {original_writemem_ss, generated_writemem_ss, 3, 2},
    {original_writemem_ds, generated_writemem_ds, 3, 3},
    {original_writemem_fs, generated_writemem_fs, 3, 4},
    {original_writemem_gs, generated_writemem_gs, 3, 5},
};
static struct observation initial;
static uintptr_t fault_begin, fault_address;
static unsigned fault_kind, fault_segment, fault_trap, fault_error;
static unsigned *fault_marker;

static void snapshot(struct observation *o) {
  __asm__ volatile("rdfsbase %0; rdgsbase %1; mov %%es,%2; mov %%cs,%3; "
                   "mov %%ss,%4; mov %%ds,%5; mov %%fs,%6; mov %%gs,%7"
                   : "=r"(o->fsbase), "=r"(o->gsbase), "=r"(o->segment[0]),
                     "=r"(o->segment[1]), "=r"(o->segment[2]),
                     "=r"(o->segment[3]), "=r"(o->segment[4]),
                     "=r"(o->segment[5]));
}

static void on_fault(int number, siginfo_t *info, void *opaque) {
  (void)number;
  (void)info;
  const ucontext_t *context = opaque;
  const uintptr_t pc = context->uc_mcontext.gregs[REG_RIP];
  if (pc < fault_begin || pc >= fault_begin + 256)
    _exit(90);
  const uint8_t *code = (const uint8_t *)pc;
  if (*code == 0x66)
    ++code;
  if ((*code & 0xf0) == 0x40)
    ++code;
  if (code[0] != (fault_kind < 2 ? 0x8c : 0x8e) ||
      ((code[1] >> 3) & 7) != fault_segment ||
      ((code[1] >> 6) != 3) != (fault_kind & 1) || *fault_marker != 17 ||
      context->uc_mcontext.gregs[REG_TRAPNO] != fault_trap ||
      (fault_trap != 14 &&
       context->uc_mcontext.gregs[REG_ERR] != fault_error) ||
      (fault_trap == 14 &&
       context->uc_mcontext.gregs[REG_CR2] != fault_address))
    _exit(91);
  struct observation out = {0};
  snapshot(&out);
  if (memcmp(&out, &initial, sizeof out))
    _exit(92);
  _exit(0);
}

static void fault_case(operation fn, const struct form *f, uint64_t value,
                       void *pointer, unsigned trap, unsigned error,
                       uintptr_t address) {
  pid_t child = fork();
  if (child < 0)
    exit(11);
  if (!child) {
    unsigned marker = 0;
    fault_begin = (uintptr_t)fn;
    fault_kind = f->kind;
    fault_segment = f->segment;
    fault_trap = trap;
    fault_error = error;
    fault_address = address;
    fault_marker = &marker;
    struct sigaction action = {.sa_sigaction = on_fault,
                               .sa_flags = SA_SIGINFO};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGSEGV, &action, 0) || sigaction(SIGBUS, &action, 0))
      _exit(93);
    struct observation out = {0};
    invoke(fn, value, pointer, &out, &marker);
    _exit(94);
  }
  int status;
  if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
      WEXITSTATUS(status)) {
    fprintf(stderr,
            "fault mismatch kind=%u segment=%u trap=%u error=%u status=%d\n",
            f->kind, f->segment, trap, error, status);
    exit(12);
  }
}

static void descriptor(unsigned index, unsigned contents, unsigned readonly,
                       unsigned absent) {
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

int main(void) {
  if (!(getauxval(AT_HWCAP2) & 2))
    return 77;
  snapshot(&initial);
  descriptor(0, 0, 0, 0);
  descriptor(1, 0, 0, 1);
  descriptor(2, 0, 1, 0);
  descriptor(3, 2, 1, 0);
  descriptor(4, 2, 0, 0);
  unsigned states = 0, boundaries = 0, faults = 0;
  const uint16_t selectors[] = {0, initial.segment[2], 7, 0x17, 0x27};
  const uint64_t high[] = {0, UINT64_C(0x123456789abc0000),
                           UINT64_C(0xffffffffffff0000)};
  for (unsigned n = 0; n < sizeof forms / sizeof *forms; ++n) {
    const struct form *f = &forms[n];
    const operation functions[] = {f->original, f->generated};
    for (unsigned s = 0; s < (f->kind < 2 ? 1u : 5u); ++s) {
      if (f->kind >= 2 && f->segment == 2 && s != 1 && s != 2)
        continue;
      for (unsigned offset = 0; offset < 16; ++offset)
        for (unsigned v = 0; v < (f->kind < 2 ? 1u : 3u); ++v) {
          struct observation out[2] = {{0}, {0}};
          for (unsigned impl = 0; impl < 2; ++impl) {
            uint8_t buffer[64], want[64];
            memset(buffer, 0xa5, sizeof buffer);
            if (f->kind == 3)
              memcpy(buffer + offset, &selectors[s], 2);
            memcpy(want, buffer, sizeof want);
            if (f->kind == 1)
              memcpy(want + offset, &initial.segment[f->segment], 2);
            unsigned marker = 0;
            invoke(functions[impl], high[v] | selectors[s], buffer + offset,
                   &out[impl], &marker);
            if (marker != 23 || memcmp(buffer, want, sizeof buffer) ||
                out[impl].value !=
                    (f->kind == 0 ? initial.segment[f->segment] : 0) ||
                out[impl].segment[f->segment] !=
                    (f->kind < 2 ? initial.segment[f->segment]
                                 : selectors[s])) {
              fprintf(stderr,
                      "state mismatch form=%u selector=%u offset=%u value=%u "
                      "impl=%u\n",
                      n, s, offset, v, impl);
              return 1;
            }
            for (unsigned r = 0; r < 6; ++r)
              if (r != f->segment && out[impl].segment[r] != initial.segment[r])
                return 2;
            if (f->kind >= 2 && f->segment >= 4 && s >= 2 &&
                (f->segment == 4 ? out[impl].fsbase : out[impl].gsbase) !=
                    0x12345000)
              return 3;
          }
          if (memcmp(out, out + 1, sizeof *out))
            return 4;
          printf("{\"form\":%u,\"selector\":%u,\"offset\":%u,\"sample\":%u}\n",
                 n, selectors[s], offset, v);
          ++states;
        }
    }
    if (f->kind & 1) {
      size_t page = sysconf(_SC_PAGESIZE);
      uint8_t *p = mmap(0, page * 2, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
      if (p == MAP_FAILED || mprotect(p + page, page, PROT_NONE))
        return 5;
      memcpy(p + page - 2, &initial.segment[2], 2);
      for (unsigned impl = 0; impl < 2; ++impl) {
        unsigned marker = 0;
        struct observation out = {0};
        invoke(functions[impl], 0, p + page - 2, &out, &marker);
        if (marker != 23)
          return 6;
        ++boundaries;
        for (unsigned missing = 1; missing <= 2; ++missing) {
          fault_case(functions[impl], f, 0, p + page - 2 + missing, 14, 0,
                     (uintptr_t)(p + page));
          ++faults;
        }
        if (f->kind == 1) {
          if (mprotect(p, page, PROT_READ))
            return 7;
          fault_case(functions[impl], f, 0, p + page - 2, 14, 0,
                     (uintptr_t)(p + page - 2));
          if (mprotect(p, page, PROT_READ | PROT_WRITE))
            return 8;
          ++faults;
        }
      }
      munmap(p, page * 2);
    }
    if (f->kind >= 2) {
      const uint16_t bad[] = {0xffff, 0x0f, 0x1f, 0, 0x17};
      for (unsigned i = 0; i < (f->segment == 2 ? 5u : 3u); ++i)
        for (unsigned impl = 0; impl < 2; ++impl) {
          uint16_t selector = bad[i];
          unsigned trap = i == 1 ? (f->segment == 2 ? 12 : 11) : 13;
          fault_case(functions[impl], f, selector, &selector, trap,
                     selector & ~3u, 0);
          ++faults;
        }
    }
  }
  printf("{\"state_observations\":%u,\"boundary_observations\":%u,\"fault_"
         "observations\":%u}\n",
         states, boundaries, faults);
  return 0;
}
