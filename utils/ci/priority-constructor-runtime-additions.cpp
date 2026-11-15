
TEST_F(TranslateTest, CoreV2PriorityQueueIntComparatorConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-ctor-comparator.cpp");
  const auto Output = tmpFile("priority-ctor-comparator.nc");
  writeFile(Source, R"r19priorityctor(#include <queue>
int main(){std::less<int> compare;std::priority_queue<int> q(compare);
if(!q.empty()||q.size()!=0)return 1;q.push(-7);q.push(12);q.push(3);
if(q.top()!=12)return 2;q.pop();if(q.top()!=3)return 3;q.pop();return q.top()!=-7;}
)r19priorityctor");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-ctor-comparator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntCopyVectorConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-ctor-copy-vector.cpp");
  const auto Output = tmpFile("priority-ctor-copy-vector.nc");
  writeFile(Source, R"r19priorityctor(#include <queue>
#include <vector>
int main(){std::less<int> compare;std::vector<int> values{3,11,3,-5};
const auto& source=values;std::priority_queue<int> q(compare,source);
if(q.size()!=4||q.top()!=11||&q.top()==&values[0])return 1;
values[1]=99;if(q.top()!=11)return 2;
int expected[4]={11,3,3,-5};for(int i=0;i!=4;++i){if(q.top()!=expected[i])return 3;q.pop();}
return !q.empty()||values.size()!=4||values[1]!=99;}
)r19priorityctor");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-ctor-copy-vector" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntMoveVectorConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-ctor-move-vector.cpp");
  const auto Output = tmpFile("priority-ctor-move-vector.nc");
  writeFile(Source, R"r19priorityctor(#include <queue>
#include <vector>
#include <utility>
int main(){std::less<int> compare;std::vector<int> values{2,14,-3,6};
const int* storage=&values[0];std::priority_queue<int> q(compare,std::move(values));
if(!values.empty()||q.size()!=4||q.top()!=14||&q.top()!=storage)return 1;
values.push_back(8);if(values[0]!=8||q.top()!=14)return 2;
int expected[4]={14,6,2,-3};for(int i=0;i!=4;++i){if(q.top()!=expected[i])return 3;q.pop();}
return !q.empty();}
)r19priorityctor");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-ctor-move-vector" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntPointerRangeConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-ctor-pointer-range.cpp");
  const auto Output = tmpFile("priority-ctor-pointer-range.nc");
  writeFile(Source, R"r19priorityctor(#include <queue>
int main(){int values[5]={4,-8,17,4,2};const int* first=values;
std::priority_queue<int> q(first,first+5);std::less<int> compare;
std::priority_queue<int> other(values,values+5,compare);
std::priority_queue<int> empty(first+5,first+5);
if(!empty.empty()||q.size()!=5||other.size()!=5)return 1;
int expected[5]={17,4,4,2,-8};for(int i=0;i!=5;++i){
if(q.top()!=expected[i]||other.top()!=expected[i])return 2;q.pop();other.pop();}
return !q.empty()||!other.empty()||values[0]!=4||values[2]!=17;}
)r19priorityctor");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-ctor-pointer-range" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntRangeCopyPrefixRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-ctor-range-copy-prefix.cpp");
  const auto Output = tmpFile("priority-ctor-range-copy-prefix.nc");
  writeFile(Source, R"r19priorityctor(#include <queue>
#include <vector>
int main(){std::less<int> compare;std::vector<int> values{4,-2};int added[3]={9,1,9};
const auto& prefix=values;std::priority_queue<int> q(added,added+3,compare,prefix);
if(q.size()!=5||values.size()!=2||values[0]!=4||values[1]!=-2)return 1;
int expected[5]={9,9,4,1,-2};for(int i=0;i!=5;++i){if(q.top()!=expected[i])return 2;q.pop();}
return !q.empty();}
)r19priorityctor");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-ctor-range-copy-prefix" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueIntRangeMovePrefixRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-ctor-range-move-prefix.cpp");
  const auto Output = tmpFile("priority-ctor-range-move-prefix.nc");
  writeFile(Source, R"r19priorityctor(#include <queue>
#include <vector>
#include <utility>
int main(){std::less<int> compare;std::vector<int> values{6,2};const int added[3]={8,-1,6};
std::priority_queue<int> q(added,added+3,compare,std::move(values));
if(!values.empty()||q.size()!=5)return 1;values.push_back(21);
int expected[5]={8,6,6,2,-1};for(int i=0;i!=5;++i){if(q.top()!=expected[i])return 2;q.pop();}
return !q.empty()||values.size()!=1||values[0]!=21;}
)r19priorityctor");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-ctor-range-move-prefix" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueOwnedComparatorConstructorRejected) {
  const auto Source = tmpFile("priority-ctor-owned-comparator-constructor.cpp");
  writeFile(Source, R"r19priorityctor(#include <queue>
namespace std { inline namespace __1 {
template<> inline priority_queue<int>::priority_queue(const less<int>&) : c(), comp() {}
}}
int main(){std::less<int> compare;std::priority_queue<int> q(compare);return !q.empty();}
)r19priorityctor");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueOwnedCopyVectorConstructorRejected) {
  const auto Source = tmpFile("priority-ctor-owned-copy-vector-constructor.cpp");
  writeFile(Source, R"r19priorityctor(#include <queue>
namespace std { inline namespace __1 {
template<> inline priority_queue<int>::priority_queue(const less<int>&, const vector<int>&) : c(), comp() {}
}}
int main(){std::less<int> compare;std::vector<int> v{3,9};std::priority_queue<int> q(compare,v);return q.empty();}
)r19priorityctor");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueOwnedMakeHeapRejected) {
  const auto Source = tmpFile("priority-ctor-owned-make-heap-body.cpp");
  writeFile(Source, R"r19priorityctor(#include <queue>
namespace std { inline namespace __1 {
template<> inline void make_heap<int*, less<int>>(int*, int*, less<int>) {}
}}
int main(){std::less<int> compare;std::vector<int> v{3,9};std::priority_queue<int> q(compare,v);return q.top()!=9;}
)r19priorityctor");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueConstructorOwnedLengthErrorRejected) {
  const auto Source = tmpFile("priority-ctor-owned-vector-length-error.cpp");
  writeFile(Source, R"r19priorityctor(#include <queue>
namespace std { inline namespace __1 {
template<> inline void vector<int>::__throw_length_error() {__builtin_trap();}
}}
int main(){std::less<int> compare;std::vector<int> v{3,9};std::priority_queue<int> q(compare,v);return q.empty();}
)r19priorityctor");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
