
TEST_F(TranslateTest, CoreV2BitsetIndexTypeLastByteIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-index-type-last-byte-index.cpp");
  const auto Output = tmpFile("bitset-index-type-last-byte-index.nc");
  writeFile(Source, R"r19bitwiden(#include <bitset>
void set(std::bitset<256>& bits, unsigned char pos) { bits.set(pos); }
int main() {
  std::bitset<256> bits;
  set(bits, 255);
  return !bits.test(255) || bits.count() != 1;
}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-index-type-last-byte-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeWordBoundariesRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-index-type-word-boundaries.cpp");
  const auto Output = tmpFile("bitset-index-type-word-boundaries.nc");
  writeFile(Source, R"r19bitwiden(#include <bitset>
void update(std::bitset<257>& bits, unsigned char a, unsigned char b) {
  bits.set(a).flip(b);
}
int main() {
  std::bitset<257> bits;
  update(bits, 63, 64);
  return !bits.test(63) || !bits.test(64) || bits.count() != 2;
}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-index-type-word-boundaries" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeBooleanIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-index-type-boolean-index.cpp");
  const auto Output = tmpFile("bitset-index-type-boolean-index.nc");
  writeFile(Source, R"r19bitwiden(#include <bitset>
bool set_and_test(std::bitset<2>& bits, bool position) {
  bits.set(position);
  return bits.test(position);
}
int main() {
  std::bitset<2> bits;
  if (!set_and_test(bits, true) || !set_and_test(bits, false)) return 1;
  return !bits.all();
}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-index-type-boolean-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeResetAndFlipRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-index-type-reset-and-flip.cpp");
  const auto Output = tmpFile("bitset-index-type-reset-and-flip.nc");
  writeFile(Source, R"r19bitwiden(#include <bitset>
void change(std::bitset<256>& bits, unsigned char pos) {
  bits.flip(pos).reset(pos);
}
int main() {
  std::bitset<256> bits;
  bits.set(128);
  change(bits, 127);
  change(bits, 128);
  return bits.any();
}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-index-type-reset-and-flip" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeUnsignedPointerLoadRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-index-type-unsigned-pointer-load.cpp");
  const auto Output = tmpFile("bitset-index-type-unsigned-pointer-load.nc");
  writeFile(Source, R"r19bitwiden(#include <bitset>
bool test(const std::bitset<256>& bits, const unsigned char* pos) {
  return bits.test(*pos);
}
int main() {
  std::bitset<256> bits;
  bits.set(211);
  unsigned char p = 211;
  return !test(bits, &p);
}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-index-type-unsigned-pointer-load" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeIndexCalledOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-index-type-index-called-once.cpp");
  const auto Output = tmpFile("bitset-index-type-index-called-once.nc");
  writeFile(Source, R"r19bitwiden(#include <bitset>
static unsigned calls;
unsigned char position() { ++calls; return 201; }
int main() {
  std::bitset<256> bits;
  bits.set(position());
  return calls != 1 || !bits.test(201) || bits.count() != 1;
}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-index-type-index-called-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeGuardSignedByteRejected) {
  const auto Source = tmpFile("bitset-index-type-guard-signed-byte.cpp");
  writeFile(Source, R"r19bitwiden(#include <bitset>
bool check(signed char p){std::bitset<256> bits;return bits.test(p);}
int main(){return check(2);}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeGuardTooShortForByteRejected) {
  const auto Source = tmpFile("bitset-index-type-guard-too-short-for-byte.cpp");
  writeFile(Source, R"r19bitwiden(#include <bitset>
bool check(unsigned char p){std::bitset<255> bits;return bits.test(p);}
int main(){return check(2);}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeGuardTooShortForBoolRejected) {
  const auto Source = tmpFile("bitset-index-type-guard-too-short-for-bool.cpp");
  writeFile(Source, R"r19bitwiden(#include <bitset>
bool check(bool p){std::bitset<1> bits;return bits.test(p);}
int main(){return check(false);}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeGuardOffsetAfterPromotionRejected) {
  const auto Source = tmpFile("bitset-index-type-guard-offset-after-promotion.cpp");
  writeFile(Source, R"r19bitwiden(#include <bitset>
bool check(unsigned char p){std::bitset<256> bits;return bits.test(p+1);}
int main(){return check(2);}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeGuardWideUnsignedRejected) {
  const auto Source = tmpFile("bitset-index-type-guard-wide-unsigned.cpp");
  writeFile(Source, R"r19bitwiden(#include <bitset>
bool check(unsigned p){std::bitset<256> bits;return bits.test(p);}
int main(){return check(2);}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeGuardExplicitSizeConversionRejected) {
  const auto Source = tmpFile("bitset-index-type-guard-explicit-size-conversion.cpp");
  writeFile(Source, R"r19bitwiden(#include <bitset>
bool check(unsigned char p){std::bitset<256> bits;return bits.test(static_cast<std::size_t>(p));}
int main(){return check(2);}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeGuardEnumOperandRejected) {
  const auto Source = tmpFile("bitset-index-type-guard-enum-operand.cpp");
  writeFile(Source, R"r19bitwiden(#include <bitset>
enum Position:unsigned char {p=2};
bool check(Position i){std::bitset<256> bits;return bits.test(i);}
int main(){return check(p);}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetIndexTypeGuardOwnedSdkBodyRejected) {
  const auto Source = tmpFile("bitset-index-type-guard-owned-sdk-body.cpp");
  writeFile(Source, R"r19bitwiden(#include <bitset>
namespace std{template<> bool bitset<256>::test(size_t) const{return true;}}
bool check(unsigned char p){std::bitset<256> bits;return bits.test(p);}
int main(){return check(2);}
)r19bitwiden");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
