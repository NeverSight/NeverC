
TEST_F(TranslateTest, CoreV2PriorityQueueIntCopyConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-value-copy-construction.cpp");
  const auto Output = tmpFile("priority-value-copy-construction.nc");
  writeFile(Source, R"r19priorityvalue(#include <queue>
int main(){std::priority_queue<int> q;q.push(3);q.push(8);q.push(1);
const auto& source=q;std::priority_queue<int> copied(source);
if(copied.size()!=3||copied.top()!=8||&copied.top()==&source.top())return 1;
q.pop();copied.push(13);if(q.top()!=3||copied.top()!=13)return 2;
copied.pop();copied.pop();if(copied.top()!=3||q.size()!=2)return 3;
std::priority_queue<int> empty;const auto& ce=empty;
std::priority_queue<int> other(ce);return !other.empty();}
)r19priorityvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-value-copy-construction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntCopyAssignmentRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-value-copy-assignment.cpp");
  const auto Output = tmpFile("priority-value-copy-assignment.nc");
  writeFile(Source, R"r19priorityvalue(#include <queue>
int main(){std::priority_queue<int> q;q.push(4);q.push(9);q.push(2);
std::priority_queue<int> other;other.push(-1);const auto& source=q;
auto& assigned=(other=source);if(&assigned!=&other||other.size()!=3||other.top()!=9)return 1;
q.pop();if(q.top()!=4||other.top()!=9)return 2;
const int* before=&other.top();other=other;
if(other.size()!=3||other.top()!=9||&other.top()!=before)return 3;
std::priority_queue<int> empty;other=empty;if(!other.empty())return 4;
empty=q;return empty.size()!=2||empty.top()!=4;}
)r19priorityvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-value-copy-assignment" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntMoveConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-value-move-construction.cpp");
  const auto Output = tmpFile("priority-value-move-construction.nc");
  writeFile(Source, R"r19priorityvalue(#include <queue>
#include <utility>
int main(){std::priority_queue<int> source;source.push(5);source.push(12);source.push(-2);
const int* address=&source.top();std::priority_queue<int> target(std::move(source));
if(!source.empty()||target.size()!=3||target.top()!=12||&target.top()!=address)return 1;
source.push(7);target.pop();if(source.top()!=7||target.top()!=5)return 2;
std::priority_queue<int> empty;std::priority_queue<int> next(std::move(empty));
return !empty.empty()||!next.empty();}
)r19priorityvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-value-move-construction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntMoveAssignmentRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-value-move-assignment.cpp");
  const auto Output = tmpFile("priority-value-move-assignment.nc");
  writeFile(Source, R"r19priorityvalue(#include <queue>
#include <utility>
int main(){std::priority_queue<int> source;source.push(6);source.push(15);source.push(1);
std::priority_queue<int> target;target.push(99);target.push(100);
const int* address=&source.top();auto& assigned=(target=std::move(source));
if(&assigned!=&target||!source.empty()||target.size()!=3||target.top()!=15||&target.top()!=address)return 1;
source.push(-3);target.pop();if(source.top()!=-3||target.top()!=6)return 2;
std::priority_queue<int> empty;target=std::move(empty);
return !target.empty()||!empty.empty();}
)r19priorityvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-value-move-assignment" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntSwapIdentitiesRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-value-swap-identities.cpp");
  const auto Output = tmpFile("priority-value-swap-identities.nc");
  writeFile(Source, R"r19priorityvalue(#include <queue>
#include <utility>
static std::priority_queue<int>* first;static std::priority_queue<int>* second;static int effects;
std::priority_queue<int>* receiver(){effects=effects*10+1;return first;}
std::priority_queue<int>& argument(){effects=effects*10+2;return *second;}
int main(){std::priority_queue<int> a;a.push(3);a.push(11);std::priority_queue<int> b;b.push(-2);
const int* aa=&a.top();const int* bb=&b.top();first=&a;second=&b;
receiver()->swap(argument());if(effects!=12||&a.top()!=bb||&b.top()!=aa)return 1;
std::swap(a,b);if(&a.top()!=aa||&b.top()!=bb)return 2;
using std::swap;swap(a,b);if(&a.top()!=bb||&b.top()!=aa)return 3;
a.swap(a);return a.size()!=1||a.top()!=-2||b.size()!=2||b.top()!=11;}
)r19priorityvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-value-swap-identities" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntEmplaceGrowthRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-value-emplace-growth.cpp");
  const auto Output = tmpFile("priority-value-emplace-growth.nc");
  writeFile(Source, R"r19priorityvalue(#include <queue>
int main(){std::priority_queue<int> q;q.emplace();if(q.size()!=1||q.top()!=0)return 1;
int value=17;q.emplace(value);q.emplace(-3);
for(int i=0;i!=2048;++i)q.emplace(q.top());
if(q.size()!=2051||q.top()!=17)return 2;
for(int i=0;i!=2049;++i){if(q.top()!=17)return 3;q.pop();}
if(q.top()!=0)return 4;q.pop();if(q.top()!=-3)return 5;q.pop();return !q.empty();}
)r19priorityvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-value-emplace-growth" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueOwnedCopyConstructorRejected) {
  const auto Source = tmpFile("priority-value-owned-copy-constructor.cpp");
  writeFile(Source, R"r19priorityvalue(#include <queue>
namespace std { inline namespace __1 {
template<> inline priority_queue<int>::priority_queue(const priority_queue&) {}
}}
int main(){std::priority_queue<int> source;source.push(3);std::priority_queue<int> copied(source);return copied.empty();}
)r19priorityvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueOwnedCopyAssignmentRejected) {
  const auto Source = tmpFile("priority-value-owned-copy-assignment.cpp");
  writeFile(Source, R"r19priorityvalue(#include <queue>
namespace std { inline namespace __1 {
template<> inline priority_queue<int>& priority_queue<int>::operator=(const priority_queue&) {return *this;}
}}
int main(){std::priority_queue<int> source;source.push(3);std::priority_queue<int> other;other=source;return other.empty();}
)r19priorityvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueOwnedSwapRejected) {
  const auto Source = tmpFile("priority-value-owned-swap-body.cpp");
  writeFile(Source, R"r19priorityvalue(#include <queue>
namespace std { inline namespace __1 {
template<> inline void priority_queue<int>::swap(priority_queue&) noexcept {}
}}
int main(){std::priority_queue<int> source;source.push(3);std::priority_queue<int> other;source.swap(other);return !source.empty();}
)r19priorityvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueOwnedEmplaceMacroRejected) {
  const auto Source = tmpFile("priority-value-owned-emplace-macro.cpp");
  writeFile(Source, R"r19priorityvalue(#define emplace owned_emplace
#include <queue>
int main(){std::priority_queue<int> q;q.emplace(3);return q.empty();}
)r19priorityvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
