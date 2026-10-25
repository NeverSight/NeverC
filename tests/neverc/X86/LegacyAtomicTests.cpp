//===- LegacyAtomicTests.cpp - Legacy RAO and VEX atomics ---------------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//

#include "../NeverCTestFixture.h"

#include <iterator>
#include <regex>

class X86LegacyAtomicTest : public NeverCTest {};

TEST_F(X86LegacyAtomicTest, NativeCallsKeepEncodingAndDeadEffects) {
  const auto Source = tmpFile("legacy-atomic.c");
  std::string Code =
      "#ifndef __NEVERC_X86_LEGACY_ATOMIC__\n#error capability\n#endif\n";
  for (unsigned Bits : {32u, 64u}) {
    const std::string Word = Bits == 32 ? "unsigned" : "unsigned long long";
    for (const std::string Op : {"aadd", "aand", "aor", "axor"}) {
      const auto Name = Op + std::to_string(Bits);
      Code += "void " + Name + "(unsigned long long p, " + Word + " v) {";
      for (const char *Controls : {"32,100", "64,101", "64,0"})
        Code +=
            "__builtin_ia32_" + Name + "_legacy_address(p,v," + Controls + ");";
      Code += "}\n";
    }
    const auto Name = "cmpccxadd" + std::to_string(Bits);
    Code += Word + " " + Name + "(unsigned long long p, " + Word + " a," +
            Word + " b) {";
    for (unsigned CC = 0; CC != 16; ++CC)
      Code += "(void)__builtin_ia32_" + Name + "_legacy_address(p,a,b," +
              std::to_string(CC) + ",32,100);";
    Code +=
        "return __builtin_ia32_" + Name + "_legacy_address(p,a,b,4,64,0); }\n";
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
        const auto Out = tmpFile(IR ? "legacy-atomic.ll" : "legacy-atomic.s");
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
        EXPECT_EQ(Text.find("{evex}"), std::string::npos);
        for (const std::string Op :
             {"aadd", "aand", "aor", "axor", "cmpccxadd"}) {
          if (IR) {
            for (unsigned Bits : {32u, 64u})
              EXPECT_NE(Text.find("@llvm.x86." + Op + std::to_string(Bits) +
                                  ".legacy.address("),
                        std::string::npos)
                  << Text;
          } else {
            const std::regex Query(Op == "cmpccxadd"
                                       ? "\\tcmp[a-z]+xadd[ \\t]"
                                       : "\\t" + Op + "[lq]?[ \\t]");
            EXPECT_EQ(std::distance(
                          std::sregex_iterator(Text.begin(), Text.end(), Query),
                          std::sregex_iterator()),
                      Op == "cmpccxadd" ? 34 : 6)
                << Text;
          }
        }
      }
}

TEST_F(X86LegacyAtomicTest, InvalidAddressAndConditionControlsAreDiagnosed) {
  for (const char *Call :
       {"aadd32_legacy_address(a,b,16,0)", "aand64_legacy_address(a,b,64,99)",
        "axor32_legacy_address(a,b,n,0)",
        "cmpccxadd64_legacy_address(a,b,b,16,64,0)",
        "cmpccxadd32_legacy_address(a,b,b,n,64,0)",
        "cmpccxadd64_legacy_address(a,b,b,0,64,n)"}) {
    const auto Source = tmpFile("invalid-legacy-atomic.c");
    writeFile(Source, "void f(unsigned long long a, unsigned long long b, "
                      "unsigned n) { (void)__builtin_ia32_" +
                          std::string(Call) + "; }");
    const auto R = ncc({"--target=x86_64-linux-gnu", "-fno-builtin-mimalloc",
                        "-fno-lto", "-ffreestanding", "-c", Source.string(),
                        "-o", tmpFile("invalid.o").string()});
    EXPECT_NE(R.exitCode, 0) << Call << R.err;
    EXPECT_EQ(R.err.find("PLEASE submit a bug report"), std::string::npos)
        << R.err;
  }
}
