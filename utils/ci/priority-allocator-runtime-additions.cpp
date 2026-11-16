
TEST_F(TranslateTest, CoreV2PriorityQueueIntAllocatorEmptyComparatorRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-alloc-empty-comparator.cpp");
  const auto Output = tmpFile("priority-alloc-empty-comparator.nc");
  writeFile(Source, R"r19priorityalloc(#include <queue>
#include <memory>
int main(){std::allocator<int> alloc;std::less<int> compare;
std::priority_queue<int> a(alloc);std::priority_queue<int> b(compare,alloc);
if(!a.empty()||!b.empty())return 1;a.push(-4);a.push(9);b.push(3);b.push(12);
if(a.top()!=9||b.top()!=12)return 2;a.pop();b.pop();return a.top()!=-4||b.top()!=3;}
)r19priorityalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-alloc-empty-comparator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntAllocatorCopyVectorRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-alloc-copy-vector.cpp");
  const auto Output = tmpFile("priority-alloc-copy-vector.nc");
  writeFile(Source, R"r19priorityalloc(#include <queue>
#include <memory>
#include <vector>
int main(){std::allocator<int> alloc;std::less<int> compare;std::vector<int> values{3,11,3,-5};
const auto& source=values;std::priority_queue<int> q(compare,source,alloc);
if(q.size()!=4||q.top()!=11||&q.top()==&values[0])return 1;
values[1]=99;if(q.top()!=11)return 2;int expected[4]={11,3,3,-5};
for(int i=0;i!=4;++i){if(q.top()!=expected[i])return 3;q.pop();}
return !q.empty()||values.size()!=4||values[1]!=99;}
)r19priorityalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-alloc-copy-vector" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntAllocatorMoveVectorRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-alloc-move-vector.cpp");
  const auto Output = tmpFile("priority-alloc-move-vector.nc");
  writeFile(Source, R"r19priorityalloc(#include <queue>
#include <memory>
#include <vector>
#include <utility>
int main(){std::allocator<int> alloc;std::less<int> compare;std::vector<int> values{2,14,-3,6};
const int* storage=&values[0];std::priority_queue<int> q(compare,std::move(values),alloc);
if(!values.empty()||q.size()!=4||q.top()!=14||&q.top()!=storage)return 1;
values.push_back(8);if(values[0]!=8||q.top()!=14)return 2;int expected[4]={14,6,2,-3};
for(int i=0;i!=4;++i){if(q.top()!=expected[i])return 3;q.pop();}return !q.empty();}
)r19priorityalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-alloc-move-vector" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntAllocatorCopyMoveQueueRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-alloc-copy-move-queue.cpp");
  const auto Output = tmpFile("priority-alloc-copy-move-queue.nc");
  writeFile(Source, R"r19priorityalloc(#include <queue>
#include <memory>
#include <utility>
int main(){std::allocator<int> alloc;std::priority_queue<int> source;
source.push(4);source.push(-2);source.push(17);const auto& original=source;
std::priority_queue<int> copy(original,alloc);
if(copy.size()!=3||copy.top()!=17||&copy.top()==&source.top())return 1;
const int* storage=&source.top();std::priority_queue<int> moved(std::move(source),alloc);
if(!source.empty()||moved.size()!=3||&moved.top()!=storage)return 2;
source.push(21);if(source.top()!=21||copy.top()!=17||moved.top()!=17)return 3;
int expected[3]={17,4,-2};for(int i=0;i!=3;++i){
if(copy.top()!=expected[i]||moved.top()!=expected[i])return 4;copy.pop();moved.pop();}
return !copy.empty()||!moved.empty();}
)r19priorityalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-alloc-copy-move-queue" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntAllocatorPointerRangeRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-alloc-pointer-range.cpp");
  const auto Output = tmpFile("priority-alloc-pointer-range.nc");
  writeFile(Source, R"r19priorityalloc(#include <queue>
#include <memory>
int main(){std::allocator<int> alloc;std::less<int> compare;int values[5]={4,-8,17,4,2};
const int* first=values;std::priority_queue<int> a(first,first+5,alloc);
std::priority_queue<int> b(values,values+5,compare,alloc);
std::priority_queue<int> empty(first+5,first+5,alloc);
if(!empty.empty()||a.size()!=5||b.size()!=5)return 1;int expected[5]={17,4,4,2,-8};
for(int i=0;i!=5;++i){if(a.top()!=expected[i]||b.top()!=expected[i])return 2;a.pop();b.pop();}
return !a.empty()||!b.empty()||values[0]!=4||values[2]!=17;}
)r19priorityalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-alloc-pointer-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntAllocatorRangeCopyMovePrefixRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-alloc-range-copy-move-prefix.cpp");
  const auto Output = tmpFile("priority-alloc-range-copy-move-prefix.nc");
  writeFile(Source, R"r19priorityalloc(#include <queue>
#include <memory>
#include <vector>
#include <utility>
int main(){std::allocator<int> alloc;std::less<int> compare;std::vector<int> prefix{6,2};
prefix.reserve(16);const int added[3]={8,-1,6};const auto& original=prefix;
std::priority_queue<int> copy(added,added+3,compare,original,alloc);
if(copy.size()!=5||prefix.size()!=2||prefix[0]!=6||&copy.top()==&prefix[0])return 1;
const int* storage=&prefix[0];std::priority_queue<int> moved(added,added+3,compare,std::move(prefix),alloc);
if(!prefix.empty()||moved.size()!=5||&moved.top()!=storage)return 2;
prefix.push_back(21);int expected[5]={8,6,6,2,-1};for(int i=0;i!=5;++i){
if(copy.top()!=expected[i]||moved.top()!=expected[i])return 3;copy.pop();moved.pop();}
return !copy.empty()||!moved.empty()||prefix.size()!=1||prefix[0]!=21;}
)r19priorityalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-alloc-range-copy-move-prefix" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueOwnedAllocatorConstructorRejected) {
  const auto Source = tmpFile("priority-alloc-owned-allocator-constructor.cpp");
  writeFile(Source, R"r19priorityalloc(#include <queue>
#include <memory>
namespace std { inline namespace __1 {
template<> template<> inline priority_queue<int>::priority_queue(const allocator<int>&) : c(), comp() {}
}}
int main(){std::allocator<int> alloc;std::priority_queue<int> q(alloc);return !q.empty();}
)r19priorityalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueAllocatorOwnedVectorCopyConstructorRejected) {
  const auto Source = tmpFile("priority-alloc-owned-vector-copy-constructor.cpp");
  writeFile(Source, R"r19priorityalloc(#include <queue>
#include <memory>
namespace std { inline namespace __1 {
template<> inline vector<int>::vector(const vector<int>&,const allocator<int>&) {}
}}
int main(){std::allocator<int> alloc;std::less<int> compare;std::vector<int> values{3,9};
const auto& original=values;std::priority_queue<int> q(compare,original,alloc);return q.empty();}
)r19priorityalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueAllocatorOwnedMakeHeapRejected) {
  const auto Source = tmpFile("priority-alloc-owned-make-heap-body.cpp");
  writeFile(Source, R"r19priorityalloc(#include <queue>
#include <memory>
namespace std { inline namespace __1 {
template<> inline void make_heap<int*,less<int>>(int*,int*,less<int>) {}
}}
int main(){std::allocator<int> alloc;std::less<int> compare;std::vector<int> values{3,9};
const auto& original=values;std::priority_queue<int> q(compare,original,alloc);return q.top()!=9;}
)r19priorityalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueAllocatorOwnedLengthErrorRejected) {
  const auto Source = tmpFile("priority-alloc-owned-vector-length-error.cpp");
  writeFile(Source, R"r19priorityalloc(#include <queue>
#include <memory>
namespace std { inline namespace __1 {
template<> inline void vector<int>::__throw_length_error() {__builtin_trap();}
}}
int main(){std::allocator<int> alloc;std::less<int> compare;std::vector<int> values{3,9};
const auto& original=values;std::priority_queue<int> q(compare,original,alloc);return q.empty();}
)r19priorityalloc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
