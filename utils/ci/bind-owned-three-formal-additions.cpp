
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
