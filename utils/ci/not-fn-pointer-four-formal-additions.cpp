
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
