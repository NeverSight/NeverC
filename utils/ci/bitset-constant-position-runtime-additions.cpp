
TEST_F(TranslateTest, CoreV2BitsetConstantPositionNamespaceConstantRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constant-position-namespace-constant.cpp");
  const auto Output = tmpFile("bitset-constant-position-namespace-constant.nc");
  writeFile(Source, R"r19bitice(#include <bitset>
constexpr std::size_t high = 64;
int main() {
  std::bitset<70> bits;
  bits.set(high);
  return !bits.test(high) || bits.count() != 1;
}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constant-position-namespace-constant" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionArithmeticRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constant-position-arithmetic.cpp");
  const auto Output = tmpFile("bitset-constant-position-arithmetic.nc");
  writeFile(Source, R"r19bitice(#include <bitset>
int main() {
  std::bitset<70> bits;
  bits.set(1u << 6);
  bits.flip(35 + 30);
  bits.reset(32 * 2);
  return bits.test(64) || !bits.test(35 + 30) || bits.count() != 1;
}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constant-position-arithmetic" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionEnumIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constant-position-enum-index.cpp");
  const auto Output = tmpFile("bitset-constant-position-enum-index.nc");
  writeFile(Source, R"r19bitice(#include <bitset>
enum Position : unsigned { boundary = 63 };
int main() {
  std::bitset<70> bits;
  bits.set(boundary).set(static_cast<unsigned>(boundary) + 1);
  return !bits.test(boundary) || !bits.test(64) || bits.count() != 2;
}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constant-position-enum-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionTypeSizeIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constant-position-type-size-index.cpp");
  const auto Output = tmpFile("bitset-constant-position-type-size-index.nc");
  writeFile(Source, R"r19bitice(#include <bitset>
int main() {
  constexpr std::size_t last = sizeof(unsigned long long) * 8 - 1;
  std::bitset<65> bits;
  bits.flip(last).set(last + 1);
  return !bits.test(last) || !bits.test(last + 1) || bits.count() != 2;
}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constant-position-type-size-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionConstexprFunctionRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constant-position-constexpr-function.cpp");
  const auto Output = tmpFile("bitset-constant-position-constexpr-function.nc");
  writeFile(Source, R"r19bitice(#include <bitset>
constexpr std::size_t at(bool high) { return high ? 65 : 0; }
int main() {
  std::bitset<70> bits;
  bits.set(at(true));
  bits.set(at(false));
  bits.reset(at(true));
  return !bits.test(at(false)) || bits.test(at(true)) || bits.count() != 1;
}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constant-position-constexpr-function" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionReceiverAndValueRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constant-position-receiver-and-value.cpp");
  const auto Output = tmpFile("bitset-constant-position-receiver-and-value.nc");
  writeFile(Source, R"r19bitice(#include <bitset>
static unsigned phase;
std::bitset<70>& receiver(std::bitset<70>& bits) { ++phase; return bits; }
bool value() { phase = phase == 1 ? 2 : 99; return true; }
int main() {
  std::bitset<70> bits;
  receiver(bits).set(2u * 33u, value());
  return phase != 2 || !bits.test(66) || bits.count() != 1;
}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constant-position-receiver-and-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionGuardNegativeRejected) {
  const auto Source = tmpFile("bitset-constant-position-guard-negative.cpp");
  writeFile(Source, R"r19bitice(#include <bitset>
int main(){std::bitset<65> b;constexpr int p=-1;return b.test(p);}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionGuardOnePastRejected) {
  const auto Source = tmpFile("bitset-constant-position-guard-one-past.cpp");
  writeFile(Source, R"r19bitice(#include <bitset>
int main(){std::bitset<65> b;constexpr unsigned p=64;b.flip(p+1);return 0;}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionGuardConvertedTooLargeRejected) {
  const auto Source = tmpFile("bitset-constant-position-guard-converted-too-large.cpp");
  writeFile(Source, R"r19bitice(#include <bitset>
int main(){std::bitset<65> b;return b.test(~std::size_t{0});}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionGuardEmptyRejected) {
  const auto Source = tmpFile("bitset-constant-position-guard-empty.cpp");
  writeFile(Source, R"r19bitice(#include <bitset>
int main(){std::bitset<0> b;constexpr unsigned p=0;b.set(p);return 0;}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionGuardConstantContextOnlyRejected) {
  const auto Source = tmpFile("bitset-constant-position-guard-constant-context-only.cpp");
  writeFile(Source, R"r19bitice(#include <bitset>
constexpr unsigned at(){return __builtin_is_constant_evaluated()?0:65;}
int main(){std::bitset<65> b;return b.test(at());}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionGuardIndexWithEffectRejected) {
  const auto Source = tmpFile("bitset-constant-position-guard-index-with-effect.cpp");
  writeFile(Source, R"r19bitice(#include <bitset>
int main(){std::bitset<65> b;unsigned p=0;b.set((++p,32));return p!=1;}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionGuardConstexprCallWithRuntimeInputRejected) {
  const auto Source = tmpFile("bitset-constant-position-guard-constexpr-call-with-runtime-input.cpp");
  writeFile(Source, R"r19bitice(#include <bitset>
constexpr unsigned at(unsigned p){return p;}
bool check(unsigned p){std::bitset<65> b;return b.test(at(p));}
int main(){return check(2);}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstantPositionGuardSourceBodyRejected) {
  const auto Source = tmpFile("bitset-constant-position-guard-source-body.cpp");
  writeFile(Source, R"r19bitice(#include <bitset>
namespace std{template<> bool bitset<65>::test(size_t) const{return true;}}
int main(){std::bitset<65> b;constexpr unsigned p=2;return b.test(p);}
)r19bitice");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
