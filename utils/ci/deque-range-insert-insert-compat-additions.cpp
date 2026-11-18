
TEST_F(TranslateTest, CoreV2DequeInsertSingleRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-insert-single.cpp");
  const auto Output = tmpFile("deque-insert-single.nc");
  writeFile(Source, R"r19dqinsert(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a;int one=1;
auto first=a.insert(a.cbegin(),one);if(first!=a.begin()||*first!=1||a.size()!=1)return 1;
int* old=&a.front();auto back=a.insert(a.cend(),3);
if(back!=a.end()-1||*back!=3||&a.front()!=old)return 2;
auto middle=a.insert(a.cbegin()+1,2);if(middle!=a.begin()+1||*middle!=2||a.size()!=3)return 3;
int* tail=&a.back();int zero=0;auto front=a.insert(a.begin(),zero);
if(front!=a.begin()||*front!=0||&a.back()!=tail)return 4;
for(unsigned i=0;i!=4;++i)if(a[i]!=(int)i)return 5;return 0;}
)r19dqinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-insert-single" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeInsertFrontGrowthRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-insert-front-growth.cpp");
  const auto Output = tmpFile("deque-insert-front-growth.nc");
  writeFile(Source, R"r19dqinsert(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a(4095);
for(unsigned i=0;i!=4095;++i)a[i]=(int)i;int value=-1;
auto first=a.insert(a.cbegin()+1,value);
if(a.size()!=4096||first!=a.begin()+1||*first!=-1||a.front()!=0||a.back()!=4094)return 1;
for(unsigned i=2;i!=4096;++i)if(a[i]!=(int)i-1)return 2;
auto second=a.insert(a.cbegin()+1024,-2);
if(a.size()!=4097||second!=a.begin()+1024||*second!=-2||a[1023]!=1022||a[1025]!=1023)return 3;
a.push_front(-3);a.push_back(4095);return a.front()!=-3||a.back()!=4095||a.size()!=4099;}
)r19dqinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-insert-front-growth" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeInsertBackGrowthRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-insert-back-growth.cpp");
  const auto Output = tmpFile("deque-insert-back-growth.nc");
  writeFile(Source, R"r19dqinsert(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a(4095);
for(unsigned i=0;i!=4095;++i)a[i]=(int)i;int value=-1;
auto first=a.insert(a.cend()-1,value);
if(a.size()!=4096||first!=a.begin()+4094||*first!=-1||a.front()!=0||a.back()!=4094)return 1;
for(unsigned i=0;i!=4094;++i)if(a[i]!=(int)i)return 2;
auto second=a.insert(a.cbegin()+3072,-2);
if(a.size()!=4097||second!=a.begin()+3072||*second!=-2||a[3071]!=3071||a[3073]!=3072)return 3;
a.push_front(-3);a.push_back(4095);return a.front()!=-3||a.back()!=4095||a.size()!=4099;}
)r19dqinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-insert-back-growth" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeInsertAliasesRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-insert-aliases.cpp");
  const auto Output = tmpFile("deque-insert-aliases.nc");
  writeFile(Source, R"r19dqinsert(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a(3071);
for(unsigned i=0;i!=3071;++i)a[i]=(int)i;
auto first=a.insert(a.cbegin()+1,a.back());if(*first!=3070||a.size()!=3072||a.front()!=0||a[2]!=1)return 1;
auto second=a.insert(a.cend()-1,a.front());if(*second!=0||a.size()!=3073||a.back()!=3070)return 2;
auto third=a.insert(a.cbegin()+1024,a[1023]);if(*third!=1022||a[1023]!=1022||a[1025]!=1023)return 3;
auto fourth=a.insert(a.cend()-2,static_cast<int&&>(a[1024]));
if(*fourth!=1022||a.size()!=3075||a[1024]!=1022||a.back()!=3070)return 4;
a.clear();int x=8;auto again=a.insert(a.end(),x);return again!=a.begin()||*again!=8||a.size()!=1;}
)r19dqinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-insert-aliases" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeInsertAllocationEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-insert-allocation-effects.cpp");
  const auto Output = tmpFile("deque-insert-allocation-effects.nc");
  writeFile(Source, R"r19dqinsert(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
static std::deque<int>* owner;static int stage,calls,bad;
void* operator new(Size n){if(stage){++calls;if(owner->size()!=1023)bad=1;owner->front()=77;}return malloc(n);}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {operator delete(p);}
int main(){std::deque<int> a(1023,8);owner=&a;stage=1;auto back=a.insert(a.cend(),a.front());stage=0;
if(!calls||bad||*back!=77||a.front()!=77||a.back()!=77||a.size()!=1024)return 1;
std::deque<int> b(1023,9);owner=&b;calls=0;stage=1;auto front=b.insert(b.cbegin(),b.front());stage=0;
if(!calls||bad||front!=b.begin()||*front!=77||b[1]!=77||b.size()!=1024)return 2;
std::deque<int> c(1023,10);owner=&c;calls=0;stage=1;auto middle=c.insert(c.cbegin()+1,c.front());stage=0;
return !calls||bad||middle!=c.begin()+1||*middle!=77||c.front()!=77||c[2]!=10||c.size()!=1024;}
)r19dqinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-insert-allocation-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeInsertCallSequencingRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-insert-call-sequencing.cpp");
  const auto Output = tmpFile("deque-insert-call-sequencing.nc");
  writeFile(Source, R"r19dqinsert(#include <deque>
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
auto one=receiver().insert(position(),value());if(bad||receivers!=1||positions!=1||values!=1||*one!=2)return 1;
p=a.cbegin()+2;auto two=receiver().insert(position(),rvalue());
if(bad||receivers!=2||positions!=2||values!=2||*two!=3||two!=a.begin()+2||a.size()!=4)return 2;
for(unsigned i=0;i!=4;++i)if(a[i]!=(int)i+1)return 3;return 0;}
)r19dqinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-insert-call-sequencing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeInsertCopyOwnedEmptyBodyRejected) {
  const auto Source = tmpFile("deque-insert-owned-copy-empty.cpp");
  writeFile(Source, R"r19dqinsert(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::iterator deque<int>::insert(const_iterator position,const value_type& value) {return begin();}
}}
int main(){std::deque<int> a={4,5,6};int value=7;a.insert(a.cbegin(),value);return 0;}
)r19dqinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeInsertCopyOwnedForwardingBodyRejected) {
  const auto Source = tmpFile("deque-insert-owned-copy-forwarding.cpp");
  writeFile(Source, R"r19dqinsert(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::iterator deque<int>::insert(const_iterator position,const value_type& value) {return insert(position,1,value);}
}}
int main(){std::deque<int> a={4,5,6};int value=7;a.insert(a.cbegin(),value);return 0;}
)r19dqinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeInsertMoveOwnedEmptyBodyRejected) {
  const auto Source = tmpFile("deque-insert-owned-move-empty.cpp");
  writeFile(Source, R"r19dqinsert(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::iterator deque<int>::insert(const_iterator position,value_type&& value) {return begin();}
}}
int main(){std::deque<int> a={4,5,6};int value=7;a.insert(a.cbegin(),static_cast<int&&>(value));return 0;}
)r19dqinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeInsertMoveOwnedForwardingBodyRejected) {
  const auto Source = tmpFile("deque-insert-owned-move-forwarding.cpp");
  writeFile(Source, R"r19dqinsert(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::iterator deque<int>::insert(const_iterator position,value_type&& value) {return insert(position,static_cast<const value_type&>(value));}
}}
int main(){std::deque<int> a={4,5,6};int value=7;a.insert(a.cbegin(),static_cast<int&&>(value));return 0;}
)r19dqinsert");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
