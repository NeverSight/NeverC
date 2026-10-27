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
  for (const char *Header : {"", "mm3dnow.h", "x86intrin.h"}) {
    SCOPED_TRACE(Header);
    const bool Builtin = std::string(Header).empty();
    std::string Source =
        Builtin ? "" : "#include <" + std::string(Header) + ">\n";
    Source += R"(
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
                "  Value left = {a}, right = {b}, out;\n";
      if (Builtin) {
        Source += "  out." + std::string(Op.Output) + " = __builtin_ia32_" +
                  Op.Name + "(left." + Op.Input;
        if (!Op.Unary)
          Source += ", right." + std::string(Op.Input);
        Source += ");\n";
      } else {
        const std::string Name =
            std::string(Op.Name) == "pfrsqit1" ? "pfrsqrtit1" : Op.Name;
        Source += "  out.bits = (U64)_m_" + Name + "((__m64)left.bits";
        if (!Op.Unary)
          Source += ", (__m64)right.bits";
        Source += ");\n";
      }
      Source +=
          "  U64 result = out.bits;\n  " +
          std::string(Builtin ? "__builtin_ia32_femms();" : "_m_femms();") +
          "\n  return result;\n}\n";
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
          if (!IR)
            EXPECT_EQ(Text.find("\tcall"), std::string::npos) << Text;
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
              EXPECT_TRUE(std::regex_search(
                  Text, std::regex("\\t" + Pattern + "[ \\t]")))
                  << Name << '\n'
                  << Text;
            }
          }
        }
  }
}

TEST_F(X86ThreeDNowTest, HeadersRequireTheInstructionsTargetFeature) {
  for (const char *Header : {"mm3dnow.h", "x86intrin.h"})
    for (const char *Target : {"x86_64-unknown-linux-gnu",
                               "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
      for (bool Enhanced : {false, true}) {
        SCOPED_TRACE(Header);
        SCOPED_TRACE(Target);
        SCOPED_TRACE(Enhanced);
        const auto Source = tmpFile("3dnow_missing_feature.c");
        const auto Output = tmpFile("3dnow_missing_feature.s");
        writeFile(Source,
                  "#include <" + std::string(Header) + ">\n" +
                      (Enhanced ? "__attribute__((target(\"3dnow\")))\n" : "") +
                      "__m64 f(__m64 a, __m64 b) { return " +
                      (Enhanced ? "_m_pfnacc" : "_m_pfadd") + "(a, b); }\n");
        const auto Result =
            ncc({std::string("--target=") + Target, "-O2", "-ffreestanding",
                 "-fno-lto", "-fno-builtin-mimalloc", "-S", Source.string(),
                 "-o", Output.string()});
        EXPECT_NE(Result.exitCode, 0) << Result.err;
        EXPECT_NE(Result.err.find("requires target feature '" +
                                  std::string(Enhanced ? "3dnowa" : "3dnow") +
                                  "'"),
                  std::string::npos)
            << Result.err;
      }
}
