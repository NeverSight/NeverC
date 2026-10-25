
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
