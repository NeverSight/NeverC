
TEST_F(TranslateTest, CoreV2DequeRangeInsertBasicRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-range-insert-basic.cpp");
  const auto Output = tmpFile("deque-range-insert-basic.nc");
  writeFile(Source, R"r19dqrnginsert(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){int input[]={1,2,3};std::deque<int> a;
auto empty=a.insert(a.cbegin(),input,input);if(empty!=a.begin()||!a.empty())return 1;
auto first=a.insert(a.cbegin(),input,input+3);if(first!=a.begin()||*first!=1||a.size()!=3)return 2;
const int more[]={4,5};auto tail=a.insert(a.cend(),more,more+2);
if(tail!=a.begin()+3||*tail!=4||a.back()!=5||a.size()!=5)return 3;
auto middle=a.insert(a.cbegin()+2,{8,9});if(middle!=a.begin()+2||*middle!=8||a.size()!=7)return 4;
auto unchanged=a.insert(a.cbegin()+3,{});if(unchanged!=a.begin()+3||a.size()!=7)return 5;
auto front=a.insert(a.cbegin(),{0});if(front!=a.begin()||a.size()!=8)return 6;
int expected[]={0,1,2,8,9,3,4,5};for(unsigned i=0;i!=8;++i)if(a[i]!=expected[i])return 7;return 0;}
)r19dqrnginsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-range-insert-basic" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeRangeInsertFrontGrowthRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-range-insert-front-growth.cpp");
  const auto Output = tmpFile("deque-range-insert-front-growth.nc");
  writeFile(Source, R"r19dqrnginsert(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a(2050);int input[3073];
for(unsigned i=0;i!=2050;++i)a[i]=(int)i;for(unsigned i=0;i!=3073;++i)input[i]=-(int)i-1;
auto first=a.insert(a.cbegin()+1,input,input+3073);
if(first!=a.begin()+1||a.size()!=5123||a.front()!=0||a.back()!=2049)return 1;
for(unsigned i=0;i!=3073;++i)if(a[i+1]!=input[i])return 2;
for(unsigned i=1;i!=2050;++i)if(a[i+3073]!=(int)i)return 3;
auto second=a.insert(a.cbegin()+1024,{-8,-9});
return second!=a.begin()+1024||a.size()!=5125||*second!=-8||a[1025]!=-9||a[1023]!=-1023||a[1026]!=-1024;}
)r19dqrnginsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-range-insert-front-growth" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeRangeInsertBackGrowthRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-range-insert-back-growth.cpp");
  const auto Output = tmpFile("deque-range-insert-back-growth.nc");
  writeFile(Source, R"r19dqrnginsert(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a(2050);int input[3073];
for(unsigned i=0;i!=2050;++i)a[i]=(int)i;for(unsigned i=0;i!=3073;++i)input[i]=-(int)i-1;
const int* begin=input;auto first=a.insert(a.cend()-1,begin,begin+3073);
if(first!=a.begin()+2049||a.size()!=5123||a.front()!=0||a.back()!=2049)return 1;
for(unsigned i=0;i!=2049;++i)if(a[i]!=(int)i)return 2;
for(unsigned i=0;i!=3073;++i)if(a[i+2049]!=input[i])return 3;
auto second=a.insert(a.cbegin()+3072,{-8,-9});
return second!=a.begin()+3072||a.size()!=5125||*second!=-8||a[3073]!=-9||a[3071]!=-1023||a[3074]!=-1024;}
)r19dqrnginsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-range-insert-back-growth" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeRangeInsertReuseAndResultsRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-range-insert-reuse-and-results.cpp");
  const auto Output = tmpFile("deque-range-insert-reuse-and-results.nc");
  writeFile(Source, R"r19dqrnginsert(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a={1,2,3,4};int input[]={7,8};
auto middle=a.insert(a.cbegin()+1,input,input+2);if(middle!=a.begin()+1||*middle!=7||a.size()!=6)return 1;
a.erase(a.cbegin()+2,a.cbegin()+4);if(a.size()!=4||a[0]!=1||a[1]!=7||a[2]!=3||a[3]!=4)return 2;
a.clear();auto empty=a.insert(a.cend(),input+1,input+1);if(empty!=a.begin()||!a.empty())return 3;
auto list=a.insert(a.cbegin(),{9,10});if(list!=a.begin()||*list!=9||a.back()!=10)return 4;
for(unsigned i=0;i!=4096;++i)a.push_back((int)i);for(unsigned i=0;i!=4096;++i)a.pop_front();
auto again=a.insert(a.cbegin(),input,input+2);return again!=a.begin()||*again!=7||a[1]!=8||a.size()!=4;}
)r19dqrnginsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-range-insert-reuse-and-results" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeRangeInsertAllocationEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-range-insert-allocation-effects.cpp");
  const auto Output = tmpFile("deque-range-insert-allocation-effects.nc");
  writeFile(Source, R"r19dqrnginsert(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
static std::deque<int>* owner;static int* input;static int stage,calls,bad;
void* operator new(Size n){if(stage){++calls;if(owner->size()!=1023)bad=1;owner->front()=77;input[0]=77;input[2049]=88;}return malloc(n);}
void operator delete(void* p) noexcept {free(p);}void operator delete(void* p,Size) noexcept {operator delete(p);}
int main(){int values[2050];for(unsigned i=0;i!=2050;++i)values[i]=(int)i;input=values;
std::deque<int> a(1023,8);owner=&a;const int* start=values;stage=1;
auto front=a.insert(a.cbegin()+1,start,start+2050);stage=0;
if(!calls||bad||front!=a.begin()+1||*front!=77||a.front()!=77||a[2050]!=88||a[2051]!=8||a.size()!=3073)return 1;
std::deque<int> b(1023,8);owner=&b;calls=0;values[0]=-5;values[2049]=-6;stage=1;
auto back=b.insert(b.cend()-1,values,values+2050);stage=0;
if(!calls||bad||back!=b.begin()+1022||*back!=77||b[3071]!=88||b.back()!=8||b.size()!=3073)return 2;
std::deque<int> c(1023,9);owner=&c;calls=0;stage=1;auto list=c.insert(c.cend(),{1,2,3});stage=0;
return !calls||bad||list!=c.begin()+1023||*list!=1||c.front()!=77||c.back()!=3||c.size()!=1026;}
)r19dqrnginsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-range-insert-allocation-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeRangeInsertCallSequencingRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-range-insert-call-sequencing.cpp");
  const auto Output = tmpFile("deque-range-insert-call-sequencing.nc");
  writeFile(Source, R"r19dqrnginsert(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
using It=std::deque<int>::const_iterator;
static std::deque<int>* owner;static const It* point;static int data[]={2,3};
static int receivers,positions,firsts,lasts,values,bad;
std::deque<int>& receiver(){++receivers;return *owner;}
It position(){++positions;if(receivers!=positions)bad=1;return *point;}
int* first(){++firsts;if(receivers!=1)bad=1;return data;}
const int* last(){++lasts;if(receivers!=1)bad=1;return data+2;}
const int* const_first(){++firsts;if(receivers!=1)bad=1;return data;}
int value(int number){++values;if(receivers!=2||(values==1&&number!=5)||(values==2&&number!=6))bad=1;return number;}
int main(){std::deque<int> a={1,4};owner=&a;It p=a.cbegin()+1;point=&p;
auto range=receiver().insert(position(),const_first(),last());
if(bad||receivers!=1||positions!=1||firsts!=1||lasts!=1||range!=a.begin()+1||a.size()!=4)return 1;
p=a.cbegin()+2;auto list=receiver().insert(position(),{value(5),value(6)});
if(bad||receivers!=2||positions!=2||values!=2||firsts!=1||lasts!=1||list!=a.begin()+2||a.size()!=6)return 2;
int expected[]={1,2,5,6,3,4};for(unsigned i=0;i!=6;++i)if(a[i]!=expected[i])return 3;return 0;}
)r19dqrnginsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-range-insert-call-sequencing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeRangeInsertOwnedMutableBodyRejected) {
  const auto Source = tmpFile("deque-range-insert-owned-mutable.cpp");
  writeFile(Source, R"r19dqrnginsert(#include <deque>
namespace std { inline namespace __1 {
template<> template<> deque<int>::iterator deque<int>::insert<int*,0>(const_iterator position,int* first,int* last) {return begin();}
}}
int main(){int input[3]={1,2,3};int* first=input;std::deque<int> a={4,5};
a.insert(a.cbegin()+1,first,first+3);return 0;}
)r19dqrnginsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeRangeInsertOwnedConstBodyRejected) {
  const auto Source = tmpFile("deque-range-insert-owned-const.cpp");
  writeFile(Source, R"r19dqrnginsert(#include <deque>
namespace std { inline namespace __1 {
template<> template<> deque<int>::iterator deque<int>::insert<const int*,0>(const_iterator position,const int* first,const int* last) {return begin();}
}}
int main(){int input[3]={1,2,3};const int* first=input;std::deque<int> a={4,5};
a.insert(a.cbegin()+1,first,first+3);return 0;}
)r19dqrnginsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeRangeInsertOwnedListEmptyBodyRejected) {
  const auto Source = tmpFile("deque-range-insert-owned-list-empty.cpp");
  writeFile(Source, R"r19dqrnginsert(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::iterator deque<int>::insert(const_iterator position,initializer_list<value_type> values) {return begin();}
}}
int main(){std::deque<int> a={4,5};a.insert(a.cbegin()+1,{1,2,3});return 0;}
)r19dqrnginsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeRangeInsertOwnedListForwardingBodyRejected) {
  const auto Source = tmpFile("deque-range-insert-owned-list-forwarding.cpp");
  writeFile(Source, R"r19dqrnginsert(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::iterator deque<int>::insert(const_iterator position,initializer_list<value_type> values) {return insert(position,values.begin(),values.end());}
}}
int main(){std::deque<int> a={4,5};a.insert(a.cbegin()+1,{1,2,3});return 0;}
)r19dqrnginsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
