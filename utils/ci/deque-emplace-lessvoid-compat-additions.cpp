
TEST_F(TranslateTest, CoreV2PriorityQueueLessVoidAllocatorEmptyComparatorRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-lessvoid-empty-comparator.cpp");
  const auto Output = tmpFile("priority-lessvoid-empty-comparator.nc");
  writeFile(Source, R"r19pqlessvoid(#include <queue>
#include <memory>
int main(){std::allocator<int> alloc;std::less<void> compare;
std::priority_queue<int,std::vector<int>,std::less<void>> a(alloc);std::priority_queue<int,std::vector<int>,std::less<void>> b(compare,alloc);
if(!a.empty()||!b.empty())return 1;a.push(-4);a.push(9);b.push(3);b.push(12);
if(a.top()!=9||b.top()!=12)return 2;a.pop();b.pop();return a.top()!=-4||b.top()!=3;}
)r19pqlessvoid");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-lessvoid-empty-comparator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueLessVoidAllocatorCopyVectorRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-lessvoid-copy-vector.cpp");
  const auto Output = tmpFile("priority-lessvoid-copy-vector.nc");
  writeFile(Source, R"r19pqlessvoid(#include <queue>
#include <memory>
#include <vector>
int main(){std::allocator<int> alloc;std::less<void> compare;std::vector<int> values{3,11,3,-5};
const auto& source=values;std::priority_queue<int,std::vector<int>,std::less<void>> q(compare,source,alloc);
if(q.size()!=4||q.top()!=11||&q.top()==&values[0])return 1;
values[1]=99;if(q.top()!=11)return 2;int expected[4]={11,3,3,-5};
for(int i=0;i!=4;++i){if(q.top()!=expected[i])return 3;q.pop();}
return !q.empty()||values.size()!=4||values[1]!=99;}
)r19pqlessvoid");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-lessvoid-copy-vector" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueLessVoidAllocatorMoveVectorRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-lessvoid-move-vector.cpp");
  const auto Output = tmpFile("priority-lessvoid-move-vector.nc");
  writeFile(Source, R"r19pqlessvoid(#include <queue>
#include <memory>
#include <vector>
#include <utility>
int main(){std::allocator<int> alloc;std::less<void> compare;std::vector<int> values{2,14,-3,6};
const int* storage=&values[0];std::priority_queue<int,std::vector<int>,std::less<void>> q(compare,std::move(values),alloc);
if(!values.empty()||q.size()!=4||q.top()!=14||&q.top()!=storage)return 1;
values.push_back(8);if(values[0]!=8||q.top()!=14)return 2;int expected[4]={14,6,2,-3};
for(int i=0;i!=4;++i){if(q.top()!=expected[i])return 3;q.pop();}return !q.empty();}
)r19pqlessvoid");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-lessvoid-move-vector" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueLessVoidAllocatorCopyMoveQueueRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-lessvoid-copy-move-queue.cpp");
  const auto Output = tmpFile("priority-lessvoid-copy-move-queue.nc");
  writeFile(Source, R"r19pqlessvoid(#include <queue>
#include <memory>
#include <utility>
int main(){std::allocator<int> alloc;std::priority_queue<int,std::vector<int>,std::less<void>> source;
source.push(4);source.push(-2);source.push(17);const auto& original=source;
std::priority_queue<int,std::vector<int>,std::less<void>> copy(original,alloc);
if(copy.size()!=3||copy.top()!=17||&copy.top()==&source.top())return 1;
const int* storage=&source.top();std::priority_queue<int,std::vector<int>,std::less<void>> moved(std::move(source),alloc);
if(!source.empty()||moved.size()!=3||&moved.top()!=storage)return 2;
source.push(21);if(source.top()!=21||copy.top()!=17||moved.top()!=17)return 3;
int expected[3]={17,4,-2};for(int i=0;i!=3;++i){
if(copy.top()!=expected[i]||moved.top()!=expected[i])return 4;copy.pop();moved.pop();}
return !copy.empty()||!moved.empty();}
)r19pqlessvoid");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-lessvoid-copy-move-queue" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueLessVoidAllocatorPointerRangeRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-lessvoid-pointer-range.cpp");
  const auto Output = tmpFile("priority-lessvoid-pointer-range.nc");
  writeFile(Source, R"r19pqlessvoid(#include <queue>
#include <memory>
int main(){std::allocator<int> alloc;std::less<void> compare;int values[5]={4,-8,17,4,2};
const int* first=values;std::priority_queue<int,std::vector<int>,std::less<void>> a(first,first+5,alloc);
std::priority_queue<int,std::vector<int>,std::less<void>> b(values,values+5,compare,alloc);
std::priority_queue<int,std::vector<int>,std::less<void>> empty(first+5,first+5,alloc);
if(!empty.empty()||a.size()!=5||b.size()!=5)return 1;int expected[5]={17,4,4,2,-8};
for(int i=0;i!=5;++i){if(a.top()!=expected[i]||b.top()!=expected[i])return 2;a.pop();b.pop();}
return !a.empty()||!b.empty()||values[0]!=4||values[2]!=17;}
)r19pqlessvoid");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-lessvoid-pointer-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueLessVoidAllocatorRangeCopyMovePrefixRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-lessvoid-range-copy-move-prefix.cpp");
  const auto Output = tmpFile("priority-lessvoid-range-copy-move-prefix.nc");
  writeFile(Source, R"r19pqlessvoid(#include <queue>
#include <memory>
#include <vector>
#include <utility>
int main(){std::allocator<int> alloc;std::less<void> compare;std::vector<int> prefix{6,2};
prefix.reserve(16);const int added[3]={8,-1,6};const auto& original=prefix;
std::priority_queue<int,std::vector<int>,std::less<void>> copy(added,added+3,compare,original,alloc);
if(copy.size()!=5||prefix.size()!=2||prefix[0]!=6||&copy.top()==&prefix[0])return 1;
const int* storage=&prefix[0];std::priority_queue<int,std::vector<int>,std::less<void>> moved(added,added+3,compare,std::move(prefix),alloc);
if(!prefix.empty()||moved.size()!=5||&moved.top()!=storage)return 2;
prefix.push_back(21);int expected[5]={8,6,6,2,-1};for(int i=0;i!=5;++i){
if(copy.top()!=expected[i]||moved.top()!=expected[i])return 3;copy.pop();moved.pop();}
return !copy.empty()||!moved.empty()||prefix.size()!=1||prefix[0]!=21;}
)r19pqlessvoid");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-lessvoid-range-copy-move-prefix" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueLessVoidOwnedComparatorRejected) {
  const auto Source = tmpFile("priority-lessvoid-owned-comparator-body.cpp");
  writeFile(Source, R"r19pqlessvoid(#include <queue>
namespace std { inline namespace __1 {
template<> inline constexpr auto less<void>::operator()<int&,int&>(int&,int&) const noexcept -> bool {return false;}
}}
int main(){int left=0,right=1;std::less<void> compare;if(compare(left,right))return 1;
std::priority_queue<int,std::vector<int>,std::less<void>> q;q.push(3);q.push(-8);return q.top()!=3;}
)r19pqlessvoid");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueLessVoidOwnedVectorCopyRejected) {
  const auto Source = tmpFile("priority-lessvoid-owned-vector-copy-constructor.cpp");
  writeFile(Source, R"r19pqlessvoid(#include <queue>
#include <memory>
namespace std { inline namespace __1 {
template<> inline vector<int>::vector(const vector<int>&,const allocator<int>&) {}
}}
int main(){std::allocator<int> alloc;std::less<void> compare;std::vector<int> values{3,9};
const auto& original=values;std::priority_queue<int,std::vector<int>,std::less<void>> q(compare,original,alloc);return q.empty();}
)r19pqlessvoid");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueLessVoidOwnedMakeHeapRejected) {
  const auto Source = tmpFile("priority-lessvoid-owned-make-heap-body.cpp");
  writeFile(Source, R"r19pqlessvoid(#include <queue>
#include <memory>
namespace std { inline namespace __1 {
template<> inline void make_heap<int*,less<void>>(int*,int*,less<void>) {}
}}
int main(){std::allocator<int> alloc;std::less<void> compare;std::vector<int> values{3,9};
const auto& original=values;std::priority_queue<int,std::vector<int>,std::less<void>> q(compare,original,alloc);return q.top()!=9;}
)r19pqlessvoid");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueLessVoidOwnedLengthErrorRejected) {
  const auto Source = tmpFile("priority-lessvoid-owned-vector-length-error.cpp");
  writeFile(Source, R"r19pqlessvoid(#include <queue>
#include <memory>
namespace std { inline namespace __1 {
template<> inline void vector<int>::__throw_length_error() {__builtin_trap();}
}}
int main(){std::allocator<int> alloc;std::less<void> compare;std::vector<int> values{3,9};
const auto& original=values;std::priority_queue<int,std::vector<int>,std::less<void>> q(compare,original,alloc);return q.empty();}
)r19pqlessvoid");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
