
TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerLocalRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-local.cpp");
  const auto Output = tmpFile("bitset-constexpr-text-pointer-local.nc");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main() {
  constexpr const char* text = "101101";
  return std::bitset<8>(text).to_ulong() != 45UL;
}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constexpr-text-pointer-local" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerNamespaceRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-namespace.cpp");
  const auto Output = tmpFile("bitset-constexpr-text-pointer-namespace.nc");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
constexpr const char* text = "0110";
bool test() { return std::bitset<8>(text).test(2); }
int main() { return !test() || std::bitset<8>(text).count() != 2; }
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constexpr-text-pointer-namespace" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerFirstNullRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-first-null.cpp");
  const auto Output = tmpFile("bitset-constexpr-text-pointer-first-null.nc");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main() {
  constexpr const char* text = "101\0x1";
  return std::bitset<8>(text).to_ullong() != 5ULL;
}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constexpr-text-pointer-first-null" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerCustomDigitsRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-custom-digits.cpp");
  const auto Output = tmpFile("bitset-constexpr-text-pointer-custom-digits.nc");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main() {
  constexpr const char* text = "xoo123";
  return std::bitset<6>(text, 3, 'o', 'x').to_ulong() != 4UL;
}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constexpr-text-pointer-custom-digits" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerPointerTypeAliasRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-pointer-type-alias.cpp");
  const auto Output = tmpFile("bitset-constexpr-text-pointer-pointer-type-alias.nc");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
using Text = const char*;
int main() {
  constexpr Text text = "101";
  std::bitset<128> bits(text);
  return bits.count() != 2 || !bits.test(0) || !bits.test(2) || bits.test(64);
}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constexpr-text-pointer-pointer-type-alias" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerZeroCountRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-zero-count.cpp");
  const auto Output = tmpFile("bitset-constexpr-text-pointer-zero-count.nc");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main() {
  constexpr const char* text = "not a binary string";
  return std::bitset<80>(text, 0).any();
}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-constexpr-text-pointer-zero-count" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerGuardMutablePointerRejected) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-guard-mutable-pointer.cpp");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main(){const char* p="1";p="0";return std::bitset<8>(p).any();}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerGuardMutableGlobalRejected) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-guard-mutable-global.cpp");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
const char* p="1";int main(){return std::bitset<8>(p).any();}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerGuardPointerArithmeticRejected) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-guard-pointer-arithmetic.cpp");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main(){constexpr const char* p="01"+1;return std::bitset<8>(p).any();}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerGuardNullRejected) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-guard-null.cpp");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main(){constexpr const char* p=nullptr;return std::bitset<8>(p).any();}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerGuardIndirectInitializerRejected) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-guard-indirect-initializer.cpp");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
constexpr const char* text="1";int main(){constexpr const char* p=text;return std::bitset<8>(p).any();}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerGuardQualifiedWithoutConstexprRejected) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-guard-qualified-without-constexpr.cpp");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main(){const char* const p="1";return std::bitset<8>(p).any();}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerGuardInvalidBeyondWidthRejected) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-guard-invalid-beyond-width.cpp");
  writeFile(Source, R"r19bitctxptr(#include <bitset>
int main(){constexpr const char* p="1?";return std::bitset<1>(p).any();}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstexprTextPointerGuardOwnedDecoderRejected) {
  const auto Source = tmpFile("bitset-constexpr-text-pointer-guard-owned-decoder.cpp");
  writeFile(Source, R"r19bitctxptr(#define __init_from_string_view nct_foreign_text_init
#include <bitset>
#undef __init_from_string_view
int main(){constexpr const char* p="01";return std::bitset<8>(p).any();}
)r19bitctxptr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
