#include "NeverCTestFixture.h"

class ConformanceTest : public NeverCTest {
protected:
  // Syntax-only smoke test: verify neverc accepts the file without crashing.
  void syntaxSmoke(const std::string &name, const fs::path &src,
                   const std::string &std) {
    if (!fs::exists(src)) {
      GTEST_SKIP() << src << " not found";
      return;
    }
    std::vector<std::string> args = {"-std=" + std, "-fsyntax-only",
                                     "-Wno-error", "-ffreestanding"};
    for (auto &f : sysrootFlags()) args.push_back(f);
    for (auto &f : archFlags()) args.push_back(f);
    args.push_back(src.string());
    auto r = ncc(args);
    // Accept both 0 (clean) and 1 (expected diagnostics); reject crash (>128).
    EXPECT_LT(r.exitCode, 128)
        << name << ": neverc crashed on " << src << "\n" << r.err;
  }
};

TEST_F(ConformanceTest, N2826_IRShape) {
  auto cDir = cTestDir();
  if (!fs::exists(cDir / "C2x" / "n2826.c")) {
    GTEST_SKIP() << "tests/C/ not found";
    return;
  }

  auto ir = tmpFile("n2826.ll");
  std::vector<std::string> args;
  for (auto &f : sysrootFlags()) args.push_back(f);
  for (auto &f : archFlags()) args.push_back(f);
  args.insert(args.end(),
              {"-ffreestanding", "-emit-llvm", "-S", "-o", ir.string(),
               "-std=c2x", (cDir / "C2x" / "n2826.c").string()});
  auto r = ncc(args);
  ASSERT_EQ(r.exitCode, 0) << "n2826 codegen\n" << r.err;

  auto irContent = readFile(ir);
  // The switch default label's block should start with `unreachable`
  EXPECT_TRUE(irContent.find("unreachable") != std::string::npos)
      << "n2826: unreachable not found in IR";
}

TEST_F(ConformanceTest, N2836_N2939_Preprocessor) {
  auto cDir = cTestDir();
  auto src = cDir / "C2x" / "n2836_n2939.c";
  if (!fs::exists(src)) {
    GTEST_SKIP() << "tests/C/C2x/n2836_n2939.c not found";
    return;
  }

  auto pp = tmpFile("n2836_n2939.pp");
  std::vector<std::string> args;
  for (auto &f : sysrootFlags()) args.push_back(f);
  for (auto &f : archFlags()) args.push_back(f);
  args.insert(args.end(), {"-x", "c", "-std=c2x", "-E", "-DPP_ONLY=1", "-o",
                           pp.string(), src.string()});
  auto r = ncc(args);
  ASSERT_EQ(r.exitCode, 0) << "preprocessor run\n" << r.err;

  auto ppContent = readFile(pp);
  EXPECT_TRUE(ppContent.find("Copyright \xC2\xA9 2012") != std::string::npos)
      << "n2836: copyright symbol not found in preprocessor output";
}

// ---- C2x feature files ----

TEST_F(ConformanceTest, N2350_BinaryIntegerConstants) {
  syntaxSmoke("n2350", cTestDir() / "C2x" / "n2350.c", "c2x");
}

TEST_F(ConformanceTest, N2975_RelaxVaStart) {
  syntaxSmoke("n2975", cTestDir() / "C2x" / "n2975.c", "c2x");
}

TEST_F(ConformanceTest, N3007_TypeInference) {
  syntaxSmoke("n3007", cTestDir() / "C2x" / "n3007.c", "c2x");
}

TEST_F(ConformanceTest, N3042_Nullptr) {
  syntaxSmoke("n3042", cTestDir() / "C2x" / "n3042.c", "c2x");
}

TEST_F(ConformanceTest, NullptrExplicitBoolPreservesValueAndEffects) {
  auto source = tmpFile("nullptr-explicit-bool.c");
  writeFile(
      source,
      R"c(
int calls;

typeof(nullptr) get_null(void) {
  ++calls;
  return nullptr;
}

int main(void) {
  bool runtime = (bool)get_null();
  if (runtime || calls != 1)
    return 1;
  if ((bool)nullptr)
    return 2;
  return 0;
}
)c");
  compileRunAndCheck("nullptr-explicit-bool", source.string(), "-std=c23");
}

TEST_F(ConformanceTest, BitCastPreservesRepresentationsAndSingleEvaluation) {
  auto Source = tmpFile("bit-cast.c");
  writeFile(Source, R"c(
#if !__has_builtin(__builtin_bit_cast)
#error missing bit cast builtin
#endif
typedef unsigned long long Words __attribute__((vector_size(16)));
typedef struct { unsigned long long a, b; } Pair;
static const float one = __builtin_bit_cast(float, 0x3f800000U);
volatile unsigned bits = 0x80000000U;
unsigned calls;
unsigned next(void) { ++calls; return bits; }
Pair pair(void) { ++calls; return (Pair){0x8172635445362718ULL, 0xfedcba9876543210ULL}; }
int main(void) {
  if (one != 1.0f) return 1;
  if (__builtin_bit_cast(int, 0xffffffffU) != -1) return 2;
  float negative_zero = __builtin_bit_cast(float, next());
  if (calls != 1 || __builtin_bit_cast(unsigned, negative_zero) != 0x80000000U)
    return 3;
  Words words = __builtin_bit_cast(Words, pair());
  Pair recovered = __builtin_bit_cast(Pair, words);
  if (calls != 2 || recovered.a != 0x8172635445362718ULL ||
      recovered.b != 0xfedcba9876543210ULL) return 4;
  unsigned _BitInt(128) wide = __builtin_bit_cast(unsigned _BitInt(128), recovered);
  Pair again = __builtin_bit_cast(Pair, wide);
  if (again.a != recovered.a || again.b != recovered.b) return 5;
  unsigned value = 0x7fc12345U;
  float nan = __builtin_bit_cast(float, value);
  if (__builtin_bit_cast(unsigned, nan) != value) return 6;
  if (!__builtin_bit_cast(bool, (unsigned char)1)) return 7;
  return 0;
}
)c");
  for (const char *Opt : {"-O0", "-O2"}) {
    SCOPED_TRACE(Opt);
    auto Executable = tmpFile(std::string("bit-cast-") + Opt + ".exe");
    std::vector<std::string> Args = {
        "-std=c23",      "-fno-lto", "-fno-builtin-mimalloc", Opt,
        Source.string(), "-o",       Executable.string()};
    for (const auto &Flag : sysrootFlags())
      Args.push_back(Flag);
    for (const auto &Flag : archFlags())
      Args.push_back(Flag);
    for (const auto &Flag : linkFlags())
      Args.push_back(Flag);
    const auto Compile = ncc(Args);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.err;
    const auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.err;
  }
}

TEST_F(ConformanceTest, BitCastRejectsInvalidObjectContracts) {
  for (const char *Expression : {"__builtin_bit_cast(long long, value)",
                                 "__builtin_bit_cast(void, value)",
                                 "__builtin_bit_cast(struct Incomplete, value)",
                                 "__builtin_bit_cast(unsigned[1], value)"}) {
    SCOPED_TRACE(Expression);
    auto Source = tmpFile("bad-bit-cast.c");
    writeFile(Source, "struct Incomplete; void f(unsigned value) { (void)" +
                          std::string(Expression) + "; }\n");
    const auto Compile =
        ncc({"-std=c23", "-fsyntax-only", "-ffreestanding", Source.string()});
    EXPECT_NE(Compile.exitCode, 0);
    EXPECT_LT(Compile.exitCode, 128) << Compile.err;
    EXPECT_NE(Compile.err.find("__builtin_bit_cast requires"),
              std::string::npos)
        << Compile.err;
  }
}

// ---- C99 / C11 feature files ----

TEST_F(ConformanceTest, N636_ImplicitFunctionDecl) {
  syntaxSmoke("n636", cTestDir() / "C99" / "n636.c", "c99");
}

TEST_F(ConformanceTest, N1330_StaticAssertions) {
  syntaxSmoke("n1330", cTestDir() / "C11" / "n1330.c", "c23");
}

// ---- Defect reports (smoke: no crash) ----

TEST_F(ConformanceTest, DR0xx) {
  syntaxSmoke("dr0xx", cTestDir() / "drs" / "dr0xx.c", "c11");
}

TEST_F(ConformanceTest, DR1xx) {
  syntaxSmoke("dr1xx", cTestDir() / "drs" / "dr1xx.c", "c11");
}

TEST_F(ConformanceTest, DR2xx) {
  syntaxSmoke("dr2xx", cTestDir() / "drs" / "dr2xx.c", "c11");
}

TEST_F(ConformanceTest, DR3xx) {
  syntaxSmoke("dr3xx", cTestDir() / "drs" / "dr3xx.c", "c11");
}

TEST_F(ConformanceTest, DR338) {
  syntaxSmoke("dr338", cTestDir() / "drs" / "dr338.c", "c11");
}

TEST_F(ConformanceTest, DR4xx) {
  syntaxSmoke("dr4xx", cTestDir() / "drs" / "dr4xx.c", "c11");
}
