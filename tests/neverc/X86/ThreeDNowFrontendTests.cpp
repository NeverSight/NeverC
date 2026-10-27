//===- ThreeDNowFrontendTests.cpp - Native packed 3DNow values ----------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//

#include "../NeverCTestFixture.h"

#include <regex>

class X86ThreeDNowTest : public NeverCTest {};

TEST_F(X86ThreeDNowTest, EveryBuiltinKeepsItsNativePackedOperation) {
  struct Operation {
    const char *Name;
    const char *Input;
    const char *Output;
    bool Unary;
    bool Enhanced;
  };
  const Operation Operations[] = {
      {"pavgusb", "bytes", "bytes", false, false},
      {"pf2id", "floats", "ints", true, false},
      {"pfacc", "floats", "floats", false, false},
      {"pfadd", "floats", "floats", false, false},
      {"pfcmpeq", "floats", "ints", false, false},
      {"pfcmpge", "floats", "ints", false, false},
      {"pfcmpgt", "floats", "ints", false, false},
      {"pfmax", "floats", "floats", false, false},
      {"pfmin", "floats", "floats", false, false},
      {"pfmul", "floats", "floats", false, false},
      {"pfrcp", "floats", "floats", true, false},
      {"pfrcpit1", "floats", "floats", false, false},
      {"pfrcpit2", "floats", "floats", false, false},
      {"pfrsqit1", "floats", "floats", false, false},
      {"pfrsqrt", "floats", "floats", true, false},
      {"pfsub", "floats", "floats", false, false},
      {"pfsubr", "floats", "floats", false, false},
      {"pi2fd", "ints", "floats", true, false},
      {"pmulhrw", "words", "words", false, false},
      {"pf2iw", "floats", "ints", true, true},
      {"pfnacc", "floats", "floats", false, true},
      {"pfpnacc", "floats", "floats", false, true},
      {"pi2fw", "ints", "floats", true, true},
      {"pswapdsi", "ints", "ints", true, true},
      {"pswapdsf", "floats", "floats", true, true}};
  std::string Source = R"(
typedef unsigned long long U64;
typedef char V8C __attribute__((vector_size(8)));
typedef short V4S __attribute__((vector_size(8)));
typedef int V2I __attribute__((vector_size(8)));
typedef float V2F __attribute__((vector_size(8)));
typedef union { U64 bits; V8C bytes; V4S words; V2I ints; V2F floats; } Value;
)";
  for (const auto &Op : Operations) {
    Source += "__attribute__((target(\"3dnowa\"), noinline)) U64 test_" +
              std::string(Op.Name) +
              "(U64 a, U64 b) {\n"
              "  Value left = {a}, right = {b}, out;\n"
              "  out." +
              Op.Output + " = __builtin_ia32_" + Op.Name + "(left." + Op.Input;
    if (!Op.Unary)
      Source += ", right." + std::string(Op.Input);
    Source += ");\n  U64 result = out.bits;\n  __builtin_ia32_femms();\n"
              "  return result;\n}\n";
  }
  const auto Input = tmpFile("3dnow.c");
  writeFile(Input, Source);
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"})
      for (const char *Mode : {"ir", "att", "intel"}) {
        SCOPED_TRACE(Target);
        SCOPED_TRACE(Opt);
        SCOPED_TRACE(Mode);
        const bool IR = std::string(Mode) == "ir";
        const auto Output = tmpFile(IR ? "3dnow.ll" : "3dnow.s");
        std::vector<std::string> Args = {
            std::string("--target=") + Target,
            Opt,
            "-fno-lto",
            "-fno-builtin-mimalloc",
            "-ffreestanding",
            "-Werror=implicit-function-declaration",
            "-mllvm",
            "-verify-machineinstrs",
            "-S",
            Input.string(),
            "-o",
            Output.string()};
        Args.push_back(IR ? "-emit-llvm" : std::string("-masm=") + Mode);
        const auto Result = ncc(Args);
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Text = readFile(Output);
        EXPECT_EQ(Text.find(IR ? " asm " : "#APP"), std::string::npos);
        for (const auto &Op : Operations) {
          const std::string Name = Op.Name;
          const std::string Mnemonic =
              Name.compare(0, 6, "pswapd") == 0 ? "pswapd" : Name;
          if (IR) {
            EXPECT_NE(
                Text.find("@llvm.x86." +
                          std::string(Op.Enhanced ? "3dnowa." : "3dnow.") +
                          Mnemonic + "("),
                std::string::npos)
                << Name << '\n'
                << Text;
          } else {
            const auto Pattern =
                Name == "pfsub" || Name == "pfsubr" ? "pfsubr?" : Mnemonic;
            EXPECT_TRUE(
                std::regex_search(Text, std::regex("\\t" + Pattern + "[ \\t]")))
                << Name << '\n'
                << Text;
          }
        }
      }
}
