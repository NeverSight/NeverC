
TEST_F(TranslateTest, CoreV2BitsetShiftZeroRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-shift-zero.cpp");
  const auto Output = tmpFile("bitset-shift-zero.nc");
  writeFile(Source, R"r19bitshift(#include <bitset>
static int calls;std::size_t amount(){++calls;return ~std::size_t(0);}
int main(){std::bitset<0> a;auto b=a<<amount();auto c=a>>amount();
if(&(a<<=amount())!=&a||&(a>>=amount())!=&a||calls!=4)return 1;
return b.any()||c.any()||!a.all();}
)r19bitshift");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-shift-zero" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetShiftSingleWordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-shift-single-word.cpp");
  const auto Output = tmpFile("bitset-shift-single-word.nc");
  writeFile(Source, R"r19bitshift(#include <bitset>
int main(){std::bitset<1> one;one.set();if((one<<0)!=one||(one>>0)!=one||(one<<1).any()||(one>>1).any())return 1;
std::bitset<63> x;x.set(0).set(62);auto left=x<<1, right=x>>1;
if(left.count()!=1||!left.test(1)||right.count()!=1||!right.test(61))return 2;
std::bitset<64> y;y.set(0).set(63);return (y<<63).count()!=1||!(y<<63).test(63)||(y>>63).count()!=1||!(y>>63).test(0)||(y<<64).any()||(y>>64).any();}
)r19bitshift");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-shift-single-word" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetShiftWordBoundariesRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-shift-word-boundaries.cpp");
  const auto Output = tmpFile("bitset-shift-word-boundaries.nc");
  writeFile(Source, R"r19bitshift(#include <bitset>
int main(){std::bitset<65> a;a.set(0).set(63).set(64);
auto l=a<<1,r=a>>1;if(l.count()!=2||!l.test(1)||!l.test(64)||r.count()!=2||!r.test(62)||!r.test(63))return 1;
auto l64=a<<64,r64=a>>64;if(l64.count()!=1||!l64.test(64)||r64.count()!=1||!r64.test(0))return 2;
std::bitset<129> b;b.set(0).set(63).set(64).set(127).set(128);auto c=b<<64,d=b>>64;
return c.count()!=3||!c.test(64)||!c.test(127)||!c.test(128)||d.count()!=3||!d.test(0)||!d.test(63)||!d.test(64)||(b<<129).any()||(b>>129).any();}
)r19bitshift");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-shift-word-boundaries" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetShiftDynamicAmountsRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-shift-dynamic-amounts.cpp");
  const auto Output = tmpFile("bitset-shift-dynamic-amounts.nc");
  writeFile(Source, R"r19bitshift(#include <bitset>
int main(){std::bitset<70> a;a.set(0).set(63).set(69);
std::size_t amounts[8]={0,1,6,63,64,69,70,~std::size_t(0)};
for(int i=0;i!=8;++i){auto n=amounts[i];auto l=a<<n,r=a>>n;std::size_t lc=0,rc=0;
if(n<70){if(n<70)++lc;if(n<7)++lc;if(n<1)++lc;
if(n<=0)++rc;if(n<=63)++rc;if(n<=69)++rc;}
if(l.count()!=lc||r.count()!=rc)return i+1;auto copy=a;copy<<=n;if(copy!=l)return 20+i;copy=a;copy>>=n;if(copy!=r)return 40+i;}
return a.count()!=3;}
)r19bitshift");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-shift-dynamic-amounts" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetShiftAssignmentIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-shift-assignment-identity.cpp");
  const auto Output = tmpFile("bitset-shift-assignment-identity.nc");
  writeFile(Source, R"r19bitshift(#include <bitset>
int main(){std::bitset<129> a;a.set(0).set(64).set(128);const auto saved=a;
auto l=a<<1,r=a>>1;auto copy=a;if(&(copy<<=1)!=&copy||copy!=l)return 1;copy=a;
if(&(copy>>=1)!=&copy||copy!=r)return 2;copy=a;(copy<<=64)>>=64;
return copy.count()!=2||!copy.test(0)||!copy.test(64)||a!=saved||(saved<<~std::size_t(0)).any();}
)r19bitshift");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-shift-assignment-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetShiftSequencingRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-shift-sequencing.cpp");
  const auto Output = tmpFile("bitset-shift-sequencing.nc");
  writeFile(Source, R"r19bitshift(#include <bitset>
static std::bitset<70>* target;static int events;
std::bitset<70>& receiver(){events=events*10+1;return *target;}
std::size_t amount(){events=events*10+2;target->set(0);return 1;}
int main(){std::bitset<70> a;target=&a;events=0;
if(&(receiver()<<=amount())!=&a||events!=21||a.count()!=1||!a.test(1))return 1;
a.reset();events=0;receiver().operator<<=(amount());if(events!=12||a.count()!=1||!a.test(1))return 2;
a.reset();events=0;auto b=receiver()<<amount();if(events!=12||b.count()!=1||!b.test(1)||a.count()!=1||!a.test(0))return 3;
a.reset();a.set(2);events=0;receiver()>>=amount();return events!=21||a.count()!=1||!a.test(1);}
)r19bitshift");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-shift-sequencing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetShiftOwnedLeftRejected) {
  const auto Source = tmpFile("bitset-shift-owned-left.cpp");
  writeFile(Source, R"r19bitshift(#include <bitset>
namespace std { inline namespace __1 {
template<> bitset<70> bitset<70>::operator<<(size_t pos) const noexcept {return {};}
}}
int main(){std::bitset<70> a;auto c=a<<1;return c.count();}
)r19bitshift");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetShiftOwnedRightRejected) {
  const auto Source = tmpFile("bitset-shift-owned-right.cpp");
  writeFile(Source, R"r19bitshift(#include <bitset>
namespace std { inline namespace __1 {
template<> bitset<70> bitset<70>::operator>>(size_t pos) const noexcept {return {};}
}}
int main(){std::bitset<70> a;auto c=a>>1;return c.count();}
)r19bitshift");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetShiftOwnedLeftAssignmentRejected) {
  const auto Source = tmpFile("bitset-shift-owned-left-assignment.cpp");
  writeFile(Source, R"r19bitshift(#include <bitset>
namespace std { inline namespace __1 {
template<> bitset<70>& bitset<70>::operator<<=(size_t pos) noexcept {return *this;}
}}
int main(){std::bitset<70> a;a<<=1;return a.count();}
)r19bitshift");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetShiftOwnedRightAssignmentRejected) {
  const auto Source = tmpFile("bitset-shift-owned-right-assignment.cpp");
  writeFile(Source, R"r19bitshift(#include <bitset>
namespace std { inline namespace __1 {
template<> bitset<70>& bitset<70>::operator>>=(size_t pos) noexcept {return *this;}
}}
int main(){std::bitset<70> a;a>>=1;return a.count();}
)r19bitshift");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetShiftOwnedForwardSpecializationRejected) {
  const auto Source = tmpFile("bitset-shift-owned-forward-specialization.cpp");
  writeFile(Source, R"r19bitshift(#include <bitset>
namespace std { inline namespace __1 {
template<> size_t*&& forward<size_t*>(size_t*& value) noexcept { return static_cast<size_t*&&>(value); }
}}
int main(){std::bitset<129> a;a.set();auto b=a<<64;return b.count()!=65;}
)r19bitshift");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetShiftOwnedForwardMacroRejected) {
  const auto Source = tmpFile("bitset-shift-owned-forward-macro.cpp");
  writeFile(Source, R"r19bitshift(#define forward intercepted_forward
#include <bitset>
#undef forward
int main(){std::bitset<129> a;a.set();auto b=a<<64;return b.count()!=65;}
)r19bitshift");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetShiftOwnedMemmoveRedeclarationRejected) {
  const auto Source = tmpFile("bitset-shift-owned-memmove-redeclaration.cpp");
  writeFile(Source, R"r19bitshift(#include <bitset>
extern "C" void* __builtin_memmove(void*, const void*, __SIZE_TYPE__);
int main(){std::bitset<129> a;a.set();auto b=a<<64;return b.count()!=65;}
)r19bitshift");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetShiftOwnedConstantQueryMacroRejected) {
  const auto Source = tmpFile("bitset-shift-owned-constant-query-macro.cpp");
  writeFile(Source, R"r19bitshift(#define __builtin_constant_p(...) 1
#include <bitset>
#undef __builtin_constant_p
int main(){std::bitset<129> a;a.set();auto b=a<<64;return b.count()!=65;}
)r19bitshift");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetShiftOwnedConstantEvaluationMacroRejected) {
  const auto Source = tmpFile("bitset-shift-owned-constant-evaluation-macro.cpp");
  writeFile(Source, R"r19bitshift(#define __builtin_is_constant_evaluated() false
#include <bitset>
#undef __builtin_is_constant_evaluated
int main(){std::bitset<129> a;a.set();auto b=a<<64;return b.count()!=65;}
)r19bitshift");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetShiftOwnedConstantEvaluationHelperMacroRejected) {
  const auto Source = tmpFile("bitset-shift-owned-constant-evaluation-helper-macro.cpp");
  writeFile(Source, R"r19bitshift(#define __libcpp_is_constant_evaluated intercepted_constant_evaluated
#include <bitset>
#undef __libcpp_is_constant_evaluated
int main(){std::bitset<129> a;a.set();auto b=a<<64;return b.count()!=65;}
)r19bitshift");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
