
TEST_F(TranslateTest, CoreV2ListValueConstructorsRunAtBothOptimizations) {
  const auto Source = tmpFile("list-value-constructors.cpp");
  const auto Output = tmpFile("list-value-constructors.nc");
  writeFile(Source, R"r19listvalue(#include <list>
#include <new>
#include <utility>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
void* operator new(Size n){return malloc(n);}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {operator delete(p);}
int main(){std::list<int> a={1,2,3};std::list<int> b(a);
if(b.size()!=3||b.front()!=1||b.back()!=3||&b.front()==&a.front())return 1;
b.front()=9;if(a.front()!=1)return 2;int* identity=&a.front();
std::list<int> c(std::move(a));if(!a.empty()||c.size()!=3||&c.front()!=identity)return 3;
a.push_back(7);if(a.size()!=1||a.front()!=7)return 4;
std::list<int> empty;std::list<int> copy(empty);std::list<int> moved(std::move(empty));
return !copy.empty()||!moved.empty()||!empty.empty();}
)r19listvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-value-constructors" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListValueCopyAssignmentRunAtBothOptimizations) {
  const auto Source = tmpFile("list-value-copy-assignment.cpp");
  const auto Output = tmpFile("list-value-copy-assignment.nc");
  writeFile(Source, R"r19listvalue(#include <list>
#include <new>
#include <utility>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
void* operator new(Size n){return malloc(n);}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {operator delete(p);}
static std::list<int>* assignment_left;static std::list<int>* assignment_right;static int assignment_events;
std::list<int>& assignment_receiver(){assignment_events=assignment_events*10+1;return *assignment_left;}
const std::list<int>& assignment_argument(){assignment_events=assignment_events*10+2;return *assignment_right;}
int main(){std::list<int> a={1,2,3};std::list<int> b={8,9,10,11};
int* reused=&b.front();std::list<int>& result=(b=a);
if(&result!=&b||b.size()!=3||b.front()!=1||b.back()!=3||&b.front()!=reused)return 1;
b.front()=7;if(a.front()!=1)return 2;b=b;if(b.front()!=7||b.size()!=3)return 3;
std::list<int> empty;b=empty;if(!b.empty())return 4;
empty=a;if(empty.size()!=3||empty.front()!=1||empty.back()!=3)return 5;
assignment_left=&b;assignment_right=&a;assignment_events=0;
assignment_receiver()=assignment_argument();if(assignment_events!=21||b.size()!=3||b.front()!=1)return 6;
assignment_events=0;assignment_receiver().operator=(assignment_argument());
if(assignment_events!=12||b.size()!=3||b.back()!=3)return 7;
a.clear();return empty.size()!=3||empty.front()!=1;}
)r19listvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-value-copy-assignment" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListValueMoveAssignmentRunAtBothOptimizations) {
  const auto Source = tmpFile("list-value-move-assignment.cpp");
  const auto Output = tmpFile("list-value-move-assignment.nc");
  writeFile(Source, R"r19listvalue(#include <list>
#include <new>
#include <utility>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
void* operator new(Size n){return malloc(n);}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {operator delete(p);}
static std::list<int>* assignment_left;static std::list<int>* assignment_right;static int assignment_events;
std::list<int>& assignment_receiver(){assignment_events=assignment_events*10+1;return *assignment_left;}
std::list<int>&& assignment_argument(){assignment_events=assignment_events*10+2;return static_cast<std::list<int>&&>(*assignment_right);}
int main(){std::list<int> a={2,4,6};std::list<int> b={8,10};
int* identity=&a.front();std::list<int>& result=(b=std::move(a));
if(&result!=&b||!a.empty()||b.size()!=3||b.back()!=6||&b.front()!=identity)return 1;
a.push_front(9);if(a.size()!=1||a.front()!=9)return 2;
std::list<int> empty;b=std::move(empty);if(!b.empty()||!empty.empty())return 3;
empty=std::move(a);if(!a.empty()||empty.size()!=1||empty.front()!=9)return 4;
std::list<int> c={1,2};std::list<int> d={3};assignment_left=&c;assignment_right=&d;assignment_events=0;
assignment_receiver()=assignment_argument();if(assignment_events!=21||c.size()!=1||c.front()!=3||!d.empty())return 5;
d.push_back(11);assignment_events=0;assignment_receiver().operator=(assignment_argument());
return assignment_events!=12||c.size()!=1||c.front()!=11||!d.empty();}
)r19listvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-value-move-assignment" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListValueSwapRunAtBothOptimizations) {
  const auto Source = tmpFile("list-value-swap.cpp");
  const auto Output = tmpFile("list-value-swap.nc");
  writeFile(Source, R"r19listvalue(#include <list>
#include <new>
#include <utility>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
void* operator new(Size n){return malloc(n);}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {operator delete(p);}
int main(){std::list<int> a={1,2};std::list<int> b={3,4,5};
int* first=&a.front();int* second=&b.front();auto iterator=a.begin();
a.swap(b);if(a.size()!=3||b.size()!=2||&a.front()!=second||&b.front()!=first||*iterator!=1)return 1;
++iterator;if(*iterator!=2)return 2;std::swap(a,b);
if(a.size()!=2||b.size()!=3||&a.front()!=first||&b.front()!=second)return 3;
std::list<int> empty;empty.swap(a);if(!a.empty()||empty.size()!=2||&empty.front()!=first)return 4;
a.push_back(7);return a.size()!=1||a.back()!=7||empty.back()!=2;}
)r19listvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-value-swap" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListValueAllocationEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("list-value-allocation-effects.cpp");
  const auto Output = tmpFile("list-value-allocation-effects.nc");
  writeFile(Source, R"r19listvalue(#include <list>
#include <new>
#include <utility>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
static std::list<int>* owner;static int* input;static int stage,calls,bad,write_value;
void* operator new(Size n){if(stage){++calls;if(owner->size()!=3)bad=1;*input=write_value;}return malloc(n);}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {operator delete(p);}
int main(){std::list<int> a={1,2,3};owner=&a;input=&a.front();write_value=77;stage=1;
std::list<int> b(a);stage=0;if(calls!=3||bad||b.front()!=77||b.back()!=3||a.front()!=77)return 1;
std::list<int> c={4,5,6};std::list<int> d={8,9};owner=&c;input=&c.back();write_value=99;calls=0;stage=1;
d=c;stage=0;return calls!=1||bad||d.size()!=3||d.front()!=4||d.back()!=99||c.back()!=99;}
)r19listvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-value-allocation-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListValueExplicitAllocatorSequencingRunAtBothOptimizations) {
  const auto Source = tmpFile("list-value-explicit-allocator-sequencing.cpp");
  const auto Output = tmpFile("list-value-explicit-allocator-sequencing.nc");
  writeFile(Source, R"r19listvalue(#include <list>
#include <new>
#include <utility>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
void* operator new(Size n){return malloc(n);}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {operator delete(p);}
static std::list<int>* owner;static int sources,allocators;
std::list<int>& source(){++sources;return *owner;}
std::allocator<int> allocator(){++allocators;return std::allocator<int>{};}
int main(){std::list<int> a={5,6,7};owner=&a;
std::list<int> b(source(),allocator());
if(sources!=1||allocators!=1||b.size()!=3||b.front()!=5||&b.front()==&a.front())return 1;
int* identity=&a.front();std::list<int> c(std::move(source()),allocator());
if(sources!=2||allocators!=2||!a.empty()||c.size()!=3||&c.front()!=identity)return 2;
a.push_back(9);return a.front()!=9||b.back()!=7||c.back()!=7;}
)r19listvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-value-explicit-allocator-sequencing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListValueOwnedCopyConstructorRejected) {
  const auto Source = tmpFile("list-value-owned-copy-constructor.cpp");
  writeFile(Source, R"r19listvalue(#include <list>
namespace std { inline namespace __1 {
template<> list<int>::list(const list<int>& source) {}
}}
int main(){std::list<int> a={1,2,3};std::list<int> b(a);return 0;}
)r19listvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListValueOwnedMoveConstructorRejected) {
  const auto Source = tmpFile("list-value-owned-move-constructor.cpp");
  writeFile(Source, R"r19listvalue(#include <list>
namespace std { inline namespace __1 {
template<> list<int>::list(list<int>&& source) noexcept {}
}}
int main(){std::list<int> a={1,2,3};std::list<int> b(static_cast<std::list<int>&&>(a));return 0;}
)r19listvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListValueOwnedCopyAssignmentRejected) {
  const auto Source = tmpFile("list-value-owned-copy-assignment.cpp");
  writeFile(Source, R"r19listvalue(#include <list>
namespace std { inline namespace __1 {
template<> list<int>& list<int>::operator=(const list<int>& source) {return *this;}
}}
int main(){std::list<int> a={1,2,3};std::list<int> b;b=a;return 0;}
)r19listvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListValueOwnedMoveAssignmentRejected) {
  const auto Source = tmpFile("list-value-owned-move-assignment.cpp");
  writeFile(Source, R"r19listvalue(#include <list>
namespace std { inline namespace __1 {
template<> list<int>& list<int>::operator=(list<int>&& source) noexcept {return *this;}
}}
int main(){std::list<int> a={1,2,3};std::list<int> b;b=static_cast<std::list<int>&&>(a);return 0;}
)r19listvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
