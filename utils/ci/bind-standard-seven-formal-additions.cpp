
TEST_F(TranslateTest, CoreV2BindStandardArithmeticRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-callable-standard-arithmetic.cpp");
  const auto Output = tmpFile("bind-callable-standard-arithmetic.nc");
  writeFile(Source, R"cpp(#include <functional>
extern "C" int replacement() {
  auto add = std::bind(std::plus<int>{}, std::placeholders::_1, 4);
  const auto copied = add;
  auto subtract = std::bind(std::minus<>{}, std::placeholders::_1, 2);
  return add(3) == 7 && copied(6) == 10 && subtract(9) == 7 ? 0 : 1;
}

int main() { return replacement(); }
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-callable-standard-arithmetic" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindStandardComparisonRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-standard-comparison.cpp");
  const auto Output = tmpFile("bind-standard-comparison.nc");
  writeFile(Source, R"cpp(#include <functional>
extern "C" int replacement() {
  auto reversed = std::bind(std::less<long>{}, std::placeholders::_2, std::placeholders::_1);
  const auto copied = reversed;
  auto at_least = std::bind(std::greater_equal<>{}, std::placeholders::_1, 2);
  return !copied(4L, 9L) && at_least(9) ? 0 : 1;
}

int main() { return replacement(); }
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-standard-comparison" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindStandardLogicalRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-standard-logical.cpp");
  const auto Output = tmpFile("bind-standard-logical.nc");
  writeFile(Source, R"cpp(#include <functional>
extern "C" int replacement() {
  auto both = std::bind(std::logical_and<bool>{}, std::placeholders::_1, true);
  const auto flip = std::bind(std::logical_not<>{}, std::placeholders::_1);
  return !both(false) && both(true) && flip(0) ? 0 : 1;
}

int main() { return replacement(); }
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-standard-logical" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindStandardUnaryRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-standard-unary.cpp");
  const auto Output = tmpFile("bind-standard-unary.nc");
  writeFile(Source, R"cpp(#include <functional>
extern "C" int replacement() {
  const auto negative = std::bind(std::negate<int>{}, std::placeholders::_1);
  auto invert = std::bind(std::bit_not<>{}, std::placeholders::_1);
  return negative(9) == -9 && invert(0u) == ~0u ? 0 : 1;
}

int main() { return replacement(); }
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-standard-unary" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindStandardBitwiseRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-standard-bitwise.cpp");
  const auto Output = tmpFile("bind-standard-bitwise.nc");
  writeFile(Source, R"cpp(#include <functional>
extern "C" int replacement() {
  auto mask = std::bind(std::bit_and<unsigned>{}, std::placeholders::_1, 7u);
  const auto twice = std::bind(std::bit_xor<>{}, std::placeholders::_1, std::placeholders::_1);
  return mask(10u) == 2u && twice(13u) == 0u ? 0 : 1;
}

int main() { return replacement(); }
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-standard-bitwise" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindStandardRemainingOperatorsRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-standard-remaining-operators.cpp");
  const auto Output = tmpFile("bind-standard-remaining-operators.nc");
  writeFile(Source, R"cpp(#include <functional>
extern "C" int replacement() {
  auto multiply = std::bind(std::multiplies<int>{}, std::placeholders::_1, 3);
  auto divide = std::bind(std::divides<>{}, std::placeholders::_1, 3);
  auto modulo = std::bind(std::modulus<int>{}, std::placeholders::_1, 3);
  auto equal = std::bind(std::equal_to<>{}, std::placeholders::_1, 7);
  auto different = std::bind(std::not_equal_to<int>{}, std::placeholders::_1, 7);
  auto greater = std::bind(std::greater<>{}, std::placeholders::_1, 7);
  auto at_most = std::bind(std::less_equal<int>{}, std::placeholders::_1, 7);
  auto either = std::bind(std::logical_or<>{}, std::placeholders::_1, false);
  auto combine = std::bind(std::bit_or<unsigned>{}, std::placeholders::_1, 4u);
  return multiply(7) == 21 && divide(21) == 7 && modulo(22) == 1 && equal(7) &&
      different(8) && greater(8) && at_most(7) && either(true) && combine(3u) == 7u ? 0 : 1;
}

int main() { return replacement(); }
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-standard-remaining-operators" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindStandardArgumentEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-standard-argument-effects.cpp");
  const auto Output = tmpFile("bind-standard-argument-effects.nc");
  writeFile(Source, R"cpp(#include <functional>
extern "C" int replacement() {
  int reads = 0;
  auto both = std::bind(std::logical_and<>{}, std::placeholders::_1, std::placeholders::_2);
  auto either = std::bind(std::logical_or<>{}, std::placeholders::_1, std::placeholders::_2);
  const bool left = both(false, ++reads);
  const bool right = either(true, ++reads);
  int arguments = 0;
  auto reordered = std::bind(std::multiplies<>{}, std::placeholders::_2, std::placeholders::_1);
  const int product = reordered(++arguments, ++arguments);
  return !left && right && reads == 2 && arguments == 2 && product == 4 ? 0 : 1;
}

int main() { return replacement(); }
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-standard-argument-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}
