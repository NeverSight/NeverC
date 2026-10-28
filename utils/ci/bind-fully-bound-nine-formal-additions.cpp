
TEST_F(TranslateTest, CoreV2BindFullyBoundValueCopiesRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-fully-bound-value-copies.cpp");
  const auto Output = tmpFile("bind-fully-bound-value-copies.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int calls;
int add(int a, int b) { ++calls; return a + b; }
int main() {
  auto f = std::bind(&add, 3, 4);
  auto copy = f;
  const auto fixed = copy;
  if (f() != 7 || copy() != 7 || fixed() != 7 || std::move(copy)() != 7) return 1;
  return calls != 4;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-fully-bound-value-copies" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCapturesFunctionPointerValueRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-captures-function-pointer-value.cpp");
  const auto Output = tmpFile("bind-captures-function-pointer-value.nc");
  writeFile(Source, R"cpp(#include <functional>
int first(int a, int b) { return a + b; }
int second(int, int) { return -1; }
int main() {
  int (*pointer)(int, int) = &first;
  auto f = std::bind(pointer, 3, 4);
  pointer = &second;
  return f() != 7 || pointer(3, 4) != -1;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-captures-function-pointer-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindBoundExpressionEffectsOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-bound-expression-effects-once.cpp");
  const auto Output = tmpFile("bind-bound-expression-effects-once.nc");
  writeFile(Source, R"cpp(#include <functional>
int left_builds, right_builds, calls;
int left() { ++left_builds; return 5; }
int right() { ++right_builds; return 7; }
int add(int a, int b) { ++calls; return a + b; }
int main() {
  auto f = std::bind(&add, left(), right());
  if (left_builds != 1 || right_builds != 1 || calls != 0) return 1;
  auto copy = f;
  if (f() != 12 || copy() != 12) return 2;
  return left_builds != 1 || right_builds != 1 || calls != 2;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-bound-expression-effects-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindBorrowedScalarArgumentRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-borrowed-scalar-argument.cpp");
  const auto Output = tmpFile("bind-borrowed-scalar-argument.nc");
  writeFile(Source, R"cpp(#include <functional>
int calls;
int add_to(int &value, int delta) { ++calls; value += delta; return value; }
int main() {
  int value = 7;
  auto f = std::bind(&add_to, std::ref(value), 3);
  auto copy = f;
  if (f() != 10 || copy() != 13 || value != 13) return 1;
  return calls != 2;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-borrowed-scalar-argument" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindLiveReferenceResultRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-live-reference-result.cpp");
  const auto Output = tmpFile("bind-live-reference-result.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <type_traits>
int &identity(int &value) { return value; }
int main() {
  int value = 7;
  auto f = std::bind(&identity, std::ref(value));
  static_assert(std::is_same<decltype(f()), int &>::value, "reference result");
  f() += 3;
  const auto copy = f;
  copy() = 14;
  return value != 14;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-live-reference-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindVoidResultRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-void-result.cpp");
  const auto Output = tmpFile("bind-void-result.nc");
  writeFile(Source, R"cpp(#include <functional>
int calls;
void store(int *target, int value) { ++calls; *target = value; }
int main() {
  int value = 0;
  auto f = std::bind(&store, &value, 9);
  f();
  if (value != 9 || calls != 1) return 1;
  value = 2;
  f();
  return value != 9 || calls != 2;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-void-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindResultAndExceptionQueriesRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-result-and-exception-queries.cpp");
  const auto Output = tmpFile("bind-result-and-exception-queries.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <type_traits>
int add(int a, int b) noexcept { return a + b; }
int main() {
  auto f = std::bind(&add, 3, 4);
  using F = decltype(f);
  static_assert(std::is_bind_expression<F>::value, "bind expression");
  static_assert(std::is_same<decltype(f()), int>::value, "result type");
  static_assert(!noexcept(f()), "pinned SDK operator has no noexcept signature");
  return f() != 7;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-result-and-exception-queries" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindLvalueCapturesAfterArgumentEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-lvalue-captures-after-argument-effects.cpp");
  const auto Output = tmpFile("bind-lvalue-captures-after-argument-effects.nc");
  writeFile(Source, R"cpp(#include <functional>
int first(int, int) { return -1; }
int second(int a, int b) { return 100 + 10 * a + b; }
int &change(int (*&pointer)(int, int), int &value) {
  pointer = &second;
  value = 3;
  return value;
}
int main() {
  int (*pointer)(int, int) = &first;
  int value = 1;
  auto f = std::bind(pointer, change(pointer, value), value);
  if (f() != 133) return 1;
  value = 9;
  pointer = &first;
  return f() != 133;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-lvalue-captures-after-argument-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindStoredStateAndRvalueReceiverRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-stored-state-and-rvalue-receiver.cpp");
  const auto Output = tmpFile("bind-stored-state-and-rvalue-receiver.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <type_traits>
#include <utility>
int increment(int &value) { return ++value; }
int main() {
  auto f = std::bind(&increment, 4);
  auto copy = f;
  using F = decltype(f);
  static_assert(!std::is_invocable<const F &>::value, "const stored value cannot bind mutable int reference");
  if (f() != 5 || std::move(f)() != 6 || copy() != 5) return 1;
  return f() != 7;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-stored-state-and-rvalue-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}
