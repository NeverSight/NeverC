
TEST_F(TranslateTest, CoreV2DequeExplicitAllocatorCountRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-explicit-allocator-count.cpp");
  const auto Output = tmpFile("deque-explicit-allocator-count.nc");
  writeFile(Source, R"r19dqalloc(#include <deque>
#include <memory>
#include <new>
using Size=decltype(sizeof(0));using Alloc=std::allocator<int>;
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){Alloc alloc;
unsigned counts[7]={0,1,1023,1024,1025,2049,3073};
for(unsigned t=0;t!=7;++t){unsigned n=counts[t];std::deque<int> a(n,alloc);
if(a.size()!=n)return 1;for(unsigned i=0;i!=n;++i)if(a[i]!=0)return 2;
if(n){int* saved=&a.front();a.push_back(17);if(&a.front()!=saved||a.back()!=17)return 3;}}
return 0;}
)r19dqalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-explicit-allocator-count" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeExplicitAllocatorFillEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-explicit-allocator-fill-effects.cpp");
  const auto Output = tmpFile("deque-explicit-allocator-fill-effects.nc");
  writeFile(Source, R"r19dqalloc(#include <deque>
#include <memory>
#include <new>
using Size=decltype(sizeof(0));using Alloc=std::allocator<int>;
extern "C" void* malloc(Size);extern "C" void free(void*);
static Size count=2050;
static int fill=7,armed,calls;
void* operator new(Size n){void* p=malloc(n);if(armed){++calls;count=1;fill=63;}return p;}
void operator delete(void* p) noexcept {free(p);}void operator delete(void* p,Size) noexcept {free(p);}
int main(){Alloc alloc;armed=1;std::deque<int> a(count,fill,alloc);armed=0;
if(a.size()!=2050||!calls||count!=1||fill!=63)return 1;
for(unsigned i=0;i!=2050;++i)if(a[i]!=63)return 2;
std::deque<int> empty((Size)0,fill,alloc);return !empty.empty();}
)r19dqalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-explicit-allocator-fill-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeExplicitAllocatorPointerRangesRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-explicit-allocator-pointer-ranges.cpp");
  const auto Output = tmpFile("deque-explicit-allocator-pointer-ranges.nc");
  writeFile(Source, R"r19dqalloc(#include <deque>
#include <memory>
#include <new>
using Size=decltype(sizeof(0));using Alloc=std::allocator<int>;
extern "C" void* malloc(Size);extern "C" void free(void*);
static int values[2050];
static const int* first;static const int* last;static int armed,calls;
void* operator new(Size n){void* p=malloc(n);if(armed){++calls;first=values+4;last=values+5;
for(unsigned i=0;i!=2050;++i)values[i]=50+(int)i;}return p;}
void operator delete(void* p) noexcept {free(p);}void operator delete(void* p,Size) noexcept {free(p);}
int main(){Alloc alloc;for(unsigned i=0;i!=2050;++i)values[i]=(int)i;
std::deque<int> a(values,values+1025,alloc);
if(a.size()!=1025||a.front()!=0||a.back()!=1024)return 1;
first=values;last=values+2050;armed=1;std::deque<int> b(first,last,alloc);armed=0;
if(!calls||b.size()!=2050||first!=values+4||last!=values+5)return 2;
for(unsigned i=0;i!=2050;++i)if(b[i]!=50+(int)i)return 3;
std::deque<int> empty(first,first,alloc);return !empty.empty();}
)r19dqalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-explicit-allocator-pointer-ranges" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeExplicitAllocatorCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-explicit-allocator-copy.cpp");
  const auto Output = tmpFile("deque-explicit-allocator-copy.nc");
  writeFile(Source, R"r19dqalloc(#include <deque>
#include <memory>
#include <new>
using Size=decltype(sizeof(0));using Alloc=std::allocator<int>;
extern "C" void* malloc(Size);extern "C" void free(void*);
static std::deque<int>* owner;static int armed,calls;
void* operator new(Size n){void* p=malloc(n);if(armed){++calls;owner->front()=91;owner->back()=92;}return p;}
void operator delete(void* p) noexcept {free(p);}void operator delete(void* p,Size) noexcept {free(p);}
int main(){Alloc alloc;std::deque<int> source(2050,7);owner=&source;
int* saved=&source.front();armed=1;std::deque<int> a(source,alloc);armed=0;
if(!calls||source.size()!=2050||a.size()!=2050||&source.front()!=saved)return 1;
if(a.front()!=91||a.back()!=92||&a.front()==saved)return 2;
for(unsigned i=1;i!=2049;++i)if(a[i]!=7)return 3;
a.front()=12;if(source.front()!=91)return 4;
const std::deque<int> empty;std::deque<int> b(empty,alloc);return !b.empty();}
)r19dqalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-explicit-allocator-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeExplicitAllocatorMoveRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-explicit-allocator-move.cpp");
  const auto Output = tmpFile("deque-explicit-allocator-move.nc");
  writeFile(Source, R"r19dqalloc(#include <deque>
#include <memory>
#include <new>
using Size=decltype(sizeof(0));using Alloc=std::allocator<int>;
extern "C" void* malloc(Size);extern "C" void free(void*);
static int armed,calls;
void* operator new(Size n){void* p=malloc(n);if(armed)++calls;return p;}
void operator delete(void* p) noexcept {free(p);}void operator delete(void* p,Size) noexcept {free(p);}
int main(){Alloc alloc;std::deque<int> source(2050,7);int* first=&source.front();int* last=&source.back();
armed=1;std::deque<int> a(std::move(source),alloc);armed=0;
if(calls||!source.empty()||a.size()!=2050||&a.front()!=first||&a.back()!=last)return 1;
for(unsigned i=0;i!=2050;++i)if(a[i]!=7)return 2;
source.push_back(11);if(source.size()!=1||source.front()!=11||a.front()!=7)return 3;
std::deque<int> empty;std::deque<int> b(std::move(empty),alloc);return !b.empty()||!empty.empty();}
)r19dqalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-explicit-allocator-move" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeExplicitAllocatorCallSequencingRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-explicit-allocator-call-sequencing.cpp");
  const auto Output = tmpFile("deque-explicit-allocator-call-sequencing.nc");
  writeFile(Source, R"r19dqalloc(#include <deque>
#include <memory>
#include <new>
using Size=decltype(sizeof(0));using Alloc=std::allocator<int>;
extern "C" void* malloc(Size);extern "C" void free(void*);
static int counts,fills,allocators,firsts,lasts;
static int values[3]={4,5,6};static Alloc* allocator;
Size count(){++counts;return 3;}const int& fill(){++fills;return values[0];}
const Alloc& alloc(){++allocators;return *allocator;}
const int* first(){++firsts;return values;}const int* last(){++lasts;return values+3;}
int main(){Alloc backing;allocator=&backing;std::deque<int> a(count(),fill(),alloc());
if(counts!=1||fills!=1||allocators!=1||a.size()!=3||a.front()!=4||a.back()!=4)return 1;
std::deque<int> b(first(),last(),alloc());
if(firsts!=1||lasts!=1||allocators!=2||b.size()!=3||b.front()!=4||b.back()!=6)return 2;
std::deque<int> c(count(),alloc());
return counts!=2||allocators!=3||c.size()!=3||c.front()!=0||c.back()!=0;}
)r19dqalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-explicit-allocator-call-sequencing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeExplicitAllocatorCountOwnedBodyRejected) {
  const auto Source = tmpFile("deque-explicit-allocator-count-owned-body.cpp");
  writeFile(Source, R"r19dqalloc(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::deque(decltype(sizeof(0)) n,const allocator<int>& alloc) : deque() {}
}}
int main(){std::allocator<int> alloc;std::deque<int> a(3,alloc);return a.size()!=0;}
)r19dqalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeExplicitAllocatorFillOwnedBodyRejected) {
  const auto Source = tmpFile("deque-explicit-allocator-fill-owned-body.cpp");
  writeFile(Source, R"r19dqalloc(#include <deque>
namespace std { inline namespace __1 {
template<> template<> deque<int>::deque(decltype(sizeof(0)) n,const int& value,const allocator<int>& alloc) : deque() {}
}}
int main(){std::allocator<int> alloc;std::deque<int> a(3,7,alloc);return a.size()!=0;}
)r19dqalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeExplicitAllocatorRangeOwnedBodiesRejected) {
  const auto Source = tmpFile("deque-explicit-allocator-range-owned-bodies.cpp");
  writeFile(Source, R"r19dqalloc(#include <deque>
namespace std { inline namespace __1 {
template<> template<> deque<int>::deque(int* first,int* last,const allocator<int>& alloc) : deque() {}
template<> template<> deque<int>::deque(const int* first,const int* last,const allocator<int>& alloc) : deque() {}
}}
int main(){std::allocator<int> alloc;int values[3]={1,2,3};const int* begin=values;
std::deque<int> a(values,values+3,alloc),b(begin,begin+3,alloc);return a.size()!=0||b.size()!=0;}
)r19dqalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeExplicitAllocatorCopyMoveOwnedBodiesRejected) {
  const auto Source = tmpFile("deque-explicit-allocator-copy-move-owned-bodies.cpp");
  writeFile(Source, R"r19dqalloc(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::deque(const deque& source,const allocator<int>& alloc) : deque() {}
template<> deque<int>::deque(deque&& source,const allocator<int>& alloc) : deque() {}
}}
int main(){std::allocator<int> alloc;std::deque<int> source;
std::deque<int> a(source,alloc),b(std::move(source),alloc);return a.size()!=0||b.size()!=0;}
)r19dqalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
