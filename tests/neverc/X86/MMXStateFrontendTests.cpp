//===- MMXStateFrontendTests.cpp - Native MMX state transitions ---------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//

#include "../NeverCTestFixture.h"

#include <iterator>
#include <regex>

class X86MMXStateTest : public NeverCTest {};

TEST_F(X86MMXStateTest, FinalResetFollowsMmxOperationsAndIntegerExtraction) {
  const auto Source = tmpFile("mmx-cleanup.c");
  for (const char *Reset : {"emms", "femms"}) {
    writeFile(Source, "#define RESET __builtin_ia32_" + std::string(Reset) + R"(
#include <stdint.h>
#include <mmintrin.h>
__attribute__((target("3dnow"), noinline))
uint64_t finish(uint64_t a, uint64_t b, uint64_t *memory) {
  volatile uint64_t frame[16] __attribute__((aligned(64)));
  volatile unsigned char *dynamic = __builtin_alloca((a & 255) + 65);
  frame[0] = a;
  frame[15] = b;
  *dynamic = 0xa5;
  *memory = a;
  union { uint64_t bits; __m64 value; } left = {a}, right = {b}, result;
  __m64 sum = _mm_add_pi32(left.value, right.value);
  __builtin_ia32_femms();
  result.value = _mm_xor_si64(sum, left.value);
  uint64_t bits = result.bits;
  RESET();
  return bits ^ frame[0] ^ frame[15] ^ *dynamic;
}
)");
    for (const char *Target : {"x86_64-unknown-linux-gnu",
                               "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
      for (const char *Opt : {"-O0", "-O2"}) {
        SCOPED_TRACE(Reset);
        SCOPED_TRACE(Target);
        SCOPED_TRACE(Opt);
        const auto Output = tmpFile("mmx-cleanup.s");
        const auto Result = ncc(
            {std::string("--target=") + Target, Opt, "-fno-builtin-mimalloc",
             "-fno-lto", "-ffreestanding", "-mllvm", "-verify-machineinstrs",
             "-S", "-masm=att", Source.string(), "-o", Output.string()});
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Text = readFile(Output);
        const auto ResetAt = Text.rfind("\t" + std::string(Reset));
        ASSERT_NE(ResetAt, std::string::npos) << Text;
        ASSERT_NE(Text.find("\tpaddd"), std::string::npos) << Text;
        const std::regex Register("%mm[0-7]\\b");
        for (auto Match =
                 std::sregex_iterator(Text.begin(), Text.end(), Register);
             Match != std::sregex_iterator(); ++Match)
          EXPECT_LT(size_t(Match->position()), ResetAt) << Text;
      }
  }
}

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
