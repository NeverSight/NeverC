//===- I386FrontendTests.cpp - i386 frontend ------------------------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//

#include "NativeBuiltinFixture.h"

class I386FrontendTest : public X86NativeBuiltinTest {};

TEST_F(I386FrontendTest, PICOptionsOwnMacrosAndModuleFlags) {
  const auto Source = tmpFile("pic.c");
  writeFile(Source, R"(
#ifndef __PIC__
#define __PIC__ 0
#endif
#ifndef __PIE__
#define __PIE__ 0
#endif
_Static_assert(__PIC__ == EXPECT_PIC, "PIC level");
_Static_assert(__PIE__ == EXPECT_PIE, "PIE level");
extern int global;
int *address(void) { return &global; }
)");
  struct Case {
    std::vector<std::string> Flags;
    unsigned PIC, PIE;
  };
  for (const auto &C : std::vector<Case>{{{}, 2, 2},
                                         {{"-fno-pic"}, 0, 0},
                                         {{"-fpic"}, 1, 0},
                                         {{"-fPIC"}, 2, 0},
                                         {{"-fpie"}, 1, 1},
                                         {{"-fPIE"}, 2, 2},
                                         {{"-fPIC", "-fno-pie"}, 0, 0},
                                         {{"-fno-PIC", "-fpie"}, 1, 1},
                                         {{"-fpie", "-fPIC"}, 2, 0}}) {
    auto Args = C.Flags;
    const auto IR = tmpFile("pic.ll");
    Args.insert(Args.end(), {"--target=i386-unknown-linux-gnu", "-S",
                             "-emit-llvm", "-fno-builtin-mimalloc",
                             "-DEXPECT_PIC=" + std::to_string(C.PIC),
                             "-DEXPECT_PIE=" + std::to_string(C.PIE),
                             Source.string(), "-o", IR.string()});
    const auto R = ncc(Args);
    ASSERT_EQ(R.exitCode, 0) << R.err;
    const auto Text = readFile(IR);
    for (const auto &[Name, Level] :
         {std::pair{"PIC Level", C.PIC}, std::pair{"PIE Level", C.PIE}}) {
      const auto Found = Text.find(std::string("\"") + Name + "\"");
      EXPECT_EQ(Found != std::string::npos, Level != 0) << Text;
      if (Level)
        EXPECT_NE(Text.find(std::string("\"") + Name + "\", i32 " +
                            std::to_string(Level)),
                  std::string::npos);
    }
  }
}

TEST_F(I386FrontendTest, OwnsILP32MacrosLayoutAndCallingConvention) {
  const auto Source = tmpFile("i386.c");
  writeFile(Source, R"(
    #if !defined(__i386__) || defined(__x86_64__) || defined(__amd64__)
    #error Wrong architecture
    #endif
    #if defined(__SSE2__) || defined(__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8)
    #error i386 must not inherit long-mode CPU features
    #endif
    _Static_assert(sizeof(void *) == 4 && sizeof(long) == 4, "ILP32");
    _Static_assert(sizeof(long double) == 12, "x87 storage");
    _Static_assert(_Alignof(double) == 4 && _Alignof(long long) == 4, "alignment");
    _Static_assert(sizeof(__builtin_va_list) == 4, "varargs");
    struct S { int x; double y; };
    _Static_assert(sizeof(struct S) == 12, "record layout");
    struct S record(struct S v) { return v; }
    long double extended(long double a) { return a; }
    int __attribute__((stdcall)) cleanup(int x) { return x; }
  )");
  for (const char *Opt : {"-O0", "-O2"}) {
    const auto IR = tmpFile("i386.ll");
    auto R = ncc({"--target=i386-unknown-linux-gnu", Opt, "-ffreestanding",
                  "-fno-builtin-mimalloc", "-S", "-emit-llvm", Source.string(),
                  "-o", IR.string()});
    ASSERT_EQ(R.exitCode, 0) << R.err;
    const auto Text = readFile(IR);
    EXPECT_NE(Text.find("p:32:32"), std::string::npos);
    EXPECT_NE(Text.find("sret(%struct.S)"), std::string::npos);
    EXPECT_NE(Text.find("byval(%struct.S) align 4"), std::string::npos);
    EXPECT_NE(Text.find("x86_fp80 @extended"), std::string::npos);
    EXPECT_NE(Text.find("x86_stdcallcc i32 @cleanup"), std::string::npos);
  }
}

TEST_F(I386FrontendTest, BoundRetainsNativeCallsEffectsAndCompleteIndex) {
  const auto Source = tmpFile("bound.c");
  writeFile(Source, R"(
    #if !__has_builtin(__builtin_ia32_bound16) || \
        !__has_builtin(__builtin_ia32_bound32_address32)
    #error Missing BOUND
    #endif
    unsigned f(unsigned x, void *p, unsigned *marker) {
      *marker = 1;
      __builtin_ia32_bound16((short)x, p);
      __builtin_ia32_bound32((int)x, p);
      __builtin_ia32_bound16_address32((short)x, (unsigned)p, 16, 0x64);
      __builtin_ia32_bound32_address32((int)x, (unsigned)p, 32, 0x36);
      __builtin_ia32_bound32((int)x, p);
      *marker = 2;
      return x;
    }
  )");
  for (const char *Opt : {"-O0", "-O2"}) {
    const auto IR = tmpFile("bound.ll");
    const auto R = ncc({"--target=i386-unknown-linux-gnu", Opt,
                        "-ffreestanding", "-fno-builtin-mimalloc", "-S",
                        "-emit-llvm", Source.string(), "-o", IR.string()});
    ASSERT_EQ(R.exitCode, 0) << R.err;
    const auto Text = readFile(IR);
    EXPECT_EQ(Text.find("asm "), std::string::npos);
    EXPECT_EQ(Text.find("memory(none)"), std::string::npos);
    for (const char *Name :
         {"bound16", "bound32", "bound16.address32", "bound32.address32"})
      EXPECT_NE(Text.find(std::string("call void @llvm.x86.") + Name + "("),
                std::string::npos);
    EXPECT_NE(Text.find("ret i32"), std::string::npos);
  }
}

TEST_F(I386FrontendTest, RefusesUnrestoredMachineBackendAndOtherPlatforms) {
  const auto Source = tmpFile("source.c");
  writeFile(Source, "int f(int x) { return x + 1; }");
  for (const char *Mode : {"-c", "-S"}) {
    auto R = ncc({"--target=i386-unknown-linux-gnu", Mode, Source.string(),
                  "-o", tmpFile("unavailable.o").string()});
    EXPECT_NE(R.exitCode, 0);
    EXPECT_NE(R.err.find("full LLVM fork"), std::string::npos) << R.err;
  }
  for (const char *Triple :
       {"i386-pc-windows-msvc", "i386-apple-macosx", "i386-linux-android29"}) {
    auto R = ncc(
        {std::string("--target=") + Triple, "-fsyntax-only", Source.string()});
    EXPECT_NE(R.exitCode, 0);
  }
}

TEST_F(I386FrontendTest, RejectsLongModeBoundAndInvalidAddressControls) {
  for (const auto &Case : std::vector<std::pair<std::string, std::string>>{
           {"x86_64", "__builtin_ia32_bound32(x, (void *)0)"},
           {"i386", "__builtin_ia32_bound32_address32(x, 0, 64, 0)"},
           {"i386", "__builtin_ia32_bound16_address32(x, 0, 16, 0x66)"},
           {"i386", "__builtin_ia32_bound16_address32(x, 0, x, 0)"}}) {
    const auto Source = tmpFile("bad-bound.c");
    writeFile(Source, "void f(int x) { " + Case.second + "; }");
    auto R = ncc({"--target=" + Case.first + "-unknown-linux-gnu",
                  "-fsyntax-only", Source.string()});
    EXPECT_NE(R.exitCode, 0) << Case.second;
  }
}

TEST_F(I386FrontendTest, RejectsLegacyCPUInLongMode) {
  const auto Source = tmpFile("legacy-cpu.c");
  writeFile(Source, "int f(void) { return 1; }");
  auto R = ncc({"--target=x86_64-unknown-linux-gnu", "-march=i386",
                "-fsyntax-only", Source.string()});
  EXPECT_NE(R.exitCode, 0);
  EXPECT_NE(R.err.find("unknown target CPU"), std::string::npos) << R.err;
}

TEST_F(I386FrontendTest, EmitsNativeArplWithOrderedStatusStore) {
  const auto Source = tmpFile("arpl.c");
  writeFile(Source, R"(
unsigned short reg(unsigned short a, unsigned short b, unsigned char *f) {
  return __builtin_ia32_arpl_reg(a, b, f);
}
unsigned char mem(void *p, unsigned short s) {
  return __builtin_ia32_arpl_mem(p, s);
}
unsigned char address(unsigned int p, unsigned short s) {
  return __builtin_ia32_arpl_mem_address32(p, s, 16, 0x64);
}
)");
  for (const char *Level : {"-O0", "-O2"}) {
    const auto IR = tmpFile(std::string("arpl") + Level + ".ll");
    auto R = ncc({"--target=i386-unknown-linux-gnu", Level, "-S", "-emit-llvm",
                  Source.string(), "-o", IR.string()});
    ASSERT_EQ(R.exitCode, 0) << R.err;
    const auto Text = readFile(IR);
    EXPECT_EQ(Text.find("asm "), std::string::npos);
    EXPECT_EQ(Text.find("memory(none)"), std::string::npos);
    EXPECT_NE(Text.find("call { i16, i8 } @llvm.x86.arpl.reg("),
              std::string::npos);
    EXPECT_NE(Text.find("call i8 @llvm.x86.arpl.mem("), std::string::npos);
    EXPECT_NE(Text.find("call i8 @llvm.x86.arpl.mem.address32("),
              std::string::npos);
    EXPECT_NE(Text.find("store i8"), std::string::npos);
  }
}

TEST_F(I386FrontendTest, RejectsLongModeArplAndInvalidAddressControls) {
  for (const auto &Case : std::vector<std::pair<std::string, std::string>>{
           {"x86_64", "__builtin_ia32_arpl_reg(x, x, (unsigned char *)0)"},
           {"x86_64", "__builtin_ia32_arpl_mem((void *)0, x)"},
           {"x86_64", "__builtin_ia32_arpl_mem_address32(0, x, 32, 0)"},
           {"i386", "__builtin_ia32_arpl_mem_address32(0, x, 64, 0)"},
           {"i386", "__builtin_ia32_arpl_mem_address32(0, x, 16, 0x66)"},
           {"i386", "__builtin_ia32_arpl_mem_address32(0, x, x, 0)"},
           {"i386", "__builtin_ia32_arpl_mem_address32(0, x, 16, x)"}}) {
    const auto Source = tmpFile("bad-arpl.c");
    writeFile(Source, "void f(int x) { " + Case.second + "; }");
    auto R = ncc({"--target=" + Case.first + "-unknown-linux-gnu",
                  "-fsyntax-only", Source.string()});
    EXPECT_NE(R.exitCode, 0) << Case.second;
  }
}

TEST_F(I386FrontendTest, IntrinsicHeadersAdmitBothX86Modes) {
  const auto Source = tmpFile("headers.c");
  writeFile(Source, R"(
#include <immintrin.h>
#include <x86intrin.h>
#include <ammintrin.h>
#include <nmmintrin.h>
#include <adcintrin.h>
#if !defined(__NEVERSIGHT_X86_HRESET__) || __NEVERSIGHT_X86_HRESET__ != 1
#error Native HRESET availability must not require global CPU features
#endif
#if defined(__i386__) && defined(_tile_loadd)
#error AMX requires long mode
#elif defined(__x86_64__) && !defined(_tile_loadd)
#error Missing AMX header interfaces
#endif
_Static_assert(sizeof(__m64) == 8, "MMX width");
_Static_assert(sizeof(__m128) == 16, "SSE width");
void marker(void) { __SSC_MARK(4); }
)");
  for (const char *Triple : {"i386", "x86_64"}) {
    const auto R =
        ncc({std::string("--target=") + Triple + "-unknown-linux-gnu",
             "-std=c23", "-Werror", "-fsyntax-only", Source.string()});
    EXPECT_EQ(R.exitCode, 0) << R.err;
  }
}

TEST_F(I386FrontendTest, HeaderHresetUsesNativeEffectfulCalls) {
  const auto Source = tmpFile("hreset-header.c");
  writeFile(Source, R"(
#include <x86gprintrin.h>
void reset(unsigned x, volatile unsigned *marker) {
  *marker = 1;
  _hreset(x);
  __builtin_ia32_hreset(x + 1);
  *marker = 2;
}
)");
  for (const char *Triple : {"i386", "x86_64"})
    for (const char *Opt : {"-O0", "-O2"}) {
      const auto IR = tmpFile("hreset-header.ll");
      const auto R =
          ncc({std::string("--target=") + Triple + "-unknown-linux-gnu", Opt,
               "-mhreset", "-ffreestanding", "-fno-builtin-mimalloc", "-S",
               "-emit-llvm", Source.string(), "-o", IR.string()});
      ASSERT_EQ(R.exitCode, 0) << R.err;
      const auto Text = readFile(IR);
      const auto First = Text.find("call void @llvm.x86.hreset(");
      ASSERT_NE(First, std::string::npos) << Text;
      const auto Second = Text.find("call void @llvm.x86.hreset(", First + 1);
      ASSERT_NE(Second, std::string::npos) << Text;
      EXPECT_LT(Text.find("store volatile i32 1"), First);
      EXPECT_LT(Second, Text.find("store volatile i32 2"));
      EXPECT_EQ(Text.find(" asm "), std::string::npos) << Text;
      EXPECT_EQ(Text.find("memory(none)"), std::string::npos) << Text;
    }
}
