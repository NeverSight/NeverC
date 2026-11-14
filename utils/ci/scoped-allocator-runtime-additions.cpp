
TEST_F(TranslateTest, CoreV2ScopedAllocatorIntRunAtBothOptimizations) {
  const auto Source = tmpFile("scoped-allocator-basic.cpp");
  const auto Output = tmpFile("scoped-allocator-basic.nc");
  writeFile(Source, R"scoped(#include <memory>
#include <scoped_allocator>
int main(){std::scoped_allocator_adaptor<std::allocator<int>> a;int *p=a.allocate(2);
a.construct(p,9);a.construct(p+1,4);int n=p[0]+p[1];
a.destroy(p+1);a.destroy(p);a.deallocate(p,2);return n!=13;}
)scoped");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("scoped-allocator-basic" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2Bitset70RunAtBothOptimizations) {
  const auto Source = tmpFile("bitset70-basic.cpp");
  const auto Output = tmpFile("bitset70-basic.nc");
  writeFile(Source, R"scoped(#include <bitset>
int main(){std::bitset<70> v;v.set(0).set(69);
bool both=v.count()==2&&v.test(0)&&v.test(69);v.flip(0);v.reset(69);return !both||v.any();}
)scoped");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bitset70-basic" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ScopedAllocatorOwnedConstructorRejected) {
  const auto Source = tmpFile("scoped-allocator-owned-constructor-body.cpp");
  writeFile(Source, R"r19guard(#include <memory>
#include <scoped_allocator>
template<> std::scoped_allocator_adaptor<std::allocator<int>>::scoped_allocator_adaptor() noexcept {asm volatile("":::"memory");}
int main(){std::scoped_allocator_adaptor<std::allocator<int>> a;return 0;}
)r19guard");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedCountRejected) {
  const auto Source = tmpFile("bitset-owned-count-body.cpp");
  writeFile(Source, R"r19guard(#include <bitset>
template<> std::size_t std::bitset<70>::count() const noexcept {asm volatile("":::"memory");return 0;}
int main(){std::bitset<70> b;return int(b.count());}
)r19guard");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedPopcountDeclarationRejected) {
  const auto Source = tmpFile("bitset-owned-popcount-declarations.cpp");
  writeFile(Source, R"popguard(extern "C" int __builtin_popcountl(unsigned long);
extern "C" int __builtin_popcountll(unsigned long long);
#include <bitset>
int main(){std::bitset<70> v;v.set(0);return v.count()!=1;}
)popguard");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2BitsetOwnedPopcountMacroRejected) {
  const auto Source = tmpFile("bitset-owned-popcount-macros.cpp");
  writeFile(Source, R"popguard(#define __builtin_popcountl(x) __builtin_popcountl((x)+0)
#define __builtin_popcountll(x) __builtin_popcountll((x)+0)
#include <bitset>
int main(){std::bitset<70> v;v.set(0);return v.count()!=1;}
)popguard");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
