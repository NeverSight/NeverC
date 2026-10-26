#include "NativeBuiltinFixture.h"

class X86BMITest : public X86NativeBuiltinTest {};

TEST_F(X86BMITest, BitCountHeadersKeepZeroDefinedWithoutGlobalFeatures) {
  const auto Source = tmpFile("bitcount_headers.c");
  writeFile(Source, R"(
#include <x86intrin.h>
#if defined(__BMI__) || defined(__LZCNT__) || defined(__POPCNT__)
#error "This test requires function targets without translation-unit features"
#endif
__attribute__((target("bmi")))
unsigned short count_tz16(unsigned short x) { return _tzcnt_u16(x); }
__attribute__((target("bmi")))
unsigned count_tz32(unsigned x) { return _tzcnt_u32(x); }
__attribute__((target("bmi")))
unsigned long long count_tz64(unsigned long long x) { return _tzcnt_u64(x); }
__attribute__((target("lzcnt")))
unsigned short count_lz16(unsigned short x) { return __lzcnt16(x); }
__attribute__((target("lzcnt")))
unsigned count_lz32(unsigned x) { return _lzcnt_u32(x); }
__attribute__((target("lzcnt")))
unsigned long long count_lz64(unsigned long long x) { return _lzcnt_u64(x); }
__attribute__((target("popcnt")))
unsigned count_pop32(unsigned x) { return _mm_popcnt_u32(x); }
__attribute__((target("popcnt")))
unsigned long long count_pop64(unsigned long long x) { return _mm_popcnt_u64(x); }
)");
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      const auto AssemblyFile = tmpFile("bitcount_headers.s");
      auto Result = ncc({Target, Opt, "-mno-bmi", "-mno-lzcnt", "-mno-popcnt",
                         "-fno-lto", "-fno-builtin-mimalloc", "-ffreestanding",
                         "-Werror=implicit-function-declaration", "-mllvm",
                         "-verify-machineinstrs", "-masm=att", "-S",
                         Source.string(), "-o", AssemblyFile.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Assembly = readFile(AssemblyFile);
      EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
      EXPECT_EQ(Assembly.find("\tcall"), std::string::npos) << Assembly;
      for (const char *Operation : {"tzcnt", "lzcnt", "popcnt"})
        for (const char *Suffix : {"l", "q"})
          EXPECT_NE(
              Assembly.find("\t" + std::string(Operation) + Suffix + "\t"),
              std::string::npos)
              << Assembly;
      const auto IRFile = tmpFile("bitcount_headers.ll");
      Result = ncc({Target, Opt, "-mno-bmi", "-mno-lzcnt", "-mno-popcnt",
                    "-fno-lto", "-fno-builtin-mimalloc", "-ffreestanding", "-S",
                    "-emit-llvm", Source.string(), "-o", IRFile.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto IR = readFile(IRFile);
      EXPECT_EQ(IR.find("asm "), std::string::npos) << IR;
      for (const char *Operation : {"ctlz", "cttz"})
        for (const char *Width : {"16", "32", "64"}) {
          EXPECT_NE(IR.find("@llvm." + std::string(Operation) + ".i" + Width),
                    std::string::npos)
              << IR;
        }
      // The zero operand must return its width, never LLVM poison.
      EXPECT_EQ(IR.find("i1 true)"), std::string::npos) << IR;
      EXPECT_NE(IR.find("@llvm.ctpop.i32"), std::string::npos) << IR;
      EXPECT_NE(IR.find("@llvm.ctpop.i64"), std::string::npos) << IR;
    }
}

TEST_F(X86BMITest, HeadersExposeFunctionTargetsAcrossObjectFormats) {
  const auto Source = tmpFile("bmi_headers.c");
  std::string Code = R"(
#include <x86intrin.h>
#if defined(__BMI__) || defined(__BMI2__)
#error "This test requires function targets without translation-unit features"
#endif
)";
  for (unsigned Width : {32u, 64u}) {
    const std::string Type = Width == 32 ? "unsigned" : "unsigned long long";
    const std::string Suffix = "_u" + std::to_string(Width);
    for (const char *Operation :
         {"andn", "blsi", "blsmsk", "blsr", "bextr", "bextr2", "bextr_split",
          "bzhi", "pdep", "pext", "mulx"}) {
      const std::string Name = Operation;
      const bool Bmi2 =
          Name == "bzhi" || Name == "pdep" || Name == "pext" || Name == "mulx";
      std::string Call;
      if (Name == "bextr")
        Call = "__bextr" + Suffix + "(x, y)";
      else if (Name == "bextr_split")
        Call = "_bextr" + Suffix + "(x, (unsigned)y, count)";
      else
        Call = "_" + Name + Suffix + "(x" +
               (Name == "blsi" || Name == "blsmsk" || Name == "blsr" ? ""
                : Name == "mulx" ? ", y, high"
                                 : ", y") +
               ")";
      Code += "__attribute__((target(\"" + std::string(Bmi2 ? "bmi2" : "bmi") +
              "\"))) " + Type + " test_" + Name + Suffix + "(" + Type + " x, " +
              Type + " y, unsigned count, " + Type + " *high) { return " +
              Call + "; }\n";
    }
  }
  writeFile(Source, Code);
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      const auto AssemblyFile = tmpFile("bmi_headers.s");
      auto Result =
          ncc({Target, Opt, "-fno-lto", "-fno-builtin-mimalloc",
               "-ffreestanding", "-Werror=implicit-function-declaration",
               "-mllvm", "-verify-machineinstrs", "-masm=att", "-S",
               Source.string(), "-o", AssemblyFile.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Assembly = readFile(AssemblyFile);
      EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
      EXPECT_EQ(Assembly.find("\tcall"), std::string::npos) << Assembly;
      for (const char *Operation : {"bextr", "bzhi", "pdep", "pext"})
        for (const char *Suffix : {"l", "q"})
          EXPECT_NE(
              Assembly.find("\t" + std::string(Operation) + Suffix + "\t"),
              std::string::npos)
              << Operation << Suffix << '\n'
              << Assembly;
      if (std::string(Opt) == "-O2") {
        for (const char *Operation : {"andn", "blsi", "blsmsk", "blsr"})
          for (const char *Suffix : {"l", "q"})
            EXPECT_NE(
                Assembly.find("\t" + std::string(Operation) + Suffix + "\t"),
                std::string::npos)
                << Operation << Suffix << '\n'
                << Assembly;
        // The 32-bit multiply can use an ordinary 64-bit multiply; the full
        // 64-bit product must retain both halves without a runtime helper.
        EXPECT_NE(Assembly.find("\tmulxq\t"), std::string::npos) << Assembly;
      }
      const auto IRFile = tmpFile("bmi_headers.ll");
      Result = ncc({Target, Opt, "-fno-lto", "-fno-builtin-mimalloc",
                    "-ffreestanding", "-S", "-emit-llvm", Source.string(), "-o",
                    IRFile.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto IR = readFile(IRFile);
      EXPECT_EQ(IR.find("asm "), std::string::npos) << IR;
      for (const char *Operation : {"bextr", "bzhi", "pdep", "pext"})
        for (const char *Width : {"32", "64"})
          EXPECT_NE(
              IR.find("@llvm.x86.bmi." + std::string(Operation) + "." + Width),
              std::string::npos)
              << Operation << Width << '\n'
              << IR;
    }
}
