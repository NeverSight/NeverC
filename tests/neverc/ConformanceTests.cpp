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
