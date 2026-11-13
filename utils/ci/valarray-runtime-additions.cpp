
TEST_F(TranslateTest, CoreV2ValarrayIntCountRunAtBothOptimizations) {
  const auto Source = tmpFile("valarray-count-layout.cpp");
  const auto Output = tmpFile("valarray-count-layout.nc");
  writeFile(Source, R"r19valarray(#include <valarray>
int main(){std::valarray<int> v(3);v[1]=5;
return sizeof(v)!=16||alignof(std::valarray<int>)!=8||v.size()!=3||v.sum()!=5;}
)r19valarray");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("valarray-count-layout" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ValarrayIntFillRunAtBothOptimizations) {
  const auto Source = tmpFile("valarray-fill-runtime.cpp");
  const auto Output = tmpFile("valarray-fill-runtime.nc");
  writeFile(Source, R"r19valarray(#include <valarray>
int main(){std::valarray<int> v(2,3);v[1]=5;v+=1;
return v.size()!=3||v[0]!=3||v[1]!=6||v.sum()!=12;}
)r19valarray");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("valarray-fill-runtime" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ValarrayIntAliasedScalarRunAtBothOptimizations) {
  const auto Source = tmpFile("valarray-live-alias.cpp");
  const auto Output = tmpFile("valarray-live-alias.nc");
  writeFile(Source, R"r19valarray(#include <valarray>
int main() {
  std::valarray<int> values(1, 3);
  values[1] = 2;
  values[2] = 3;
  values += values[0];
  const std::valarray<int>& view = values;
  return view.size() != 3 || view[0] != 2 || view[1] != 4 ||
         view[2] != 5 || view.sum() != 11;
}
)r19valarray");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("valarray-live-alias" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ValarrayIntOwnedArgumentEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("valarray-owned-argument-effects.cpp");
  const auto Output = tmpFile("valarray-owned-argument-effects.nc");
  writeFile(Source, R"r19valarray(#include <valarray>
const int& update_first(std::valarray<int>& values) {
  values[0] = 3;
  return values[0];
}
int main() {
  std::valarray<int> values(1, 3);
  values += update_first(values);
  return values[0] != 6 || values[1] != 7 || values[2] != 7 || values.sum() != 20;
}
)r19valarray");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("valarray-owned-argument-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ValarrayIntEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("valarray-empty-and-zero-count.cpp");
  const auto Output = tmpFile("valarray-empty-and-zero-count.nc");
  writeFile(Source, R"r19valarray(#include <valarray>
int main() {
  std::valarray<int> empty;
  std::valarray<int> zero_count(0);
  empty += 7;
  const std::valarray<int>& view = empty;
  return view.size() != 0 || view.sum() != 0 || zero_count.size() != 0 ||
         zero_count.sum() != 0;
}
)r19valarray");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("valarray-empty-and-zero-count" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ValarrayIntSelectedHeapRunAtBothOptimizations) {
  const auto Source = tmpFile("valarray-selected-global-allocation.cpp");
  const auto Output = tmpFile("valarray-selected-global-allocation.nc");
  writeFile(Source, R"r19valarray(using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <valarray>
#include <new>
#include <cstdlib>
int allocations = 0;
int releases = 0;
bool delete_saw_empty = false;
std::valarray<int>* watched = nullptr;
void* operator new(std::size_t bytes) {
  ++allocations;
  return ::malloc(bytes);
}
void operator delete(void* pointer) noexcept {
  ++releases;
  if (watched) delete_saw_empty = watched->size() == 0;
  ::free(pointer);
}
int main() {
  {
    std::valarray<int> values(4, 3);
    watched = &values;
    if (values.sum() != 12) return 1;
  }
  watched = nullptr;
  return allocations != releases || allocations > 1 || (allocations && !delete_saw_empty);
}
)r19valarray");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("valarray-selected-global-allocation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ValarrayOwnedFillConstructorRejected) {
  const auto Source = tmpFile("valarray-owned-fill-body.cpp");
  writeFile(Source, R"r19valarray(#include <valarray>
namespace std { inline namespace __1 {
template<> inline valarray<int>::valarray(const int& x,size_t n)
  :__begin_(nullptr),__end_(nullptr){resize(n,x);}
}}
int main(){std::valarray<int> v(2,3);return v.size()!=3;}
)r19valarray");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ValarrayOwnedSumRejected) {
  const auto Source = tmpFile("valarray-owned-sum-body.cpp");
  writeFile(Source, R"r19valarray(#include <valarray>
namespace std { inline namespace __1 {
template<> inline int valarray<int>::sum() const{return 6;}
}}
int main(){std::valarray<int> v(2,3);return v.sum()!=6;}
)r19valarray");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ValarrayOwnedSumMacroRejected) {
  const auto Source = tmpFile("valarray-owned-sum-macro.cpp");
  writeFile(Source, R"r19valarray(#define sum owned_sum
#include <valarray>
int main(){std::valarray<int> v(2,3);return v.sum()!=6;}
)r19valarray");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ValarrayOwnedStorageMacroRejected) {
  const auto Source = tmpFile("valarray-owned-storage-macro.cpp");
  writeFile(Source, R"r19valarray(#define __begin_ owned_begin
#include <valarray>
int main(){std::valarray<int> v(2,3);return v.size()!=3;}
)r19valarray");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
