//===- FlagsFrontendTests.cpp - Exact-width flag images -----------------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//
/// \file
/// Word flag builtins retain width, effects and the compiler stack boundary.
//===----------------------------------------------------------------------===//

#include "NativeBuiltinFixture.h"

#include <regex>

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
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"})
      for (bool IR : {false, true}) {
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
