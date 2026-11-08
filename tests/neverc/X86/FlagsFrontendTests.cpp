//===- FlagsFrontendTests.cpp - Exact-width flag images -----------------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//
/// \file
/// Flag builtins retain width, effects and the compiler stack boundary.
//===----------------------------------------------------------------------===//

#include "NativeBuiltinFixture.h"

#include <regex>
#include <tuple>

class X86FlagsFrontendTest : public X86NativeBuiltinTest {};

TEST_F(X86FlagsFrontendTest, WordFlagsUseNativeIRAndTwoByteStackOperations) {
  const auto Source = tmpFile("flags16.c");
  writeFile(Source, R"(
    #if !__has_builtin(__builtin_ia32_readeflags_u16) || \
        !__has_builtin(__builtin_ia32_writeeflags_u16)
    #error Word FLAGS builtins are missing
    #endif
    unsigned short read_flags(void) {
      return __builtin_ia32_readeflags_u16();
    }
    void write_flags(unsigned short flags) {
      __builtin_ia32_writeeflags_u16(flags);
    }
    void unused_reads(void) {
      (void)__builtin_ia32_readeflags_u16();
      (void)__builtin_ia32_readeflags_u16();
    }
    unsigned short ordered(unsigned *p, unsigned short flags) {
      *p = 17;
      __builtin_ia32_writeeflags_u16(flags);
      *p = 34;
      return __builtin_ia32_readeflags_u16();
    }
  )");
  for (const char *Target :
       {"x86_64-unknown-linux-gnu", "x86_64-pc-windows-msvc",
        "x86_64-apple-darwin", "i386-unknown-linux-gnu"})
    for (const char *Opt : {"-O0", "-O2"})
      for (bool IR : {false, true}) {
        if (!IR && std::string(Target).rfind("i386", 0) == 0)
          continue; // The full LLVM backend owns i386 machine emission.
        SCOPED_TRACE(Target);
        SCOPED_TRACE(Opt);
        SCOPED_TRACE(IR);
        const auto Output = tmpFile(IR ? "flags16.ll" : "flags16.s");
        std::vector<std::string> Args{std::string("--target=") + Target,
                                      Opt,
                                      "-ffreestanding",
                                      "-fno-builtin-mimalloc",
                                      "-fno-lto",
                                      "-masm=att",
                                      "-mllvm",
                                      "-verify-machineinstrs",
                                      "-S",
                                      Source.string(),
                                      "-o",
                                      Output.string()};
        if (IR)
          Args.push_back("-emit-llvm");
        const auto R = ncc(Args);
        ASSERT_EQ(R.exitCode, 0) << R.err;
        const auto Text = readFile(Output);
        EXPECT_EQ(Text.find(IR ? " asm " : "#APP"), std::string::npos);
        for (const auto &[Pattern, Count] :
             std::vector<std::pair<std::string, unsigned>>{
                 {IR ? "call i16 @llvm.x86.flags.read.u16" : "\\tpushfw", 4},
                 {IR ? "call void @llvm.x86.flags.write.u16" : "\\tpopfw",
                  2}}) {
          const std::regex RE(Pattern);
          EXPECT_EQ(
              std::distance(std::sregex_iterator(Text.begin(), Text.end(), RE),
                            std::sregex_iterator()),
              Count)
              << Text;
        }
        EXPECT_EQ(Text.find(IR ? "llvm.x86.flags.write.u64" : "\tpopfq"),
                  std::string::npos)
            << Text;
        if (IR) {
          const auto FirstStore = Text.find("store i32 17");
          const auto Write =
              Text.find("call void @llvm.x86.flags.write.u16", FirstStore);
          const auto SecondStore = Text.find("store i32 34", Write);
          const auto Read =
              Text.find("call i16 @llvm.x86.flags.read.u16", SecondStore);
          EXPECT_LT(FirstStore, Write);
          EXPECT_LT(Write, SecondStore);
          EXPECT_LT(SecondStore, Read);
          EXPECT_NE(Read, std::string::npos);
        }
      }
}

TEST_F(X86FlagsFrontendTest, NativeAndGenericFlagsFollowCodeMode) {
  const auto Source = tmpFile("native_flags.c");
  writeFile(Source, R"(
    #if defined(USE_HEADER)
    #include <x86intrin.h>
    #if defined(_MSC_VER)
    #include <intrin.h>
    #endif
    #endif
    #ifdef __i386__
    typedef unsigned image_t;
    #define READ __builtin_ia32_readeflags_u32
    #define WRITE __builtin_ia32_writeeflags_u32
    #if !__has_builtin(__builtin_ia32_readeflags_u32) || \
        !__has_builtin(__builtin_ia32_writeeflags_u32)
    #error Native EFLAGS builtins are missing
    #endif
    #else
    typedef unsigned long long image_t;
    #define READ __builtin_ia32_readeflags_u64
    #define WRITE __builtin_ia32_writeeflags_u64
    #if !__has_builtin(__builtin_ia32_readeflags_u64) || \
        !__has_builtin(__builtin_ia32_writeeflags_u64)
    #error Native RFLAGS builtins are missing
    #endif
    #endif
    _Static_assert(sizeof(READ()) == sizeof(image_t), "native image width");
    _Static_assert(_Generic(__readeflags(), __SIZE_TYPE__: 1, default: 0),
                   "generic source ABI");
    image_t native_read(void) { return READ(); }
    void native_write(image_t value) { WRITE(value); }
    __SIZE_TYPE__ generic_read(void) { return __readeflags(); }
    void generic_write(__SIZE_TYPE__ value) { __writeeflags(value); }
    void unused_reads(void) {
      (void)__readeflags();
      (void)__readeflags();
    }
    __SIZE_TYPE__ ordered(unsigned *p, __SIZE_TYPE__ value) {
      *p = 17;
      __writeeflags(value);
      *p = 34;
      return __readeflags();
    }
  )");
  for (const char *Target :
       {"i386-unknown-linux-gnu", "x86_64-unknown-linux-gnu",
        "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"})
      for (bool Header : {false, true}) {
        SCOPED_TRACE(Target);
        SCOPED_TRACE(Opt);
        SCOPED_TRACE(Header);
        const bool I386 = std::string(Target).rfind("i386", 0) == 0;
        const auto Output = tmpFile("native_flags.ll");
        std::vector<std::string> Args = {std::string("--target=") + Target,
                                         Opt,
                                         "-ffreestanding",
                                         "-fno-builtin-mimalloc",
                                         "-fno-lto",
                                         "-Werror",
                                         "-S",
                                         "-emit-llvm",
                                         Source.string(),
                                         "-o",
                                         Output.string()};
        if (Header) {
          Args.push_back("-DUSE_HEADER");
          if (I386)
            Args.insert(Args.end(), {"-fms-extensions", "-D_MSC_VER=1930"});
        }
        const auto R = ncc(Args);
        ASSERT_EQ(R.exitCode, 0) << R.err;
        const auto Text = readFile(Output);
        const std::string Bits = I386 ? "32" : "64";
        const std::string Read =
            "call i" + Bits + " @llvm.x86.flags.read.u" + Bits;
        const std::string Write = "call void @llvm.x86.flags.write.u" + Bits;
        for (const auto &[Call, Count] :
             {std::pair{Read, 5}, std::pair{Write, 3}}) {
          unsigned Actual = 0;
          for (size_t Pos = 0;
               (Pos = Text.find(Call, Pos)) != std::string::npos;
               Pos += Call.size())
            ++Actual;
          EXPECT_EQ(Actual, Count) << Text;
        }
        EXPECT_EQ(Text.find(" asm "), std::string::npos) << Text;
        EXPECT_EQ(Text.find(I386 ? ".u64" : ".u32"), std::string::npos) << Text;
        const auto FirstStore = Text.find("store i32 17");
        const auto WritePos = Text.find(Write, FirstStore);
        const auto SecondStore = Text.find("store i32 34", WritePos);
        const auto ReadPos = Text.find(Read, SecondStore);
        ASSERT_NE(ReadPos, std::string::npos) << Text;
        EXPECT_LT(FirstStore, WritePos);
        EXPECT_LT(WritePos, SecondStore);
        EXPECT_LT(SecondStore, ReadPos);
      }
}

TEST_F(X86FlagsFrontendTest, ExactWidthsRejectWrongModesAndArguments) {
  for (const auto &[Target, Call, Diagnostic] :
       {std::tuple{"x86_64-unknown-linux-gnu",
                   "__builtin_ia32_readeflags_u32()", "32-bit x86"},
        {"x86_64-pc-windows-msvc", "__builtin_ia32_writeeflags_u32(0)",
         "32-bit x86"},
        {"i386-unknown-linux-gnu", "__builtin_ia32_readeflags_u64()",
         "undeclared identifier"},
        {"i386-unknown-linux-gnu", "__builtin_ia32_writeeflags_u64(0)",
         "undeclared identifier"},
        {"i386-unknown-linux-gnu", "__builtin_ia32_readeflags_u32(0)",
         "too many arguments"},
        {"i386-unknown-linux-gnu", "__builtin_ia32_writeeflags_u32()",
         "too few arguments"},
        {"i386-unknown-linux-gnu", "__builtin_ia32_writeeflags_u32(0, 0)",
         "too many arguments"}}) {
    SCOPED_TRACE(Call);
    const auto Source = tmpFile("invalid_flags.c");
    writeFile(Source, std::string("void f(void) { ") + Call + "; }\n");
    const auto R = ncc({std::string("--target=") + Target, "-ffreestanding",
                        "-fsyntax-only", Source.string()});
    EXPECT_NE(R.exitCode, 0);
    EXPECT_NE(R.err.find(Diagnostic), std::string::npos) << R.err;
  }
}

TEST_F(X86FlagsFrontendTest, RejectsWrongArgumentCounts) {
  for (const char *Call :
       {"__builtin_ia32_readeflags_u16(1)", "__builtin_ia32_writeeflags_u16()",
        "__builtin_ia32_writeeflags_u16(1, 2)"}) {
    const auto Source = tmpFile("invalid_flags16.c");
    writeFile(Source, "void f(void) { (void)" + std::string(Call) + "; }");
    auto R = ncc({"--target=x86_64-unknown-linux-gnu", "-ffreestanding",
                  "-fno-builtin-mimalloc", "-fsyntax-only", Source.string()});
    EXPECT_NE(R.exitCode, 0) << R.out;
  }
}
