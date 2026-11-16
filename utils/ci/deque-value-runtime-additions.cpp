
TEST_F(TranslateTest, CoreV2DequeValueCopyConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-value-copy-construction.cpp");
  const auto Output = tmpFile("deque-value-copy-construction.nc");
  writeFile(Source, R"r19dqvalue(#include <deque>
#include <utility>
int basic_value_contract(){std::deque<int> a;for(int i=0;i!=2050;++i)a.push_back(i);
for(int i=1;i!=1030;++i)a.push_front(-i);
const std::deque<int>& source=a;std::deque<int> b(source);
if(b.size()!=a.size()||b.front()!=-1029||b.back()!=2049)return 1;
for(unsigned i=0;i!=a.size();++i)if(b[i]!=a[i]||&b[i]==&a[i])return 2;
b.front()=91;b.back()=93;if(a.front()!=-1029||a.back()!=2049)return 3;
std::deque<int> c(std::move(source));
if(c.size()!=a.size()||c.front()!=a.front()||&c.front()==&a.front())return 4;
std::deque<int> empty;std::deque<int> copied(empty);copied.push_back(7);
return !empty.empty()||copied.front()!=7;
}

using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
#include <deque>
static std::deque<int>* copy_source;static std::deque<int>* replacement;
static int copy_armed,copy_swaps;
void* operator new(Size n){void* p=malloc(n);if(copy_armed){copy_armed=0;copy_source->swap(*replacement);++copy_swaps;}return p;}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {free(p);}
int observable_value_contract(){std::deque<int> source,other;for(int i=0;i!=2050;++i)source.push_back(i);
other.push_back(77);copy_source=&source;replacement=&other;copy_armed=1;
std::deque<int> copied(source);copy_armed=0;
if(copy_swaps!=1||source.size()!=1||source.front()!=77||other.size()!=2050||copied.size()!=2050)return 1;
for(int i=0;i!=2050;++i)if(copied[i]!=i||&copied[i]==&other[i])return 2;
return 0;}

int main(){int r=basic_value_contract();return r?r:observable_value_contract();}
)r19dqvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-value-copy-construction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeValueCopyAssignmentRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-value-copy-assignment.cpp");
  const auto Output = tmpFile("deque-value-copy-assignment.nc");
  writeFile(Source, R"r19dqvalue(#include <deque>
int basic_value_contract(){std::deque<int> a,b;for(int i=0;i!=2050;++i)a.push_back(i);
b.push_back(-1);std::deque<int>& result=(b=a);
if(&result!=&b||b.size()!=2050||b.back()!=2049||&b.front()==&a.front())return 1;
b.front()=7;if(a.front()!=0)return 2;
a.pop_back();a.pop_front();b=a;
if(b.size()!=2048||b.front()!=1||b.back()!=2048)return 3;
std::deque<int>* self=&b;b=*self;if(b.size()!=2048||b.front()!=1)return 4;
std::deque<int> empty;b=empty;if(!b.empty()||!empty.empty())return 5;
empty=a;if(empty.size()!=2048||empty.front()!=1)return 6;
return &empty.front()==&a.front();
}

using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
#include <deque>
static std::deque<int>* assign_source;static int assign_armed,assign_changes;
void* operator new(Size n){void* p=malloc(n);if(assign_armed){assign_armed=0;(*assign_source)[0]=99;(*assign_source)[1]=88;(*assign_source)[2049]=93;++assign_changes;}return p;}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {free(p);}
int observable_value_contract(){std::deque<int> source,target;for(int i=0;i!=2050;++i)source.push_back(i);
target.push_back(-1);int* retained=&target.front();assign_source=&source;assign_armed=1;
target=source;assign_armed=0;
if(assign_changes!=1||target.size()!=2050||&target.front()!=retained)return 1;
return target[0]!=0||target[1]!=88||target.back()!=93||source.front()!=99;}

int main(){int r=basic_value_contract();return r?r:observable_value_contract();}
)r19dqvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-value-copy-assignment" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeValueMoveConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-value-move-construction.cpp");
  const auto Output = tmpFile("deque-value-move-construction.nc");
  writeFile(Source, R"r19dqvalue(#include <deque>
#include <utility>
int basic_value_contract(){std::deque<int> a;for(int i=0;i!=2050;++i)a.push_back(i);
for(int i=1;i!=1030;++i)a.push_front(-i);
int* first=&a.front();int* middle=&a[1029];int* last=&a.back();
std::deque<int> b(std::move(a));
if(!a.empty()||b.size()!=3079||&b.front()!=first||&b[1029]!=middle||&b.back()!=last)return 1;
*middle=31;if(b[1029]!=31)return 2;a.push_back(7);
if(a.front()!=7||b.front()!=-1029)return 3;
std::deque<int> empty;std::deque<int> c(std::move(empty));
c.push_front(9);return !empty.empty()||c.front()!=9;
}

using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
#include <deque>
static std::deque<int>* allocation_target;static Size allocation_bytes[16],allocation_sizes[16];
static int allocation_armed,allocation_count;
void* operator new(Size n){void* p=malloc(n);if(allocation_armed){int i=allocation_count++;if(i<16){allocation_bytes[i]=n;allocation_sizes[i]=allocation_target->size();}}return p;}
void operator delete(void* p) noexcept {free(p);}
void operator delete(void* p,Size) noexcept {free(p);}
int observable_value_contract(){std::deque<int> source,target;for(int i=0;i!=2050;++i)source.push_back(i);
allocation_target=&target;allocation_armed=1;target=source;allocation_armed=0;
if(allocation_count!=4||allocation_bytes[0]!=24||target.size()!=2050)return 1;
for(int i=0;i!=4;++i)if(allocation_sizes[i]!=0||(i&&allocation_bytes[i]!=4096))return 2;
return target.front()!=0||target.back()!=2049;}

int main(){int r=basic_value_contract();return r?r:observable_value_contract();}
)r19dqvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-value-move-construction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeValueMoveAssignmentAndSwapRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-value-move-assignment-swap.cpp");
  const auto Output = tmpFile("deque-value-move-assignment-swap.nc");
  writeFile(Source, R"r19dqvalue(#include <deque>
#include <utility>
int basic_value_contract(){std::deque<int> a,b;for(int i=0;i!=2050;++i)a.push_back(i);
for(int i=0;i!=1030;++i)b.push_front(-i-1);
int* old=&a[1024];std::deque<int>& result=(b=std::move(a));
if(&result!=&b||!a.empty()||b.size()!=2050||&b[1024]!=old)return 1;
a.push_back(7);int* seven=&a.front();b.swap(a);
if(a.size()!=2050||&a[1024]!=old||b.size()!=1||&b.front()!=seven)return 2;
std::swap(a,b);if(a.size()!=1||&a.front()!=seven||&b[1024]!=old)return 3;
b.swap(b);if(b.size()!=2050||&b[1024]!=old)return 4;
std::deque<int> empty;b=std::move(empty);
if(!b.empty()||!empty.empty())return 5;b.push_back(11);
return b.front()!=11||*seven!=7;
}

using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
#include <deque>
#include <utility>
static void* release_blocks[16];static int release_collect,release_armed,release_block_count,release_count,release_order[16];
void* operator new(Size n){void* p=malloc(n);if(release_collect&&n==4096){int i=release_block_count++;if(i<16)release_blocks[i]=p;}return p;}
void release_record(void* p){if(release_armed)for(int i=0;i!=release_block_count&&i<16;++i)if(p==release_blocks[i]){int n=release_count++;if(n<16)release_order[n]=i;}free(p);}
void operator delete(void* p) noexcept {release_record(p);}
void operator delete(void* p,Size) noexcept {release_record(p);}
int observable_value_contract(){std::deque<int> source,target;source.push_back(7);source.push_back(9);int* retained=&source.front();
release_collect=1;for(int i=0;i!=2050;++i)target.push_back(i);release_collect=0;
if(release_block_count!=3)return 1;release_armed=1;target=std::move(source);release_armed=0;
if(release_count!=3||release_order[0]!=0||release_order[1]!=2||release_order[2]!=1)return 2;
return !source.empty()||target.size()!=2||&target.front()!=retained||target.back()!=9;}

int main(){int r=basic_value_contract();return r?r:observable_value_contract();}
)r19dqvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-value-move-assignment-swap" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2StackValueCopyMoveAndSwapRunAtBothOptimizations) {
  const auto Source = tmpFile("stack-value-copy-move-swap.cpp");
  const auto Output = tmpFile("stack-value-copy-move-swap.nc");
  writeFile(Source, R"r19dqvalue(#include <stack>
#include <utility>
int basic_value_contract(){std::stack<int> a;for(int i=0;i!=2050;++i)a.push(i);
std::stack<int> b(a);if(b.size()!=2050||b.top()!=2049||&a.top()==&b.top())return 1;
b.top()=7;if(a.top()!=2049)return 2;
int* top=&a.top();std::stack<int> c(std::move(a));
if(!a.empty()||&c.top()!=top)return 3;a.push(11);b=c;
if(b.size()!=2050||b.top()!=2049||&b.top()==top)return 4;
std::stack<int>& assigned=(b=std::move(c));
if(&assigned!=&b||!c.empty()||&b.top()!=top)return 5;
a.swap(b);if(&a.top()!=top||b.top()!=11)return 6;
std::swap(a,b);if(a.top()!=11||&b.top()!=top)return 7;
for(int i=2049;i>=0;--i){if(b.top()!=i)return 8;b.pop();}
return !b.empty()||a.size()!=1;
}

#include <deque>
#include <utility>
static std::deque<int>* sequence_left;static std::deque<int>* sequence_right;static int sequence_effects;
std::deque<int>* left_deque(){sequence_effects=sequence_effects*10+1;return sequence_left;}
std::deque<int>* right_deque(){sequence_effects=sequence_effects*10+2;return sequence_right;}
int observable_value_contract(){std::deque<int> left,right;left.push_back(1);right.push_back(7);sequence_left=&left;sequence_right=&right;
std::deque<int>& first=(*left_deque()=*right_deque());if(sequence_effects!=21||&first!=&left||left.front()!=7)return 1;
sequence_effects=0;left_deque()->operator=(*right_deque());if(sequence_effects!=12)return 2;
sequence_effects=0;*left_deque()=std::move(*right_deque());if(sequence_effects!=21||!right.empty()||left.front()!=7)return 3;
right.push_back(9);sequence_effects=0;left_deque()->operator=(std::move(*right_deque()));
if(sequence_effects!=12||!right.empty()||left.front()!=9)return 4;
std::deque<int>* alias=&left;left=std::move(*alias);if(!left.empty())return 5;
left.push_back(11);int* retained=&left.front();left.swap(*alias);std::swap(left,*alias);
return left.size()!=1||left.front()!=11||&left.front()!=retained;}

int main(){int r=basic_value_contract();return r?r:observable_value_contract();}
)r19dqvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("stack-value-copy-move-swap" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2QueueValueCopyMoveAndSwapRunAtBothOptimizations) {
  const auto Source = tmpFile("queue-value-copy-move-swap.cpp");
  const auto Output = tmpFile("queue-value-copy-move-swap.nc");
  writeFile(Source, R"r19dqvalue(#include <queue>
#include <utility>
int basic_value_contract(){std::queue<int> a;for(int i=0;i!=2050;++i)a.push(i);
std::queue<int> b(a);if(b.size()!=2050||b.front()!=0||b.back()!=2049||&a.front()==&b.front())return 1;
b.front()=7;if(a.front()!=0)return 2;
int* front=&a.front();int* back=&a.back();std::queue<int> c(std::move(a));
if(!a.empty()||&c.front()!=front||&c.back()!=back)return 3;a.push(11);b=c;
if(b.size()!=2050||b.front()!=0||&b.front()==front)return 4;
std::queue<int>& assigned=(b=std::move(c));
if(&assigned!=&b||!c.empty()||&b.front()!=front)return 5;
a.swap(b);if(&a.front()!=front||b.front()!=11)return 6;
std::swap(a,b);if(a.front()!=11||&b.back()!=back)return 7;
for(int i=0;i!=2050;++i){if(b.front()!=i)return 8;b.pop();}
return !b.empty()||a.size()!=1;
}

#include <stack>
#include <queue>
#include <utility>
static std::stack<int>* sl;static std::stack<int>* sr;static std::queue<int>* ql;static std::queue<int>* qr;static int adapter_effects;
std::stack<int>* left_stack(){adapter_effects=adapter_effects*10+1;return sl;}
std::stack<int>* right_stack(){adapter_effects=adapter_effects*10+2;return sr;}
std::queue<int>* left_queue(){adapter_effects=adapter_effects*10+1;return ql;}
std::queue<int>* right_queue(){adapter_effects=adapter_effects*10+2;return qr;}
int observable_value_contract(){std::stack<int> a,b;a.push(1);b.push(7);sl=&a;sr=&b;
std::stack<int>& s=(*left_stack()=*right_stack());if(adapter_effects!=21||&s!=&a||a.top()!=7)return 1;
adapter_effects=0;left_stack()->operator=(std::move(*right_stack()));if(adapter_effects!=12||!b.empty()||a.top()!=7)return 2;
std::queue<int> c,d;c.push(1);d.push(9);ql=&c;qr=&d;adapter_effects=0;
std::queue<int>& q=(*left_queue()=*right_queue());if(adapter_effects!=21||&q!=&c||c.front()!=9)return 3;
adapter_effects=0;left_queue()->operator=(std::move(*right_queue()));
return adapter_effects!=12||!d.empty()||c.size()!=1||c.front()!=9;}

int main(){int r=basic_value_contract();return r?r:observable_value_contract();}
)r19dqvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("queue-value-copy-move-swap" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeValueCopyConstructorOwnedBodyRejected) {
  const auto Source = tmpFile("deque-copy-constructor-owned-body.cpp");
  writeFile(Source, R"r19dqvalue(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::deque(const deque& value) : deque() {}
}}
int main(){std::deque<int> a;std::deque<int> b(a);return b.size()!=0;}
)r19dqvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeValueCopyAssignmentOwnedBodyRejected) {
  const auto Source = tmpFile("deque-copy-assignment-owned-body.cpp");
  writeFile(Source, R"r19dqvalue(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>& deque<int>::operator=(const deque& value){return *this;}
}}
int main(){std::deque<int> a,b;return &(b=a)!=&b;}
)r19dqvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeValueMoveConstructorOwnedBodyRejected) {
  const auto Source = tmpFile("deque-move-constructor-owned-body.cpp");
  writeFile(Source, R"r19dqvalue(#include <deque>
#include <utility>
namespace std { inline namespace __1 {
template<> deque<int>::deque(deque&& value) noexcept : deque() {}
}}
int main(){std::deque<int> a;std::deque<int> b(std::move(a));return b.size()!=0;}
)r19dqvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeValueSwapOwnedBodyRejected) {
  const auto Source = tmpFile("deque-swap-owned-body.cpp");
  writeFile(Source, R"r19dqvalue(#include <deque>
namespace std { inline namespace __1 {
template<> void deque<int>::swap(deque& value) noexcept {}
}}
int main(){std::deque<int> a,b;a.swap(b);return 0;}
)r19dqvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
