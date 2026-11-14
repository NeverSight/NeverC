
TEST_F(TranslateTest, CoreV2DequeAdapterIntEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-adapter-empty-layout.cpp");
  const auto Output = tmpFile("deque-adapter-empty-layout.nc");
  writeFile(Source, R"r19adapter(#include <stack>
#include <queue>
int main(){std::stack<int> s;std::queue<int> q;
const auto& cs=s;const auto& cq=q;
return sizeof(s)!=48||sizeof(q)!=48||alignof(decltype(s))!=8||alignof(decltype(q))!=8||
!cs.empty()||cs.size()!=0||!cq.empty()||cq.size()!=0;}
)r19adapter");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-adapter-empty-layout" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2StackIntLifoReferencesRunAtBothOptimizations) {
  const auto Source = tmpFile("stack-lifo-references.cpp");
  const auto Output = tmpFile("stack-lifo-references.nc");
  writeFile(Source, R"r19adapter(#include <stack>
int main(){std::stack<int> s;s.push(1);int value=2;s.push(value);s.push(3);
const auto& c=s;if(c.size()!=3||c.top()!=3)return 1;
s.top()=7;if(c.top()!=7)return 2;s.pop();
if(s.top()!=2||s.size()!=2)return 3;s.pop();s.pop();return !s.empty();}
)r19adapter");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("stack-lifo-references" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2QueueIntFifoReferencesRunAtBothOptimizations) {
  const auto Source = tmpFile("queue-fifo-references.cpp");
  const auto Output = tmpFile("queue-fifo-references.nc");
  writeFile(Source, R"r19adapter(#include <queue>
int main(){std::queue<int> q;q.push(1);int value=2;q.push(value);q.push(3);
const auto& c=q;if(c.size()!=3||c.front()!=1||c.back()!=3)return 1;
q.front()=5;q.back()=7;if(c.front()!=5||c.back()!=7)return 2;
q.pop();if(q.front()!=2||q.back()!=7)return 3;q.pop();q.pop();return !q.empty();}
)r19adapter");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("queue-fifo-references" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2QueueIntBlockBoundariesRunAtBothOptimizations) {
  const auto Source = tmpFile("queue-block-boundaries.cpp");
  const auto Output = tmpFile("queue-block-boundaries.nc");
  writeFile(Source, R"r19adapter(#include <queue>
int main(){std::queue<int> q;for(int i=0;i!=2049;++i)q.push(i);
int* saved=&q.back();for(int i=0;i!=1024;++i){if(q.front()!=i)return 1;q.pop();}
for(int i=2049;i!=3073;++i)q.push(i);
if(q.size()!=2049||q.front()!=1024||q.back()!=3072||*saved!=2048)return 2;
*saved=9;for(int i=1024;i!=3073;++i){if(q.front()!=(i==2048?9:i))return 3;q.pop();}
q.push(7);return q.size()!=1||q.front()!=7||q.back()!=7;}
)r19adapter");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("queue-block-boundaries" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2StackIntAllocationReferencesRunAtBothOptimizations) {
  const auto Source = tmpFile("stack-live-allocation-reference.cpp");
  const auto Output = tmpFile("stack-live-allocation-reference.nc");
  writeFile(Source, R"r19adapter(using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
#include <stack>
static std::stack<int>* current;static int armed,observed;
void* operator new(Size n){void* p=malloc(n);if(armed&&current){++observed;current->top()=9;}return p;}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {free(p);}
int main(){std::stack<int> s;s.push(3);current=&s;armed=1;
for(int i=0;i!=2048&&!observed;++i)s.push(s.top());
armed=0;current=nullptr;return !observed||s.size()<2||s.top()!=9;}
)r19adapter");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("stack-live-allocation-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2QueueIntArgumentEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("queue-owned-argument-effects.cpp");
  const auto Output = tmpFile("queue-owned-argument-effects.nc");
  writeFile(Source, R"r19adapter(#include <queue>
static std::queue<int>* current;static int effects;
std::queue<int>* receiver(){effects=effects*10+1;return current;}
const int& argument(){effects=effects*10+2;return current->front();}
int main(){std::queue<int> q;q.push(2);current=&q;
receiver()->push(argument());receiver()->push(argument());
return effects!=1212||q.size()!=3||q.front()!=2||q.back()!=2;}
)r19adapter");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("queue-owned-argument-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2StackOwnedPushRejected) {
  const auto Source = tmpFile("stack-owned-push-body.cpp");
  writeFile(Source, R"r19adapter(#include <stack>
namespace std { inline namespace __1 {
template<> inline void stack<int>::push(const int&) {}
}}
int main(){std::stack<int> s;int x=1;s.push(x);return s.empty();}
)r19adapter");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2QueueOwnedFrontMacroRejected) {
  const auto Source = tmpFile("queue-owned-front-macro.cpp");
  writeFile(Source, R"r19adapter(#define front owned_front
#include <queue>
int main(){std::queue<int> q;q.push(1);return q.front()!=1;}
)r19adapter");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2StackOwnedContainerMacroRejected) {
  const auto Source = tmpFile("stack-owned-container-macro.cpp");
  writeFile(Source, R"r19adapter(#define c owned_container
#include <stack>
int main(){std::stack<int> s;s.push(1);return s.top()!=1;}
)r19adapter");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2QueueOwnedEmptyRejected) {
  const auto Source = tmpFile("queue-owned-empty-body.cpp");
  writeFile(Source, R"r19adapter(#include <queue>
namespace std { inline namespace __1 {
template<> inline bool queue<int>::empty() const {return true;}
}}
int main(){std::queue<int> q;q.push(1);return q.empty();}
)r19adapter");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
