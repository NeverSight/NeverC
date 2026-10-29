// Native Linux x64 oracle for legacy x87 environment and register images.
// Raw instruction bytes, independently checked state fields and guard pages
// keep this oracle separate from compiler lowering and mnemonic parsing.
#define _GNU_SOURCE
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <ucontext.h>
#include <unistd.h>

typedef void (*operation)(void *, unsigned *);
#define DECLARE(name) \
  extern void original_##name(void *, unsigned *), generated_##name(void *, unsigned *); \
  extern long double live_x87_##name(void *, volatile long double *, volatile long double *); \
  extern uint64_t live_mmx_##name(void *, uint64_t, uint64_t);
DECLARE(fnstenv16) DECLARE(fnstenv32) DECLARE(fldenv16) DECLARE(fldenv32)
DECLARE(fnsave16) DECLARE(fnsave32) DECLARE(frstor16) DECLARE(frstor32)
extern void original_defer_fldenv16(void *, unsigned *), original_defer_fldenv32(void *, unsigned *);
extern void original_defer_frstor16(void *, unsigned *), original_defer_frstor32(void *, unsigned *);
extern void generated_defer_fldenv16(void *, unsigned *), generated_defer_fldenv32(void *, unsigned *);
extern void generated_defer_frstor16(void *, unsigned *), generated_defer_frstor32(void *, unsigned *);
#define DEFER(name, bytes) \
  ".globl original_defer_" #name "\noriginal_defer_" #name ":\n" \
  "movl $17,(%rsi)\n.byte " bytes "\nmovl $19,(%rsi)\n.byte 0x9b\nmovl $23,(%rsi)\nret\n"
#define ORIGINAL(name, bytes) \
  ".globl original_" #name "\noriginal_" #name ":\n" \
  "movl $17,(%rsi)\n.byte " bytes "\nmovl $23,(%rsi)\nret\n"
extern void invoke(operation, void *, const void *, void *, unsigned *);
__asm__(".text\n"
  ORIGINAL(fnstenv16, "0x66,0xd9,0x37") ORIGINAL(fnstenv32, "0xd9,0x37")
  ORIGINAL(fldenv16, "0x66,0xd9,0x27") ORIGINAL(fldenv32, "0xd9,0x27")
  ORIGINAL(fnsave16, "0x66,0xdd,0x37") ORIGINAL(fnsave32, "0xdd,0x37")
  ORIGINAL(frstor16, "0x66,0xdd,0x27") ORIGINAL(frstor32, "0xdd,0x27")
  DEFER(fldenv16, "0x66,0xd9,0x27") DEFER(fldenv32, "0xd9,0x27")
  DEFER(frstor16, "0x66,0xdd,0x27") DEFER(frstor32, "0xdd,0x27")
  ".globl invoke\ninvoke:\n"
  "push %rbx\npush %r12\npush %r13\nsub $512,%rsp\n"
  "mov %rdx,%rbx\nmov %rcx,%r12\nmov %rdi,%r13\nmov %rsi,%rdi\n"
  "mov %r8,%rsi\nfxsave64 (%rsp)\nfxrstor64 (%rbx)\ncall *%r13\n"
  "fxsave64 (%r12)\nfxrstor64 (%rsp)\n"
  "add $512,%rsp\npop %r13\npop %r12\npop %rbx\nret\n");

static unsigned word(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
static void put_word(uint8_t *p, unsigned x) { p[0] = x; p[1] = x >> 8; }
static void put_dword(uint8_t *p, unsigned x) {
  put_word(p, x); put_word(p + 2, x >> 16);
}
static unsigned image_size(unsigned kind) {
  return (kind & 1 ? 28 : 14) + (kind >= 4 ? 80 : 0);
}
static unsigned is_load(unsigned kind) { return kind == 2 || kind == 3 || kind >= 6; }
static void seed(uint8_t *state, unsigned top, unsigned tags, unsigned pc,
                 unsigned rc, unsigned sticky) {
  memset(state, 0, 512);
  put_word(state, 0x7f | pc << 8 | rc << 10);
  put_word(state + 2, 0x4700 | top << 11 | (sticky ? 0x25 : 0));
  state[4] = tags;
  put_word(state + 6, 0x123);
  put_dword(state + 8, 0x23456789); put_dword(state + 12, 0x12345678);
  put_dword(state + 16, 0x3456789a); put_dword(state + 20, 0x23456789);
  put_word(state + 24, 0x1f80);
  for (unsigned reg = 0; reg < 8; ++reg) {
    uint8_t *value = state + 32 + reg * 16;
    if (reg % 4 == 1) continue; // Zero.
    value[0] = reg + 1;
    value[7] = reg % 4 == 3 ? 0 : 0x80;
    put_word(value + 8, reg % 4 == 2 ? 0x7fff : reg % 4 == 3 ? 0 : 0x3fff);
  }
}
static unsigned full_tag(const uint8_t *state) {
  const unsigned top = word(state + 2) >> 11 & 7;
  unsigned tag = 0;
  for (unsigned logical = 0; logical < 8; ++logical) {
    const unsigned physical = (top + logical) & 7;
    const uint8_t *value = state + 32 + logical * 16;
    uint64_t significand;
    memcpy(&significand, value, 8);
    const unsigned exponent = word(value + 8) & 0x7fff;
    const unsigned code = !(state[4] & 1u << physical) ? 3 :
        !exponent && !significand ? 1 :
        !exponent || exponent == 0x7fff || !(significand >> 63) ? 2 : 0;
    tag |= code << (physical * 2);
  }
  return tag;
}
static void image(uint8_t *p, unsigned kind, unsigned top, unsigned tags) {
  memset(p, 0, image_size(kind));
  const unsigned stride = kind & 1 ? 4 : 2;
  put_word(p, 0x37f);
  put_word(p + stride, top << 11 | 0x4104);
  put_word(p + stride * 2, tags);
  if (kind & 1) {
    put_dword(p + 12, 0x456789ab); put_word(p + 18, 0x321);
    put_dword(p + 20, 0x56789abc);
  } else {
    put_word(p + 6, 0x89ab); put_word(p + 10, 0x9abc);
  }
  if (kind >= 6)
    for (unsigned reg = 0; reg < 8; ++reg) {
      uint8_t *value = p + stride * 7 + reg * 10;
      value[0] = reg + 23; value[7] = 0x80;
      put_word(value + 8, 0x4000);
    }
}

static uintptr_t expected_begin;
static unsigned expected_kind, expected_pending, expected_signal;
static unsigned *fault_marker;
static void on_fault(int number, siginfo_t *info, void *opaque) {
  (void)info;
  const ucontext_t *context = opaque;
  const uintptr_t pc = context->uc_mcontext.gregs[REG_RIP];
  const unsigned trap = context->uc_mcontext.gregs[REG_TRAPNO];
  if (number != expected_signal || trap != (number == SIGFPE ? 16u : 14u) ||
      pc < expected_begin || pc >= expected_begin + 128 || *fault_marker != (expected_pending == 2 ? 19u : 17u))
    _exit(90);
  const uint8_t *instruction = (const uint8_t *)pc;
  unsigned prefix = 0;
  if (*instruction == 0x66) { ++instruction; prefix = 1; }
  if ((*instruction & 0xf0) == 0x40) ++instruction;
  if (expected_pending == 2 ? instruction[0] != 0x9b :
      (prefix != !(expected_kind & 1) ||
      instruction[0] != (expected_kind < 4 ? 0xd9 : 0xdd) ||
      (instruction[1] >> 6) == 3 ||
      ((instruction[1] >> 3) & 7) != (is_load(expected_kind) ? 4u : 6u)))
    _exit(91);
  // A fault during a multi-access restore may expose partial state. Only
  // pending #MF promises that the incoming environment has not been loaded.
  if (number == SIGFPE &&
      (context->uc_mcontext.fpregs->cwd != 0x37e ||
       (context->uc_mcontext.fpregs->swd & 0x81) != 0x81)) _exit(92);
  _exit(0);
}
static int fault_case(operation fn, unsigned pending, unsigned bad, unsigned kind,
                      uint8_t *pages, size_t pagesize) {
  const pid_t child = fork();
  if (child < 0) return 1;
  if (!child) {
    unsigned marker = 0;
    expected_begin = (uintptr_t)fn; expected_kind = kind; expected_pending = pending;
    expected_signal = pending && is_load(kind) ? SIGFPE : bad ? SIGSEGV : 0;
    fault_marker = &marker;
    struct sigaction action = {.sa_sigaction = on_fault, .sa_flags = SA_SIGINFO};
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGFPE, &action, 0) || sigaction(SIGSEGV, &action, 0)) _exit(93);
    _Alignas(16) uint8_t before[512], after[512];
    seed(before, 0, 0, 3, 0, 0);
    put_word(before, pending == 1 ? 0x37e : 0x37f); put_word(before + 2, pending == 1 ? 0x81 : 0);
    uint8_t *operand = pages + pagesize - (bad == 1 ? 0 : image_size(kind) - (bad == 2));
    if (pending == 2) {
      put_word(operand, 0x37e); put_word(operand + (kind & 1 ? 4 : 2), 0x81);
    }
    invoke(fn, operand, before, after, &marker);
    if (!expected_signal && kind < 2 &&
        ((word(after) & 0x3f) != 0x3f || word(operand) != word(before))) _exit(95);
    if (!expected_signal && (kind == 4 || kind == 5) &&
        (word(after) != 0x37f || word(after + 2) || after[4])) _exit(96);
    _exit(expected_signal || marker != 23 ? 94 : 0);
  }
  int status = 0;
  if (waitpid(child, &status, 0) != child || !WIFEXITED(status) || WEXITSTATUS(status)) {
    fprintf(stderr, "fault case: kind=%u pending=%u bad=%u status=%d\n", kind, pending, bad, status);
    return 1;
  }
  return 0;
}

#define LIST(prefix) prefix##fnstenv16, prefix##fnstenv32, prefix##fldenv16, prefix##fldenv32, \
                     prefix##fnsave16, prefix##fnsave32, prefix##frstor16, prefix##frstor32
int main(void) {
  const operation functions[2][8] = {{LIST(original_)}, {LIST(generated_)}};
  const operation deferred[2][8] = {
    {0,0,original_defer_fldenv16,original_defer_fldenv32,0,0,original_defer_frstor16,original_defer_frstor32},
    {0,0,generated_defer_fldenv16,generated_defer_fldenv32,0,0,generated_defer_frstor16,generated_defer_frstor32}};
  unsigned observations = 0, boundaries = 0, fault_cases = 0, faults = 0, live_cases = 0;
  for (unsigned top = 0; top < 8; ++top)
    for (unsigned tags = 0; tags < 3; ++tags)
      for (unsigned pc = 0; pc < 4; ++pc) {
        if (pc == 1) continue;
        for (unsigned rc = 0; rc < 4; ++rc)
          for (unsigned sticky = 0; sticky < 2; ++sticky)
            for (unsigned kind = 0; kind < 8; ++kind) {
              _Alignas(16) uint8_t before[512], after[2][512];
              uint8_t buffer[2][128];
              seed(before, top, (unsigned[]){0, 0x55, 0xff}[tags], pc, rc, sticky);
              memset(buffer, 0xa5, sizeof(buffer));
              for (unsigned implementation = 0; implementation < 2; ++implementation) {
                uint8_t *area = buffer[implementation] + 3;
                uint8_t *state = after[implementation];
                if (is_load(kind)) image(area, kind, (top + 3) & 7, 0xe4e4);
                unsigned marker = 0;
                invoke(functions[implementation][kind], area, before, state, &marker);
                if (marker != 23) return 1;
                for (unsigned index = 0; index < 128; ++index)
                  if ((index < 3 || index >= 3 + image_size(kind)) &&
                      buffer[implementation][index] != 0xa5) return 2;
                const unsigned stride = kind & 1 ? 4 : 2;
                if (!is_load(kind)) {
                  if (word(area) != word(before) || word(area + stride) != word(before + 2) ||
                      word(area + stride * 2) != full_tag(before)) return 3;
                  if (kind >= 4)
                    for (unsigned reg = 0; reg < 8; ++reg)
                      if (memcmp(area + stride * 7 + reg * 10, before + 32 + reg * 16, 10)) return 4;
                }
                if (kind == 4 || kind == 5) {
                  if (word(state) != 0x37f || word(state + 2) || state[4]) return 5;
                } else if (is_load(kind)) {
                  if (word(state) != word(area) || word(state + 2) != word(area + stride) ||
                      state[4] != 0x77) return 6;
                } else if (word(state) != (word(before) | 0x3f)) return 7;
                for (unsigned reg = 0; reg < 8; ++reg) {
                  const unsigned source = kind >= 4 && kind < 6 ? (reg + 8 - top) & 7 :
                                          kind >= 2 && kind < 4 ? (reg + 3) & 7 : reg;
                  const uint8_t *expected = kind >= 6 ? area + stride * 7 + reg * 10 :
                                                          before + 32 + source * 16;
                  if (memcmp(expected, state + 32 + reg * 16, 10)) return 8;
                }
                if (kind < 2) state[3] &= 0xb8; // FNSTENV's C0-C3 are undefined.
                printf("{\"kind\":%u,\"generated\":%u,\"top\":%u,\"tags\":%u,"
                       "\"pc\":%u,\"rc\":%u,\"sticky\":%u}\n",
                       kind, implementation, top, tags, pc, rc, sticky);
                ++observations;
              }
              if (memcmp(after[0], after[1], 24) || memcmp(buffer[0], buffer[1], 128)) return 9;
            }
      }
  const long pagesize = sysconf(_SC_PAGESIZE);
  if (pagesize <= 0) return 10;
  uint8_t *pages = mmap(0, pagesize * 2, PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (pages == MAP_FAILED || mprotect(pages + pagesize, pagesize, PROT_NONE)) return 11;
  for (unsigned implementation = 0; implementation < 2; ++implementation)
    for (unsigned kind = 0; kind < 8; ++kind) {
      _Alignas(16) uint8_t before[512], after[512];
      seed(before, 0, 0, 3, 0, 0);
      uint8_t *operand = pages + pagesize - image_size(kind);
      image(operand, kind, 0, 0xffff); operand[-1] = 0xa5;
      unsigned marker = 0;
      invoke(functions[implementation][kind], operand, before, after, &marker);
      if (marker != 23 || operand[-1] != 0xa5) return 12;
      printf("{\"boundary\":true,\"kind\":%u,\"generated\":%u}\n", kind, implementation);
      ++boundaries;
      for (unsigned pending = 0; pending < 2; ++pending)
        for (unsigned bad = 0; bad < 3; ++bad) {
          image(operand, kind, 0, 0xffff);
          if (fault_case(functions[implementation][kind], pending, bad, kind, pages, pagesize)) return 13;
          const unsigned signal = pending && is_load(kind) ? SIGFPE : bad ? SIGSEGV : 0;
          printf("{\"fault_case\":true,\"generated\":%u,\"kind\":%u,\"pending\":%u,"
                 "\"bad\":%u,\"signal\":%u}\n", implementation, kind, pending, bad, signal);
          ++fault_cases; faults += !!signal;
        }
      if (is_load(kind)) {
        image(operand, kind, 0, 0xffff);
        if (fault_case(deferred[implementation][kind], 2, 0, kind, pages, pagesize)) return 16;
        printf("{\"deferred_exception\":true,\"kind\":%u,\"generated\":%u}\n", kind, implementation);
        ++fault_cases; ++faults;
      }
    }
  long double (*fp[8])(void *, volatile long double *, volatile long double *) = {LIST(live_x87_)};
  uint64_t (*mmx[8])(void *, uint64_t, uint64_t) = {LIST(live_mmx_)};
  for (unsigned kind = 0; kind < 8; ++kind)
    for (unsigned i = 0; i < 64; ++i) {
      uint8_t area[128];
      image(area, kind, 0, 0xffff); put_word(area + (kind & 1 ? 4 : 2), 0);
      volatile long double a = (long double)((int)i - 32) / 4;
      volatile long double b = (long double)(i + 1) / 8;
      const long double expected = a + b;
      if (fp[kind](area, &a, &b) != expected) return 14;
      image(area, kind, 0, 0xffff); put_word(area + (kind & 1 ? 4 : 2), 0);
      const uint64_t x = UINT64_C(0x01030507090b0d0f) * i, y = UINT64_C(0xff12ef34df56cf78) ^ i;
      uint64_t sum = 0;
      for (unsigned byte = 0; byte < 8; ++byte)
        sum |= (((x >> (byte * 8)) + (y >> (byte * 8))) & 255) << (byte * 8);
      const uint64_t actual = mmx[kind](area, x, y);
      _Alignas(16) uint8_t state[512];
      __asm__ volatile("fxsave64 %0" : "=m"(state));
      if (actual != (sum ^ x) || state[4]) return 15;
      printf("{\"live_values\":true,\"kind\":%u,\"case\":%u}\n", kind, i);
      ++live_cases;
    }
  munmap(pages, pagesize * 2);
  printf("{\"state_observations\":%u,\"fault_observations\":%u,"
         "\"fault_and_nonfault_cases\":%u,\"boundary_observations\":%u,"
         "\"live_value_cases\":%u}\n", observations, faults, fault_cases, boundaries, live_cases);
  return 0;
}
