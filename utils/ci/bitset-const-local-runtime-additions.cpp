
TEST_F(TranslateTest, CoreV2BitsetConstLocalEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-const-local-empty.cpp");
  const auto Output = tmpFile("bitset-const-local-empty.nc");
  writeFile(Source, R"r19bitlocal(#include <bitset>
int main() {
  const std::bitset<129> bits;
  return bits.to_ulong() != 0UL || bits.to_ullong() != 0ULL;
}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-const-local-empty" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalSingleWordFitRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-const-local-single-word-fit.cpp");
  const auto Output = tmpFile("bitset-const-local-single-word-fit.nc");
  writeFile(Source, R"r19bitlocal(#include <bitset>
int main() {
  const std::bitset<65> bits{0xfedcba98ULL};
  return bits.to_ulong() != 0xfedcba98UL;
}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-const-local-single-word-fit" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalWideUnsignedArgumentRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-const-local-wide-unsigned-argument.cpp");
  const auto Output = tmpFile("bitset-const-local-wide-unsigned-argument.nc");
  writeFile(Source, R"r19bitlocal(#include <bitset>
unsigned long long value() { return ~0ULL; }
int main() {
  const std::bitset<129> bits(value());
  return bits.to_ullong() != ~0ULL;
}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-const-local-wide-unsigned-argument" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalOneTimeInitializationRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-const-local-one-time-initialization.cpp");
  const auto Output = tmpFile("bitset-const-local-one-time-initialization.nc");
  writeFile(Source, R"r19bitlocal(#include <bitset>
static unsigned calls;
unsigned long long next() { ++calls; return 0xabcdef0123ULL; }
int main() {
  const std::bitset<129> first(next() & 0xffffffffULL), second(next() % 257ULL);
  const auto a = first.to_ulong();
  const auto b = second.to_ulong();
  return a != 0xcdef0123UL || b != 0xabcdef0123ULL % 257ULL || calls != 2;
}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-const-local-one-time-initialization" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalLiveSourceIndependenceRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-const-local-live-source-independence.cpp");
  const auto Output = tmpFile("bitset-const-local-live-source-independence.nc");
  writeFile(Source, R"r19bitlocal(#include <bitset>
int main() {
  unsigned long long value = 31;
  const std::bitset<65> bits(value & 0xffffffffULL);
  value = 73;
  return bits.to_ulong() != 31UL || value != 73;
}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-const-local-live-source-independence" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalConstAliasTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-const-local-const-alias-type.cpp");
  const auto Output = tmpFile("bitset-const-local-const-alias-type.nc");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<192>;
using Frozen = const Bits;
int main() {
  Frozen bits{123456789ULL};
  return bits.to_ulong() != 123456789UL || bits.to_ullong() != 123456789ULL;
}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-const-local-const-alias-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalGuardMutableLocalRejected) {
  const auto Source = tmpFile("bitset-const-local-guard-mutable-local.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
int main(){Bits bits(1);bits.set(1);return bits.to_ullong()!=3;}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalGuardAliasToMutableRejected) {
  const auto Source = tmpFile("bitset-const-local-guard-alias-to-mutable.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
int main(){Bits bits(1);const Bits& r=bits;bits.set(1);return r.to_ullong()!=3;}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalGuardConstReferenceRejected) {
  const auto Source = tmpFile("bitset-const-local-guard-const-reference.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
int main(){const Bits& bits=Bits(2);return bits.to_ullong()!=2;}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalGuardStaticConstRejected) {
  const auto Source = tmpFile("bitset-const-local-guard-static-const.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
int main(){static const Bits bits(2);return bits.to_ullong()!=2;}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalGuardConstParameterRejected) {
  const auto Source = tmpFile("bitset-const-local-guard-const-parameter.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
unsigned long long convert(const Bits& bits){return bits.to_ullong();}
int main(){const Bits bits(2);return convert(bits)!=2;}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalGuardCopyInitializedRejected) {
  const auto Source = tmpFile("bitset-const-local-guard-copy-initialized.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
int main(){Bits a(3);const Bits b=a;return b.to_ullong()!=3;}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalGuardConstMemberRejected) {
  const auto Source = tmpFile("bitset-const-local-guard-const-member.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
struct State{const Bits bits{2};};
int main(){State state;return state.bits.to_ullong()!=2;}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetConstLocalGuardConditionalReceiverRejected) {
  const auto Source = tmpFile("bitset-const-local-guard-conditional-receiver.cpp");
  writeFile(Source, R"r19bitlocal(#include <bitset>
using Bits = std::bitset<129>;
unsigned long long convert(bool select){const Bits a(1),b(2);return (select?a:b).to_ullong();}
int main(){return convert(true)!=1||convert(false)!=2;}
)r19bitlocal");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
