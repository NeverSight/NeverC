//===- X87ControlFrontendTests.cpp - Native x87 control words
//---------------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//

#include "../NeverCTestFixture.h"

#include <iterator>
#include <regex>
#include <string>
#include <vector>

class X86X87ControlTest : public NeverCTest {};

TEST_F(X86X87ControlTest, MemoryOperationsRetainNativeIdentityAndOrdering) {
  const auto Source = tmpFile("x87-control.c");
  writeFile(Source, R"(
#if !__has_builtin(__builtin_ia32_fnstcw) || !__has_builtin(__builtin_ia32_fldcw)
#error missing native x87 control word builtins
#endif
void step(unsigned short *out, const unsigned short *in, unsigned *marker) {
  *marker = 17;
  __builtin_ia32_fnstcw(out);
  __builtin_ia32_fldcw(in);
  __builtin_ia32_fnstcw(out);
  __builtin_ia32_fldcw(in);
  *marker = 23;
}
)");
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"})
      for (bool IR : {false, true}) {
        SCOPED_TRACE(Target);
        SCOPED_TRACE(Opt);
        const auto Output = tmpFile(IR ? "control.ll" : "control.s");
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
        const auto Text = readFile(Output);
        EXPECT_EQ(Text.find(IR ? " asm " : "#APP"), std::string::npos);
        for (const char *Operation : {"fnstcw", "fldcw"}) {
          const std::regex Pattern(IR ? "call void @llvm\\.x86\\." +
                                            std::string(Operation) + "\\(ptr"
                                      : "\\t" + std::string(Operation) +
                                            "[ \\t]");
          EXPECT_EQ(std::distance(
                        std::sregex_iterator(Text.begin(), Text.end(), Pattern),
                        std::sregex_iterator()),
                    2)
              << Text;
        }
        if (IR) {
          EXPECT_LT(Text.find("store i32 17"),
                    Text.find("call void @llvm.x86.fnstcw"));
          EXPECT_NE(Text.find("store i32 23",
                              Text.rfind("call void @llvm.x86.fldcw")),
                    std::string::npos)
              << Text;
        }
      }
}

TEST_F(X86X87ControlTest, BuiltinsRequireOneCorrectlyQualifiedPointer) {
  const auto Source = tmpFile("x87-control-signature.c");
  for (const char *Call :
       {"__builtin_ia32_fnstcw()", "__builtin_ia32_fldcw()",
        "__builtin_ia32_fnstcw(1)", "__builtin_ia32_fldcw(1)",
        "__builtin_ia32_fnstcw(p)", "__builtin_ia32_fldcw(p, p)"}) {
    SCOPED_TRACE(Call);
    writeFile(Source, "void bad(const unsigned short *p) { " +
                          std::string(Call) + "; }\n");
    EXPECT_NE(ncc({"-Werror", "-fsyntax-only", Source.string()}).exitCode, 0);
  }
}
