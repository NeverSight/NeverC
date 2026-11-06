
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
