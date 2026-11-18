
TEST_F(TranslateTest, CoreV2ValarrayValueConstructorsRunAtBothOptimizations) {
  const auto Source = tmpFile("valarray-value-constructors.cpp");
  const auto Output = tmpFile("valarray-value-constructors.nc");
  writeFile(Source, R"r19valvalue(#include <valarray>
#include <new>
#include <utility>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
void* operator new(Size n){return malloc(n);}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {operator delete(p);}
int main(){std::valarray<int> a(3);a[0]=1;a[1]=2;a[2]=3;
std::valarray<int> b(a);if(b.size()!=3||b[0]!=1||b[2]!=3||&b[0]==&a[0])return 1;
b[0]=9;if(a[0]!=1)return 2;int* identity=&a[0];std::valarray<int> c(std::move(a));
if(a.size()!=0||c.size()!=3||&c[0]!=identity)return 3;
a.resize(2,7);if(a.size()!=2||a[0]!=7||a[1]!=7)return 4;
std::valarray<int> empty;std::valarray<int> copied(empty);std::valarray<int> moved(std::move(empty));
return copied.size()!=0||moved.size()!=0||empty.size()!=0;}
)r19valvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("valarray-value-constructors" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ValarrayValueCopyAssignmentRunAtBothOptimizations) {
  const auto Source = tmpFile("valarray-value-copy-assignment.cpp");
  const auto Output = tmpFile("valarray-value-copy-assignment.nc");
  writeFile(Source, R"r19valvalue(#include <valarray>
#include <new>
#include <utility>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
void* operator new(Size n){return malloc(n);}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {operator delete(p);}
int main(){std::valarray<int> a(3);a[0]=1;a[1]=2;a[2]=3;
std::valarray<int> b(8,3);int* reused=&b[0];std::valarray<int>& result=(b=a);
if(&result!=&b||b.size()!=3||b[0]!=1||b[2]!=3||&b[0]!=reused)return 1;
b[0]=7;if(a[0]!=1)return 2;b=b;if(b[0]!=7||b.size()!=3)return 3;
std::valarray<int> small(9,1);small=a;if(small.size()!=3||small[0]!=1||small[2]!=3)return 4;
std::valarray<int> empty;b=empty;if(b.size()!=0)return 5;
empty=a;return empty.size()!=3||empty[0]!=1||empty[2]!=3;}
)r19valvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("valarray-value-copy-assignment" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ValarrayValueMoveAssignmentRunAtBothOptimizations) {
  const auto Source = tmpFile("valarray-value-move-assignment.cpp");
  const auto Output = tmpFile("valarray-value-move-assignment.nc");
  writeFile(Source, R"r19valvalue(#include <valarray>
#include <new>
#include <utility>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
void* operator new(Size n){return malloc(n);}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {operator delete(p);}
int main(){std::valarray<int> a(3);a[0]=2;a[1]=4;a[2]=6;
std::valarray<int> b(8,2);int* identity=&a[0];std::valarray<int>& result=(b=std::move(a));
if(&result!=&b||a.size()!=0||b.size()!=3||b[2]!=6||&b[0]!=identity)return 1;
a.resize(1,9);if(a.size()!=1||a[0]!=9)return 2;
std::valarray<int> empty;b=std::move(empty);if(b.size()!=0||empty.size()!=0)return 3;
empty=std::move(a);return a.size()!=0||empty.size()!=1||empty[0]!=9;}
)r19valvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("valarray-value-move-assignment" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ValarrayValueResizeRunAtBothOptimizations) {
  const auto Source = tmpFile("valarray-value-resize.cpp");
  const auto Output = tmpFile("valarray-value-resize.nc");
  writeFile(Source, R"r19valvalue(#include <valarray>
#include <new>
#include <utility>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
void* operator new(Size n){return malloc(n);}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {operator delete(p);}
int main(){std::valarray<int> a(5,3);a[0]=99;
a.resize(3,7);if(a.size()!=3||a[0]!=7||a[2]!=7)return 1;
a.resize(5);if(a.size()!=5)return 2;for(Size i=0;i<a.size();++i)if(a[i]!=0)return 3;
a.resize(2,-4);if(a.size()!=2||a[0]!=-4||a[1]!=-4)return 4;
a.resize(0,8);if(a.size()!=0)return 5;a.resize(1,6);return a.size()!=1||a[0]!=6;}
)r19valvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("valarray-value-resize" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ValarrayValueSwapRunAtBothOptimizations) {
  const auto Source = tmpFile("valarray-value-swap.cpp");
  const auto Output = tmpFile("valarray-value-swap.nc");
  writeFile(Source, R"r19valvalue(#include <valarray>
#include <new>
#include <utility>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
void* operator new(Size n){return malloc(n);}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {operator delete(p);}
int main(){std::valarray<int> a(1,2);std::valarray<int> b(3,3);
int* first=&a[0];int* second=&b[0];a.swap(b);
if(a.size()!=3||b.size()!=2||&a[0]!=second||&b[0]!=first||a[2]!=3)return 1;
std::swap(a,b);if(a.size()!=2||b.size()!=3||&a[0]!=first||&b[0]!=second)return 2;
std::valarray<int> empty;empty.swap(a);if(a.size()!=0||empty.size()!=2||&empty[0]!=first)return 3;
a.resize(1,7);return a[0]!=7||empty[1]!=1;}
)r19valvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("valarray-value-swap" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ValarrayValueAllocationAndSequencingRunAtBothOptimizations) {
  const auto Source = tmpFile("valarray-value-allocation-and-sequencing.cpp");
  const auto Output = tmpFile("valarray-value-allocation-and-sequencing.nc");
  writeFile(Source, R"r19valvalue(#include <valarray>
#include <new>
#include <utility>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
static std::valarray<int>* destination;static std::valarray<int>* argument;
static int* input;static int active,calls,write_value,events;
void* operator new(Size n){if(active){++calls;*input=write_value;}return malloc(n);}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {operator delete(p);}
std::valarray<int>& receiver(){events=events*10+1;return *destination;}
std::valarray<int>& source(){events=events*10+2;return *argument;}
int main(){std::valarray<int> a(3);a[0]=1;a[1]=2;a[2]=3;input=&a[0];write_value=77;active=1;
std::valarray<int> b(a);active=0;if(calls!=1||b[0]!=77||b[2]!=3)return 1;
std::valarray<int> c(9,1);destination=&c;argument=&a;events=0;receiver()=source();
if(events!=21||c.size()!=3||c[0]!=77)return 2;events=0;receiver().operator=(source());
if(events!=12)return 3;events=0;receiver()=std::move(source());if(events!=21||a.size()!=0)return 4;
int fill=7;input=&fill;write_value=99;calls=0;active=1;c.resize(4,fill);active=0;
return calls!=1||fill!=99||c.size()!=4||c[0]!=7||c[3]!=7;}
)r19valvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("valarray-value-allocation-and-sequencing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ValarrayValueOwnedCopyConstructorRejected) {
  const auto Source = tmpFile("valarray-value-owned-copy-constructor.cpp");
  writeFile(Source, R"r19valvalue(#include <valarray>
namespace std { inline namespace __1 {
template<> valarray<int>::valarray(const valarray<int>& source) {}
}}
int main(){std::valarray<int> a(3);std::valarray<int> b(a);return 0;}
)r19valvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ValarrayValueOwnedMoveAssignmentRejected) {
  const auto Source = tmpFile("valarray-value-owned-move-assignment.cpp");
  writeFile(Source, R"r19valvalue(#include <valarray>
namespace std { inline namespace __1 {
template<> valarray<int>& valarray<int>::operator=(valarray<int>&& source) noexcept {return *this;}
}}
int main(){std::valarray<int> a(3);std::valarray<int> b;b=static_cast<std::valarray<int>&&>(a);return 0;}
)r19valvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ValarrayValueOwnedResizeRejected) {
  const auto Source = tmpFile("valarray-value-owned-resize.cpp");
  writeFile(Source, R"r19valvalue(#include <valarray>
namespace std { inline namespace __1 {
template<> void valarray<int>::resize(size_t n,int value) {}
}}
int main(){std::valarray<int> a(3);a.resize(5,7);return 0;}
)r19valvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ValarrayValueOwnedSDKSwapRejected) {
  const auto Source = tmpFile("valarray-value-owned-sdk-swap.cpp");
  writeFile(Source, R"r19valvalue(#include <valarray>
namespace std { inline namespace __1 {
template<> void swap(valarray<int>& a,valarray<int>& b) noexcept {}
}}
int main(){std::valarray<int> a(3);std::valarray<int> b(2);std::swap(a,b);return 0;}
)r19valvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
