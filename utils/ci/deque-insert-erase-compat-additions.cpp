
TEST_F(TranslateTest, CoreV2DequeEraseSingleRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-erase-single.cpp");
  const auto Output = tmpFile("deque-erase-single.nc");
  writeFile(Source, R"r19dqerase(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a={0,1,2,3,4,5,6,7,8};
int* tail=&a.back();auto first=a.erase(a.cbegin());
if(first!=a.begin()||*first!=1||a.size()!=8||&a.back()!=tail)return 1;
auto middle=a.erase(a.begin()+2);if(middle!=a.begin()+2||*middle!=4||a.size()!=7)return 2;
int* front=&a.front();auto end=a.erase(a.cend()-1);
if(end!=a.end()||a.size()!=6||a.back()!=7||&a.front()!=front)return 3;
const int expected[6]={1,2,4,5,6,7};for(unsigned i=0;i!=6;++i)if(a[i]!=expected[i])return 4;
return 0;}
)r19dqerase");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-erase-single" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeEraseFrontRangeRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-erase-front-range.cpp");
  const auto Output = tmpFile("deque-erase-front-range.nc");
  writeFile(Source, R"r19dqerase(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a(4097);for(unsigned i=0;i!=4097;++i)a[i]=(int)i;
int* tail=&a.back();int* following=&a[1027];auto next=a.erase(a.cbegin()+2,a.cbegin()+1027);
if(a.size()!=3072||next!=a.begin()+2||*next!=1027||&*next!=following||&a.back()!=tail)return 1;
for(unsigned i=0;i!=3072;++i)if(a[i]!=(int)(i<2?i:i+1025))return 2;
a.push_front(-1);a.push_back(4097);return a.front()!=-1||a.back()!=4097||a.size()!=3074;}
)r19dqerase");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-erase-front-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeEraseBackRangeRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-erase-back-range.cpp");
  const auto Output = tmpFile("deque-erase-back-range.nc");
  writeFile(Source, R"r19dqerase(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a(4097);for(unsigned i=0;i!=4097;++i)a[i]=(int)i;
int* first=&a.front();auto next=a.erase(a.begin()+3070,a.begin()+4094);
if(a.size()!=3073||next!=a.begin()+3070||*next!=4094||&a.front()!=first)return 1;
for(unsigned i=0;i!=3073;++i)if(a[i]!=(int)(i<3070?i:i+1024))return 2;
auto end=a.erase(a.cbegin()+3070,a.cend());if(end!=a.end()||a.size()!=3070||a.back()!=3069)return 3;
a.push_front(-1);a.push_back(5000);return a.front()!=-1||a.back()!=5000||&a[1]!=first;}
)r19dqerase");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-erase-back-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeEraseEmptyAndReuseRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-erase-empty-and-reuse.cpp");
  const auto Output = tmpFile("deque-erase-empty-and-reuse.nc");
  writeFile(Source, R"r19dqerase(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
int main(){std::deque<int> a;
auto empty=a.erase(a.cbegin(),a.cend());if(empty!=a.end()||!a.empty())return 1;
a.resize(2050,7);int* first=&a.front();auto middle=a.erase(a.cbegin()+1024,a.cbegin()+1024);
if(middle!=a.begin()+1024||*middle!=7||a.size()!=2050||&a.front()!=first)return 2;
auto end=a.erase(a.end(),a.end());if(end!=a.end()||a.size()!=2050)return 3;
auto all=a.erase(a.cbegin(),a.cend());if(all!=a.end()||!a.empty())return 4;
a.push_front(8);auto one=a.erase(a.cbegin());if(one!=a.end()||!a.empty())return 5;
a.push_front(9);a.push_back(10);return a.size()!=2||a.front()!=9||a.back()!=10;}
)r19dqerase");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-erase-empty-and-reuse" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeEraseReleaseEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-erase-release-effects.cpp");
  const auto Output = tmpFile("deque-erase-release-effects.nc");
  writeFile(Source, R"r19dqerase(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
static std::deque<int>* owner;static int stage,calls,bad;
void* operator new(Size n){return malloc(n);}
void operator delete(void* p) noexcept {if(stage){++calls;
if(owner->size()!=1024||owner->front()!=(stage==1?3073:0)||owner->back()!=(stage==1?4096:1023))bad=1;}free(p);}
void operator delete(void* p,Size) noexcept {operator delete(p);}
int main(){std::deque<int> a(4097);owner=&a;for(unsigned i=0;i!=4097;++i)a[i]=(int)i;
int* last=&a.back();stage=1;auto front=a.erase(a.cbegin(),a.cbegin()+3073);stage=0;
if(!calls||bad||front!=a.begin()||*front!=3073||&a.back()!=last)return 1;
a.resize(4097);for(unsigned i=0;i!=4097;++i)a[i]=(int)i;
int* first=&a.front();calls=0;stage=2;auto end=a.erase(a.cbegin()+1024,a.cend());stage=0;
if(!calls||bad||end!=a.end()||&a.front()!=first||a.size()!=1024)return 2;
a.push_back(1024);return a.back()!=1024||a.size()!=1025;}
)r19dqerase");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-erase-release-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeEraseCallSequencingRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-erase-call-sequencing.cpp");
  const auto Output = tmpFile("deque-erase-call-sequencing.nc");
  writeFile(Source, R"r19dqerase(#include <deque>
#include <new>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
using It=std::deque<int>::const_iterator;
static std::deque<int>* owner;static const It* first_value;static const It* last_value;
static int receivers,firsts,lasts,bad;
std::deque<int>& receiver(){++receivers;return *owner;}
It first(){++firsts;if(receivers!=firsts)bad=1;return *first_value;}
It last(){++lasts;if(receivers!=2)bad=1;return *last_value;}
int main(){std::deque<int> a={0,1,2,3,4,5};owner=&a;It f=a.cbegin()+1;first_value=&f;
auto one=receiver().erase(first());if(bad||receivers!=1||firsts!=1||*one!=2||a.size()!=5)return 1;
f=a.cbegin()+1;It l=a.cbegin()+3;last_value=&l;auto two=receiver().erase(first(),last());
return bad||receivers!=2||firsts!=2||lasts!=1||two!=a.begin()+1||*two!=4||a.size()!=3||a.front()!=0||a.back()!=5;}
)r19dqerase");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-erase-call-sequencing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeEraseSingleOwnedEmptyBodyRejected) {
  const auto Source = tmpFile("deque-erase-owned-single-empty.cpp");
  writeFile(Source, R"r19dqerase(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::iterator deque<int>::erase(const_iterator first) {return begin();}
}}
int main(){std::deque<int> a={4,5,6};a.erase(a.cbegin());return 0;}
)r19dqerase");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeEraseSingleOwnedForwardingBodyRejected) {
  const auto Source = tmpFile("deque-erase-owned-single-forwarding.cpp");
  writeFile(Source, R"r19dqerase(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::iterator deque<int>::erase(const_iterator first) {return erase(first,first+1);}
}}
int main(){std::deque<int> a={4,5,6};a.erase(a.cbegin());return 0;}
)r19dqerase");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeEraseRangeOwnedEmptyBodyRejected) {
  const auto Source = tmpFile("deque-erase-owned-range-empty.cpp");
  writeFile(Source, R"r19dqerase(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::iterator deque<int>::erase(const_iterator first,const_iterator last) {return begin();}
}}
int main(){std::deque<int> a={4,5,6};a.erase(a.cbegin(),a.cbegin()+1);return 0;}
)r19dqerase");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeEraseRangeOwnedForwardingBodyRejected) {
  const auto Source = tmpFile("deque-erase-owned-range-forwarding.cpp");
  writeFile(Source, R"r19dqerase(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::iterator deque<int>::erase(const_iterator first,const_iterator last) {return erase(first,last);}
}}
int main(){std::deque<int> a={4,5,6};a.erase(a.cbegin(),a.cbegin()+1);return 0;}
)r19dqerase");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
