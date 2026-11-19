
TEST_F(TranslateTest, CoreV2BitsetWideTemporaryEmptyMultiwordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-wide-temporary-empty-multiword.cpp");
  const auto Output = tmpFile("bitset-wide-temporary-empty-multiword.nc");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
int main(){return std::bitset<129>{}.to_ulong()!=0UL;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-wide-temporary-empty-multiword" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryMaxUnsignedWordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-wide-temporary-max-unsigned-word.cpp");
  const auto Output = tmpFile("bitset-wide-temporary-max-unsigned-word.nc");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
int main(){return std::bitset<129>{~0ULL}.to_ullong()!=~0ULL;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-wide-temporary-max-unsigned-word" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryKnownNarrowResultRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-wide-temporary-known-narrow-result.cpp");
  const auto Output = tmpFile("bitset-wide-temporary-known-narrow-result.nc");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
int main(){return std::bitset<65>(0xfedcba98ULL).to_ulong()!=0xfedcba98UL;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-wide-temporary-known-narrow-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryArgumentCalledOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-wide-temporary-argument-called-once.cpp");
  const auto Output = tmpFile("bitset-wide-temporary-argument-called-once.nc");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
static int calls;
unsigned long long word(){++calls;return 0x8000000000000000ULL;}
int main(){auto v=std::bitset<65>(word()).to_ullong();return v!=0x8000000000000000ULL||calls!=1;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-wide-temporary-argument-called-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryMaskFitRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-wide-temporary-mask-fit.cpp");
  const auto Output = tmpFile("bitset-wide-temporary-mask-fit.nc");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
static int calls;
unsigned long long word(){++calls;return 0x1020304005060708ULL;}
int main(){auto v=std::bitset<129>(word()&0xffffffffULL).to_ulong();return v!=0x05060708UL||calls!=1;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-wide-temporary-mask-fit" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryModulusFitRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-wide-temporary-modulus-fit.cpp");
  const auto Output = tmpFile("bitset-wide-temporary-modulus-fit.nc");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
static int calls;
unsigned long long source(){++calls;return 9876543210ULL;}
int main(){auto v=std::bitset<65>(source()%257ULL).to_ulong();return v!=9876543210ULL%257ULL||calls!=1;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-wide-temporary-modulus-fit" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardSavedObjectRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-saved-object.cpp");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
int main(){std::bitset<129> b(33);return b.to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardOverflowingWordRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-overflowing-word.cpp");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
int main(){std::bitset<129> b(33);b.set(128);return b.to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardFactoryValueRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-factory-value.cpp");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
std::bitset<129> make(){return std::bitset<129>{33};}
int main(){return make().to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardIndirectReceiverRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-indirect-receiver.cpp");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
int main(){auto* b=new std::bitset<129>{33};auto v=b->to_ullong();delete b;return v!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardCopyConstructionRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-copy-construction.cpp");
  writeFile(Source, R"r19bitwidechar(#include <bitset>
int main(){std::bitset<129> b(33);return std::bitset<129>{b}.to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardStorageRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-storage.cpp");
  writeFile(Source, R"r19bitwidechar(#define __first_ nct_alternate_first
#include <bitset>
#undef __first_
int main(){return std::bitset<129>{33}.to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardRangeDependencyRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-range-dependency.cpp");
  writeFile(Source, R"r19bitwidechar(#define __make_iter nct_alternate_make_iter
#include <bitset>
#undef __make_iter
int main(){return std::bitset<129>{33}.to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardOverflowDeclarationRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-overflow-declaration.cpp");
  writeFile(Source, R"r19bitwidechar(#define __throw_overflow_error nct_alternate_throw_overflow_error
#include <bitset>
#undef __throw_overflow_error
int main(){return std::bitset<129>{33}.to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardCtzMacroRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-ctz-macro.cpp");
  writeFile(Source, R"r19bitwidechar(int alternate_l(unsigned long value) { return value ? __builtin_ctzl(value) : 0; }
int alternate_ll(unsigned long long value) { return value ? __builtin_ctzll(value) : 0; }
#define __builtin_ctzl alternate_l
#define __builtin_ctzll alternate_ll
#include <bitset>
#undef __builtin_ctzll
#undef __builtin_ctzl
int main() { return std::bitset<129>{33}.to_ullong() != 33; }
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardCharBitRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-char-bit.cpp");
  writeFile(Source, R"r19bitwidechar(#include <climits>
#undef CHAR_BIT
#define CHAR_BIT 8
#include <bitset>
int main(){return std::bitset<129>{33}.to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWideTemporaryGuardCompilerCharBitRejected) {
  const auto Source = tmpFile("bitset-wide-temporary-guard-compiler-char-bit.cpp");
  writeFile(Source, R"r19bitwidechar(#include <climits>
#undef __CHAR_BIT__
#define __CHAR_BIT__ 8
#include <bitset>
int main(){return std::bitset<129>{33}.to_ullong()!=33;}
)r19bitwidechar");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
