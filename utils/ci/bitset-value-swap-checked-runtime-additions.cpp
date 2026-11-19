
TEST_F(TranslateTest, CoreV2BitsetValueSwapEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-value-swap-empty.cpp");
  const auto Output = tmpFile("bitset-value-swap-empty.nc");
  writeFile(Source, R"r19bitvalswap(#include <bitset>
#include <utility>
int main() {
  std::bitset<0> a, b;
  std::swap(a, b);
  return a.size() != 0 || !b.none();
}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-value-swap-empty" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapOneWordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-value-swap-one-word.cpp");
  const auto Output = tmpFile("bitset-value-swap-one-word.nc");
  writeFile(Source, R"r19bitvalswap(#include <bitset>
#include <utility>
int main() {
  std::bitset<9> a(0x155), b(0x0fe);
  std::swap(a, b);
  return a.to_ulong() != 0x0fe || b.to_ulong() != 0x155;
}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-value-swap-one-word" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapMultiwordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-value-swap-multiword.cpp");
  const auto Output = tmpFile("bitset-value-swap-multiword.nc");
  writeFile(Source, R"r19bitvalswap(#include <bitset>
#include <utility>
int main() {
  std::bitset<129> a, b;
  a.set(128);
  b.set(64).set(0);
  std::swap(a, b);
  return a.count() != 2 || !a.test(64) || !a.test(0) || b.count() != 1 || !b.test(128);
}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-value-swap-multiword" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapExactTemplateRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-value-swap-exact-template.cpp");
  const auto Output = tmpFile("bitset-value-swap-exact-template.nc");
  writeFile(Source, R"r19bitvalswap(#include <bitset>
#include <utility>
int main() {
  std::bitset<65> a(1), b(2);
  b.set(64);
  std::swap<std::bitset<65>>(a, b);
  return a.count() != 2 || !a.test(64) || !a.test(1) || b.count() != 1 || !b.test(0);
}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-value-swap-exact-template" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapSelfAliasAndEvaluationRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-value-swap-self-alias-and-evaluation.cpp");
  const auto Output = tmpFile("bitset-value-swap-self-alias-and-evaluation.nc");
  writeFile(Source, R"r19bitvalswap(#include <bitset>
#include <utility>
using Bits = std::bitset<65>;
static unsigned left_calls, right_calls;
Bits& left(Bits& value) { ++left_calls; return value; }
Bits& right(Bits& value) { ++right_calls; return value; }
int main() {
  Bits value(1);
  value.set(64);
  std::swap(left(value), right(value));
  return value.count() != 2 || !value.test(0) || !value.test(64) ||
         left_calls != 1 || right_calls != 1;
}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-value-swap-self-alias-and-evaluation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapExistingProxyRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-value-swap-existing-proxy.cpp");
  const auto Output = tmpFile("bitset-value-swap-existing-proxy.nc");
  writeFile(Source, R"r19bitvalswap(#include <bitset>
#include <utility>
int main() {
  std::bitset<70> a, b;
  a.set(65);
  b.set(64);
  auto bit = a[65];
  std::swap(a, b);
  if (bit || !b.test(65)) return 1;
  bit.flip();
  return !a.test(65) || !a.test(64) || !b.test(65) || b.test(64);
}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-value-swap-existing-proxy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapGuardSdkOperandRejected) {
  const auto Source = tmpFile("bitset-value-swap-guard-sdk-operand.cpp");
  writeFile(Source, R"r19bitvalswap(#define __x nct_swap_operand
#include <bitset>
#include <utility>
#undef __x
int main(){std::bitset<65> a(1),b(2);std::swap(a,b);return !a.test(1);}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapGuardSdkTemporaryRejected) {
  const auto Source = tmpFile("bitset-value-swap-guard-sdk-temporary.cpp");
  writeFile(Source, R"r19bitvalswap(#define __t nct_swap_temporary
#include <bitset>
#include <utility>
#undef __t
int main(){std::bitset<65> a(1),b(2);std::swap(a,b);return !a.test(1);}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapGuardIndirectCallRejected) {
  const auto Source = tmpFile("bitset-value-swap-guard-indirect-call.cpp");
  writeFile(Source, R"r19bitvalswap(#include <bitset>
#include <utility>
using Bits=std::bitset<65>;using Swap=void(*)(Bits&,Bits&) noexcept;
int main(){Bits a(1),b(2);Swap function=&std::swap<Bits>;function(a,b);return !a.test(1);}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetValueSwapGuardCastCalleeRejected) {
  const auto Source = tmpFile("bitset-value-swap-guard-cast-callee.cpp");
  writeFile(Source, R"r19bitvalswap(#include <bitset>
#include <utility>
using Bits=std::bitset<65>;using Swap=void(*)(Bits&,Bits&) noexcept;
int main(){Bits a(1),b(2);static_cast<Swap>(std::swap<Bits>)(a,b);return !a.test(1);}
)r19bitvalswap");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
