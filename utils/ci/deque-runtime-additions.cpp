
TEST_F(TranslateTest, CoreV2DequeIntEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-empty-layout.cpp");
  const auto Output = tmpFile("deque-empty-layout.nc");
  writeFile(Source, R"r19deque(#include <deque>
int main(){std::deque<int> v;std::deque<int>::iterator i,j;
std::deque<int>::const_iterator a,b;
return sizeof(v)!=48||alignof(decltype(v))!=8||sizeof(i)!=16||sizeof(a)!=16||!v.empty()||v.size()!=0||i!=j||a!=b;}
)r19deque");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-empty-layout" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeIntIteratorsRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-iterator-layout.cpp");
  const auto Output = tmpFile("deque-iterator-layout.nc");
  writeFile(Source, R"r19deque(#include <deque>
int main(){std::deque<int> v{1,2,3};auto i=v.begin();i+=2;auto a=i--;*i=5;
std::deque<int>::const_iterator c=i;auto b=c+1;auto j=v.end();--j;
return *a!=3||*i!=5||*b!=3||b-c!=1||i-v.begin()!=1||*j!=3||c[1]!=3;}
)r19deque");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-iterator-layout" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeIntOperationsRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-original-runtime.cpp");
  const auto Output = tmpFile("deque-original-runtime.nc");
  writeFile(Source, R"r19deque(#include <deque>
int main(){std::deque<int> v{2,3};v.push_front(1);v.push_back(4);v.pop_front();v.pop_back();
return v.size()!=2||v.front()!=2||v.back()!=3;}
)r19deque");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-original-runtime" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeIntBlockBoundariesRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-block-boundaries.cpp");
  const auto Output = tmpFile("deque-block-boundaries.nc");
  writeFile(Source, R"r19deque(#include <deque>
int main(){std::deque<int> v;for(int i=0;i!=2049;++i)v.push_back(i);
int* saved=&v[1500];for(int i=0;i!=1024;++i)v.pop_front();
for(int i=0;i!=1024;++i)v.push_front(-i-1);
if(v.size()!=2049||v.front()!=-1024||v.back()!=2048||*saved!=1500||&v[1500]!=saved)return 1;
*saved=9;if(v[1500]!=9)return 2;v.clear();v.push_back(7);return v.size()!=1||v.front()!=7||v.back()!=7;}
)r19deque");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-block-boundaries" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeIntAllocationReferencesRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-live-allocation-reference.cpp");
  const auto Output = tmpFile("deque-live-allocation-reference.nc");
  writeFile(Source, R"r19deque(using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
#include <deque>
static std::deque<int>* current;static int armed,observed;
void* operator new(Size n){void* p=malloc(n);if(armed&&current){++observed;current->front()=9;}return p;}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {free(p);}
int main(){std::deque<int> v{3};current=&v;armed=1;
for(int i=0;i!=2048&&!observed;++i)v.push_back(v.front());
armed=0;current=nullptr;int expected=observed?9:3;
return v.size()<2||v.front()!=expected||v.back()!=expected;}
)r19deque");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-live-allocation-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeIntArgumentEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-owned-argument-effects.cpp");
  const auto Output = tmpFile("deque-owned-argument-effects.nc");
  writeFile(Source, R"r19deque(#include <deque>
static std::deque<int>* current;static int effects;
std::deque<int>* receiver(){effects=effects*10+1;return current;}
const int& argument(){effects=effects*10+2;return current->front();}
int main(){std::deque<int> v{2};current=&v;receiver()->push_back(argument());receiver()->push_front(argument());
return effects!=1212||v.size()!=3||v.front()!=2||v.back()!=2;}
)r19deque");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-owned-argument-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeOwnedPushRejected) {
  const auto Source = tmpFile("deque-owned-push-body.cpp");
  writeFile(Source, R"r19deque(#include <deque>
namespace std { inline namespace __1 {
template<> inline void deque<int>::push_front(const int&) {}
}}
int main(){std::deque<int> v;int x=1;v.push_front(x);return v.empty();}
)r19deque");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeOwnedFrontMacroRejected) {
  const auto Source = tmpFile("deque-owned-front-macro.cpp");
  writeFile(Source, R"r19deque(#define front owned_front
#include <deque>
int main(){std::deque<int> v{1};return v.front()!=1;}
)r19deque");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeOwnedMapMacroRejected) {
  const auto Source = tmpFile("deque-owned-map-macro.cpp");
  writeFile(Source, R"r19deque(#define __map_ owned_map
#include <deque>
int main(){std::deque<int> v{1};return v.front()!=1;}
)r19deque");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeOwnedEmptyRejected) {
  const auto Source = tmpFile("deque-owned-empty-body.cpp");
  writeFile(Source, R"r19deque(#include <deque>
namespace std { inline namespace __1 {
template<> inline bool deque<int>::empty() const noexcept {return true;}
}}
int main(){std::deque<int> v;return !v.empty();}
)r19deque");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
