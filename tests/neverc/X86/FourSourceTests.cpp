//===- FourSourceTests.cpp - Native four-register arithmetic ------------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//

#include "../NeverCTestFixture.h"

#include <iterator>
#include <regex>

class X86FourSourceTest : public NeverCTest {};

namespace {
constexpr const char *Operations[] = {"v4fmaddps",  "v4fnmaddps", "v4fmaddss",
                                      "v4fnmaddss", "vp4dpwssd",  "vp4dpwssds"};
}

TEST_F(X86FourSourceTest, NativeCallsRetainEverySourceAndMaskedEffect) {
  const auto Source = tmpFile("four-source.c");
  std::string Code =
      "#ifndef __NEVERC_X86_FOUR_SOURCE__\n#error capability\n#endif\n"
      "typedef unsigned int V128 __attribute__((vector_size(16)));\n"
      "typedef unsigned int V512 __attribute__((vector_size(64)));\n";
  for (const std::string Name : Operations) {
    const bool Scalar = Name == "v4fmaddss" || Name == "v4fnmaddss";
    const std::string V = Scalar ? "V128" : "V512";
    const auto Builtin = "__builtin_ia32_" + Name + "_native";
    Code += "__attribute__((target(\"avx512f\"))) " + V + " four_" + Name +
            "(const void *p, unsigned short k, " + V + " a, " + V + " b, " + V +
            " c, " + V + " d, " + V + " e) {\n";
    Code += V + " r=" + Builtin + "(p,a,b,c,d,e,k,0,0x64);\n";
    Code += "(void)" + Builtin + "(p,a,b,c,d,e,0,1,0x65);\n";
    Code += "(void)" + Builtin + "(p,a,b,c,d,e,0xffff,0,0);\nreturn r;\n}\n";
  }
  writeFile(Source, Code);
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"})
      for (const char *Mode : {"ir", "att", "intel"}) {
        SCOPED_TRACE(Target);
        SCOPED_TRACE(Opt);
        SCOPED_TRACE(Mode);
        const bool IR = std::string(Mode) == "ir";
        const auto Out = tmpFile(IR ? "four-source.ll" : "four-source.s");
        std::vector<std::string> Args = {std::string("--target=") + Target,
                                         Opt,
                                         "-Werror",
                                         "-fno-builtin-mimalloc",
                                         "-fno-lto",
                                         "-ffreestanding",
                                         "-mllvm",
                                         "-verify-machineinstrs",
                                         "-S",
                                         Source.string(),
                                         "-o",
                                         Out.string()};
        Args.push_back(IR ? "-emit-llvm" : std::string("-masm=") + Mode);
        const auto R = ncc(Args);
        ASSERT_EQ(R.exitCode, 0) << R.err;
        const auto Text = readFile(Out);
        EXPECT_EQ(Text.find(IR ? " asm " : "#APP"), std::string::npos);
        for (const std::string Name : Operations) {
          if (IR) {
            EXPECT_NE(Text.find("@llvm.x86.avx512." + Name + ".native("),
                      std::string::npos)
                << Text;
          } else {
            const std::regex Query("\\t" + Name + "[ \\t]");
            EXPECT_EQ(std::distance(
                          std::sregex_iterator(Text.begin(), Text.end(), Query),
                          std::sregex_iterator()),
                      3)
                << Text;
          }
        }
      }
}

TEST_F(X86FourSourceTest, InvalidControlsFailBeforeCodeGeneration) {
  for (const std::string Controls : {"k,2,0", "k,k,0", "k,0,1", "k,0,k"}) {
    const auto Source = tmpFile("invalid-four-source.c");
    writeFile(Source,
              "typedef unsigned int V __attribute__((vector_size(16)));\n"
              "__attribute__((target(\"avx512f\"))) V f(const void *p, "
              "unsigned short k, V v) { return "
              "__builtin_ia32_v4fmaddss_native(p,v,v,v,v,v," +
                  Controls + "); }\n");
    const auto R = ncc({"--target=x86_64-linux-gnu", "-fno-builtin-mimalloc",
                        "-fno-lto", "-ffreestanding", "-c", Source.string(),
                        "-o", tmpFile("invalid.o").string()});
    EXPECT_NE(R.exitCode, 0) << Controls << R.err;
    EXPECT_EQ(R.err.find("PLEASE submit a bug report"), std::string::npos)
        << R.err;
  }
}
