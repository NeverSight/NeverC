#include "NativeBuiltinFixture.h"

class X86NativeAddressTest : public X86NativeBuiltinTest {};

TEST_F(X86NativeAddressTest, StackControlUsesNativeInstructionsAcrossFormats) {
  const auto Source = tmpFile("native_stack_address.c");
  writeFile(Source, R"(
#if __NEVERC_X86_NATIVE_MEMORY_STACK_REFERENCE__ != 1
#error "missing stack-reference capability"
#endif
typedef unsigned v4 __attribute__((vector_size(16)));
typedef unsigned v16 __attribute__((vector_size(64)));
unsigned query(unsigned long long p, unsigned prior, unsigned char *zf) {
  return __builtin_ia32_lar_mem32_address(p, 64, 0x100, prior, zf);
}
void system_memory(unsigned long long p, unsigned long long field) {
  __builtin_ia32_vmwrite_mem_address(field, p, 64, 0x136);
  __builtin_ia32_verr_mem_address(p, 64, 0x13e);
}
__attribute__((target("amx-tile")))
void config(unsigned long long p) {
  __builtin_ia32_ldtilecfg_address(p, 64, 0x100, 0);
  __builtin_ia32_sttilecfg_address(p, 64, 0x126, 0);
}
__attribute__((target("avx512f,avx512vl")))
void compact(void *p, v4 v) {
  __builtin_ia32_vpcompressd128_native(p, 0, v, 0x100);
  __builtin_ia32_vpexpandd128_native(p, 0, v, 1, 0x12e);
}
__attribute__((target("avx512f")))
void four(void *p, v16 v) {
  __builtin_ia32_vp4dpwssd_native(p, v, v, v, v, v, 0, 0, 0x100);
}
void atomic_memory(unsigned long long p, unsigned long long v) {
  __builtin_ia32_aadd64_legacy_address(p, v, 64, 0x100);
}
)");
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      const auto AssemblyFile = tmpFile("native_stack_address.s");
      const auto Result = ncc({Target, Opt, "-fno-lto", "-fno-builtin-mimalloc",
                               "-ffreestanding", "-Werror", "-mllvm",
                               "-verify-machineinstrs", "-masm=att", "-S",
                               Source.string(), "-o", AssemblyFile.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Assembly = readFile(AssemblyFile);
      EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
      EXPECT_EQ(Assembly.find("\tcall"), std::string::npos) << Assembly;
      for (const char *Name :
           {"larl", "vmwriteq", "verr", "ldtilecfg", "sttilecfg", "vpcompressd",
            "vpexpandd", "vp4dpwssd", "aaddq"}) {
        const auto Start = Assembly.find(Name);
        ASSERT_NE(Start, std::string::npos) << Name << Assembly;
        const auto Line =
            Assembly.substr(Start, Assembly.find('\n', Start) - Start);
        EXPECT_NE(Line.find("(%rbp,%"), std::string::npos) << Line;
      }
    }
}

TEST_F(X86NativeAddressTest, InvalidStackControlsFailBeforeCodeGeneration) {
  for (const char *Call :
       {"__builtin_ia32_verr_mem_address(p, 32, 0x100)",
        "__builtin_ia32_verr_mem_address(p, 64, 0x164)",
        "__builtin_ia32_verr_mem_address(p, 64, 0x165)",
        "__builtin_ia32_verr_mem_address(p, 64, 0x200)",
        "__builtin_ia32_tileloadd64_address(0, p, 64, 64, 0x100, 0)",
        "__builtin_ia32_bndmov_load(0, p, 64, 0x100)",
        "__builtin_ia32_bndldx(0, p, 0, 64, 0x100)"}) {
    SCOPED_TRACE(Call);
    const auto Source = tmpFile("invalid_stack_address.c");
    writeFile(Source, "__attribute__((target(\"amx-tile\"))) "
                      "void probe(unsigned long long p) { " +
                          std::string(Call) + "; }\n");
    const auto Result = ncc({"--target=x86_64-unknown-linux-gnu",
                             "-fsyntax-only", Source.string()});
    EXPECT_NE(Result.exitCode, 0);
    EXPECT_NE(Result.err.find("stack-reference bit"), std::string::npos)
        << Result.err;
  }
}

TEST_F(X86NativeAddressTest, NativeResultsFaultClassesAndRealignedStackAgree) {
#if !defined(__linux__) || !defined(__x86_64__)
  GTEST_SKIP() << "native trap-number oracle requires Linux x86-64";
#else
  const auto Source = tmpFile("native_stack_oracle.c");
  writeFile(Source, R"(
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <unistd.h>
#include <sys/wait.h>
#include <ucontext.h>
__attribute__((noinline)) void touch(void *p) {
  *(volatile unsigned char *)p = 0xa5;
}
__attribute__((noinline)) void check(uint64_t value) {
  if (value != 0x12345678) abort();
}
#define CASE(control) \
__attribute__((noinline)) uint64_t simple_##control(uint64_t p, unsigned prior, unsigned n) { \
  unsigned char zf; \
  unsigned value = __builtin_ia32_lar_mem32_address(p, 64, control, prior, &zf); \
  return value | ((uint64_t)zf << 32); \
} \
__attribute__((noinline)) uint64_t dynamic_##control(uint64_t p, unsigned prior, unsigned n) { \
  volatile uint64_t fixed[16] __attribute__((aligned(64))); \
  fixed[0] = 0x12345678; fixed[15] = 0x12345678; \
  void *a = __builtin_alloca(n); touch(a); \
  unsigned char zf; \
  unsigned value = __builtin_ia32_lar_mem32_address(p, 64, control, prior, &zf); \
  touch(a); check(fixed[0]); check(fixed[15]); \
  return value | ((uint64_t)zf << 32); \
}
CASE(0) CASE(0x26) CASE(0x2e) CASE(0x36) CASE(0x3e)
CASE(0x100) CASE(0x126) CASE(0x12e) CASE(0x136) CASE(0x13e)
typedef uint64_t (*probe)(uint64_t, unsigned, unsigned);
#define ENTRY(control) {simple_##control, control}, {dynamic_##control, control}
static const struct { probe fn; unsigned control; } functions[] = {
  ENTRY(0), ENTRY(0x26), ENTRY(0x2e), ENTRY(0x36), ENTRY(0x3e),
  ENTRY(0x100), ENTRY(0x126), ENTRY(0x12e), ENTRY(0x136), ENTRY(0x13e)
};
static uint64_t reference(uint16_t selector, unsigned prior) {
  unsigned value = prior; unsigned char zf;
  __asm__ volatile("larl %w2,%0; sete %1"
                   : "+r"(value), "=qm"(zf) : "r"(selector) : "cc");
  return (uint64_t)(zf ? value : prior) | ((uint64_t)zf << 32);
}
static void fault(int sig, siginfo_t *info, void *context) {
  ucontext_t *u = context;
  _exit(100 + u->uc_mcontext.gregs[REG_TRAPNO]);
}
int main(void) {
  uint16_t *heap = malloc(2), stack;
  if (!heap) return 1;
  for (unsigned i = 0; i < sizeof(functions) / sizeof(functions[0]); ++i) {
    for (unsigned selector = 0; selector <= 65535; selector += 17) {
      *heap = stack = selector;
      unsigned prior = 0x92ed6af1u ^ selector;
      uint64_t expected = reference(selector, prior);
      // LAR leaves result bits 19:16 undefined on success.
      uint64_t mask = ~((expected >> 32) ? UINT64_C(0xf0000) : 0);
      for (unsigned place = 0; place < 2; ++place) {
        uint64_t actual = functions[i].fn((uintptr_t)(place ? heap : &stack),
                                         prior, 65 + (selector & 511));
        if ((actual ^ expected) & mask) return 2;
      }
    }
    for (unsigned high = 0; high < 2; ++high) {
      pid_t child = fork();
      if (child < 0) return 3;
      if (!child) {
        struct sigaction action = {0};
        action.sa_sigaction = fault; action.sa_flags = SA_SIGINFO;
        sigaction(SIGSEGV, &action, 0); sigaction(SIGBUS, &action, 0);
        alarm(5);
        functions[i].fn(high ? UINT64_C(0xfd00000000000000)
                             : UINT64_C(0x0200000000000000), 0x1234, 100);
        _exit(1);
      }
      int status;
      if (waitpid(child, &status, 0) != child) return 4;
      unsigned expected = functions[i].control & 0x100 ? 112 : 113;
      if (!WIFEXITED(status) || WEXITSTATUS(status) != expected) return 5;
    }
  }
  free(heap);
  return 0;
}
)");
  for (const char *Opt : {"-O0", "-O2"})
    compileRunAndCheck(
        std::string("native_stack_oracle") + Opt, Source.string(),
        std::string("-std=c23 -fno-lto -fno-builtin-mimalloc ") + Opt);
#endif
}
