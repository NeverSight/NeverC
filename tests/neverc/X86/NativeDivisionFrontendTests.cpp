#include "NativeBuiltinFixture.h"

#include <iterator>
#include <regex>
#include <string>
#include <vector>

class X86NativeDivisionTest : public X86NativeBuiltinTest {};

TEST_F(X86NativeDivisionTest, FullDividendAndUnusedEffectsUseNativeIntrinsics) {
  std::string Source;
  for (unsigned Bits : {8U, 16U, 32U, 64U})
    for (const char *Kind : {"u", "s"}) {
      const std::string Name =
          std::string(Kind) + "divrem" + std::to_string(Bits);
      const std::string Type = Bits == 8    ? "unsigned char"
                               : Bits == 16 ? "unsigned short"
                               : Bits == 32 ? "unsigned int"
                                            : "unsigned long long";
      Source += "#if !__has_builtin(__builtin_ia32_" + Name +
                ")\n#error missing division\n#endif\n";
      Source += Type + " probe_" + Name + "(" + Type + " lo," + Type + " hi," +
                Type + " divisor," + Type +
                " *remainder,unsigned char *marker) {\n";
      Source += "*marker=17; " + Type + " ignored;\n__builtin_ia32_" + Name +
                "(lo,hi,divisor,&ignored);\n*marker=23;\n" + Type +
                " q=__builtin_ia32_" + Name +
                "(lo,hi,divisor,remainder);\n*marker=29;return q;\n}\n";
    }
  const auto Input = tmpFile("native-division.c");
  writeFile(Input, Source);
  for (const char *Target : {"x86_64-unknown-linux-gnu",
                             "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
    for (const char *Level : {"-O0", "-O2"})
      for (bool IR : {false, true}) {
        SCOPED_TRACE(std::string(Target) + Level + (IR ? " IR" : " asm"));
        const auto Output = tmpFile(IR ? "divide.ll" : "divide.s");
        std::vector<std::string> Flags = {"--target=" + std::string(Target),
                                          "-std=c23",
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
          Flags.push_back("-emit-llvm");
        const auto Result = ncc(Flags);
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Text = readFile(Output);
        EXPECT_EQ(Text.find(IR ? "asm " : "#APP"), std::string::npos) << Text;
        EXPECT_EQ(Text.find("__udivti3"), std::string::npos) << Text;
        for (unsigned Bits : {8U, 16U, 32U, 64U})
          for (const char *Kind : {"u", "s"}) {
            const std::string Name =
                std::string(Kind) + "divrem" + std::to_string(Bits);
            if (IR) {
              const std::string Type = "i" + std::to_string(Bits);
              const std::regex Call("call \\{ " + Type + ", " + Type +
                                    " \\} @llvm\\.x86\\." + Name + "\\(");
              EXPECT_EQ(std::distance(std::sregex_iterator(Text.begin(),
                                                           Text.end(), Call),
                                      std::sregex_iterator()),
                        2)
                  << Text;
            } else {
              const std::string Mnemonic = (Kind[0] == 's' ? "idiv" : "div") +
                                           std::string(Bits == 8    ? "b"
                                                       : Bits == 16 ? "w"
                                                       : Bits == 32 ? "l"
                                                                    : "q");
              const std::regex Instruction("\\t" + Mnemonic + "[ \\t]");
              EXPECT_EQ(
                  std::distance(std::sregex_iterator(Text.begin(), Text.end(),
                                                     Instruction),
                                std::sregex_iterator()),
                  2)
                  << Text;
            }
          }
      }
}

TEST_F(X86NativeDivisionTest, MicrosoftWideDivisionKeepsNativeOverflow) {
  const auto Input = tmpFile("ms-division.c");
  writeFile(Input, R"C(
#include <intrin.h>
void overflow(unsigned long long *remainder, unsigned *marker) {
  *marker = 17;
  (void)_udiv128(1, 0, 1, remainder);
  *marker = 29;
}
)C");
  const auto Output = tmpFile("ms-division.ll");
  const auto Result =
      ncc({"--target=x86_64-pc-windows-msvc", "-std=c23", "-O2",
           "-ffreestanding", "-fno-lto", "-fno-builtin-mimalloc", "-S",
           "-emit-llvm", Input.string(), "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.err;
  const auto Text = readFile(Output);
  EXPECT_NE(Text.find("@llvm.x86.udivrem64(i64 0, i64 1, i64 1)"),
            std::string::npos)
      << Text;
  EXPECT_EQ(Text.find("udiv i128"), std::string::npos) << Text;
  EXPECT_EQ(Text.find("memory(none)"), std::string::npos) << Text;
}

TEST_F(X86NativeDivisionTest, RejectsWrongArityAndRemainderType) {
  for (const char *Call :
       {"__builtin_ia32_udivrem8(1,2,3)", "__builtin_ia32_sdivrem16(1,2,3,p)",
        "__builtin_ia32_udivrem64(1,2,3,1)"}) {
    const auto Input = tmpFile("bad-division.c");
    writeFile(Input, "void f(unsigned *p) { " + std::string(Call) + "; }\n");
    const auto Result = ncc({"--target=x86_64-unknown-linux-gnu", "-std=c23",
                             "-Werror", "-ffreestanding", "-S", Input.string(),
                             "-o", tmpFile("bad.s").string()});
    EXPECT_NE(Result.exitCode, 0) << Result.err;
  }
}

TEST_F(X86NativeDivisionTest, EvaluatesRemainderPointerOnce) {
  for (const char *Level : {"-O0", "-O2"})
    for (unsigned Bits : {8U, 16U, 32U, 64U})
      for (const char *Kind : {"u", "s", "ms"}) {
        if (std::string(Kind) == "ms" && Bits != 64)
          continue;
        const std::string Type = Bits == 8    ? "unsigned char"
                                 : Bits == 16 ? "unsigned short"
                                 : Bits == 32 ? "unsigned int"
                                              : "unsigned long long";
        const std::string Name = std::string(Kind) == "ms"
                                     ? "_udiv128"
                                     : "__builtin_ia32_" + std::string(Kind) +
                                           "divrem" + std::to_string(Bits);
        SCOPED_TRACE(Name + Level);
        const auto Input = tmpFile("division-pointer.c");
        writeFile(Input, "#include <intrin.h>\n" + Type +
                             " *next_remainder(void);\n" + Type + " probe(" +
                             Type + " a," + Type + " b," + Type +
                             " c) { return " + Name +
                             "(a,b,c,next_remainder()); }\n");
        const auto Output = tmpFile("division-pointer.ll");
        const auto Result = ncc(
            {"--target=x86_64-pc-windows-msvc", "-std=c23", Level,
             "-ffreestanding", "-fno-lto", "-fno-builtin-mimalloc", "-S",
             "-emit-llvm", Input.string(), "-o", Output.string()});
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Text = readFile(Output);
        const std::regex Call("call[^\\n]*@next_remainder\\(");
        EXPECT_EQ(std::distance(std::sregex_iterator(Text.begin(), Text.end(), Call),
                                std::sregex_iterator()),
                  1)
            << Text;
      }
}
