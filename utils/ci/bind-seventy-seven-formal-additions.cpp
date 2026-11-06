
TEST_F(TranslateTest, CoreV2NotFnFunctionPointerRunsAtBothOptimizations) {
  const auto Source = tmpFile("not-fn-function-pointer.cpp");
  const auto Output = tmpFile("not-fn-function-pointer.nc");
  writeFile(Source, R"cpp(#include <functional>
int calls;
bool odd(int x) { ++calls; return (x & 1) != 0; }
int main() {
  auto f = std::not_fn(&odd);
  return f(3) || !f(4) || f(7) || calls != 3;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("not-fn-function-pointer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2NotFnFunctionPointerReferencesRunAtBothOptimizations) {
  const auto Source = tmpFile("not-fn-reference-arguments.cpp");
  const auto Output = tmpFile("not-fn-reference-arguments.nc");
  writeFile(Source, R"cpp(#include <functional>
int calls;
bool update(int &x, const int &y) { ++calls; x += y; return x != 0; }
int main() {
  auto f = std::not_fn(&update);
  int x = -3, y = 3;
  if (!f(x,y) || x != 0 || calls != 1) return 1;
  if (f(x,y) || x != 3 || calls != 2) return 2;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("not-fn-reference-arguments" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2NotFnFunctionPointerFactoryAndArgumentsRunOnce) {
  const auto Source = tmpFile("not-fn-pointer-factory-argument-once.cpp");
  const auto Output = tmpFile("not-fn-pointer-factory-argument-once.nc");
  writeFile(Source, R"cpp(#include <functional>
int builds, calls, arguments;
bool odd(int x) { ++calls; return (x & 1) != 0; }
using FP = bool (*)(int);
FP choose() { ++builds; return &odd; }
int argument(int x) { ++arguments; return x; }
int main() {
  auto f = std::not_fn(choose());
  const bool first = f(argument(3));
  const bool second = f(argument(4));
  return first || !second || builds != 1 || calls != 2 || arguments != 2;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("not-fn-pointer-factory-argument-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2NotFnFunctionPointerFourCvrefCalls) {
  const auto Source = tmpFile("not-fn-pointer-four-cvref.cpp");
  const auto Output = tmpFile("not-fn-pointer-four-cvref.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int calls;
bool odd(int x) noexcept { ++calls; return (x & 1) != 0; }
int main() {
  auto a = std::not_fn(&odd);
  const auto b = std::not_fn(&odd);
  const bool lvalue = a(3);
  const bool constant_lvalue = b(4);
  const bool rvalue = std::move(a)(5);
  const bool constant_rvalue = std::move(b)(6);
  return lvalue || !constant_lvalue || rvalue || !constant_rvalue || calls != 4;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("not-fn-pointer-four-cvref" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2NotFnSignatureQueriesRunAtBothOptimizations) {
  const auto Source = tmpFile("not-fn-signature-queries.cpp");
  const auto Output = tmpFile("not-fn-signature-queries.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <type_traits>
#include <utility>
bool value(int x) noexcept { return x > 0; }
int main() {
  auto f = std::not_fn(&value);
  using F = decltype(f);
  static_assert(std::is_same<decltype(f(1)), bool>::value, "negation result");
  static_assert(std::is_nothrow_invocable<F &,int>::value, "mutable lvalue");
  static_assert(std::is_nothrow_invocable<const F &,int>::value, "const lvalue");
  static_assert(std::is_nothrow_invocable<F &&,int>::value, "mutable rvalue");
  static_assert(std::is_nothrow_invocable<const F &&,int>::value, "const rvalue");
  static_assert(!noexcept(std::not_fn(&value)), "factory exception signature");
  if (f(1) || !f(0)) return 1;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("not-fn-signature-queries" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2NotFnCustomNegationResultAndEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("not-fn-custom-negation-result.cpp");
  const auto Output = tmpFile("not-fn-custom-negation-result.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <type_traits>
int negations;
struct R {
  int value;
  int operator!() const { ++negations; return value + 7; }
};
R value(int x) { return R{x}; }
int main() {
  auto f = std::not_fn(&value);
  static_assert(std::is_same<decltype(f(1)), int>::value, "exact negation result");
  if (f(0) != 7 || f(3) != 10) return 1;
  return negations != 2;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("not-fn-custom-negation-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2NotFnOwnedCallableCopiedStateRunAtBothOptimizations) {
  const auto Source = tmpFile("not-fn-copied-state.cpp");
  const auto Output = tmpFile("not-fn-copied-state.nc");
  writeFile(Source, R"cpp(#include <functional>
int copies, moves, calls;
struct P {
  int state;
  P(int x) : state(x) {}
  P(const P &p) : state(p.state) { ++copies; }
  P(P &&p) : state(p.state) { ++moves; p.state = -1; }
  bool operator()(int x) { ++calls; state += x; return state > 5; }
};
int main() {
  P original(1);
  auto f = std::not_fn(original);
  if (copies != 1 || moves != 0 || original.state != 1) return 1;
  if (!f(2) || f(4) || original.state != 1 || calls != 2) return 2;
  auto g = f;
  if (copies != 2 || g(0) || calls != 3) return 3;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("not-fn-copied-state" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2NotFnOwnedCallableFourCvrefCallsRunAtBothOptimizations) {
  const auto Source = tmpFile("not-fn-four-cvref-calls.cpp");
  const auto Output = tmpFile("not-fn-four-cvref-calls.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int path;
struct P {
  bool operator()(int x) & { path = path * 10 + 1; return x == 1; }
  bool operator()(int x) const & { path = path * 10 + 2; return x == 2; }
  bool operator()(int x) && { path = path * 10 + 3; return x == 3; }
  bool operator()(int x) const && { path = path * 10 + 4; return x == 4; }
};
int main() {
  auto a = std::not_fn(P{});
  const auto b = std::not_fn(P{});
  if (a(1) || b(2) || std::move(a)(3) || std::move(b)(4)) return 1;
  return path != 1234;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("not-fn-four-cvref-calls" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2NotFnOwnedCallableMoveOnlyCleanupRunAtBothOptimizations) {
  const auto Source = tmpFile("not-fn-move-only-cleanup.cpp");
  const auto Output = tmpFile("not-fn-move-only-cleanup.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int moves, live, destroyed;
struct P {
  int value;
  P(int x) : value(x) { ++live; }
  P(const P &) = delete;
  P(P &&p) : value(p.value) { p.value = -1; ++moves; ++live; }
  ~P() { --live; ++destroyed; }
  bool operator()(int x) { return value == x; }
};
int main() {
  {
    P source(5);
    auto f = std::not_fn(std::move(source));
    if (moves != 1 || live != 2 || source.value != -1 || f(5) || !f(4)) return 1;
  }
  return live != 0 || destroyed != 2;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("not-fn-move-only-cleanup" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2NotFnOwnedCallableFactoryAndCleanupEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("not-fn-factory-effects.cpp");
  const auto Output = tmpFile("not-fn-factory-effects.nc");
  writeFile(Source, R"cpp(#include <functional>
int builds, calls, destroyed;
struct P {
  int value;
  bool operator()(int x) { ++calls; return x == value; }
  ~P() { ++destroyed; }
};
P make(int &n) { ++n; ++builds; return P{n}; }
int main() {
  int n = 3;
  {
    auto f = std::not_fn(make(n));
    if (n != 4 || builds != 1 || f(4) || !f(3) || calls != 2) return 1;
    if (destroyed != 1) return 2;
  }
  return destroyed != 2;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("not-fn-factory-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2NotFnReferenceWrapperRunsAtBothOptimizations) {
  const auto Source = tmpFile("not-fn-reference-wrapper.cpp");
  const auto Output = tmpFile("not-fn-reference-wrapper.nc");
  writeFile(Source, R"cpp(#include <functional>
struct P {
  int value;
  bool operator()(int x) { value += x; return value > 4; }
};
struct C {
  int value;
  bool operator()(int x) const { return value == x; }
};
int main() {
  P p{1}; const C c{7};
  auto f = std::not_fn(std::ref(p));
  auto g = std::not_fn(std::cref(c));
  if (!f(2) || p.value != 3 || f(3) || p.value != 6) return 1;
  auto copy = f;
  if (copy(1) || p.value != 7 || g(7) || !g(6)) return 2;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("not-fn-reference-wrapper" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2NotFnNestedAdaptersRunAtBothOptimizations) {
  const auto Source = tmpFile("not-fn-nested-adapters.cpp");
  const auto Output = tmpFile("not-fn-nested-adapters.nc");
  writeFile(Source, R"cpp(#include <functional>
int calls;
int value(int x) { ++calls; return x; }
int main() {
  auto f = std::not_fn(std::not_fn(&value));
  if (f(0) || !f(2) || calls != 2) return 1;
  auto g = f;
  if (!g(-3) || calls != 3) return 2;
  return 0;
}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("not-fn-nested-adapters" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

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

TEST_F(TranslateTest, CoreV2BindCallUnusedPrvalueRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-prvalue.cpp");
  const auto Output = tmpFile("bind-call-unused-prvalue.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int twice(int n){return n*2;}
int main(){auto f=std::bind(&twice,21);return f(99)!=42;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-prvalue" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedCvLvaluesRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-cv-lvalues.cpp");
  const auto Output = tmpFile("bind-call-unused-cv-lvalues.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int plus(int a,int b){return a+b;}
int main(){int n=8;const int c=9;auto f=std::bind(&plus,3,4);return f(n,c)!=7||n!=8;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-cv-lvalues" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedEffectsOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-effects-once.cpp");
  const auto Output = tmpFile("bind-call-unused-effects-once.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int bump(int& n){return ++n;}
int plus(int a,int b){return a+b;}
int main(){int a=0,b=0;auto f=std::bind(&plus,3,4);int r=f(bump(a),bump(b));return r!=7||a!=1||b!=1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-effects-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedUpdatesReferentRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-updates-referent.cpp");
  const auto Output = tmpFile("bind-call-unused-updates-referent.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int plus(int a,int b){return a+b;}
int main(){int n=4;auto f=std::bind(&plus,std::ref(n),3);int r=f((n=9));return r!=12||n!=9;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-updates-referent" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedSameBindReentryRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-same-bind-reentry.cpp");
  const auto Output = tmpFile("bind-call-unused-same-bind-reentry.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int bump(int& n){return ++n;}
int main(){auto f=std::bind(&bump,4);int r=f(f());int s=f();return r!=6||s!=7;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-same-bind-reentry" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedConstReceiverRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-const-receiver.cpp");
  const auto Output = tmpFile("bind-call-unused-const-receiver.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int plus(int a,int b){return a+b;}
int main(){const auto f=std::bind(&plus,3,4);return f(9)!=7;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-const-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedRvalueReceiverRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-rvalue-receiver.cpp");
  const auto Output = tmpFile("bind-call-unused-rvalue-receiver.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int bump(int& n){return ++n;}
int main(){auto f=std::bind(&bump,4);int calls=0;int r=std::move(f)(++calls);int s=f();return r!=5||s!=6||calls!=1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-rvalue-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedVoidTargetRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-void-target.cpp");
  const auto Output = tmpFile("bind-call-unused-void-target.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
void add(int& n,int x){n+=x;}
int main(){int n=1,a=0,b=0;auto f=std::bind(&add,std::ref(n),4);f(++a,++b);return n!=5||a!=1||b!=1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-void-target" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedReferenceResultRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-reference-result.cpp");
  const auto Output = tmpFile("bind-call-unused-reference-result.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int& identity(int& n){return n;}
int main(){int n=1;auto f=std::bind(&identity,std::ref(n));int& r=f((n=11));r=12;return n!=12;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-reference-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallReceiverBeforeUnusedEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-receiver-before-unused-effects.cpp");
  const auto Output = tmpFile("bind-call-receiver-before-unused-effects.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int bump(int& n){return ++n;}
int main(){auto a=std::bind(&bump,4);auto b=std::bind(&bump,10);auto* p=&a;int r=(*p)((p=&b,0));int s=a();int t=b();return r!=5||s!=6||t!=11||p!=&b;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-receiver-before-unused-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedRvalueScalarRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-rvalue-scalar.cpp");
  const auto Output = tmpFile("bind-call-unused-rvalue-scalar.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int twice(int n){return n*2;}
int main(){int n=9;auto f=std::bind(&twice,21);return f(std::move(n))!=42||n!=9;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-rvalue-scalar" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindZeroBoundValueRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-zero-bound-value.cpp");
  const auto Output = tmpFile("bind-zero-bound-value.nc");
  writeFile(Source, R"cpp(#include <functional>
int answer(){return 37;}
int main(){auto f=std::bind(&answer);return f()!=37;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-zero-bound-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindZeroBoundDesignatorRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-zero-bound-designator.cpp");
  const auto Output = tmpFile("bind-zero-bound-designator.nc");
  writeFile(Source, R"cpp(#include <functional>
int calls;int answer(){++calls;return 37;}
int main(){auto f=std::bind(answer);return f()!=37||calls!=1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-zero-bound-designator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindZeroBoundCopyCvrefRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-zero-bound-copy-cvref.cpp");
  const auto Output = tmpFile("bind-zero-bound-copy-cvref.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int calls;int answer(){++calls;return 37;}
int main(){auto f=std::bind(&answer);auto copy=f;const auto fixed=copy;return f()!=37||copy()!=37||fixed()!=37||std::move(copy)()!=37||calls!=4;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-zero-bound-copy-cvref" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindZeroBoundFactoryOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-zero-bound-factory-once.cpp");
  const auto Output = tmpFile("bind-zero-bound-factory-once.nc");
  writeFile(Source, R"cpp(#include <functional>
using Callback=int(*)();int selections;int first(){return 7;}int second(){return 9;}
Callback selected=&first;Callback select(){++selections;return selected;}
int main(){auto f=std::bind(select());selected=&second;return f()!=7||f()!=7||selections!=1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-zero-bound-factory-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindZeroBoundVoidTargetRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-zero-bound-void-target.cpp");
  const auto Output = tmpFile("bind-zero-bound-void-target.nc");
  writeFile(Source, R"cpp(#include <functional>
int calls;void touch(){++calls;}
int main(){const auto f=std::bind(&touch);f();f();return calls!=2;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-zero-bound-void-target" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindZeroBoundReferenceResultRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-zero-bound-reference-result.cpp");
  const auto Output = tmpFile("bind-zero-bound-reference-result.nc");
  writeFile(Source, R"cpp(#include <functional>
int stored=5;int& access(){return stored;}
int main(){auto f=std::bind(&access);f()=17;const auto fixed=f;return &fixed()!=&stored||stored!=17;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-zero-bound-reference-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindZeroBoundUnusedEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-zero-bound-unused-effects.cpp");
  const auto Output = tmpFile("bind-zero-bound-unused-effects.nc");
  writeFile(Source, R"cpp(#include <functional>
int effects;int targets;int bump(){return ++effects;}int answer(){++targets;return 37;}
int main(){auto f=std::bind(&answer);int value=3;const int fixed=4;return f(bump(),bump())!=37||effects!=2||targets!=1||f(value,fixed)!=37||targets!=2||value!=3;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-zero-bound-unused-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindZeroBoundResultKindsRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-zero-bound-result-kinds.cpp");
  const auto Output = tmpFile("bind-zero-bound-result-kinds.nc");
  writeFile(Source, R"cpp(#include <functional>
int stored=5;int* address(){return &stored;}double half(){return 0.5;}
int main(){auto pointer=std::bind(&address);auto real=std::bind(&half);return pointer()!=&stored||*pointer()!=5||real()!=0.5;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-zero-bound-result-kinds" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindBoundPackThreeValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-bound-pack-three-values.cpp");
  const auto Output = tmpFile("bind-bound-pack-three-values.nc");
  writeFile(Source, R"cpp(#include <functional>
int sum(int a,int b,int c){return a+b+c;}
int main(){auto f=std::bind(&sum,2,3,5);return f()!=10;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-bound-pack-three-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindBoundPackFourMixedRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-bound-pack-four-mixed.cpp");
  const auto Output = tmpFile("bind-bound-pack-four-mixed.nc");
  writeFile(Source, R"cpp(#include <functional>
double combine(int a,double b,bool c,int* p){*p+=a;return b+*p+(c?1:0);}
int main(){int n=2;auto f=std::bind(&combine,3,0.5,true,&n);return f()!=6.5||n!=5;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-bound-pack-four-mixed" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindBoundPackEightValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-bound-pack-eight-values.cpp");
  const auto Output = tmpFile("bind-bound-pack-eight-values.nc");
  writeFile(Source, R"cpp(#include <functional>
int sum(int a,int b,int c,int d,int e,int f,int g,int h){return a+b+c+d+e+f+g+h;}
int main(){auto f=std::bind(&sum,1,2,3,4,5,6,7,8);return f()!=36;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-bound-pack-eight-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindBoundPackThirdReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-bound-pack-third-reference.cpp");
  const auto Output = tmpFile("bind-bound-pack-third-reference.nc");
  writeFile(Source, R"cpp(#include <functional>
int add_to(int a,int b,int& n){n+=a+b;return n;}
int main(){int n=5;auto f=std::bind(&add_to,2,3,std::ref(n));const auto copy=f;return f()!=10||copy()!=15||f((n=20))!=25||n!=25;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-bound-pack-third-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindBoundPackCopiedStateRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-bound-pack-copied-state.cpp");
  const auto Output = tmpFile("bind-bound-pack-copied-state.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int advance(int a,int b,int& c){return a+b+ ++c;}
int main(){auto f=std::bind(&advance,4,5,6);auto copy=f;return f()!=16||std::move(f)()!=17||copy()!=16||f()!=18;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-bound-pack-copied-state" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindBoundPackCaptureEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-bound-pack-capture-effects.cpp");
  const auto Output = tmpFile("bind-bound-pack-capture-effects.nc");
  writeFile(Source, R"cpp(#include <functional>
int left_builds,middle_builds,right_builds,calls;
int left(){++left_builds;return 3;}int middle(){++middle_builds;return 5;}int right(){++right_builds;return 7;}
int sum(int a,int b,int c){++calls;return a+b+c;}
int main(){auto f=std::bind(&sum,left(),middle(),right());int unused=0;return left_builds!=1||middle_builds!=1||right_builds!=1||calls!=0||f(++unused,++unused)!=15||unused!=2||calls!=1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-bound-pack-capture-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindBoundPackCapturedPointerRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-bound-pack-captured-pointer.cpp");
  const auto Output = tmpFile("bind-bound-pack-captured-pointer.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int first(int a,int b,int c){return a+b+c;}int second(int,int,int){return -1;}
int main(){int(*pointer)(int,int,int)=&first;auto f=std::bind(pointer,2,3,5);pointer=&second;const auto copy=f;return f()!=10||copy()!=10||std::move(f)()!=10||pointer(2,3,5)!=-1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-bound-pack-captured-pointer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindBoundPackResultKindsRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-bound-pack-result-kinds.cpp");
  const auto Output = tmpFile("bind-bound-pack-result-kinds.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <type_traits>
void store(int* p,int a,int b){*p=a+b;}int& identity(int& n,int,int){return n;}
int main(){int n=0;auto write=std::bind(&store,&n,3,4);write();auto live=std::bind(&identity,std::ref(n),1,2);using F=decltype(live);static_assert(std::is_bind_expression<F>::value,"bind expression");static_assert(std::is_same<decltype(live()),int&>::value,"live reference");static_assert(!std::is_nothrow_invocable<F&>::value,"pinned bind exception signature");if(n!=7)return 1;live()=14;return n!=14||&live()!=&n;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-bound-pack-result-kinds" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

// Placeholder runtime goldens: proposed until current source and generated executions pass.

TEST_F(TranslateTest, CoreV2FactoryPlaceholderOneRunAtBothOptimizations) {
  const auto Source = tmpFile("factory-placeholder-one.cpp");
  const auto Output = tmpFile("factory-placeholder-one.nc");
  writeFile(Source, R"cpp(#include <functional>
int identity(int n){return n;}
int main(){auto f=std::bind(&identity,std::placeholders::_1);return 0;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("factory-placeholder-one" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2FactoryPlaceholderParenthesesRunAtBothOptimizations) {
  const auto Source = tmpFile("factory-placeholder-parentheses.cpp");
  const auto Output = tmpFile("factory-placeholder-parentheses.nc");
  writeFile(Source, R"cpp(#include <functional>
int sum(int a,int b){return a+b;}
int main(){auto f=std::bind(&sum,(std::placeholders::_2),((std::placeholders::_1)));return 0;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("factory-placeholder-parentheses" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2FactoryPlaceholderRepeatedRunAtBothOptimizations) {
  const auto Source = tmpFile("factory-placeholder-repeated.cpp");
  const auto Output = tmpFile("factory-placeholder-repeated.nc");
  writeFile(Source, R"cpp(#include <functional>
int sum(int a,int b){return a+b;}
int main(){auto f=std::bind(&sum,std::placeholders::_1,std::placeholders::_1);return 0;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("factory-placeholder-repeated" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2FactoryPlaceholderMixedReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("factory-placeholder-mixed-reference.cpp");
  const auto Output = tmpFile("factory-placeholder-mixed-reference.nc");
  writeFile(Source, R"cpp(#include <functional>
int combine(int& n,int a,int b){return n+a+b;}
int main(){int n=2;auto f=std::bind(&combine,std::ref(n),4,std::placeholders::_1);return n!=2;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("factory-placeholder-mixed-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2FactoryPlaceholderEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("factory-placeholder-effects.cpp");
  const auto Output = tmpFile("factory-placeholder-effects.nc");
  writeFile(Source, R"cpp(#include <functional>
using Callback=int(*)(int,int);int selections,captures;int sum(int a,int b){return a+b;}Callback select(){++selections;return &sum;}int capture(){++captures;return 7;}
int main(){auto f=std::bind(select(),capture(),std::placeholders::_1);return selections!=1||captures!=1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("factory-placeholder-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindPlaceholderOneRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-placeholder-one.cpp");
  const auto Output = tmpFile("bind-placeholder-one.nc");
  writeFile(Source, R"cpp(#include <functional>
int twice(int n){return n*2;}
int main(){auto f=std::bind(&twice,std::placeholders::_1);return f(9)!=18;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-placeholder-one" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindPlaceholderReorderedRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-placeholder-reordered.cpp");
  const auto Output = tmpFile("bind-placeholder-reordered.nc");
  writeFile(Source, R"cpp(#include <functional>
int encode(int a,int b){return 10*a+b;}
int main(){auto f=std::bind(&encode,std::placeholders::_2,std::placeholders::_1);const auto copy=f;return f(3,7)!=73||copy(4,8)!=84;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-placeholder-reordered" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindPlaceholderRepeatedOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-placeholder-repeated-once.cpp");
  const auto Output = tmpFile("bind-placeholder-repeated-once.nc");
  writeFile(Source, R"cpp(#include <functional>
int sum(int a,int b){return a+b;}
int main(){int n=5;auto f=std::bind(&sum,std::placeholders::_1,std::placeholders::_1);return f(++n)!=12||n!=6;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-placeholder-repeated-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindPlaceholderIndependentBoundStateRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-placeholder-independent-bound-state.cpp");
  const auto Output = tmpFile("bind-placeholder-independent-bound-state.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int advance(int& n,int step){n+=step;return n;}
int main(){auto f=std::bind(&advance,1,std::placeholders::_1);auto copy=f;return f(2)!=3||std::move(f)(3)!=6||copy(2)!=3;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-placeholder-independent-bound-state" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindPlaceholderBorrowedStateRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-placeholder-borrowed-state.cpp");
  const auto Output = tmpFile("bind-placeholder-borrowed-state.nc");
  writeFile(Source, R"cpp(#include <functional>
int advance(int& n,int step){n+=step;return n;}
int main(){int n=5;auto f=std::bind(&advance,std::ref(n),std::placeholders::_1);const auto copy=f;return f(2)!=7||copy(3)!=10||f((n=20,4))!=24||n!=24;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-placeholder-borrowed-state" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindPlaceholderUnusedEffectsBeforeReadRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-placeholder-unused-effects-before-read.cpp");
  const auto Output = tmpFile("bind-placeholder-unused-effects-before-read.nc");
  writeFile(Source, R"cpp(#include <functional>
int read(int n){return n;}
int main(){int n=0;auto f=std::bind(&read,std::placeholders::_1);return f(++n,++n)!=2||n!=2;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-placeholder-unused-effects-before-read" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindPlaceholderLiveReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-placeholder-live-reference.cpp");
  const auto Output = tmpFile("bind-placeholder-live-reference.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int& identity(int& n){return n;}
int main(){int n=5;auto f=std::bind(&identity,std::placeholders::_1);const auto copy=f;f(n)=9;return &copy(n)!=&n||std::move(f)(n)!=9;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-placeholder-live-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindPlaceholderCaptureAndQueriesRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-placeholder-capture-and-queries.cpp");
  const auto Output = tmpFile("bind-placeholder-capture-and-queries.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <type_traits>
using Callback=int(*)(int);int selections,calls;int first(int n){++calls;return n+1;}int second(int){return -1;}Callback target=&first;Callback select(){++selections;return target;}
int main(){auto f=std::bind(select(),std::placeholders::_2);using F=decltype(f);static_assert(!std::is_invocable<F&,int>::value,"second call argument required");static_assert(std::is_invocable<F&,int,int>::value,"two call arguments");static_assert(std::is_same<decltype(f(3,4)),int>::value,"scalar result");static_assert(!std::is_nothrow_invocable<F&,int,int>::value,"pinned exception signature");target=&second;return selections!=1||calls!=0||f(3,4)!=5||calls!=1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-placeholder-capture-and-queries" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

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

TEST_F(TranslateTest, CoreV2BindOwnedCallableConstRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-callable-owned-const.cpp");
  const auto Output = tmpFile("bind-callable-owned-const.nc");
  writeFile(Source, R"cpp(#include <functional>
struct Op { int offset; int operator()(int x, int y) const { return offset + x + y; } };
extern "C" int replacement() {
  auto b = std::bind(Op{3}, std::placeholders::_1, 4);
  return b(5) == 12 ? 0 : 1;
}

int main() { return replacement(); }
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-callable-owned-const" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindOwnedCallableIndependentStateRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-callable-independent-state.cpp");
  const auto Output = tmpFile("bind-callable-independent-state.nc");
  writeFile(Source, R"cpp(#include <functional>
struct Counter { int value; int operator()(int x) { value += x; return value; } };
extern "C" int replacement() {
  auto b = std::bind(Counter{1}, std::placeholders::_1);
  int first = b(2);
  auto c = b;
  int second = b(3);
  int copied = c(4);
  return first == 3 && second == 6 && copied == 7 && b(0) == 6 && c(0) == 7 ? 0 : 1;
}

int main() { return replacement(); }
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-callable-independent-state" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindOwnedCallableReceiverCvrefRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-callable-receiver-cvref.cpp");
  const auto Output = tmpFile("bind-callable-receiver-cvref.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
struct Op {
  int value;
  int operator()(int x) & { return value + x + 1; }
  int operator()(int x) const & { return value + x + 10; }
};
extern "C" int replacement() {
  auto b = std::bind(Op{2}, std::placeholders::_1);
  const auto &c = b;
  int left = b(3);
  int constant = c(3);
  int right = std::move(b)(3);
  return left == 6 && constant == 15 && right == 6 ? 0 : 1;
}

int main() { return replacement(); }
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-callable-receiver-cvref" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindReferenceCallableSharedStateRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-callable-reference-target.cpp");
  const auto Output = tmpFile("bind-callable-reference-target.nc");
  writeFile(Source, R"cpp(#include <functional>
struct Counter { int value; int operator()(int x) { value += x; return value; } };
extern "C" int replacement() {
  Counter target{1};
  auto b = std::bind(std::ref(target), std::placeholders::_1);
  auto c = b;
  int first = b(2);
  int shared = c(4);
  target.value = 10;
  int live = b(1);
  return first == 3 && shared == 7 && live == 11 && target.value == 11 ? 0 : 1;
}

int main() { return replacement(); }
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-callable-reference-target" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindReferenceCallableConstWrapperRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-callable-reference-const-wrapper.cpp");
  const auto Output = tmpFile("bind-callable-reference-const-wrapper.nc");
  writeFile(Source, R"cpp(#include <functional>
struct Counter { int value; int operator()(int x) & { value += x; return value; } };
extern "C" int replacement() {
  Counter target{1};
  auto reference = std::ref(target);
  const auto b = std::bind(reference, std::placeholders::_1);
  auto c = b;
  int first = b(2);
  int shared = c(4);
  target.value = 20;
  int live = b(3);
  return first == 3 && shared == 7 && live == 23 && target.value == 23 ? 0 : 1;
}

int main() { return replacement(); }
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-callable-reference-const-wrapper" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindReferenceCallableConstReferentRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-callable-reference-const-referent.cpp");
  const auto Output = tmpFile("bind-callable-reference-const-referent.nc");
  writeFile(Source, R"cpp(#include <functional>
struct Counter { int value; int operator()(int x) const & { return value + x; } };
extern "C" int replacement() {
  const Counter target{7};
  auto b = std::bind(std::cref(target), std::placeholders::_1);
  const auto c = b;
  return b(2) == 9 && c(4) == 11 && target.value == 7 ? 0 : 1;
}

int main() { return replacement(); }
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-callable-reference-const-referent" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}
