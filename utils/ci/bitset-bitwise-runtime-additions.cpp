
TEST_F(TranslateTest, CoreV2BitsetBitwiseZeroRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bitwise-zero.cpp");
  const auto Output = tmpFile("bitset-bitwise-zero.nc");
  writeFile(Source, R"r19bitops(#include <bitset>
int main(){std::bitset<0> a,b;auto c=~a;auto d=a&b;auto e=a|b;auto f=a^b;
if(!(a==b)||a!=b||c.any()||d.any()||e.any()||f.any()||!c.all())return 1;
if(&(a&=b)!=&a||&(a|=b)!=&a||&(a^=b)!=&a)return 2;return a.count()!=0;}
)r19bitops");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bitwise-zero" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetBitwiseSingleWordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bitwise-single-word.cpp");
  const auto Output = tmpFile("bitset-bitwise-single-word.nc");
  writeFile(Source, R"r19bitops(#include <bitset>
int main(){std::bitset<1> a,b;b.set();auto c=~a;if(c!=b||a==b||(a|b)!=b||(a&b)!=a||(a^b)!=b)return 1;
std::bitset<63> x,y;x.set();y.set(0);auto n=~y;if(n.count()!=62||(x&y)!=y||(x|y)!=x||(x^y)!=n)return 2;
std::bitset<64> all,edge;all.set();edge.set(63);auto opposite=~edge;
return opposite.count()!=63||(all&edge)!=edge||(all^edge)!=opposite||(~all).any();}
)r19bitops");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bitwise-single-word" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetBitwiseMultiWordRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bitwise-multi-word.cpp");
  const auto Output = tmpFile("bitset-bitwise-multi-word.nc");
  writeFile(Source, R"r19bitops(#include <bitset>
int main(){std::bitset<70> a,b;a.set(0).set(63).set(69);b.set(63).set(64);
auto both=a&b,either=a|b,different=a^b,opposite=~a;
if(both.count()!=1||!both.test(63)||either.count()!=4||different.count()!=3||different.test(63)||opposite.count()!=67)return 1;
if(!opposite.test(64)||opposite.test(69)||a.count()!=3||b.count()!=2)return 2;
auto chained=(a&b)|(a^b);return chained!=either;}
)r19bitops");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bitwise-multi-word" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetBitwiseWordBoundariesRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bitwise-word-boundaries.cpp");
  const auto Output = tmpFile("bitset-bitwise-word-boundaries.nc");
  writeFile(Source, R"r19bitops(#include <bitset>
int main(){std::bitset<65> a,b;a.set();b.set(0).set(64);auto c=a^b;
if(c.count()!=63||c.test(0)||c.test(64)||(~a).any()||(a&b)!=b)return 1;
std::bitset<129> x,y;x.set(0).set(63).set(64).set(127).set(128);y.set(64).set(128);
auto both=x&y,opposite=~x;if(both!=y||opposite.count()!=124||opposite.test(128)||!opposite.test(126))return 2;
auto combined=x|opposite;return !combined.all()||combined.count()!=129||(x^x).any();}
)r19bitops");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bitwise-word-boundaries" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetBitwiseAssignmentAliasRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bitwise-assignment-alias.cpp");
  const auto Output = tmpFile("bitset-bitwise-assignment-alias.nc");
  writeFile(Source, R"r19bitops(#include <bitset>
int main(){std::bitset<70> a,b;a.set(0).set(69);b.set(63).set(69);auto both=a&b,either=a|b,different=a^b;
auto c=a;if(&(c&=b)!=&c||c!=both)return 1;c=a;if(&(c|=b)!=&c||c!=either)return 2;c=a;
if(&(c^=b)!=&c||c!=different)return 3;c=a;c&=c;if(c!=a)return 4;c|=c;if(c!=a)return 5;c^=c;
if(c.any())return 6;const auto saved=a;auto copy=saved;copy&=saved;return copy!=saved||a!=saved;}
)r19bitops");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bitwise-assignment-alias" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetBitwiseAssignmentSequencingRunAtBothOptimizations) {
  const auto Source = tmpFile("bitset-bitwise-assignment-sequencing.cpp");
  const auto Output = tmpFile("bitset-bitwise-assignment-sequencing.nc");
  writeFile(Source, R"r19bitops(#include <bitset>
static std::bitset<70>* left;static std::bitset<70>* right;static int events;
std::bitset<70>& receiver(){events=events*10+1;right->set(69);return *left;}
const std::bitset<70>& source(){events=events*10+2;return *right;}
int main(){std::bitset<70> a,b;a.set();left=&a;right=&b;events=0;
if(&(receiver()&=source())!=&a||events!=21||a.count()!=1||!a.test(69))return 1;
a.reset();b.reset();events=0;receiver().operator|=(source());if(events!=12||a.count()!=1||!a.test(69))return 2;
a.set();b.reset();events=0;receiver()^=source();return events!=21||a.count()!=69||a.test(69);}
)r19bitops");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset-bitwise-assignment-sequencing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BitsetBitwiseOwnedEqualRejected) {
  const auto Source = tmpFile("bitset-bitwise-owned-equal.cpp");
  writeFile(Source, R"r19bitops(#include <bitset>
namespace std { inline namespace __1 {
template<> bool bitset<70>::operator==(const bitset<70>& source) const noexcept {return true;}
}}
int main(){std::bitset<70> a,b;return a==b;}
)r19bitops");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetBitwiseOwnedComplementRejected) {
  const auto Source = tmpFile("bitset-bitwise-owned-complement.cpp");
  writeFile(Source, R"r19bitops(#include <bitset>
namespace std { inline namespace __1 {
template<> bitset<70> bitset<70>::operator~() const noexcept {return {};}
}}
int main(){std::bitset<70> a,b;auto c=~a;return c.count();}
)r19bitops");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetBitwiseOwnedAndAssignmentRejected) {
  const auto Source = tmpFile("bitset-bitwise-owned-and-assignment.cpp");
  writeFile(Source, R"r19bitops(#include <bitset>
namespace std { inline namespace __1 {
template<> bitset<70>& bitset<70>::operator&=(const bitset<70>& source) noexcept {return *this;}
}}
int main(){std::bitset<70> a,b;a&=b;return a.count();}
)r19bitops");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetBitwiseOwnedFreeOrRejected) {
  const auto Source = tmpFile("bitset-bitwise-owned-free-or.cpp");
  writeFile(Source, R"r19bitops(#include <bitset>
namespace std { inline namespace __1 {
template<> bitset<70> operator|(const bitset<70>& a,const bitset<70>& b) noexcept {return {};}
}}
int main(){std::bitset<70> a,b;auto c=a|b;return c.count();}
)r19bitops");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
