
TEST_F(TranslateTest, CoreV2BitsetZeroLayoutRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-zero-layout.cpp");
  const auto Output = tmpFile("bitset-zero-layout.nc");
  writeFile(Source, R"r19bitzero(#include <bitset>
static unsigned calls;unsigned long long value(){++calls;return ~0ULL;}
int main(){std::bitset<0> a(value());if(calls!=1||sizeof(a)!=1||alignof(decltype(a))!=1)return 1;
if(a.size()||a.count()||a.any()||!a.none()||!a.all())return 2;
if(&a.set()!=&a||&a.flip()!=&a||&a.reset()!=&a)return 3;
const std::bitset<0> b(a);std::bitset<0> c(b);c=b;
return c.size()||c.count()||c.any()||!c.none()||!c.all();}
)r19bitzero");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-zero-layout" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWholeMasksRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-whole-masks.cpp");
  const auto Output = tmpFile("bitset-whole-masks.nc");
  writeFile(Source, R"r19bitzero(#include <bitset>
template<unsigned N> int check(){std::bitset<N> a;if(a.size()!=N||a.count()||a.any()||!a.none())return 1;
if(&a.set()!=&a||a.count()!=N||!a.all()||a.none())return 2;
if(&a.flip()!=&a||a.count()||a.any()||!a.none())return 3;
a.flip();if(a.count()!=N||!a.all())return 4;
if(&a.reset()!=&a||a.count()||a.any()||!a.none())return 5;return 0;}
int main(){return check<1>()||check<63>()||check<64>()||check<65>()||check<70>()||check<127>()||check<128>()||check<129>();}
)r19bitzero");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-whole-masks" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWholeReturnAliasRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-whole-return-alias.cpp");
  const auto Output = tmpFile("bitset-whole-return-alias.nc");
  writeFile(Source, R"r19bitzero(#include <bitset>
int main(){std::bitset<70> a;auto& x=a.set();auto& y=x.flip();auto& z=y.reset();
if(&x!=&a||&y!=&a||&z!=&a||a.count())return 1;
a.set().flip().flip();if(a.count()!=70||!a.all())return 2;
a.reset().flip().reset();if(a.any()||!a.none())return 3;return 0;}
)r19bitzero");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-whole-return-alias" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWholeUnsignedMaskRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-whole-unsigned-mask.cpp");
  const auto Output = tmpFile("bitset-whole-unsigned-mask.nc");
  writeFile(Source, R"r19bitzero(#include <bitset>
int main(){std::bitset<1> a(~0ULL);std::bitset<7> b(0x181ULL);std::bitset<63> c(~0ULL);
std::bitset<64> d(~0ULL);std::bitset<70> e(~0ULL);if(a.count()!=1||b.count()!=1||c.count()!=63||d.count()!=64||e.count()!=64)return 1;
if(!a.all()||b.all()||!c.all()||!d.all()||e.all()||e.none())return 2;
e.flip();if(e.count()!=6||e.all()||e.none())return 3;
b.flip();if(b.count()!=6||b.all())return 4;return 0;}
)r19bitzero");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-whole-unsigned-mask" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWholeConstCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-whole-const-copy.cpp");
  const auto Output = tmpFile("bitset-whole-const-copy.nc");
  writeFile(Source, R"r19bitzero(#include <bitset>
int main(){std::bitset<129> a;a.set();const std::bitset<129> b(a);std::bitset<129> c(b);
if(b.size()!=129||b.count()!=129||!b.any()||b.none()||!b.all()||c.count()!=129)return 1;
a.reset();if(b.count()!=129||c.count()!=129)return 2;c.flip();if(c.any()||!c.none()||c.all())return 3;
c=b;if(c.count()!=129||!c.all())return 4;return 0;}
)r19bitzero");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-whole-const-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetWholeReceiverEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-whole-receiver-effects.cpp");
  const auto Output = tmpFile("bitset-whole-receiver-effects.nc");
  writeFile(Source, R"r19bitzero(#include <bitset>
static std::bitset<70>* owner;static unsigned calls;
std::bitset<70>& receiver(){++calls;return *owner;}
int main(){std::bitset<70> a;owner=&a;if(&receiver().set()!=&a||calls!=1)return 1;
if(receiver().size()!=70||calls!=2)return 2;if(!receiver().all()||calls!=3)return 3;
if(receiver().none()||calls!=4)return 4;if(&receiver().flip()!=&a||calls!=5)return 5;
if(receiver().any()||calls!=6)return 6;if(receiver().count()||calls!=7)return 7;
if(&receiver().reset()!=&a||calls!=8)return 8;return 0;}
)r19bitzero");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-whole-receiver-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetZeroOwnedSizeRejected) {
  const auto Source = tmpFile("bitset-zero-size-source.cpp");
  writeFile(Source, R"r19bitzero(#include <bitset>
namespace std { inline namespace __1 {
template<> inline constexpr size_t bitset<0>::size() const noexcept { return 0; }
}}
int main(){std::bitset<0> a;return a.size();}
)r19bitzero");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWholeOwnedSetRejected) {
  const auto Source = tmpFile("bitset-whole-set-source.cpp");
  writeFile(Source, R"r19bitzero(#include <bitset>
namespace std { inline namespace __1 {
template<> inline bitset<70>& bitset<70>::set() noexcept { return *this; }
}}
int main(){std::bitset<70> a;a.set();return a.any();}
)r19bitzero");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWholeOwnedResetRejected) {
  const auto Source = tmpFile("bitset-whole-reset-source.cpp");
  writeFile(Source, R"r19bitzero(#include <bitset>
namespace std { inline namespace __1 {
template<> inline bitset<70>& bitset<70>::reset() noexcept { return *this; }
}}
int main(){std::bitset<70> a;a.reset();return a.any();}
)r19bitzero");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetWholeOwnedFlipRejected) {
  const auto Source = tmpFile("bitset-whole-flip-source.cpp");
  writeFile(Source, R"r19bitzero(#include <bitset>
namespace std { inline namespace __1 {
template<> inline bitset<70>& bitset<70>::flip() noexcept { return *this; }
}}
int main(){std::bitset<70> a;a.flip();return a.any();}
)r19bitzero");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
