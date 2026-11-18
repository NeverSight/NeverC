
TEST_F(TranslateTest, CoreV2DequePositionalEmplaceSingleRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-positional-emplace-single.cpp");
  const auto Output = tmpFile("deque-positional-emplace-single.nc");
  writeFile(Source, R"r19dqposemplace(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a;int one=1;
auto first=a.emplace(a.cbegin(),one);if(first!=a.begin()||*first!=1||a.size()!=1)return 1;
int* old=&a.front();auto back=a.emplace(a.cend(),3);
if(back!=a.end()-1||*back!=3||&a.front()!=old)return 2;
auto middle=a.emplace(a.cbegin()+1,2);if(middle!=a.begin()+1||*middle!=2||a.size()!=3)return 3;
int* tail=&a.back();int zero=0;auto front=a.emplace(a.begin(),zero);
if(front!=a.begin()||*front!=0||&a.back()!=tail)return 4;
for(unsigned i=0;i!=4;++i)if(a[i]!=(int)i)return 5;
auto zero_iterator=a.emplace(a.cend());if(zero_iterator!=a.end()-1||*zero_iterator!=0||a.size()!=5)return 6;
auto front_zero=a.emplace(a.cbegin());if(front_zero!=a.begin()||*front_zero!=0||a.size()!=6)return 7;
auto middle_zero=a.emplace(a.cbegin()+2);if(middle_zero!=a.begin()+2||*middle_zero!=0||a.size()!=7)return 8;
int expected[]={0,0,0,1,2,3,0};for(unsigned i=0;i!=7;++i)if(a[i]!=expected[i])return 9;return 0;}
)r19dqposemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-positional-emplace-single" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequePositionalEmplaceFrontGrowthRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-positional-emplace-front-growth.cpp");
  const auto Output = tmpFile("deque-positional-emplace-front-growth.nc");
  writeFile(Source, R"r19dqposemplace(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a(4095);
for(unsigned i=0;i!=4095;++i)a[i]=(int)i;int value=-1;
auto first=a.emplace(a.cbegin()+1,value);
if(a.size()!=4096||first!=a.begin()+1||*first!=-1||a.front()!=0||a.back()!=4094)return 1;
for(unsigned i=2;i!=4096;++i)if(a[i]!=(int)i-1)return 2;
auto second=a.emplace(a.cbegin()+1024,-2);
if(a.size()!=4097||second!=a.begin()+1024||*second!=-2||a[1023]!=1022||a[1025]!=1023)return 3;
a.push_front(-3);a.push_back(4095);return a.front()!=-3||a.back()!=4095||a.size()!=4099;}
)r19dqposemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-positional-emplace-front-growth" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequePositionalEmplaceBackGrowthRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-positional-emplace-back-growth.cpp");
  const auto Output = tmpFile("deque-positional-emplace-back-growth.nc");
  writeFile(Source, R"r19dqposemplace(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a(4095);
for(unsigned i=0;i!=4095;++i)a[i]=(int)i;int value=-1;
auto first=a.emplace(a.cend()-1,value);
if(a.size()!=4096||first!=a.begin()+4094||*first!=-1||a.front()!=0||a.back()!=4094)return 1;
for(unsigned i=0;i!=4094;++i)if(a[i]!=(int)i)return 2;
auto second=a.emplace(a.cbegin()+3072,-2);
if(a.size()!=4097||second!=a.begin()+3072||*second!=-2||a[3071]!=3071||a[3073]!=3072)return 3;
a.push_front(-3);a.push_back(4095);return a.front()!=-3||a.back()!=4095||a.size()!=4099;}
)r19dqposemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-positional-emplace-back-growth" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequePositionalEmplaceAliasesRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-positional-emplace-aliases.cpp");
  const auto Output = tmpFile("deque-positional-emplace-aliases.nc");
  writeFile(Source, R"r19dqposemplace(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a(3071);
for(unsigned i=0;i!=3071;++i)a[i]=(int)i;
auto first=a.emplace(a.cbegin()+1,a.back());if(*first!=3070||a.size()!=3072||a.front()!=0||a[2]!=1)return 1;
auto second=a.emplace(a.cend()-1,a.front());if(*second!=0||a.size()!=3073||a.back()!=3070)return 2;
auto third=a.emplace(a.cbegin()+1024,a[1023]);if(*third!=1022||a[1023]!=1022||a[1025]!=1023)return 3;
auto fourth=a.emplace(a.cend()-2,static_cast<int&&>(a[1024]));
if(*fourth!=1022||a.size()!=3075||a[1024]!=1022||a.back()!=3070)return 4;
a.clear();int x=8;auto again=a.emplace(a.end(),x);if(again!=a.begin()||*again!=8||a.size()!=1)return 5;
std::deque<int> b={0,1,2,3,4,5,6,7};auto prefix=b.emplace(b.cbegin()+2,static_cast<int&&>(b.front()));
if(*prefix!=0||b.front()!=0||b[1]!=1||b[3]!=2||b.size()!=9)return 6;
std::deque<int> c={0,1,2,3,4,5,6,7};auto suffix=c.emplace(c.cbegin()+6,static_cast<int&&>(c.back()));
return *suffix!=7||c.back()!=7||c[7]!=6||c.size()!=9;}
)r19dqposemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-positional-emplace-aliases" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequePositionalEmplaceAllocationEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-positional-emplace-allocation-effects.cpp");
  const auto Output = tmpFile("deque-positional-emplace-allocation-effects.nc");
  writeFile(Source, R"r19dqposemplace(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
static std::deque<int>* owner;static int stage,calls,bad;
void* operator new(Size n){if(stage){++calls;if(owner->size()!=1023)bad=1;owner->front()=77;}return malloc(n);}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {operator delete(p);}
int main(){std::deque<int> a(1023,8);owner=&a;stage=1;auto back=a.emplace(a.cend(),a.front());stage=0;
if(!calls||bad||*back!=77||a.front()!=77||a.back()!=77||a.size()!=1024)return 1;
std::deque<int> b(1023,9);owner=&b;calls=0;stage=1;auto front=b.emplace(b.cbegin(),b.front());stage=0;
if(!calls||bad||front!=b.begin()||*front!=77||b[1]!=77||b.size()!=1024)return 2;
std::deque<int> c(1023,10);owner=&c;calls=0;stage=1;auto middle=c.emplace(c.cbegin()+1,c.front());stage=0;
return !calls||bad||middle!=c.begin()+1||*middle!=77||c.front()!=77||c[2]!=10||c.size()!=1024;}
)r19dqposemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-positional-emplace-allocation-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequePositionalEmplaceCallSequencingRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-positional-emplace-call-sequencing.cpp");
  const auto Output = tmpFile("deque-positional-emplace-call-sequencing.nc");
  writeFile(Source, R"r19dqposemplace(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
using It=std::deque<int>::const_iterator;
static std::deque<int>* owner;static const It* position_value;static int receivers,positions,values,bad,number=2;
std::deque<int>& receiver(){++receivers;return *owner;}
It position(){++positions;if(receivers!=positions)bad=1;return *position_value;}
const int& value(){++values;if(receivers!=values)bad=1;return number;}
int rvalue(){++values;if(receivers!=values)bad=1;return 3;}
int main(){std::deque<int> a={1,4};owner=&a;It p=a.cbegin()+1;position_value=&p;
auto one=receiver().emplace(position(),value());if(bad||receivers!=1||positions!=1||values!=1||*one!=2)return 1;
p=a.cbegin()+2;auto two=receiver().emplace(position(),rvalue());
if(bad||receivers!=2||positions!=2||values!=2||*two!=3||two!=a.begin()+2||a.size()!=4)return 2;
for(unsigned i=0;i!=4;++i)if(a[i]!=(int)i+1)return 3;return 0;}
)r19dqposemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-positional-emplace-call-sequencing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequePositionalEmplaceOwnedZeroEmptyBodyRejected) {
  const auto Source = tmpFile("deque-positional-emplace-owned-zero-empty.cpp");
  writeFile(Source, R"r19dqposemplace(#include <deque>
namespace std { inline namespace __1 {
template<> template<> deque<int>::iterator deque<int>::emplace<>(const_iterator position) {return begin();}
}}
int main(){std::deque<int> a={4,5};int value=7;a.emplace(a.cbegin()+1);return 0;}
)r19dqposemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequePositionalEmplaceOwnedZeroForwardingBodyRejected) {
  const auto Source = tmpFile("deque-positional-emplace-owned-zero-forwarding.cpp");
  writeFile(Source, R"r19dqposemplace(#include <deque>
namespace std { inline namespace __1 {
template<> template<> deque<int>::iterator deque<int>::emplace<>(const_iterator position) {return insert(position,0);}
}}
int main(){std::deque<int> a={4,5};int value=7;a.emplace(a.cbegin()+1);return 0;}
)r19dqposemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequePositionalEmplaceOwnedLvalueEmptyBodyRejected) {
  const auto Source = tmpFile("deque-positional-emplace-owned-lvalue-empty.cpp");
  writeFile(Source, R"r19dqposemplace(#include <deque>
namespace std { inline namespace __1 {
template<> template<> deque<int>::iterator deque<int>::emplace<int&>(const_iterator position,int& value) {return begin();}
}}
int main(){std::deque<int> a={4,5};int value=7;a.emplace(a.cbegin()+1,value);return 0;}
)r19dqposemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequePositionalEmplaceOwnedRvalueForwardingBodyRejected) {
  const auto Source = tmpFile("deque-positional-emplace-owned-rvalue-forwarding.cpp");
  writeFile(Source, R"r19dqposemplace(#include <deque>
namespace std { inline namespace __1 {
template<> template<> deque<int>::iterator deque<int>::emplace<int>(const_iterator position,int&& value) {return insert(position,static_cast<int&&>(value));}
}}
int main(){std::deque<int> a={4,5};int value=7;a.emplace(a.cbegin()+1,static_cast<int&&>(value));return 0;}
)r19dqposemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
