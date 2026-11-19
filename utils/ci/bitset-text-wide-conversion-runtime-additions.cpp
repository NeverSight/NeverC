
TEST_F(TranslateTest, CoreV2BitsetTextWideConversionUnsignedLongWidthRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-text-wide-conversion-unsigned-long-width.cpp");
  const auto Output = tmpFile("bitset-text-wide-conversion-unsigned-long-width.nc");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){return std::bitset<128>("11111111111111111111111111111111").to_ulong()!=0xffffffffUL;}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-text-wide-conversion-unsigned-long-width" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionUnsignedLongLongWidthRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-text-wide-conversion-unsigned-long-long-width.cpp");
  const auto Output = tmpFile("bitset-text-wide-conversion-unsigned-long-long-width.nc");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){return std::bitset<128>("1000000000000000000000000000000000000000000000000000000000000001").to_ullong()!=0x8000000000000001ULL;}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-text-wide-conversion-unsigned-long-long-width" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionConstexprPointerRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-text-wide-conversion-constexpr-pointer.cpp");
  const auto Output = tmpFile("bitset-text-wide-conversion-constexpr-pointer.nc");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
constexpr const char* text="00100111";
int main(){return std::bitset<257>(text).to_ullong()!=39ULL;}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-text-wide-conversion-constexpr-pointer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionOwningSubstringRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-text-wide-conversion-owning-substring.cpp");
  const auto Output = tmpFile("bitset-text-wide-conversion-owning-substring.nc");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){
  const std::bitset<129> value(std::string("skipxoo"),4,3,'o','x');
  return value.to_ulong()!=4UL;
}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-text-wide-conversion-owning-substring" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionEmptyOwningRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-text-wide-conversion-empty-owning.cpp");
  const auto Output = tmpFile("bitset-text-wide-conversion-empty-owning.nc");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){return std::bitset<129>(std::string()).to_ulong()!=0UL;}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-text-wide-conversion-empty-owning" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionZeroUpperStorageRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-text-wide-conversion-zero-upper-storage.cpp");
  const auto Output = tmpFile("bitset-text-wide-conversion-zero-upper-storage.nc");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){
  const std::bitset<194> value("0000000000000000000000000000000000000000000000000000000000000000001000000000000000000000000000000000000000000000000000000000000001");
  return value.to_ullong()!=0x8000000000000001ULL;
}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-text-wide-conversion-zero-upper-storage" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionAboveUnsignedLongRejected) {
  const auto Source = tmpFile("bitset-text-wide-conversion-above-unsigned-long.cpp");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){return std::bitset<128>("10000000000000000000000000000000000000000000000000000000000000000").to_ulong()==0UL;}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionAboveUnsignedLongLongRejected) {
  const auto Source = tmpFile("bitset-text-wide-conversion-above-unsigned-long-long.cpp");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){
  const std::bitset<129> value(std::string("10000000000000000000000000000000000000000000000000000000000000000"));
  return value.to_ullong()==0ULL;
}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionHighWordFromCustomDigitsRejected) {
  const auto Source = tmpFile("bitset-text-wide-conversion-high-word-from-custom-digits.cpp");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){return std::bitset<129>("xoooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooooo",65,'o','x').to_ullong()==0ULL;}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionMutableObjectRejected) {
  const auto Source = tmpFile("bitset-text-wide-conversion-mutable-object.cpp");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){std::bitset<129> value("1");value.set(128);return value.to_ullong()==0ULL;}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionRuntimeTextRejected) {
  const auto Source = tmpFile("bitset-text-wide-conversion-runtime-text.cpp");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(int argc,char** argv){
  return argc==2 && std::bitset<129>(argv[1]).to_ullong()==0ULL;
}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetTextWideConversionInvalidDigitInSelectedRangeRejected) {
  const auto Source = tmpFile("bitset-text-wide-conversion-invalid-digit-in-selected-range.cpp");
  writeFile(Source, R"r19bittextwide(#include <bitset>
#include <string>
int main(){return std::bitset<129>(std::string("101?")).to_ullong()==0ULL;}
)r19bittextwide");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
