
TEST_F(TranslateTest, CoreV2DequeCountInsertBasicRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-count-insert-basic.cpp");
  const auto Output = tmpFile("deque-count-insert-basic.nc");
  writeFile(Source, R"r19dqcntinsert(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a;
auto empty=a.insert(a.cbegin(),0,7);if(empty!=a.end()||!a.empty())return 1;
auto first=a.insert(a.cend(),3,4);if(first!=a.begin()||*first!=4||a.size()!=3)return 2;
int* original=&a.front();auto back=a.insert(a.cend(),2,8);
if(back!=a.begin()+3||*back!=8||&a.front()!=original)return 3;
int* tail=&a.back();auto front=a.insert(a.cbegin(),2,1);
if(front!=a.begin()||*front!=1||&a.back()!=tail)return 4;
auto middle=a.insert(a.cbegin()+3,2,6);const int expected[9]={1,1,4,6,6,4,4,8,8};
if(middle!=a.begin()+3||a.size()!=9)return 5;
for(unsigned i=0;i!=9;++i)if(a[i]!=expected[i])return 6;
int* stable=&a.front();auto zero=a.insert(a.cbegin()+4,0,99);
if(zero!=a.begin()+4||a.size()!=9||&a.front()!=stable)return 7;
a.clear();a.insert(a.end(),1,9);return a.size()!=1||a.front()!=9;}
)r19dqcntinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-count-insert-basic" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeCountInsertFrontGrowthRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-count-insert-front-growth.cpp");
  const auto Output = tmpFile("deque-count-insert-front-growth.nc");
  writeFile(Source, R"r19dqcntinsert(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a(2050);
for(unsigned i=0;i!=2050;++i)a[i]=(int)i;auto next=a.insert(a.cbegin()+2,3073,-1);
if(a.size()!=5123||next!=a.begin()+2||*next!=-1||a.front()!=0||a.back()!=2049)return 1;
for(unsigned i=0;i!=5123;++i)if(a[i]!=(i<2?(int)i:i<3075?-1:(int)i-3073))return 2;
a.push_front(-2);a.push_back(2050);return a.front()!=-2||a.back()!=2050||a.size()!=5125;}
)r19dqcntinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-count-insert-front-growth" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeCountInsertBackGrowthRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-count-insert-back-growth.cpp");
  const auto Output = tmpFile("deque-count-insert-back-growth.nc");
  writeFile(Source, R"r19dqcntinsert(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a(2050);
for(unsigned i=0;i!=2050;++i)a[i]=(int)i;auto next=a.insert(a.cbegin()+2048,3073,-2);
if(a.size()!=5123||next!=a.begin()+2048||*next!=-2||a.front()!=0||a.back()!=2049)return 1;
for(unsigned i=0;i!=5123;++i)if(a[i]!=(i<2048?(int)i:i<5121?-2:(int)i-3073))return 2;
a.push_front(-3);a.push_back(2050);return a.front()!=-3||a.back()!=2050||a.size()!=5125;}
)r19dqcntinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-count-insert-back-growth" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeCountInsertAliasesRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-count-insert-aliases.cpp");
  const auto Output = tmpFile("deque-count-insert-aliases.nc");
  writeFile(Source, R"r19dqcntinsert(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a={0,1,2,3,4,5,6,7};
auto first=a.insert(a.cbegin()+2,3,a.front());const int one[11]={0,1,0,0,0,2,3,4,5,6,7};
if(first!=a.begin()+2||a.size()!=11)return 1;for(unsigned i=0;i!=11;++i)if(a[i]!=one[i])return 2;
auto second=a.insert(a.cend()-2,5,a.back());
if(second!=a.begin()+9||a.size()!=16||a.back()!=7||a[14]!=6)return 3;
for(unsigned i=0;i!=9;++i)if(a[i]!=one[i])return 4;
for(unsigned i=9;i!=14;++i)if(a[i]!=7)return 5;
std::deque<int> b(1023);for(unsigned i=0;i!=1023;++i)b[i]=(int)i;
auto third=b.insert(b.cbegin()+1,2049,b[2]);
if(third!=b.begin()+1||*third!=2||b.size()!=3072||b.front()!=0||b.back()!=1022)return 6;
for(unsigned i=1;i!=2050;++i)if(b[i]!=2)return 7;
for(unsigned i=2050;i!=3072;++i)if(b[i]!=(int)i-2049)return 8;return 0;}
)r19dqcntinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-count-insert-aliases" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeCountInsertAllocationEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-count-insert-allocation-effects.cpp");
  const auto Output = tmpFile("deque-count-insert-allocation-effects.nc");
  writeFile(Source, R"r19dqcntinsert(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
static std::deque<int>* owner;static int* input;
static int stage,calls,bad;
void* operator new(Size n){if(stage){++calls;if(owner->size()!=1023)bad=1;*input=77;}return malloc(n);}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {operator delete(p);}
int main(){std::deque<int> a(1023,8);owner=&a;input=&a.front();stage=1;
auto front=a.insert(a.cbegin(),2050,*input);stage=0;
if(!calls||bad||front!=a.begin()||a.size()!=3073||a[2050]!=77||a.back()!=8)return 1;
for(unsigned i=0;i!=2050;++i)if(a[i]!=77)return 2;
std::deque<int> b(1023,9);owner=&b;input=&b.front();calls=0;stage=1;
auto back=b.insert(b.cend(),2050,*input);stage=0;
if(!calls||bad||back!=b.begin()+1023||b.size()!=3073||b.front()!=77||b[1022]!=9)return 3;
for(unsigned i=1023;i!=3073;++i)if(b[i]!=77)return 4;return 0;}
)r19dqcntinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-count-insert-allocation-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeCountInsertCallSequencingRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-count-insert-call-sequencing.cpp");
  const auto Output = tmpFile("deque-count-insert-call-sequencing.nc");
  writeFile(Source, R"r19dqcntinsert(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
using It=std::deque<int>::const_iterator;
static std::deque<int>* owner;static const It* position_value;
static int receivers,positions,counts,values,bad,number=2;static Size amount=2;
std::deque<int>& receiver(){++receivers;return *owner;}
It position(){++positions;if(receivers!=positions)bad=1;return *position_value;}
Size count(){++counts;if(receivers!=counts)bad=1;return amount;}
const int& value(){++values;if(receivers!=values)bad=1;return number;}
int main(){std::deque<int> a={1,4};owner=&a;It p=a.cbegin()+1;position_value=&p;
auto one=receiver().insert(position(),count(),value());
if(bad||receivers!=1||positions!=1||counts!=1||values!=1||one!=a.begin()+1||a.size()!=4)return 1;
const int expected[4]={1,2,2,4};for(unsigned i=0;i!=4;++i)if(a[i]!=expected[i])return 2;
p=a.cend();amount=0;auto zero=receiver().insert(position(),count(),value());
return bad||receivers!=2||positions!=2||counts!=2||values!=2||zero!=a.end()||a.size()!=4;}
)r19dqcntinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-count-insert-call-sequencing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeCountInsertOwnedEmptyBodyRejected) {
  const auto Source = tmpFile("deque-count-insert-owned-empty.cpp");
  writeFile(Source, R"r19dqcntinsert(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::iterator deque<int>::insert(const_iterator position,size_type count,const value_type& value) {return begin();}
}}
int main(){std::deque<int> a={4,5,6};int value=7;a.insert(a.cbegin()+1,2,value);return 0;}
)r19dqcntinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeCountInsertOwnedForwardingBodyRejected) {
  const auto Source = tmpFile("deque-count-insert-owned-forwarding.cpp");
  writeFile(Source, R"r19dqcntinsert(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::iterator deque<int>::insert(const_iterator position,size_type count,const value_type& value) {return insert(position,value);}
}}
int main(){std::deque<int> a={4,5,6};int value=7;a.insert(a.cbegin()+1,2,value);return 0;}
)r19dqcntinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeCountInsertOwnedCountForwardingBodyRejected) {
  const auto Source = tmpFile("deque-count-insert-owned-count-forwarding.cpp");
  writeFile(Source, R"r19dqcntinsert(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::iterator deque<int>::insert(const_iterator position,size_type count,const value_type& value) {assign(count,value);return begin();}
}}
int main(){std::deque<int> a={4,5,6};int value=7;a.insert(a.cbegin()+1,2,value);return 0;}
)r19dqcntinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeCountInsertOwnedConditionalForwardingBodyRejected) {
  const auto Source = tmpFile("deque-count-insert-owned-conditional-forwarding.cpp");
  writeFile(Source, R"r19dqcntinsert(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::iterator deque<int>::insert(const_iterator position,size_type count,const value_type& value) {if(count==0)return begin();return insert(position,value);}
}}
int main(){std::deque<int> a={4,5,6};int value=7;a.insert(a.cbegin()+1,2,value);return 0;}
)r19dqcntinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
