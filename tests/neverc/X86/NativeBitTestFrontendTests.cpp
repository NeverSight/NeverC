//===- NativeBitTestFrontendTests.cpp - Native memory bit APIs ----------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//
/// \file
/// Native IR, retained effects, compatibility APIs and argument evaluation.
//===----------------------------------------------------------------------===//

#include "NativeBuiltinFixture.h"

#include <iterator>
#include <regex>
#include <string>
#include <vector>

class X86NativeBitTestFrontendTest : public X86NativeBuiltinTest {};

TEST_F(X86NativeBitTestFrontendTest,
       MemoryBitStringsUseNativeIRAndInstructions) {
  std::string Source;
  for (unsigned Bits : {16U, 32U, 64U})
    for (const char *Operation : {"bt", "bts", "btr", "btc"}) {
      const std::string Name =
          std::string(Operation) + "_mem" + std::to_string(Bits);
      const std::string Type = Bits == 16   ? "short"
                               : Bits == 32 ? "int"
                                            : "long long";
      Source += "#if !__has_builtin(__builtin_ia32_" + Name +
                ")\n#error missing bit operation\n#endif\n";
      Source += "unsigned char probe_" + Name + "(void *p," + Type +
                " i,unsigned char *marker) {\n*marker=17;\n";
      Source += "__builtin_ia32_" + Name + "(p,i);\n*marker=23;\n";
      Source += "unsigned char cf=__builtin_ia32_" + Name +
                "(p,i);\n*marker=29;return cf;\n}\n";
    }
  const auto Input = tmpFile("native-bits.c");
  writeFile(Input, Source);
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Level : {"-O0", "-O2"})
      for (bool IR : {false, true}) {
        SCOPED_TRACE(std::string(Target) + Level + (IR ? " IR" : " asm"));
        const auto Output = tmpFile(IR ? "bits.ll" : "bits.s");
        std::vector<std::string> Args{"--target=" + std::string(Target),
                                      Level,
                                      "-ffreestanding",
                                      "-fno-builtin-mimalloc",
                                      "-fno-lto",
                                      "-mllvm",
                                      "-verify-machineinstrs",
                                      "-masm=att",
                                      "-S",
                                      Input.string(),
                                      "-o",
                                      Output.string()};
        if (IR)
          Args.push_back("-emit-llvm");
        const auto Result = ncc(Args);
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Text = readFile(Output);
        EXPECT_EQ(Text.find(IR ? "asm " : "#APP"), std::string::npos) << Text;
        for (unsigned Bits : {16U, 32U, 64U})
          for (const char *Operation : {"bt", "bts", "btr", "btc"}) {
            const std::string Pattern =
                IR ? "call i8 @llvm\\.x86\\." + std::string(Operation) +
                         "\\.mem" + std::to_string(Bits) + "\\("
                   : "\\b" + std::string(Operation) +
                         (Bits == 16   ? "w"
                          : Bits == 32 ? "l"
                                       : "q") +
                         "\\s";
            const std::regex Match(Pattern);
            EXPECT_EQ(std::distance(
                          std::sregex_iterator(Text.begin(), Text.end(), Match),
                          std::sregex_iterator()),
                      2)
                << Pattern << Text;
          }
      }
}

TEST_F(X86NativeBitTestFrontendTest, MicrosoftBitsAndScansAvoidInlineAssembly) {
  const auto Input = tmpFile("microsoft-bits.c");
  writeFile(Input, R"c(
#include <intrin.h>
unsigned char bits(long *p,long i,__int64 *q,__int64 j,unsigned long *out) {
  unsigned char v=_bittest(p,i);
  v^=_bittestandset(p,i); v^=_bittestandreset(p,i); v^=_bittestandcomplement(p,i);
  v^=_bittest64(q,j);
  v^=_bittestandset64(q,j); v^=_bittestandreset64(q,j); v^=_bittestandcomplement64(q,j);
  v^=_interlockedbittestandset(p,i); v^=_interlockedbittestandreset(p,i);
  v^=_interlockedbittestandset64(q,j); v^=_interlockedbittestandreset64(q,j);
  v^=_BitScanForward(out,(unsigned long)i); v^=_BitScanReverse(out,(unsigned long)i);
  return v;
}
)c");
  for (const char *Level : {"-O0", "-O2"}) {
    const auto Output = tmpFile("microsoft-bits.ll");
    const auto Result =
        ncc({"--target=x86_64-pc-windows-msvc", Level, "-ffreestanding",
             "-fno-builtin-mimalloc", "-fno-lto", "-S", "-emit-llvm",
             Input.string(), "-o", Output.string()});
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    const auto Text = readFile(Output);
    EXPECT_EQ(Text.find("asm "), std::string::npos) << Text;
    for (const char *Name : {"bt.mem", "bts.mem", "btr.mem", "btc.mem"})
      for (unsigned Bits : {32U, 64U})
        EXPECT_NE(
            Text.find("@llvm.x86." + std::string(Name) + std::to_string(Bits)),
            std::string::npos)
            << Text;
    for (const char *Name : {"bts", "btr"})
      for (unsigned Bits : {32U, 64U})
        EXPECT_NE(Text.find("@llvm.x86.atomic." + std::string(Name) + ".rm.i" +
                            std::to_string(Bits)),
                  std::string::npos)
            << Text;
    EXPECT_NE(Text.find("@llvm.cttz.i32"), std::string::npos);
    EXPECT_NE(Text.find("@llvm.ctlz.i32"), std::string::npos);
    const auto Assembly = tmpFile("microsoft-bits.s");
    const auto Compiled = ncc(
        {"--target=x86_64-pc-windows-msvc", Level, "-ffreestanding",
         "-fno-builtin-mimalloc", "-fno-lto", "-mllvm", "-verify-machineinstrs",
         "-masm=att", "-S", Input.string(), "-o", Assembly.string()});
    ASSERT_EQ(Compiled.exitCode, 0) << Compiled.err;
    const auto Asm = readFile(Assembly);
    EXPECT_EQ(Asm.find("#APP"), std::string::npos) << Asm;
    const std::regex Locks("\\block\\s+bt[sr][lq]\\s");
    EXPECT_EQ(std::distance(std::sregex_iterator(Asm.begin(), Asm.end(), Locks),
                            std::sregex_iterator()),
              4)
        << Asm;
  }
}

TEST_F(X86NativeBitTestFrontendTest, EvaluatesMemoryAndIndexOnce) {
  const auto Input = tmpFile("bit-arguments.c");
  writeFile(Input, R"c(
void *base(void); int index(void);
unsigned char test(void) { return __builtin_ia32_bts_mem32(base(),index()); }
)c");
  const auto Output = tmpFile("bit-arguments.ll");
  const auto Result =
      ncc({"--target=x86_64-unknown-linux-gnu", "-O0", "-ffreestanding",
           "-fno-builtin-mimalloc", "-fno-lto", "-S", "-emit-llvm",
           Input.string(), "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  const auto Text = readFile(Output);
  for (const char *Name : {"base", "index"}) {
    const std::regex Call("call [^\\n]*@" + std::string(Name) + "\\(");
    EXPECT_EQ(
        std::distance(std::sregex_iterator(Text.begin(), Text.end(), Call),
                      std::sregex_iterator()),
        1)
        << Text;
  }
}
