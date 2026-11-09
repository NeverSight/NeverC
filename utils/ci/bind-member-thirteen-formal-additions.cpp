
TEST_F(TranslateTest, CoreV2MemberSourceBindCallableMemberPointerRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-callable-member-pointer.cpp");
  const auto Output = tmpFile("bind-callable-member-pointer.nc");
  writeFile(Source, R"member(#include <functional>
struct Counter { int value; int add(int x) { value += x; return value; } };
extern "C" int replacement() {
  Counter target{2};
  auto b = std::bind(&Counter::add, std::ref(target), std::placeholders::_1);
  int first = b(3);
  int second = b(4);
  return first == 5 && second == 9 && target.value == 9 ? 0 : 1;
}

int main() { return replacement(); }
)member");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-callable-member-pointer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2MemberSourceBindMemberOwnedCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-member-owned-copy.cpp");
  const auto Output = tmpFile("bind-member-owned-copy.nc");
  writeFile(Source, R"member(#include <functional>
struct Counter { int value; int add(int x) { value += x; return value; } };
extern "C" int replacement() {
  Counter target{4};
  auto bound = std::bind(&Counter::add, target, std::placeholders::_1);
  const int first = bound(3);
  auto copy = bound;
  const int independent = copy(2);
  return first == 7 && independent == 9 && bound(1) == 8 && target.value == 4 ? 0 : 1;
}

int main() { return replacement(); }
)member");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-member-owned-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2MemberSourceBindMemberPointerReceiversRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-member-pointer-receivers.cpp");
  const auto Output = tmpFile("bind-member-pointer-receivers.nc");
  writeFile(Source, R"member(#include <functional>
struct Counter { int value; int add(int x) { value += x; return value; } };
extern "C" int replacement() {
  Counter first{4}, second{10};
  auto bound = std::bind(&Counter::add, &first, std::placeholders::_1);
  auto incoming = std::bind(&Counter::add, std::placeholders::_1, 2);
  return bound(3) == 7 && incoming(&second) == 12 && first.value == 7 && second.value == 12 ? 0 : 1;
}

int main() { return replacement(); }
)member");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-member-pointer-receivers" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2MemberSourceBindMemberIncomingObjectRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-member-incoming-object.cpp");
  const auto Output = tmpFile("bind-member-incoming-object.nc");
  writeFile(Source, R"member(#include <functional>
struct Counter { int value; int add(int x) { value += x; return value; } };
extern "C" int replacement() {
  Counter target{4};
  auto call = std::bind(&Counter::add, std::placeholders::_1, std::placeholders::_2);
  const int result = call(target, 3);
  return result == 7 && target.value == 7 ? 0 : 1;
}

int main() { return replacement(); }
)member");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-member-incoming-object" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2MemberSourceBindMemberConstMethodRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-member-const-method.cpp");
  const auto Output = tmpFile("bind-member-const-method.nc");
  writeFile(Source, R"member(#include <functional>
struct Counter { int value; int read(int x) const { return value + x; } };
extern "C" int replacement() {
  const Counter target{4};
  auto borrowed = std::bind(&Counter::read, std::cref(target), std::placeholders::_1);
  const auto owned = std::bind(&Counter::read, target, std::placeholders::_1);
  return borrowed(3) == 7 && owned(5) == 9 && target.value == 4 ? 0 : 1;
}

int main() { return replacement(); }
)member");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-member-const-method" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2MemberSourceBindMemberDataReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-member-data-reference.cpp");
  const auto Output = tmpFile("bind-member-data-reference.nc");
  writeFile(Source, R"member(#include <functional>
struct Counter { int value; };
extern "C" int replacement() {
  Counter target{4};
  const auto borrowed = std::bind(&Counter::value, std::ref(target));
  auto incoming = std::bind(&Counter::value, std::placeholders::_1);
  auto pointer = std::bind(&Counter::value, &target);
  borrowed() = 9;
  incoming(target) += 3;
  pointer() += 2;
  return target.value == 14 && &borrowed() == &target.value ? 0 : 1;
}

int main() { return replacement(); }
)member");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-member-data-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2MemberSourceBindMemberDistinctTargetsRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-member-distinct-targets.cpp");
  const auto Output = tmpFile("bind-member-distinct-targets.nc");
  writeFile(Source, R"member(#include <functional>
struct Counter {
  int value;
  int add(int x) { value += x; return value; }
  int subtract(int x) { value -= x; return value; }
};
extern "C" int replacement() {
  Counter target{4};
  auto add = std::bind(&Counter::add, std::ref(target), std::placeholders::_1);
  auto subtract = std::bind(&Counter::subtract, std::ref(target), std::placeholders::_1);
  const auto copy = subtract;
  const int first = add(3);
  const int second = subtract(2);
  return first == 7 && second == 5 && copy(1) == 4 && target.value == 4 ? 0 : 1;
}

int main() { return replacement(); }
)member");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-member-distinct-targets" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2MemberSourceBindMemberOrdinaryBodyGuardRejected) {
  const auto Source = tmpFile("bind-member-ordinary-body-guard.cpp");
  const auto Output = tmpFile("bind-member-ordinary-body-guard.nc");
  writeFile(Source, R"member(#include <functional>
struct Counter { int value; int add(int x) { asm volatile("" ::: "memory"); return value + x; } };
extern "C" int replacement() {
  Counter target{4};
  auto call = std::bind(&Counter::add, std::ref(target), std::placeholders::_1);
  return call(3);
}
)member");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_FALSE(fs::exists(Output));
  EXPECT_NE(Result.err.find("TR0201"), std::string::npos) << Result.err;
  EXPECT_NE(Result.err.find("GCCAsmStmt"), std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2MemberSourceBindMemberInvalidConstOwnedRejected) {
  const auto Source = tmpFile("bind-member-invalid-const-owned.cpp");
  const auto Output = tmpFile("bind-member-invalid-const-owned.nc");
  writeFile(Source, R"member(#include <functional>
struct Counter { int value; int add(int x) { value += x; return value; } };
extern "C" int replacement() {
  Counter target{4};
  const auto call = std::bind(&Counter::add, target, std::placeholders::_1);
  return call(3);
}
)member");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_FALSE(fs::exists(Output));
  EXPECT_NE(Result.err.find("TR0202"), std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2MemberSourceBindMemberValueAssignmentGuardRejected) {
  const auto Source = tmpFile("bind-member-value-assignment-guard.cpp");
  const auto Output = tmpFile("bind-member-value-assignment-guard.nc");
  writeFile(Source, R"member(#include <functional>
struct Counter {
  int value;
  int add(int x) { value += x; return value; }
  int subtract(int x) { value -= x; return value; }
};
extern "C" int replacement() {
  Counter target{4};
  auto bound = std::bind(&Counter::add, std::ref(target), std::placeholders::_1);
  auto other = std::bind(&Counter::subtract, std::ref(target), std::placeholders::_1);
  bound = other;
  return bound(2);
}
)member");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_FALSE(fs::exists(Output));
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2MemberSourceBindMemberValueAddressEscapeGuardRejected) {
  const auto Source = tmpFile("bind-member-value-address-escape-guard.cpp");
  const auto Output = tmpFile("bind-member-value-address-escape-guard.nc");
  writeFile(Source, R"member(#include <functional>
struct Counter { int value; int add(int x) { value += x; return value; } };
extern "C" void mutate(void *);
extern "C" int replacement() {
  Counter target{4};
  auto bound = std::bind(&Counter::add, std::ref(target), std::placeholders::_1);
  mutate(&bound);
  return bound(2);
}
)member");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_FALSE(fs::exists(Output));
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2MemberSourceBindMemberValueRetargetedInputGuardRejected) {
  const auto Source = tmpFile("bind-member-value-retargeted-input-guard.cpp");
  const auto Output = tmpFile("bind-member-value-retargeted-input-guard.nc");
  writeFile(Source, R"member(#include <functional>
struct Counter {
  int value;
  int add(int x) { value += x; return value; }
  int subtract(int x) { value -= x; return value; }
};
extern "C" int replacement() {
  Counter target{4};
  auto method = &Counter::add;
  method = &Counter::subtract;
  auto bound = std::bind(method, std::ref(target), std::placeholders::_1);
  return bound(2);
}
)member");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_FALSE(fs::exists(Output));
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2MemberSourceBindMemberValueConditionalObjectGuardRejected) {
  const auto Source = tmpFile("bind-member-value-conditional-object-guard.cpp");
  const auto Output = tmpFile("bind-member-value-conditional-object-guard.nc");
  writeFile(Source, R"member(#include <functional>
struct Counter {
  int value;
  int add(int x) { value += x; return value; }
  int subtract(int x) { value -= x; return value; }
};
extern "C" int replacement() {
  Counter target{4};
  auto first = std::bind(&Counter::add, std::ref(target), std::placeholders::_1);
  auto second = std::bind(&Counter::subtract, std::ref(target), std::placeholders::_1);
  const bool choose = true;
  return (choose ? first : second)(2);
}
)member");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_FALSE(fs::exists(Output));
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
