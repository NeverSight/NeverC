//===- CompressExpandTests.cpp - Native compact memory tests -------------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//

#include "../NeverCTestFixture.h"

#include <iterator>
#include <regex>

class X86CompressExpandTest : public NeverCTest {};

TEST_F(X86CompressExpandTest,
       PreservesZeroMasksDeadResultsAndInstructionKinds) {
  const auto Source = tmpFile("compact.c");
  std::string Code =
      "#ifndef __NEVERC_X86_COMPRESS_EXPAND__\n#error capability\n#endif\n";
  for (unsigned Bits : {128u, 256u, 512u}) {
    const auto V = "V" + std::to_string(Bits);
    Code += "typedef unsigned int " + V + " __attribute__((vector_size(" +
            std::to_string(Bits / 8) + ")));\n";
    for (const char *Suffix : {"b", "w", "d", "q", "ps", "pd"}) {
      const bool Bytes =
          std::string(Suffix) == "b" || std::string(Suffix) == "w";
      const std::string Features = std::string("avx512f") +
                                   (Bytes ? ",avx512bw,avx512vbmi2" : "") +
                                   (Bits == 512 ? ",evex512" : ",avx512vl");
      for (bool Expand : {false, true}) {
        const auto Name = std::string(Suffix[0] == 'p' ? "v" : "vp") +
                          (Expand ? "expand" : "compress") + Suffix +
                          std::to_string(Bits);
        const auto Builtin = "__builtin_ia32_" + Name + "_native";
        Code += "__attribute__((target(\"" + Features + "\"))) void " + Name +
                "(void *p, unsigned long long k, " + V + " v) {\n";
        Code +=
            "(void)" + Builtin + "(p,k,v," + (Expand ? "0," : "") + "0x64);\n";
        Code += "(void)" + Builtin + "(p,0,v," + (Expand ? "1," : "") +
                "0x65);\n}\n";
      }
    }
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
        const auto Out = tmpFile(IR ? "compact.ll" : "compact.s");
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
                                         Out.string()};
        Args.push_back(IR ? "-emit-llvm" : std::string("-masm=") + Mode);
        const auto R = ncc(Args);
        ASSERT_EQ(R.exitCode, 0) << R.err;
        const auto Text = readFile(Out);
        EXPECT_EQ(Text.find(IR ? " asm " : "#APP"), std::string::npos);
        if (IR) {
          for (unsigned Bits : {128u, 256u, 512u})
            for (const char *Op : {"compressstore", "expandload"})
              EXPECT_NE(Text.find("@llvm.x86.avx512." + std::string(Op) +
                                  std::to_string(Bits) + ".native("),
                        std::string::npos)
                  << Text;
        } else {
          for (const char *Suffix : {"b", "w", "d", "q", "ps", "pd"})
            for (const char *Op : {"compress", "expand"}) {
              const std::regex Query(
                  "\\t" + std::string(Suffix[0] == 'p' ? "v" : "vp") + Op +
                  Suffix + "[ \\t]");
              EXPECT_EQ(std::distance(std::sregex_iterator(Text.begin(),
                                                           Text.end(), Query),
                                      std::sregex_iterator()),
                        6)
                  << Text;
            }
        }
      }
}

TEST_F(X86CompressExpandTest, RejectsInvalidControlsAndMissingFeatures) {
  for (const char *Body :
       {"__builtin_ia32_vpcompressd128_native(p, k, v, 1)",
        "__builtin_ia32_vpcompressd128_native(p, k, v, k)",
        "__builtin_ia32_vpexpandd128_native(p, k, v, 2, 0)",
        "__builtin_ia32_vpexpandd128_native(p, k, v, k, 0)",
        "__builtin_ia32_vpexpandb128_native(p, k, v, 0, 0)"}) {
    const auto Source = tmpFile("invalid.c");
    writeFile(
        Source,
        std::string(
            "typedef unsigned int V __attribute__((vector_size(16)));\n") +
            "__attribute__((target(\"avx512f,avx512vl\")))\n" +
            "void f(void *p, unsigned long long k, V v) { (void)" + Body +
            "; }\n");
    const auto R = ncc({"--target=x86_64-linux-gnu", "-fno-builtin-mimalloc",
                        "-fno-lto", "-ffreestanding", "-c", Source.string(),
                        "-o", tmpFile("invalid.o").string()});
    EXPECT_NE(R.exitCode, 0) << Body << R.err;
    EXPECT_EQ(R.err.find("PLEASE submit a bug report"), std::string::npos)
        << R.err;
  }
}
