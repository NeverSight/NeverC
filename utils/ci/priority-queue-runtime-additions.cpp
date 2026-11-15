
TEST_F(TranslateTest, CoreV2PriorityQueueIntEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-queue-empty-and-top.cpp");
  const auto Output = tmpFile("priority-queue-empty-and-top.nc");
  writeFile(Source, R"r19priority(#include <queue>
int main(){std::priority_queue<int> q;const auto& c=q;
if(!c.empty()||c.size()!=0)return 1;q.push(3);q.push(8);q.push(1);
if(c.empty()||c.size()!=3||c.top()!=8)return 2;
q.pop();if(c.top()!=3)return 3;q.pop();q.pop();return !c.empty();}
)r19priority");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-queue-empty-and-top" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntExtremesRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-queue-duplicates-and-extremes.cpp");
  const auto Output = tmpFile("priority-queue-duplicates-and-extremes.nc");
  writeFile(Source, R"r19priority(#include <queue>
int main(){std::priority_queue<int> q;
int input[]={-2147483647-1,-7,0,7,2147483647,7};
int expected[]={2147483647,7,7,0,-7,-2147483647-1};
for(int i=0;i!=6;++i)q.push(input[i]);
for(int i=0;i!=6;++i){if(q.top()!=expected[i])return 1;q.pop();}
return !q.empty();}
)r19priority");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-queue-duplicates-and-extremes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntAliasedGrowthRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-queue-growth-alias.cpp");
  const auto Output = tmpFile("priority-queue-growth-alias.nc");
  writeFile(Source, R"r19priority(#include <queue>
int main(){std::priority_queue<int> q;q.push(11);
for(int i=0;i!=2048;++i)q.push(q.top());
if(q.size()!=2049)return 1;
for(int i=0;i!=2049;++i){if(q.top()!=11)return 2;q.pop();}
q.push(-3);return q.size()!=1||q.top()!=-3;}
)r19priority");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-queue-growth-alias" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntInterleavedRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-queue-interleaved-heaps.cpp");
  const auto Output = tmpFile("priority-queue-interleaved-heaps.nc");
  writeFile(Source, R"r19priority(#include <queue>
int main(){std::priority_queue<int> q;
for(int i=0;i!=1025;++i)q.push(i-512);
for(int i=512;i!=0;--i){if(q.top()!=i)return 1;q.pop();}
for(int i=0;i!=1025;++i)q.push(i+513);
for(int i=1537;i!=512;--i){if(q.top()!=i)return 2;q.pop();}
for(int i=0;i!=-513;--i){if(q.top()!=i)return 3;q.pop();}
return !q.empty();}
)r19priority");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-queue-interleaved-heaps" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntAllocationReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-queue-live-allocation-argument.cpp");
  const auto Output = tmpFile("priority-queue-live-allocation-argument.nc");
  writeFile(Source, R"r19priority(using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
#include <queue>
static int* armed;static int observed;
void* operator new(Size n){void* p=malloc(n);if(armed){++observed;*armed=19;}return p;}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {free(p);}
int main(){std::priority_queue<int> q;int value=3;armed=&value;q.push(value);armed=nullptr;
return !observed||value!=19||q.size()!=1||q.top()!=19;}
)r19priority");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-queue-live-allocation-argument" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntArgumentEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-queue-receiver-argument-effects.cpp");
  const auto Output = tmpFile("priority-queue-receiver-argument-effects.nc");
  writeFile(Source, R"r19priority(#include <queue>
static std::priority_queue<int>* current;static int effects;
std::priority_queue<int>* receiver(){effects=effects*10+1;return current;}
const int& argument(){effects=effects*10+2;return current->top();}
int main(){std::priority_queue<int> q;q.push(7);current=&q;
receiver()->push(argument());receiver()->push(argument());
return effects!=1212||q.size()!=3||q.top()!=7;}
)r19priority");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-queue-receiver-argument-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueOwnedPushRejected) {
  const auto Source = tmpFile("priority-queue-owned-push-body.cpp");
  writeFile(Source, R"r19priority(#include <queue>
namespace std { inline namespace __1 {
template<> inline void priority_queue<int>::push(const int&) {}
}}
int main(){std::priority_queue<int> q;int x=1;q.push(x);return q.empty();}
)r19priority");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueOwnedTopMacroRejected) {
  const auto Source = tmpFile("priority-queue-owned-top-macro.cpp");
  writeFile(Source, R"r19priority(#define top owned_top
#include <queue>
int main(){std::priority_queue<int> q;q.push(1);return q.top()!=1;}
)r19priority");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueOwnedComparatorFieldMacroRejected) {
  const auto Source = tmpFile("priority-queue-owned-comparator-field-macro.cpp");
  writeFile(Source, R"r19priority(#define comp owned_comp
#include <queue>
int main(){std::priority_queue<int> q;q.push(1);return q.top()!=1;}
)r19priority");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueOwnedEmptyRejected) {
  const auto Source = tmpFile("priority-queue-owned-empty-body.cpp");
  writeFile(Source, R"r19priority(#include <queue>
namespace std { inline namespace __1 {
template<> inline bool priority_queue<int>::empty() const {return true;}
}}
int main(){std::priority_queue<int> q;return !q.empty();}
)r19priority");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
