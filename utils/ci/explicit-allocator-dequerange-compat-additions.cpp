
TEST_F(TranslateTest, CoreV2DequeRangeMutableConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-range-mutable-construction.cpp");
  const auto Output = tmpFile("deque-range-mutable-construction.nc");
  writeFile(Source, R"r19dqrange(#include <deque>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
int main(){
int values[3074];for(unsigned i=0;i!=3074;++i)values[i]=(int)i-17;
unsigned counts[7]={0,1,1023,1024,1025,2049,3073};
for(unsigned t=0;t!=7;++t){unsigned n=counts[t];std::deque<int> a(values,values+n);
if(a.size()!=n)return 1;for(unsigned i=0;i!=n;++i)if(a[i]!=values[i])return 2;
if(n){int old=a.front();values[0]=999;if(a.front()!=old)return 3;values[0]=-17;
int* saved=&a.front();a.push_back(23);if(&a.front()!=saved||a.back()!=23)return 4;}}
return 0;}
)r19dqrange");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-range-mutable-construction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeRangeConstConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-range-const-construction.cpp");
  const auto Output = tmpFile("deque-range-const-construction.nc");
  writeFile(Source, R"r19dqrange(#include <deque>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
static int armed,calls;static int data[2050];
static const int* first;static const int* last;
void* operator new(Size n){void* p=malloc(n);if(armed){++calls;first=data+10;last=data+11;
for(unsigned i=0;i!=2050;++i)data[i]=40+(int)i;}return p;}
void operator delete(void* p) noexcept {free(p);}void operator delete(void* p,Size) noexcept {free(p);}
int main(){for(unsigned i=0;i!=2050;++i)data[i]=(int)i;first=data;last=data+2050;
armed=1;std::deque<int> a(first,last);armed=0;
if(a.size()!=2050||calls!=4||first!=data+10||last!=data+11)return 1;
for(unsigned i=0;i!=2050;++i)if(a[i]!=40+(int)i)return 2;
const int* b=data+7;std::deque<int> empty(b,b);if(!empty.empty())return 3;
return 0;}
)r19dqrange");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-range-const-construction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeRangeAssignmentGrowRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-range-assign-grow.cpp");
  const auto Output = tmpFile("deque-range-assign-grow.nc");
  writeFile(Source, R"r19dqrange(#include <deque>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
static std::deque<int>* owner;static int values[2050];
static int armed,calls,bad;
void* operator new(Size n){void* p=malloc(n);if(armed){
if(owner->size()!=1||owner->front()!=7)bad=1;++calls;
for(unsigned i=1;i!=2050;++i)values[i]=70+(int)i;}return p;}
void operator delete(void* p) noexcept {free(p);}void operator delete(void* p,Size) noexcept {free(p);}
int main(){for(unsigned i=0;i!=2050;++i)values[i]=7+(int)i;
std::deque<int> a;a.push_back(5);int* saved=&a.front();owner=&a;armed=1;
a.assign(values,values+2050);armed=0;
if(bad||!calls||a.size()!=2050||a.front()!=7||&a.front()!=saved)return 1;
for(unsigned i=1;i!=2050;++i)if(a[i]!=70+(int)i)return 2;
return 0;}
)r19dqrange");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-range-assign-grow" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeRangeAssignmentShrinkRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-range-assign-shrink.cpp");
  const auto Output = tmpFile("deque-range-assign-shrink.nc");
  writeFile(Source, R"r19dqrange(#include <deque>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
static std::deque<int>* owner;static void* blocks[8];
static int collect,armed,count,releases,bad,order[8];
void* operator new(Size n){void* p=malloc(n);if(collect&&n==4096){if(count<8)blocks[count]=p;++count;}return p;}
void release(void* p){if(armed)for(int i=0;i!=count&&i<8;++i)if(p==blocks[i]){
if(releases<8)order[releases]=i;++releases;
if(owner->size()!=1||owner->front()!=99)bad=1;}free(p);}
void operator delete(void* p) noexcept {release(p);}void operator delete(void* p,Size) noexcept {release(p);}
int main(){collect=1;std::deque<int> a(4100,7);collect=0;if(count!=5)return 1;
const int values[2]={99,100};int* saved=&a.front();owner=&a;armed=1;
a.assign(values,values+1);armed=0;
if(bad||a.size()!=1||a.front()!=99||&a.front()!=saved||releases!=3)return 2;
for(int i=0;i!=3;++i)if(order[i]!=4-i)return 3;
a.assign(values,values);return !a.empty();}
)r19dqrange");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-range-assign-shrink" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeRangeConstAssignmentRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-range-const-assignment.cpp");
  const auto Output = tmpFile("deque-range-const-assignment.nc");
  writeFile(Source, R"r19dqrange(#include <deque>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
int main(){int storage[3075];
for(unsigned i=0;i!=3075;++i)storage[i]=3*(int)i-40;const int* values=storage;
std::deque<int> a;unsigned counts[8]={0,1,1023,1024,2049,3073,1025,0};
for(unsigned t=0;t!=8;++t){unsigned n=counts[t];a.assign(values+1,values+1+n);
if(a.size()!=n)return 1;for(unsigned i=0;i!=n;++i)if(a[i]!=storage[i+1])return 2;}
a.assign(values+4,values+9);int* saved=&a.front();a.assign(values+5,values+10);
if(&a.front()!=saved||a.size()!=5||a.front()!=storage[5]||a.back()!=storage[9])return 3;
return 0;}
)r19dqrange");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-range-const-assignment" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeRangeCallSequencingRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-range-call-sequencing.cpp");
  const auto Output = tmpFile("deque-range-call-sequencing.nc");
  writeFile(Source, R"r19dqrange(#include <deque>
using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
static std::deque<int>* owner;
static int values[3]={4,5,6};static int receivers,firsts,lasts,bad;
std::deque<int>& receiver(){++receivers;return *owner;}
int* begin_argument(){++firsts;if(receivers!=1)bad=1;return values;}
int* end_argument(){++lasts;if(receivers!=1)bad=1;return values+3;}
const int* empty_begin(){++firsts;return values+1;}
const int* empty_end(){++lasts;return values+1;}
int main(){std::deque<int> target;owner=&target;receiver().assign(begin_argument(),end_argument());
if(bad||receivers!=1||firsts!=1||lasts!=1||owner->size()!=3||owner->front()!=4||owner->back()!=6)return 1;
std::deque<int> empty(empty_begin(),empty_end());
if(firsts!=2||lasts!=2||!empty.empty())return 2;
owner->assign(values,values);return !owner->empty();}
)r19dqrange");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-range-call-sequencing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeRangeMutableConstructorOwnedBodyRejected) {
  const auto Source = tmpFile("deque-range-mutable-constructor-owned-body.cpp");
  writeFile(Source, R"r19dqrange(#include <deque>
namespace std { inline namespace __1 {
template<> template<> deque<int>::deque(int* first,int* last) : deque() {}
}}
int main(){int values[3]={1,2,3};int* first=values;
std::deque<int> a(first,first+3);return a.size()!=0;}
)r19dqrange");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeRangeMutableAssignmentOwnedBodyRejected) {
  const auto Source = tmpFile("deque-range-mutable-assign-owned-body.cpp");
  writeFile(Source, R"r19dqrange(#include <deque>
namespace std { inline namespace __1 {
template<> template<> void deque<int>::assign<int*,0>(int* first,int* last) {}
}}
int main(){int values[3]={1,2,3};int* first=values;
std::deque<int> a;a.assign(first,first+3);return a.size()!=0;}
)r19dqrange");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeRangeConstConstructorOwnedBodyRejected) {
  const auto Source = tmpFile("deque-range-const-constructor-owned-body.cpp");
  writeFile(Source, R"r19dqrange(#include <deque>
namespace std { inline namespace __1 {
template<> template<> deque<int>::deque(const int* first,const int* last) : deque() {}
}}
int main(){int values[3]={1,2,3};const int* first=values;
std::deque<int> a(first,first+3);return a.size()!=0;}
)r19dqrange");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeRangeConstAssignmentOwnedBodyRejected) {
  const auto Source = tmpFile("deque-range-const-assign-owned-body.cpp");
  writeFile(Source, R"r19dqrange(#include <deque>
namespace std { inline namespace __1 {
template<> template<> void deque<int>::assign<const int*,0>(const int* first,const int* last) {}
}}
int main(){int values[3]={1,2,3};const int* first=values;
std::deque<int> a;a.assign(first,first+3);return a.size()!=0;}
)r19dqrange");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
