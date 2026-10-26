#include "NativeBuiltinFixture.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/TargetParser/Host.h"

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

TEST_F(X86BMITest, RotateAndVariableShiftHeadersProduceNativeInstructions) {
  const auto Source = tmpFile("bmi2_shifts.c");
  std::string Code = R"(
#ifdef _MSC_VER
#include <intrin.h>
#else
#include <x86intrin.h>
#endif
#if defined(__BMI2__)
#error "Function targets must work without translation-unit BMI2"
#endif
)";
  for (unsigned Width : {32u, 64u}) {
    const std::string Type = Width == 32 ? "unsigned" : "unsigned long long";
    const std::string Signed = Width == 32 ? "int" : "long long";
    for (const char *Operation : {"rorx", "sarx", "shlx", "shrx"}) {
      const bool Arithmetic = std::string(Operation) == "sarx";
      const std::string Suffix =
          (Arithmetic ? "_i" : "_u") + std::to_string(Width);
      Code += "__attribute__((target(\"bmi2\"))) " + Type + " test_" +
              Operation + Suffix + "(" + Type +
              " x, unsigned count) { return _" + Operation + Suffix + "((" +
              (Arithmetic ? Signed : Type) + ")x, " +
              (std::string(Operation) == "rorx" ? "13" : "count") + "); }\n";
    }
  }
  writeFile(Source, Code);
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      const auto AssemblyFile = tmpFile("bmi2_shifts.s");
      auto Result =
          ncc({Target, Opt, "-mno-bmi2", "-fno-lto", "-fno-builtin-mimalloc",
               "-ffreestanding", "-Werror=implicit-function-declaration",
               "-mllvm", "-verify-machineinstrs", "-masm=att", "-S",
               Source.string(), "-o", AssemblyFile.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Assembly = readFile(AssemblyFile);
      EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
      EXPECT_EQ(Assembly.find("\tcall"), std::string::npos) << Assembly;
      for (const char *Operation : {"rorx", "sarx", "shlx", "shrx"})
        for (const char *Suffix : {"l", "q"})
          EXPECT_NE(
              Assembly.find("\t" + std::string(Operation) + Suffix + "\t"),
              std::string::npos)
              << Assembly;
      const auto IRFile = tmpFile("bmi2_shifts.ll");
      Result = ncc({Target, Opt, "-mno-bmi2", "-fno-lto",
                    "-fno-builtin-mimalloc", "-ffreestanding", "-S",
                    "-emit-llvm", Source.string(), "-o", IRFile.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto IR = readFile(IRFile);
      EXPECT_EQ(IR.find("asm "), std::string::npos) << IR;
      for (const char *Width : {"32", "64"}) {
        // InstCombine may canonicalize a constant right rotation to fshl.
        EXPECT_TRUE(
            IR.find("@llvm.fshr.i" + std::string(Width)) != std::string::npos ||
            IR.find("@llvm.fshl.i" + std::string(Width)) != std::string::npos)
            << IR;
        for (const char *Operation : {"ashr", "lshr", "shl"})
          EXPECT_NE(IR.find(std::string(Operation) + " i" + Width),
                    std::string::npos)
              << IR;
      }
    }
}

TEST_F(X86BMITest, RotateImmediateAndFeatureRequirementsAreDiagnosed) {
  for (unsigned Width : {32u, 64u}) {
    const std::string Type = Width == 32 ? "unsigned" : "unsigned long long";
    const std::string Suffix = std::to_string(Width);
    for (const char *Count : {"count", "256", "-1"}) {
      const auto Source = tmpFile("invalid_rorx.c");
      writeFile(Source,
                "#include <x86intrin.h>\n__attribute__((target(\"bmi2\"))) " +
                    Type + " f(" + Type +
                    " x, unsigned count) { return _rorx_u" + Suffix + "(x, " +
                    Count + "); }\n");
      const auto Result = ncc({"-fsyntax-only", Source.string()});
      EXPECT_NE(Result.exitCode, 0) << Count;
      EXPECT_NE(Result.err.find(std::string(Count) == "count"
                                    ? "constant integer"
                                    : "outside the valid range"),
                std::string::npos)
          << Result.err;
    }
    for (const char *Operation : {"rorx", "sarx", "shlx", "shrx"}) {
      const auto Source = tmpFile("bmi2_feature.c");
      const std::string Name =
          std::string(Operation) +
          (std::string(Operation) == "sarx" ? "_i" : "_u") + Suffix;
      writeFile(Source, "#include <x86intrin.h>\n" + Type + " f(" + Type +
                            " x) { return _" + Name + "(x, 13); }\n");
      const auto Output = tmpFile("bmi2_feature.o");
      const auto Result = ncc(
          {"-mno-bmi2", "-O2", "-c", Source.string(), "-o", Output.string()});
      EXPECT_NE(Result.exitCode, 0) << Name;
      EXPECT_NE(Result.err.find("bmi2"), std::string::npos) << Result.err;
    }
  }
}

TEST_F(X86BMITest, RotateAndShiftsMatchBitLoopsAndEvaluateArgumentsOnce) {
  llvm::StringMap<bool, llvm::MallocAllocator> Features;
  if (!llvm::sys::getHostCPUFeatures(Features) || !Features.lookup("bmi2"))
    GTEST_SKIP() << "native execution requires BMI2";
  std::string Code = R"(
#include <immintrin.h>
#include <stdint.h>
__attribute__((noinline))
uint64_t reference(unsigned operation, uint64_t x, unsigned count, unsigned width) {
  uint64_t result = 0;
  count %= width;
  for (unsigned bit = 0; bit < width; ++bit) {
    int source = operation == 2 ? (int)bit - (int)count : (int)(bit + count);
    unsigned present;
    if (operation == 0) present = (x >> ((unsigned)source % width)) & 1;
    else if (source < 0) present = 0;
    else if ((unsigned)source >= width) present = operation == 1 && ((x >> (width - 1)) & 1);
    else present = (x >> source) & 1;
    result |= (uint64_t)present << bit;
  }
  return result;
}
)";
  for (unsigned Width : {32u, 64u}) {
    const std::string W = std::to_string(Width);
    const std::string Type = Width == 32 ? "unsigned" : "unsigned long long";
    Code += "__attribute__((target(\"bmi2\"),noinline)) " + Type + " rotate" +
            W + "(" + Type + " x, unsigned n) { switch (n & 255) {\n";
    for (unsigned Count = 0; Count != 256; ++Count)
      Code += "case " + std::to_string(Count) + ": return _rorx_u" + W +
              "(x, " + std::to_string(Count) + ");\n";
    Code += "} return 0; }\n";
    Code += "__attribute__((target(\"bmi2\"),noinline)) " + Type + " shift" +
            W + "(unsigned op, " + Type +
            " x, unsigned n) { if (op == 1) return _sarx_i" + W + "((" +
            (Width == 32 ? "int" : "long long") +
            ")x, n); if (op == 2) return _shlx_u" + W +
            "(x, n); return _shrx_u" + W + "(x, n); }\n";
  }
  Code += R"(
__attribute__((target("bmi2"))) int once(void) {
  unsigned x = 1, n = 32;
  if (_shlx_u32(x++, n++) != 1 || x != 2 || n != 33) return 1;
  x = 0x80000000; n = 32;
  if (_sarx_i32((int)x++, n++) != (int)0x80000000 || x != 0x80000001 || n != 33) return 2;
  unsigned long long y = 1; n = 64;
  if (_shrx_u64(y++, n++) != 1 || y != 2 || n != 65) return 3;
  y = 1;
  if (_rorx_u64(y++, 64) != 1 || y != 2) return 4;
  return 0;
}
int main(void) {
  const uint64_t fixed[] = {0, 1, UINT64_MAX, 0x8000000000000000ULL, 0x80000000, 0xfedcba9876543210ULL};
  for (unsigned sample = 0; sample < 70; ++sample) {
    uint64_t x = sample < 6 ? fixed[sample] : UINT64_C(1) << (sample - 6);
    for (unsigned index = 0; index < 514; ++index) {
      unsigned count = index == 512 ? UINT32_MAX : index == 513 ? 0xdead0040 : index;
      for (unsigned op = 0; op < 4; ++op) {
        uint64_t a = op == 0 ? rotate32((unsigned)x, count) : shift32(op, (unsigned)x, count);
        uint64_t b = op == 0 ? rotate64(x, count) : shift64(op, x, count);
        if (a != reference(op, x, count, 32)) return 10 + op;
        if (b != reference(op, x, count, 64)) return 20 + op;
      }
    }
  }
  return once();
}
)";
  const auto Source = tmpFile("bmi2_values.c");
  writeFile(Source, Code);
  for (const char *Opt : {"-O0", "-O2"})
    compileRunAndCheck(
        std::string("bmi2_values") + Opt, Source.string(),
        std::string("-std=c23 -mno-bmi2 -fno-lto -fno-builtin-mimalloc ") +
            Opt);
}
