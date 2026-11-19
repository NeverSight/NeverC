
TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralDefaultDigitsRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-owned-literal-default-digits.cpp");
  const auto Output = tmpFile("bitset-owned-literal-default-digits.nc");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main() {
  std::bitset<7> value{std::string("0010110")};
  return value.to_ulong() != 22 || value.count() != 3;
}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-owned-literal-default-digits" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralOffsetAndCountRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-owned-literal-offset-and-count.cpp");
  const auto Output = tmpFile("bitset-owned-literal-offset-and-count.nc");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main() {
  std::bitset<8> value{std::string("prefixyxxymore"), 6, 4, 'x', 'y'};
  return value.to_ulong() != 9 || value.count() != 2;
}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-owned-literal-offset-and-count" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralAllocatedStringCrossesWordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-owned-literal-allocated-string-crosses-word.cpp");
  const auto Output = tmpFile("bitset-owned-literal-allocated-string-crosses-word.nc");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main() {
  std::bitset<70> value{std::string("1000000000000000000000000000000000000000000000000000000000000000001001", 70)};
  return value.count() != 3 || !value.test(69) || !value.test(3) || !value.test(0);
}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-owned-literal-allocated-string-crosses-word" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralZeroAsADigitRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-owned-literal-zero-as-a-digit.cpp");
  const auto Output = tmpFile("bitset-owned-literal-zero-as-a-digit.nc");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main() {
  std::bitset<6> value{std::string("A\0A?", 4), 0, 3, 'A', '\0'};
  return value.to_ulong() != 2 || value.count() != 1;
}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-owned-literal-zero-as-a-digit" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralEmptyInputRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-owned-literal-empty-input.cpp");
  const auto Output = tmpFile("bitset-owned-literal-empty-input.nc");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main() {
  std::bitset<0> empty{std::string{}};
  std::bitset<8> end{std::string("11"), 2};
  std::bitset<8> zero{std::string("invalid"), 0, 0};
  return empty.any() || !empty.all() || end.any() || zero.any();
}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-owned-literal-empty-input" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralEqualDigitsRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-owned-literal-equal-digits.cpp");
  const auto Output = tmpFile("bitset-owned-literal-equal-digits.nc");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main() {
  std::bitset<4> value{std::string("zzz"), 0, 3, 'z', 'z'};
  return value.to_ulong() != 7 || value.count() != 3;
}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-owned-literal-equal-digits" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardNamedStringRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-named-string.cpp");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main(){std::string text("101");return std::bitset<8>{text}.any();}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardFactoryStringRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-factory-string.cpp");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
std::string text(){return std::string("101");}
int main(){return std::bitset<8>{text()}.any();}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardOutOfRangeOffsetRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-out-of-range-offset.cpp");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main(){return std::bitset<8>{std::string("101"),4}.any();}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardRuntimeOffsetRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-runtime-offset.cpp");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
bool use(unsigned position){return std::bitset<8>{std::string("101"),position}.any();}
int main(){return use(1)!=true;}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardInvalidSelectedDigitRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-invalid-selected-digit.cpp");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main(){return std::bitset<1>{std::string("1x"),0,2}.any();}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardNonliteralConstructorInputRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-nonliteral-constructor-input.cpp");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
bool use(const char* data){return std::bitset<8>{std::string(data)}.any();}
int main(){return use("101")!=true;}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardNonCharTraitsRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-non-char-traits.cpp");
  writeFile(Source, R"r19bitowned(#include <bitset>
#include <string>
int main(){return std::bitset<8>{std::u16string(u"101")}.any();}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardSdkBodyRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-sdk-body.cpp");
  writeFile(Source, R"r19bitowned(#define __init_from_string_view nct_alternate_init
#include <bitset>
#include <string>
#undef __init_from_string_view
int main(){return std::bitset<8>{std::string("101")}.to_ulong()!=5;}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedLiteralGuardRepresentationQueryRejected) {
  const auto Source = tmpFile("bitset-owned-literal-guard-representation-query.cpp");
  writeFile(Source, R"r19bitowned(#define __builtin_constant_p(...) 0
#include <bitset>
#include <string>
#undef __builtin_constant_p
int main(){return std::bitset<8>{std::string("101")}.to_ulong()!=5;}
)r19bitowned");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
