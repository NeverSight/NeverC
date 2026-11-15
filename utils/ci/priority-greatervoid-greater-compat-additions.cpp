
TEST_F(TranslateTest, CoreV2PriorityQueueGreaterIntHeapRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-greater-heap-extremes.cpp");
  const auto Output = tmpFile("priority-greater-heap-extremes.nc");
  writeFile(Source, R"r19pqgreater(#include <queue>
#include <vector>
#include <functional>
#include <memory>
#include <utility>
using Minimum=std::priority_queue<int,std::vector<int>,std::greater<int>>;
int main(){Minimum q;
if(!q.empty()||q.size()!=0)return 1;int input[6]={7,-3,7,2147483647,-2147483647-1,0};
for(int x:input)q.push(x);int expected[6]={-2147483647-1,-3,0,7,7,2147483647};
for(int i=0;i!=6;++i){if(q.size()!=static_cast<unsigned>(6-i)||q.top()!=expected[i])return 2;q.pop();}
return !q.empty();}
)r19pqgreater");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-greater-heap-extremes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueGreaterIntValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-greater-value-operations.cpp");
  const auto Output = tmpFile("priority-greater-value-operations.nc");
  writeFile(Source, R"r19pqgreater(#include <queue>
#include <vector>
#include <functional>
#include <memory>
#include <utility>
using Minimum=std::priority_queue<int,std::vector<int>,std::greater<int>>;
int main(){Minimum q;q.push(9);q.push(-3);q.push(4);
Minimum copy(q);if(copy.top()!=-3||&copy.top()==&q.top())return 1;
Minimum assigned;Minimum& reference=(assigned=copy);if(&reference!=&assigned||assigned.size()!=3)return 2;
const int* storage=&q.top();Minimum moved(std::move(q));if(!q.empty()||&moved.top()!=storage)return 3;
Minimum other;other.push(12);other.swap(moved);if(other.top()!=-3||&other.top()!=storage||moved.top()!=12)return 4;
Minimum final;final=std::move(assigned);if(!assigned.empty()||final.top()!=-3)return 5;
std::swap(final,moved);if(final.top()!=12||moved.top()!=-3)return 6;
q.emplace();q.emplace(-11);q.emplace(q.top());if(q.size()!=3||q.top()!=-11)return 7;
q.pop();if(q.top()!=-11)return 8;q.pop();return q.top()!=0;}
)r19pqgreater");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-greater-value-operations" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueGreaterIntVectorsRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-greater-vector-constructors.cpp");
  const auto Output = tmpFile("priority-greater-vector-constructors.nc");
  writeFile(Source, R"r19pqgreater(#include <queue>
#include <vector>
#include <functional>
#include <memory>
#include <utility>
using Minimum=std::priority_queue<int,std::vector<int>,std::greater<int>>;
int main(){std::greater<int> compare;
Minimum empty(compare);if(!empty.empty())return 1;std::vector<int> values{9,-2,6,6};
const auto& original=values;Minimum copy(compare,original);values[0]=11;
const int* storage=&values[0];Minimum moved(compare,std::move(values));
if(!values.empty()||&moved.top()!=storage||&copy.top()==storage)return 2;
int a[4]={-2,6,6,9},b[4]={-2,6,6,11};for(int i=0;i!=4;++i){
if(copy.top()!=a[i]||moved.top()!=b[i])return 3;copy.pop();moved.pop();}
values.push_back(21);return !copy.empty()||!moved.empty()||values[0]!=21;}
)r19pqgreater");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-greater-vector-constructors" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueGreaterIntPointerRangesRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-greater-pointer-ranges.cpp");
  const auto Output = tmpFile("priority-greater-pointer-ranges.nc");
  writeFile(Source, R"r19pqgreater(#include <queue>
#include <vector>
#include <functional>
#include <memory>
#include <utility>
using Minimum=std::priority_queue<int,std::vector<int>,std::greater<int>>;
int main(){int values[5]={4,-8,17,4,2};const int* first=values;
Minimum a(first,first+5);std::greater<int> compare;Minimum b(values,values+5,compare);
Minimum empty(first+5,first+5);if(!empty.empty()||a.size()!=5||b.size()!=5)return 1;
int expected[5]={-8,2,4,4,17};for(int i=0;i!=5;++i){
if(a.top()!=expected[i]||b.top()!=expected[i])return 2;a.pop();b.pop();}
return !a.empty()||!b.empty()||values[0]!=4||values[2]!=17;}
)r19pqgreater");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-greater-pointer-ranges" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueGreaterIntRangePrefixesRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-greater-range-prefixes.cpp");
  const auto Output = tmpFile("priority-greater-range-prefixes.nc");
  writeFile(Source, R"r19pqgreater(#include <queue>
#include <vector>
#include <functional>
#include <memory>
#include <utility>
using Minimum=std::priority_queue<int,std::vector<int>,std::greater<int>>;
int main(){std::greater<int> compare;std::vector<int> prefix{6,2};
prefix.reserve(16);const int added[3]={8,-1,6};const auto& original=prefix;
Minimum copy(added,added+3,compare,original);const int* storage=&prefix[0];
Minimum moved(added,added+3,compare,std::move(prefix));
if(!prefix.empty()||copy.size()!=5||moved.size()!=5||&moved.top()!=storage||&copy.top()==storage)return 1;
int expected[5]={-1,2,6,6,8};for(int i=0;i!=5;++i){
if(copy.top()!=expected[i]||moved.top()!=expected[i])return 2;copy.pop();moved.pop();}
prefix.push_back(21);return !copy.empty()||!moved.empty()||prefix[0]!=21;}
)r19pqgreater");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-greater-range-prefixes" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueGreaterIntAllocatorsRunAtBothOptimizations) {
  const auto Source = tmpFile("priority-greater-allocator-constructors.cpp");
  const auto Output = tmpFile("priority-greater-allocator-constructors.nc");
  writeFile(Source, R"r19pqgreater(#include <queue>
#include <vector>
#include <functional>
#include <memory>
#include <utility>
using Minimum=std::priority_queue<int,std::vector<int>,std::greater<int>>;
int main(){std::allocator<int> alloc;std::greater<int> compare;
Minimum a(alloc),b(compare,alloc);if(!a.empty()||!b.empty())return 1;
std::vector<int> values{5,-1};const auto& original=values;Minimum c(compare,original,alloc);
Minimum d(compare,std::move(values),alloc);Minimum e(c,alloc);const int* queue_storage=&d.top();
Minimum f(std::move(d),alloc);if(!values.empty()||!d.empty()||&f.top()!=queue_storage||&e.top()==&c.top())return 2;
int range[2]={3,-7};Minimum g(range,range+2,alloc),h(range,range+2,compare,alloc);
std::vector<int> prefix{8,2},moved_prefix{6,4};moved_prefix.reserve(16);const int* storage=&moved_prefix[0];
const auto& constant_prefix=prefix;Minimum i(range,range+2,compare,constant_prefix,alloc);
Minimum j(range,range+2,compare,std::move(moved_prefix),alloc);
if(c.top()!=-1||e.top()!=-1||f.top()!=-1||g.top()!=-7||h.top()!=-7||i.top()!=-7||j.top()!=-7)return 3;
if(c.size()!=2||e.size()!=2||f.size()!=2||g.size()!=2||h.size()!=2||i.size()!=4||j.size()!=4)return 4;
return prefix.size()!=2||prefix[0]!=8||!moved_prefix.empty()||&j.top()!=storage;}
)r19pqgreater");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("priority-greater-allocator-constructors" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2PriorityQueueGreaterOwnedComparatorRejected) {
  const auto Source = tmpFile("priority-greater-owned-comparator-body.cpp");
  writeFile(Source, R"r19pqgreater(#include <queue>
#include <vector>
#include <functional>
#include <memory>
#include <utility>
using Minimum=std::priority_queue<int,std::vector<int>,std::greater<int>>;
namespace std { inline namespace __1 {
template<> inline constexpr bool greater<int>::operator()(const int&,const int&) const {return false;}
}}
int main(){Minimum q;q.push(3);q.push(-8);return q.top()!=-8;}
)r19pqgreater");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueGreaterOwnedPushRejected) {
  const auto Source = tmpFile("priority-greater-owned-push-body.cpp");
  writeFile(Source, R"r19pqgreater(#include <queue>
#include <vector>
#include <functional>
#include <memory>
#include <utility>
using Minimum=std::priority_queue<int,std::vector<int>,std::greater<int>>;
namespace std { inline namespace __1 {
template<> inline void priority_queue<int,vector<int>,greater<int>>::push(const int&) {}
}}
int main(){Minimum q;int value=3;q.push(value);return q.empty();}
)r19pqgreater");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueGreaterOwnedMakeHeapRejected) {
  const auto Source = tmpFile("priority-greater-owned-make-heap-body.cpp");
  writeFile(Source, R"r19pqgreater(#include <queue>
#include <vector>
#include <functional>
#include <memory>
#include <utility>
using Minimum=std::priority_queue<int,std::vector<int>,std::greater<int>>;
namespace std { inline namespace __1 {
template<> inline void make_heap<int*,greater<int>>(int*,int*,greater<int>) {}
}}
int main(){std::greater<int> compare;std::vector<int> values{3,-8};const auto& original=values;
Minimum q(compare,original);return q.top()!=-8;}
)r19pqgreater");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2PriorityQueueGreaterOwnedLengthErrorRejected) {
  const auto Source = tmpFile("priority-greater-owned-vector-length-error.cpp");
  writeFile(Source, R"r19pqgreater(#include <queue>
#include <vector>
#include <functional>
#include <memory>
#include <utility>
using Minimum=std::priority_queue<int,std::vector<int>,std::greater<int>>;
namespace std { inline namespace __1 {
template<> inline void vector<int>::__throw_length_error() {__builtin_trap();}
}}
int main(){std::greater<int> compare;std::allocator<int> alloc;std::vector<int> values{3,-8};
const auto& original=values;Minimum q(compare,original,alloc);return q.empty();}
)r19pqgreater");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
