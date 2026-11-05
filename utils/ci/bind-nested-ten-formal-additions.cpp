
TEST_F(TranslateTest, CoreV2BindNestedNullaryRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-nested-nullary.cpp");
  const auto Output = tmpFile("bind-nested-nullary.nc");
  writeFile(Source, R"cpp(#include <functional>
int inner_calls, outer_calls;
int value() { ++inner_calls; return 6; }
int add(int x, int y) { ++outer_calls; return x + y; }
int main() {
  auto f = std::bind(&add, std::bind(&value), 7);
  return f() == 13 && inner_calls == 1 && outer_calls == 1 ? 0 : 1;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-nested-nullary" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindNestedOnePlaceholderRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-nested-one-placeholder.cpp");
  const auto Output = tmpFile("bind-nested-one-placeholder.nc");
  writeFile(Source, R"cpp(#include <functional>
int inner_calls, outer_calls;
int twice(int x) { ++inner_calls; return x * 2; }
int add(int x, int y) { ++outer_calls; return x + y; }
int main() {
  auto f = std::bind(&add, std::bind(&twice, std::placeholders::_1), 7);
  return f(3) == 13 && inner_calls == 1 && outer_calls == 1 ? 0 : 1;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-nested-one-placeholder" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindNestedReorderedRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-nested-reordered.cpp");
  const auto Output = tmpFile("bind-nested-reordered.nc");
  writeFile(Source, R"cpp(#include <functional>
int inner_calls, outer_calls;
int subtract(int x, int y) { ++inner_calls; return x - y; }
int add(int x, int y) { ++outer_calls; return x + y; }
int main() {
  auto f = std::bind(&add,
    std::bind(&subtract, std::placeholders::_2, std::placeholders::_1), 4);
  return f(3, 8) == 9 && inner_calls == 1 && outer_calls == 1 ? 0 : 1;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-nested-reordered" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindNestedTwoSharedInputsRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-nested-two-shared-inputs.cpp");
  const auto Output = tmpFile("bind-nested-two-shared-inputs.nc");
  writeFile(Source, R"cpp(#include <functional>
int left_calls, right_calls, outer_calls;
int add(int x, int y) { ++left_calls; return x + y; }
int subtract(int x, int y) { ++right_calls; return x - y; }
int combine(int x, int y) { ++outer_calls; return x - y; }
int main() {
  auto f = std::bind(&combine,
    std::bind(&add, std::placeholders::_2, std::placeholders::_1),
    std::bind(&subtract, std::placeholders::_1, std::placeholders::_2));
  return f(3, 8) == 16 && left_calls == 1 && right_calls == 1 && outer_calls == 1 ? 0 : 1;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-nested-two-shared-inputs" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindNestedLiveReferenceResultRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-nested-live-reference-result.cpp");
  const auto Output = tmpFile("bind-nested-live-reference-result.nc");
  writeFile(Source, R"cpp(#include <functional>
int inner_calls, outer_calls;
int& select(int& value, int ignored) { ++inner_calls; return value; }
int write(int& target, int value) { ++outer_calls; target = value; return target; }
int main() {
  int value = 2;
  auto inner = std::bind(&select, std::ref(value), std::placeholders::_1);
  auto f = std::bind(&write, inner, 5);
  if (f(0) != 5 || value != 5) return 1;
  value = 9;
  return f(4) == 5 && value == 5 && inner_calls == 2 && outer_calls == 2 ? 0 : 2;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-nested-live-reference-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindNestedIndependentCapturedStateRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-nested-independent-captured-state.cpp");
  const auto Output = tmpFile("bind-nested-independent-captured-state.nc");
  writeFile(Source, R"cpp(#include <functional>
int inner_calls, outer_calls;
int advance(int& stored, int delta) { ++inner_calls; stored += delta; return stored; }
int add(int x, int y) { ++outer_calls; return x + y; }
int main() {
  auto inner = std::bind(&advance, 5, std::placeholders::_1);
  auto f = std::bind(&add, inner, 1);
  if (inner(2) != 7 || f(3) != 9) return 1;
  auto g = f;
  if (g(2) != 11 || f(1) != 10) return 2;
  return inner(1) == 8 && inner_calls == 5 && outer_calls == 3 ? 0 : 3;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-nested-independent-captured-state" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindNestedUnusedEffectsBeforeReadRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-nested-unused-effects-before-read.cpp");
  const auto Output = tmpFile("bind-nested-unused-effects-before-read.nc");
  writeFile(Source, R"cpp(#include <functional>
int effects, inner_calls, outer_calls, wrong;
int argument(int value) { ++effects; return value; }
int read(int x) { if (effects != 2) wrong = 1; ++inner_calls; return x; }
int add(int x, int y) { ++outer_calls; return x + y; }
int main() {
  auto f = std::bind(&add, std::bind(&read, std::placeholders::_1), 4);
  return f(argument(3), argument(8)) == 7 && effects == 2 && !wrong &&
    inner_calls == 1 && outer_calls == 1 ? 0 : 1;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-nested-unused-effects-before-read" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindNestedConstCopyAndTemporaryRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-nested-const-copy-and-temporary.cpp");
  const auto Output = tmpFile("bind-nested-const-copy-and-temporary.nc");
  writeFile(Source, R"cpp(#include <functional>
int inner_calls, outer_calls;
int add(int x, int y) { ++inner_calls; return x + y; }
int twice(int x) { ++outer_calls; return x * 2; }
int main() {
  const auto f = std::bind(&twice,
    std::bind(&add, std::placeholders::_1, 4));
  auto g = f;
  if (f(3) != 14 || g(5) != 18) return 1;
  return std::bind(&twice, std::bind(&add, std::placeholders::_1, 4))(2) == 12 &&
    inner_calls == 3 && outer_calls == 3 ? 0 : 2;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-nested-const-copy-and-temporary" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindNestedLateReferenceValueReadRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-nested-late-reference-value-read.cpp");
  const auto Output = tmpFile("bind-nested-late-reference-value-read.nc");
  writeFile(Source, R"cpp(#include <functional>
int inner_calls, outer_calls;
int change(int& value) { ++inner_calls; value = 9; return 1; }
int add(int x, int y) { ++outer_calls; return x + y; }
int main() {
  int value = 3;
  auto inner = std::bind(&change, std::ref(value));
  auto f = std::bind(&add, std::ref(value), inner);
  return f() == 10 && value == 9 && inner_calls == 1 && outer_calls == 1 ? 0 : 1;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-nested-late-reference-value-read" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindNestedLatePlaceholderValueReadRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-nested-late-placeholder-value-read.cpp");
  const auto Output = tmpFile("bind-nested-late-placeholder-value-read.nc");
  writeFile(Source, R"cpp(#include <functional>
int inner_calls, outer_calls;
int change(int& value) { ++inner_calls; value = 9; return 1; }
int add(int x, int y) { ++outer_calls; return x + y; }
int main() {
  int value = 3;
  auto f = std::bind(&add, std::placeholders::_1,
    std::bind(&change, std::placeholders::_1));
  return f(value) == 10 && value == 9 && inner_calls == 1 && outer_calls == 1 ? 0 : 1;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-nested-late-placeholder-value-read" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}
