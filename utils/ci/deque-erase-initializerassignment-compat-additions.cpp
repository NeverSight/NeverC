
TEST_F(TranslateTest, CoreV2DequeInitializerAssignmentBasicRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-initializer-assignment-basic.cpp");
  const auto Output = tmpFile("deque-initializer-assignment-basic.nc");
  writeFile(Source, R"r19dqinitassign(#include <deque>
#include <initializer_list>
#include <new>
using Size=decltype(sizeof(0));using List=std::initializer_list<int>;
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a(8,7);List values={4,5,6};
int* saved=&a.front();a.assign(values);
if(a.size()!=3||a.front()!=4||a.back()!=6||&a.front()!=saved)return 1;
std::deque<int>& result=(a=values);if(&result!=&a||a.size()!=3||a[1]!=5)return 2;
std::deque<int>& explicit_result=a.operator=({8,9});
if(&explicit_result!=&a||a.size()!=2||a.front()!=8||a.back()!=9)return 3;
a={-3,-2,-1,0};return a.size()!=4||a.front()!=-3||a.back()!=0;}
)r19dqinitassign");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-initializer-assignment-basic" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeInitializerAssignmentGrowthRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-initializer-assignment-growth.cpp");
  const auto Output = tmpFile("deque-initializer-assignment-growth.nc");
  writeFile(Source, R"r19dqinitassign(#include <deque>
#include <initializer_list>
#include <new>
using Size=decltype(sizeof(0));using List=std::initializer_list<int>;
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){List values={-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6};
std::deque<int> a(3,77);int* saved=&a.front();
a.assign(values);if(a.size()!=2050||&a.front()!=saved)return 1;
for(unsigned i=0;i!=2050;++i)if(a[i]!=(int)((i*7)%29)-11)return 2;
a={41,42};if(a.size()!=2||a.front()!=41||a.back()!=42)return 3;
a=values;if(a.size()!=2050)return 4;
for(unsigned i=0;i!=2050;++i)if(a[i]!=(int)((i*7)%29)-11)return 5;
return 0;}
)r19dqinitassign");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-initializer-assignment-growth" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeInitializerAssignmentShrinkEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-initializer-assignment-shrink-effects.cpp");
  const auto Output = tmpFile("deque-initializer-assignment-shrink-effects.nc");
  writeFile(Source, R"r19dqinitassign(#include <deque>
#include <initializer_list>
#include <new>
using Size=decltype(sizeof(0));using List=std::initializer_list<int>;
extern "C" void* malloc(Size);extern "C" void free(void*);
static std::deque<int>* owner;static int armed,calls,bad;
void* operator new(Size n){return malloc(n);}
void operator delete(void* p) noexcept {if(armed){++calls;if(owner->size()!=3||owner->front()!=4||owner->back()!=6)bad=1;}free(p);}
void operator delete(void* p,Size) noexcept {operator delete(p);}
int main(){std::deque<int> a(3073,17);owner=&a;int* first=&a.front();int* third=&a[2];
List values={4,5,6};armed=1;a.assign(values);armed=0;
if(!calls||bad||a.size()!=3||&a.front()!=first||&a[2]!=third||a[1]!=5)return 1;
a.push_back(7);a.push_front(3);return a.size()!=5||a.front()!=3||a.back()!=7;}
)r19dqinitassign");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-initializer-assignment-shrink-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeInitializerAssignmentEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-initializer-assignment-empty.cpp");
  const auto Output = tmpFile("deque-initializer-assignment-empty.nc");
  writeFile(Source, R"r19dqinitassign(#include <deque>
#include <initializer_list>
#include <new>
using Size=decltype(sizeof(0));using List=std::initializer_list<int>;
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){List empty;std::deque<int> a(2050,7);
a.assign(empty);if(!a.empty())return 1;
a.push_back(8);a=empty;if(!a.empty())return 2;
a.push_front(9);std::deque<int>& result=(a={});if(&result!=&a||!a.empty())return 3;
a.assign({});a.push_back(11);a.push_front(10);return a.size()!=2||a.front()!=10||a.back()!=11;}
)r19dqinitassign");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-initializer-assignment-empty" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeInitializerAssignmentPrefixAllocationRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-initializer-assignment-prefix-allocation.cpp");
  const auto Output = tmpFile("deque-initializer-assignment-prefix-allocation.nc");
  writeFile(Source, R"r19dqinitassign(#include <deque>
#include <initializer_list>
#include <new>
using Size=decltype(sizeof(0));using List=std::initializer_list<int>;
extern "C" void* malloc(Size);extern "C" void free(void*);
static std::deque<int>* owner;static int armed,calls,bad;
void* operator new(Size n){void* p=malloc(n);if(armed){++calls;
if(owner->size()!=3||owner->front()!=-11||(*owner)[1]!=-4||owner->back()!=3)bad=1;}return p;}
void operator delete(void* p) noexcept {free(p);}void operator delete(void* p,Size) noexcept {free(p);}
int main(){List values={-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6,13,-9,-2,5,12,-10,-3,4,11,-11,-4,3,10,17,-5,2,9,16,-6,1,8,15,-7,0,7,14,-8,-1,6};
std::deque<int> a(3,77);owner=&a;int* saved=&a.front();
armed=1;a=values;armed=0;
if(!calls||bad||a.size()!=2050||&a.front()!=saved)return 1;
for(unsigned i=0;i!=2050;++i)if(a[i]!=(int)((i*7)%29)-11)return 2;
return 0;}
)r19dqinitassign");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-initializer-assignment-prefix-allocation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeInitializerAssignmentSequencingRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-initializer-assignment-sequencing.cpp");
  const auto Output = tmpFile("deque-initializer-assignment-sequencing.nc");
  writeFile(Source, R"r19dqinitassign(#include <deque>
#include <initializer_list>
#include <new>
using Size=decltype(sizeof(0));using List=std::initializer_list<int>;
extern "C" void* malloc(Size);extern "C" void free(void*);
static std::deque<int>* owner;static const List* input_list;
static int trace,receivers,inputs;
std::deque<int>* receiver(){trace=trace*10+1;++receivers;return owner;}
List input(){trace=trace*10+2;++inputs;return *input_list;}
int main(){std::deque<int> a(8,7);List values={4,5,6};owner=&a;input_list=&values;
std::deque<int>& first=(*receiver()=input());
if(trace!=21||receivers!=1||inputs!=1||&first!=&a||a.front()!=4||a.back()!=6)return 1;
trace=0;std::deque<int>& second=receiver()->operator=(input());
if(trace!=12||receivers!=2||inputs!=2||&second!=&a||a.size()!=3)return 2;
trace=0;receiver()->assign(input());
return trace!=12||receivers!=3||inputs!=3||a.size()!=3||a.front()!=4||a.back()!=6;}
)r19dqinitassign");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-initializer-assignment-sequencing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeInitializerAssignOwnedEmptyBodyRejected) {
  const auto Source = tmpFile("deque-initializer-owned-assign-empty.cpp");
  writeFile(Source, R"r19dqinitassign(#include <deque>
#include <initializer_list>
namespace std { inline namespace __1 {
template<> void deque<int>::assign(initializer_list<int> values) {}
}}
int main(){std::deque<int> a;std::initializer_list<int> values={4,5,6};a.assign(values);return 0;}
)r19dqinitassign");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeInitializerAssignOwnedForwardingBodyRejected) {
  const auto Source = tmpFile("deque-initializer-owned-assign-forwarding.cpp");
  writeFile(Source, R"r19dqinitassign(#include <deque>
#include <initializer_list>
namespace std { inline namespace __1 {
template<> void deque<int>::assign(initializer_list<int> values) {assign(values.begin(),values.end());}
}}
int main(){std::deque<int> a;std::initializer_list<int> values={4,5,6};a.assign(values);return 0;}
)r19dqinitassign");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeInitializerOperatorOwnedEmptyBodyRejected) {
  const auto Source = tmpFile("deque-initializer-owned-operator-empty.cpp");
  writeFile(Source, R"r19dqinitassign(#include <deque>
#include <initializer_list>
namespace std { inline namespace __1 {
template<> deque<int>& deque<int>::operator=(initializer_list<int> values) {return *this;}
}}
int main(){std::deque<int> a;std::initializer_list<int> values={4,5,6};a=values;return 0;}
)r19dqinitassign");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeInitializerOperatorOwnedForwardingBodyRejected) {
  const auto Source = tmpFile("deque-initializer-owned-operator-forwarding.cpp");
  writeFile(Source, R"r19dqinitassign(#include <deque>
#include <initializer_list>
namespace std { inline namespace __1 {
template<> deque<int>& deque<int>::operator=(initializer_list<int> values) {assign(values.begin(),values.end());return *this;}
}}
int main(){std::deque<int> a;std::initializer_list<int> values={4,5,6};a=values;return 0;}
)r19dqinitassign");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
