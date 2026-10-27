//===- MMXStateFrontendTests.cpp - Native MMX state transitions ---------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//

#include "../NeverCTestFixture.h"

#include <iterator>
#include <regex>

class X86MMXStateTest : public NeverCTest {};

TEST_F(X86MMXStateTest, FemmsKeepsNativeIdentityAndRepeatedStateEffects) {
  const auto Source = tmpFile("mmx-state.c");
  writeFile(Source, R"(
__attribute__((target("3dnow")))
void reset_mmx(unsigned *p, unsigned value) {
  *p = value;
  __builtin_ia32_femms();
  __builtin_ia32_femms();
  *p = 0;
}
)");
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"})
      for (const char *Mode : {"ir", "att", "intel"}) {
        SCOPED_TRACE(Target);
        SCOPED_TRACE(Opt);
        SCOPED_TRACE(Mode);
        const bool IR = std::string(Mode) == "ir";
        const auto Output = tmpFile(IR ? "mmx-state.ll" : "mmx-state.s");
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
        Args.push_back(IR ? "-emit-llvm" : std::string("-masm=") + Mode);
        const auto Result = ncc(Args);
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Text = readFile(Output);
        EXPECT_EQ(Text.find(IR ? " asm " : "#APP"), std::string::npos);
        const std::regex Operation(IR ? "call void @llvm\\.x86\\.mmx\\.femms\\("
                                      : "\\tfemms[ \\t]*[\\r\\n]");
        EXPECT_EQ(std::distance(
                      std::sregex_iterator(Text.begin(), Text.end(), Operation),
                      std::sregex_iterator()),
                  2)
            << Text;
        if (IR) {
          const auto First = Text.find("call void @llvm.x86.mmx.femms");
          const auto Last = Text.rfind("call void @llvm.x86.mmx.femms");
          EXPECT_LT(Text.find("store i32"), First) << Text;
          EXPECT_NE(Text.find("store i32 0", Last), std::string::npos) << Text;
        }
      }
}
