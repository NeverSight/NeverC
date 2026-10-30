
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
