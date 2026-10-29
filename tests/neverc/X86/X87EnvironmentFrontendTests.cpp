//===- X87EnvironmentFrontendTests.cpp - Native x87 images ----------------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//

#include "../NeverCTestFixture.h"

#include <iterator>
#include <regex>
#include <string>
#include <vector>

class X86X87EnvironmentTest : public NeverCTest {};

TEST_F(X86X87EnvironmentTest,
       BothFormatsUseNativeInstructionsInEveryObjectFormat) {
  const auto Source = tmpFile("x87-environment.c");
  std::string Text;
  for (const char *Op : {"fnstenv", "fldenv", "fnsave", "frstor"})
    for (unsigned Bits : {16, 32}) {
      const std::string Name = std::string(Op) + std::to_string(Bits);
      Text += "#if !__has_builtin(__builtin_ia32_" + Name +
              ")\n#error missing builtin\n#endif\n";
      Text += "void " + Name +
              "(void *p, unsigned *marker) { *marker = 17; "
              "__builtin_ia32_" +
              Name + "(p); __builtin_ia32_" + Name + "(p); *marker = 23; }\n";
    }
  writeFile(Source, Text);
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"})
      for (bool IR : {false, true}) {
        SCOPED_TRACE(Target);
        SCOPED_TRACE(Opt);
        const auto Output = tmpFile(IR ? "environment.ll" : "environment.s");
        std::vector<std::string> Args = {std::string("--target=") + Target,
                                         Opt,
                                         "-fno-builtin-mimalloc",
                                         "-fno-lto",
                                         "-ffreestanding",
                                         "-mllvm",
                                         "-verify-machineinstrs",
                                         "-S",
                                         Source.string(),
                                         "-o",
                                         Output.string()};
        if (IR)
          Args.push_back("-emit-llvm");
        const auto Result = ncc(Args);
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Code = readFile(Output);
        EXPECT_EQ(Code.find(IR ? " asm " : "#APP"), std::string::npos);
        for (const char *Op : {"fnstenv", "fldenv", "fnsave", "frstor"})
          for (unsigned Bits : {16, 32}) {
            const std::string Name = std::string(Op) + std::to_string(Bits);
            const std::regex Pattern(
                IR ? "call void @llvm\\.x86\\." + Name + "\\(ptr"
                   : "\\t" + std::string(Op) + (Bits == 16 ? "s" : "") +
                         "[ \\t]");
            EXPECT_EQ(std::distance(std::sregex_iterator(Code.begin(),
                                                         Code.end(), Pattern),
                                    std::sregex_iterator()),
                      2)
                << Name << Code;
          }
      }
}

TEST_F(X86X87EnvironmentTest, PointerArityAndConstQualificationAreEnforced) {
  const auto Source = tmpFile("environment-signature.c");
  for (const char *Op : {"fnstenv", "fldenv", "fnsave", "frstor"})
    for (unsigned Bits : {16, 32}) {
      const std::string Name =
          "__builtin_ia32_" + std::string(Op) + std::to_string(Bits);
      for (const char *Args : {"", "1", "p, p"}) {
        SCOPED_TRACE(Name + "(" + Args + ")");
        writeFile(Source,
                  "void bad(void *p) { " + Name + "(" + Args + "); }\n");
        EXPECT_NE(ncc({"-Werror", "-fsyntax-only", Source.string()}).exitCode,
                  0);
      }
      writeFile(Source, "void access(const void *p) { " + Name + "(p); }\n");
      EXPECT_EQ(ncc({"-Werror", "-fsyntax-only", Source.string()}).exitCode ==
                    0,
                std::string(Op) == "fldenv" || std::string(Op) == "frstor");
    }
}
