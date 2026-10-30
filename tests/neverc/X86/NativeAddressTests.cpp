#include "NativeBuiltinFixture.h"

class X86NativeAddressTest : public X86NativeBuiltinTest {};

TEST_F(X86NativeAddressTest, MpxBoundsMemoryRetainsStackProvenanceAndFrames) {
  const auto Source = tmpFile("mpx_stack_memory.c");
  writeFile(Source, R"(
#if __NEVERC_X86_MPX_STACK_MEMORY__ != 1
#error "missing MPX stack-memory capability"
#endif
unsigned probe(unsigned long long p, unsigned n) {
  _Alignas(64) volatile unsigned char fixed[64];
  volatile unsigned char *dynamic = __builtin_alloca(n);
  fixed[0] = 33; dynamic[0] = 44;
  __builtin_ia32_bndmov_load(0, p, 64, 0x100);
  __builtin_ia32_bndmov_store(1, p, 32, 0x126);
  __builtin_ia32_bndmov_store_lock(2, p, 64, 0x136);
  return fixed[0] + dynamic[0];
}
)");
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      const auto Output = tmpFile("mpx_stack_memory.s");
      const auto Result =
          ncc({Target, Opt, "-fno-lto", "-fno-builtin-mimalloc",
               "-ffreestanding", "-Werror", "-mllvm", "-verify-machineinstrs",
               "-masm=att", "-S", Source.string(), "-o", Output.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Assembly = readFile(Output);
      EXPECT_EQ(Assembly.find("#APP"), std::string::npos);
      EXPECT_NE(Assembly.find("(%rbp,%"), std::string::npos) << Assembly;
      EXPECT_NE(Assembly.find("%es:(%ebp,%"), std::string::npos) << Assembly;
      EXPECT_NE(Assembly.find("%ss:(%rbp,%"), std::string::npos) << Assembly;
      const auto IRFile = tmpFile("mpx_stack_memory.ll");
      const auto IR =
          ncc({Target, Opt, "-fno-builtin-mimalloc", "-S", "-emit-llvm",
               Source.string(), "-o", IRFile.string()});
      ASSERT_EQ(IR.exitCode, 0) << IR.err;
      const auto Text = readFile(IRFile);
      EXPECT_EQ(Text.find(" asm "), std::string::npos);
      EXPECT_NE(Text.find("@llvm.x86.bndmov.load("), std::string::npos);
      EXPECT_NE(Text.find("@llvm.x86.bndmov.store("), std::string::npos);
      EXPECT_NE(Text.find("@llvm.x86.bndmov.store.lock("), std::string::npos);
    }
}

TEST_F(X86NativeAddressTest, StackTileMemoryOwnsItsFrameAndNativeStride) {
  const auto Source = tmpFile("amx_stack_memory.c");
  writeFile(Source, R"(
#if __NEVERC_X86_AMX_STACK_MEMORY__ != 1
#error "missing AMX stack-memory capability"
#endif
unsigned long long probe(unsigned long long, unsigned long long, unsigned);
unsigned long long caller(unsigned long long p, unsigned long long stride,
                           unsigned n) {
  return probe(p, stride, n);
}
__attribute__((target("amx-tile,amx-movrs,egpr")))
unsigned long long probe(unsigned long long p, unsigned long long stride,
                         unsigned n) {
  _Alignas(64) volatile unsigned long long fixed[8];
  volatile char *dynamic = __builtin_alloca(n);
  fixed[0] = 0x123456789abcdef0ULL;
  dynamic[0] = 42;
  __builtin_ia32_tileloadd64_address(0, p, stride, 64, 0x100, 0);
  __builtin_ia32_tileloaddt164_address(1, p, stride, 64, 0x126, 1);
  __builtin_ia32_tileloaddrs64_address(2, p, stride, 64, 0x12e, 0);
  __builtin_ia32_tileloaddrst164_address(3, p, stride, 64, 0x136, 1);
  __builtin_ia32_tilestored64_address(7, p, stride, 64, 0x13e, 0);
  return fixed[0] ^ dynamic[0];
}
)");
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      const auto AssemblyFile = tmpFile("amx_stack_memory.s");
      const auto Result = ncc({Target, Opt, "-fno-lto", "-fno-builtin-mimalloc",
                               "-ffreestanding", "-Werror", "-mllvm",
                               "-verify-machineinstrs", "-masm=att", "-S",
                               Source.string(), "-o", AssemblyFile.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Assembly = readFile(AssemblyFile);
      EXPECT_EQ(Assembly.find("#APP"), std::string::npos);
      EXPECT_NE(Assembly.find("__llvm_x86_amx_stack"), std::string::npos);
      for (const char *Name : {"tileloadd\t", "tileloaddt1\t", "tileloaddrs\t",
                               "tileloaddrst1\t", "tilestored\t"}) {
        const auto Start = Assembly.find(Name);
        ASSERT_NE(Start, std::string::npos) << Name << Assembly;
        const auto Line =
            Assembly.substr(Start, Assembly.find('\n', Start) - Start);
        EXPECT_NE(Line.find("(%rbp,%rcx)"), std::string::npos) << Line;
      }
      const auto IRFile = tmpFile("amx_stack_memory.ll");
      const auto IR =
          ncc({Target, Opt, "-fno-builtin-mimalloc", "-S", "-emit-llvm",
               Source.string(), "-o", IRFile.string()});
      ASSERT_EQ(IR.exitCode, 0) << IR.err;
      EXPECT_EQ(readFile(IRFile).find(" asm "), std::string::npos);
    }
}

TEST_F(X86NativeAddressTest, DefinitionRefreshesTargetFeaturesAfterEarlierUse) {
  const auto Source = tmpFile("target_redeclaration.c");
  writeFile(Source, R"(
unsigned ordinary(unsigned);
__attribute__((target("bmi2"))) unsigned inherited(unsigned);
unsigned late(unsigned);
unsigned disabled(unsigned);
unsigned use(unsigned v) {
  return ordinary(v) + inherited(v) + late(v) + disabled(v);
}
unsigned ordinary(unsigned v) { return v + 1; }
unsigned inherited(unsigned v) { return v + 2; }
__attribute__((target("arch=haswell,tune=skylake")))
unsigned late(unsigned v) { return v + 3; }
__attribute__((target("no-avx2")))
unsigned disabled(unsigned v) { return v + 4; }
)");
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"}) {
    SCOPED_TRACE(Target);
    const auto IRFile = tmpFile("target_redeclaration.ll");
    const auto Result =
        ncc({Target, "-O0", "-mavx2", "-fno-builtin-mimalloc", "-S",
             "-emit-llvm", Source.string(), "-o", IRFile.string()});
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    const auto IR = readFile(IRFile);
    auto Attributes = [&](const char *Name) {
      for (auto Start = IR.find("\ndefine "); Start != std::string::npos;
           Start = IR.find("\ndefine ", Start + 1)) {
        const auto Line = IR.substr(Start, IR.find('\n', Start + 1) - Start);
        if (Line.find(std::string("@") + Name + "(") == std::string::npos)
          continue;
        const auto Group = Line.find('#');
        if (Group == std::string::npos)
          break;
        const auto End = Line.find(' ', Group);
        const auto Attr =
            IR.find("attributes " + Line.substr(Group, End - Group) + " =");
        return Attr == std::string::npos
                   ? std::string()
                   : IR.substr(Attr, IR.find('\n', Attr) - Attr);
      }
      return std::string();
    };
    EXPECT_NE(Attributes("ordinary").find("+avx2"), std::string::npos);
    EXPECT_NE(Attributes("inherited").find("+bmi2"), std::string::npos);
    EXPECT_NE(Attributes("late").find("\"target-cpu\"=\"haswell\""),
              std::string::npos);
    EXPECT_NE(Attributes("late").find("\"tune-cpu\"=\"skylake\""),
              std::string::npos);
    EXPECT_NE(Attributes("disabled").find("-avx2"), std::string::npos);
    EXPECT_EQ(Attributes("disabled").find("+avx2"), std::string::npos);
    EXPECT_NE(Attributes("use").find("+avx2"), std::string::npos);
    EXPECT_EQ(Attributes("use").find("\"target-cpu\"=\"haswell\""),
              std::string::npos);
  }
}

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
        "__builtin_ia32_tileloadd64_address(0, p, 64, 32, 0x100, 0)",
        "__builtin_ia32_tileloadd64_address(0, p, 64, 64, 0x164, 0)",
        "__builtin_ia32_tilestored64_address(0, p, 64, 64, 0x165, 0)",
        "__builtin_ia32_bndmov_load(0, p, 16, 0x100)",
        "__builtin_ia32_bndmov_store(0, p, 32, 0x164)",
        "__builtin_ia32_bndmov_store_lock(0, p, 64, 0x200)",
        "__builtin_ia32_bndcl_address(0, p, 64, 0x100)",
        "__builtin_ia32_bndmk(0, p, 0, 64, 0x100)",
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
