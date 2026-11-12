
TEST_F(TranslateTest, CoreV2BindResultScalarConversionRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-result-scalar-conversion.cpp");
  const auto Output = tmpFile("bind-result-scalar-conversion.nc");
  writeFile(Source, R"result_bind(#include <functional>
#include <type_traits>
int add(int left, int right) { return left + right; }
extern "C" int replacement() {
  const auto value = std::bind<long>(&add, std::placeholders::_1, 7);
  static_assert(std::is_same<decltype(value(3)), long>::value, "explicit result");
  static_assert(std::is_same<typename decltype(value)::result_type, long>::value, "result_type");
  return value(3) == 10L ? 0 : 1;
}

int main() { return replacement(); }
)result_bind");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-result-scalar-conversion" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindResultVoidDiscardRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-result-void-discard.cpp");
  const auto Output = tmpFile("bind-result-void-discard.nc");
  writeFile(Source, R"result_bind(#include <functional>
#include <type_traits>
#include <utility>
int add_to(int& value, int amount) { value += amount; return value; }
extern "C" int replacement() {
  int value = 5;
  auto discard = std::bind<void>(&add_to, std::ref(value), std::placeholders::_1);
  static_assert(std::is_same<decltype(discard(4)), void>::value, "void result");
  discard(4);
  std::move(discard)(3);
  return value == 12 ? 0 : 1;
}

int main() { return replacement(); }
)result_bind");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-result-void-discard" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindResultLiveReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-result-live-reference.cpp");
  const auto Output = tmpFile("bind-result-live-reference.nc");
  writeFile(Source, R"result_bind(#include <functional>
#include <type_traits>
int& identity(int& value) { return value; }
extern "C" int replacement() {
  int value = 4;
  const auto read = std::bind<const int&>(&identity, std::placeholders::_1);
  auto write = std::bind<int&>(&identity, std::placeholders::_1);
  static_assert(std::is_same<decltype(read(value)), const int&>::value, "const live reference");
  const int& live = read(value);
  write(value) = 11;
  return &live == &value && &write(value) == &value && live == 11 ? 0 : 1;
}

int main() { return replacement(); }
)result_bind");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-result-live-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindResultNullaryUnusedEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-result-nullary-unused-effects.cpp");
  const auto Output = tmpFile("bind-result-nullary-unused-effects.nc");
  writeFile(Source, R"result_bind(#include <functional>
#include <utility>
int effects;
int calls;
int target() { ++calls; return effects + 5; }
extern "C" int replacement() {
  auto value = std::bind<unsigned>(&target);
  const auto copy = value;
  const unsigned first = value(++effects, ++effects);
  const unsigned second = std::move(copy)(effects);
  return first == 7u && second == 7u && effects == 2 && calls == 2 ? 0 : 1;
}

int main() { return replacement(); }
)result_bind");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-result-nullary-unused-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindResultOwnedCallableCopiesRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-result-owned-callable-copies.cpp");
  const auto Output = tmpFile("bind-result-owned-callable-copies.nc");
  writeFile(Source, R"result_bind(#include <functional>
#include <utility>
struct Counter {
  int value;
  int operator()(int amount) { value += amount; return value; }
};
extern "C" int replacement() {
  auto value = std::bind<long>(Counter{4}, std::placeholders::_1);
  auto copy = value;
  const long first = value(3);
  const long independent = copy(2);
  const long moved_expression = std::move(value)(1);
  return first == 7L && independent == 6L && moved_expression == 8L ? 0 : 1;
}

int main() { return replacement(); }
)result_bind");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-result-owned-callable-copies" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindResultMemberTargetsRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-result-member-targets.cpp");
  const auto Output = tmpFile("bind-result-member-targets.nc");
  writeFile(Source, R"result_bind(#include <functional>
struct Item {
  int value;
  int add(int amount) { value += amount; return value; }
  int read() const { return value; }
};
extern "C" int replacement() {
  Item item{3};
  auto owned = std::bind<long>(&Item::add, Item{5}, std::placeholders::_1);
  const auto borrowed = std::bind<long>(&Item::read, std::cref(item));
  auto data = std::bind<int&>(&Item::value, std::placeholders::_1);
  const long first = owned(2);
  data(item) = 11;
  return first == 7L && borrowed() == 11L && &data(item) == &item.value ? 0 : 1;
}

int main() { return replacement(); }
)result_bind");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-result-member-targets" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindResultStandardCallablesRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-result-standard-callables.cpp");
  const auto Output = tmpFile("bind-result-standard-callables.nc");
  writeFile(Source, R"result_bind(#include <functional>
#include <type_traits>
extern "C" int replacement() {
  const auto sum = std::bind<long>(std::plus<int>{}, std::placeholders::_2, std::placeholders::_1);
  auto bit = std::bind<unsigned>(std::less<>{}, std::placeholders::_1, 5);
  static_assert(std::is_same<decltype(bit(3)), unsigned>::value, "converted bool");
  return sum(4, 9) == 13L && bit(3) == 1u && bit(8) == 0u ? 0 : 1;
}

int main() { return replacement(); }
)result_bind");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-result-standard-callables" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindResultOrdinaryBodyGuardRejected) {
  const auto Source = tmpFile("bind-result-ordinary-body-guard.cpp");
  const auto Output = tmpFile("bind-result-ordinary-body-guard.nc");
  writeFile(Source, R"result_bind(#include <functional>
int forbidden(int value) { asm volatile("" ::: "memory"); return value; }
extern "C" int replacement() {
  auto value = std::bind<long>(&forbidden, std::placeholders::_1);
  return value(3) == 3L ? 0 : 1;
}
)result_bind");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_FALSE(fs::exists(Output));
  EXPECT_NE(Result.err.find("TR0201"), std::string::npos) << Result.err;
  EXPECT_NE(Result.err.find("GCCAsmStmt"), std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BindResultInvalidConversionRejected) {
  const auto Source = tmpFile("bind-result-invalid-conversion.cpp");
  const auto Output = tmpFile("bind-result-invalid-conversion.nc");
  writeFile(Source, R"result_bind(#include <functional>
struct Unrelated {};
int target() { return 3; }
extern "C" int replacement() {
  auto value = std::bind<Unrelated>(&target);
  value();
  return 0;
}
)result_bind");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_FALSE(fs::exists(Output));
  EXPECT_NE(Result.err.find("TR0202"), std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BindResultVoidBodyGuardRejected) {
  const auto Source = tmpFile("bind-result-void-body-guard.cpp");
  const auto Output = tmpFile("bind-result-void-body-guard.nc");
  writeFile(Source, R"result_bind(#include <functional>
int forbidden(int value) { asm volatile("" ::: "memory"); return value; }
extern "C" int replacement() {
  auto discard = std::bind<void>(&forbidden, std::placeholders::_1);
  discard(3);
  return 0;
}
)result_bind");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_FALSE(fs::exists(Output));
  EXPECT_NE(Result.err.find("TR0201"), std::string::npos) << Result.err;
  EXPECT_NE(Result.err.find("GCCAsmStmt"), std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BindResultInvalidReferenceRejected) {
  const auto Source = tmpFile("bind-result-invalid-reference.cpp");
  const auto Output = tmpFile("bind-result-invalid-reference.nc");
  writeFile(Source, R"result_bind(#include <functional>
int target(int value) { return value; }
extern "C" int replacement() {
  auto reference = std::bind<int&>(&target, std::placeholders::_1);
  reference(3);
  return 0;
}
)result_bind");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_FALSE(fs::exists(Output));
  EXPECT_NE(Result.err.find("TR0202"), std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BindResultTemporaryReferenceLifetimeGuardRejected) {
  const auto Source = tmpFile("bind-result-temporary-reference-lifetime-guard.cpp");
  const auto Output = tmpFile("bind-result-temporary-reference-lifetime-guard.nc");
  writeFile(Source, R"result_bind(#include <functional>
int target(int value) { return value; }
extern "C" int replacement() {
  auto borrow = std::bind<const int&>(&target, std::placeholders::_1);
  return borrow(3);
}
)result_bind");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_FALSE(fs::exists(Output));
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
