
TEST_F(TranslateTest, CoreV2DequeEmplaceValueReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-emplace-value-reference.cpp");
  const auto Output = tmpFile("deque-emplace-value-reference.nc");
  writeFile(Source, R"r19dqemplace(#include <deque>
int main(){std::deque<int> d;
int& first=d.emplace_back();if(first!=0||&first!=&d.back())return 1;
first=7;const int value=3;int& front=d.emplace_front(value);
if(front!=3||&front!=&d.front()||&first!=&d.back())return 2;
int& copy=d.emplace_back(d.front());copy=9;
if(d.size()!=3||d.front()!=3||d[1]!=7||d.back()!=9)return 3;
int& zero=d.emplace_front();return zero!=0||&zero!=&d.front()||d.size()!=4;
}
)r19dqemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-emplace-value-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeEmplaceBlockBoundariesRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-emplace-block-boundaries.cpp");
  const auto Output = tmpFile("deque-emplace-block-boundaries.nc");
  writeFile(Source, R"r19dqemplace(#include <deque>
int main(){std::deque<int> d;int* middle=&d.emplace_back(17);
for(int i=1;i!=2050;++i){int& r=d.emplace_back(i);if(&r!=&d.back()||r!=i)return 1;}
for(int i=1;i!=2050;++i){int& r=d.emplace_front(-i);if(&r!=&d.front()||r!=-i)return 2;}
if(d.size()!=4099||*middle!=17||&d[2049]!=middle)return 3;
*middle=23;for(int i=2049;i!=0;--i){if(d.front()!=-i)return 4;d.pop_front();}
if(d.front()!=23)return 5;d.pop_front();
for(int i=1;i!=2050;++i){if(d.front()!=i)return 6;d.pop_front();}
return !d.empty()||d.emplace_back()!=0;
}
)r19dqemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-emplace-block-boundaries" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2StackEmplaceValueReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("stack-emplace-value-reference.cpp");
  const auto Output = tmpFile("stack-emplace-value-reference.nc");
  writeFile(Source, R"r19dqemplace(#include <stack>
int main(){std::stack<int> s;int& first=s.emplace();
if(first!=0||&first!=&s.top())return 1;first=7;
const int value=3;int& second=s.emplace(value);
if(second!=3||&second!=&s.top())return 2;
int& copy=s.emplace(s.top());copy=9;
if(s.size()!=3||s.top()!=9||first!=7)return 3;
s.pop();if(s.top()!=3)return 4;s.pop();
if(&s.top()!=&first||s.top()!=7)return 5;s.pop();return !s.empty();
}
)r19dqemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("stack-emplace-value-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2QueueEmplaceValueReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("queue-emplace-value-reference.cpp");
  const auto Output = tmpFile("queue-emplace-value-reference.nc");
  writeFile(Source, R"r19dqemplace(#include <queue>
int main(){std::queue<int> q;int& first=q.emplace();
if(first!=0||&first!=&q.front()||&first!=&q.back())return 1;first=7;
const int value=3;int& second=q.emplace(value);
if(second!=3||&second!=&q.back())return 2;
int& copy=q.emplace(q.front());copy=9;
if(q.size()!=3||q.front()!=7||q.back()!=9||second!=3)return 3;
q.pop();if(q.front()!=3)return 4;q.pop();
if(&q.front()!=&copy||q.front()!=9)return 5;q.pop();return !q.empty();
}
)r19dqemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("queue-emplace-value-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeEmplaceLiveAllocationReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-emplace-live-allocation-reference.cpp");
  const auto Output = tmpFile("deque-emplace-live-allocation-reference.nc");
  writeFile(Source, R"r19dqemplace(using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
#include <deque>
static int* watched;static int armed,observed;
void* operator new(Size n){void* p=malloc(n);if(armed&&watched){++observed;*watched=9;}return p;}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {free(p);}
int check(bool front){std::deque<int> d;d.emplace_back(3);watched=&d.front();observed=0;armed=1;
for(int i=0;i!=2048&&!observed;++i){int& r=front?d.emplace_front(*watched):d.emplace_back(*watched);
if(observed&&(r!=9||&r!=(front?&d.front():&d.back()))){armed=0;watched=nullptr;return 1;}}
armed=0;watched=nullptr;return !observed;}
int main(){return check(false)||check(true);}
)r19dqemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-emplace-live-allocation-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeEmplaceOwnedArgumentEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-emplace-owned-argument-effects.cpp");
  const auto Output = tmpFile("deque-emplace-owned-argument-effects.nc");
  writeFile(Source, R"r19dqemplace(#include <deque>
static std::deque<int>* current;static int effects;
std::deque<int>* receiver(){effects=effects*10+1;return current;}
int& argument(){effects=effects*10+2;return current->front();}
int main(){std::deque<int> d;d.emplace_back(2);current=&d;
int& back=receiver()->emplace_back(argument());
int& front=receiver()->emplace_front(argument());
if(effects!=1212||d.size()!=3||&back!=&d.back()||&front!=&d.front())return 1;
back=7;front=9;int& zero=receiver()->emplace_front();
return effects!=12121||zero!=0||&zero!=&d.front()||d[1]!=9||d.back()!=7;
}
)r19dqemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-emplace-owned-argument-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeEmplaceBackOwnedBodyRejected) {
  const auto Source = tmpFile("deque-emplace-back-owned-body.cpp");
  writeFile(Source, R"r19dqemplace(#include <deque>
namespace std { inline namespace __1 {
template<> template<> deque<int>::reference deque<int>::emplace_back<int&>(int& value){return value;}
}}
int main(){std::deque<int> d;int value=7;int& result=d.emplace_back(value);return &result!=&value;}
)r19dqemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeEmplaceFrontOwnedBodyRejected) {
  const auto Source = tmpFile("deque-emplace-front-owned-body.cpp");
  writeFile(Source, R"r19dqemplace(#include <deque>
namespace std { inline namespace __1 {
template<> template<> deque<int>::reference deque<int>::emplace_front<int&>(int& value){return value;}
}}
int main(){std::deque<int> d;int value=7;int& result=d.emplace_front(value);return &result!=&value;}
)r19dqemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2StackEmplaceOwnedBodyRejected) {
  const auto Source = tmpFile("stack-emplace-owned-body.cpp");
  writeFile(Source, R"r19dqemplace(#include <stack>
namespace std { inline namespace __1 {
template<> template<> decltype(auto) stack<int>::emplace<int&>(int& value){return (value);}
}}
int main(){std::stack<int> s;int value=7;int& result=s.emplace(value);return &result!=&value;}
)r19dqemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2QueueEmplaceOwnedBodyRejected) {
  const auto Source = tmpFile("queue-emplace-owned-body.cpp");
  writeFile(Source, R"r19dqemplace(#include <queue>
namespace std { inline namespace __1 {
template<> template<> decltype(auto) queue<int>::emplace<int&>(int& value){return (value);}
}}
int main(){std::queue<int> q;int value=7;int& result=q.emplace(value);return &result!=&value;}
)r19dqemplace");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
