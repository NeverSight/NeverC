
TEST_F(TranslateTest, CoreV2DequeCountDefaultConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-count-default-construction.cpp");
  const auto Output = tmpFile("deque-count-default-construction.nc");
  writeFile(Source, R"r19dqcount(#include <deque>
int basic_count_contract(){unsigned counts[]={0,1,1023,1024,1025,2049,3073};
for(unsigned k=0;k!=7;++k){std::deque<int> a(counts[k]);
if(a.size()!=counts[k])return 1;
for(unsigned i=0;i!=counts[k];++i)if(a[i]!=0)return 2;
if(!a.empty()){a.front()=7;a.back()=9;if(a.back()!=9)return 3;}
a.push_back(31);if(a.back()!=31||a.size()!=counts[k]+1)return 4;}
return 0;}

using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
#include <deque>
static unsigned countfx_size=2050;static int countfx_armed,countfx_calls;static Size countfx_bytes[16];
void* operator new(Size n){void* p=malloc(n);if(countfx_armed){int i=countfx_calls++;if(i<16)countfx_bytes[i]=n;countfx_size=1;}return p;}
void operator delete(void* p) noexcept {free(p);}void operator delete(void* p,Size) noexcept {free(p);}
int observable_count_contract(){countfx_armed=1;std::deque<int> a(countfx_size);countfx_armed=0;
if(countfx_size!=1||a.size()!=2050||countfx_calls!=4||countfx_bytes[0]!=24)return 1;
for(int i=1;i!=4;++i)if(countfx_bytes[i]!=4096)return 2;
for(unsigned i=0;i!=a.size();++i)if(a[i]!=0)return 3;
return 0;}

int main(){int r=basic_count_contract();return r?r:observable_count_contract();}
)r19dqcount");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-count-default-construction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeCountFillConstructionRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-count-fill-construction.cpp");
  const auto Output = tmpFile("deque-count-fill-construction.nc");
  writeFile(Source, R"r19dqcount(#include <deque>
int basic_count_contract(){unsigned counts[]={0,1,1023,1024,1025,2049,3073};
for(unsigned k=0;k!=7;++k){int value=-31-int(k);const int& fill=value;
std::deque<int> a(counts[k],fill);value=77;
if(a.size()!=counts[k])return 1;
for(unsigned i=0;i!=counts[k];++i)if(a[i]!=-31-int(k))return 2;
a.push_front(41);if(a.front()!=41||a.size()!=counts[k]+1)return 3;}
return 0;}

using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
#include <deque>
static int countfx_armed,countfx_calls,countfx_value=13,countfx_arguments;
const int& countfx_argument(){++countfx_arguments;return countfx_value;}
void* operator new(Size n){void* p=malloc(n);if(countfx_armed)countfx_value=100+countfx_calls++;return p;}
void operator delete(void* p) noexcept {free(p);}void operator delete(void* p,Size) noexcept {free(p);}
int observable_count_contract(){countfx_armed=1;std::deque<int> empty(0,countfx_argument());countfx_armed=0;
if(countfx_calls||countfx_arguments!=1||!empty.empty())return 1;
countfx_armed=1;std::deque<int> a(2050,countfx_argument());countfx_armed=0;
if(countfx_calls!=4||countfx_arguments!=2||countfx_value!=103||a.size()!=2050)return 2;
for(unsigned i=0;i!=a.size();++i)if(a[i]!=103)return 3;
return 0;}

int main(){int r=basic_count_contract();return r?r:observable_count_contract();}
)r19dqcount");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-count-fill-construction" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeCountResizeDefaultRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-count-resize-default.cpp");
  const auto Output = tmpFile("deque-count-resize-default.nc");
  writeFile(Source, R"r19dqcount(#include <deque>
int basic_count_contract(){std::deque<int> a;a.resize(2050);
for(unsigned i=0;i!=2050;++i){if(a[i]!=0)return 1;a[i]=int(i)+1;}
int* first=&a.front();int* middle=&a[1023];
a.resize(1024);if(a.size()!=1024||&a.front()!=first||&a[1023]!=middle)return 2;
a.resize(3073);if(a.size()!=3073||&a.front()!=first||&a[1023]!=middle)return 3;
for(unsigned i=0;i!=1024;++i)if(a[i]!=int(i)+1)return 4;
for(unsigned i=1024;i!=3073;++i)if(a[i]!=0)return 5;
a.resize(0);if(!a.empty())return 6;a.resize(1);
return a.front()!=0||a.size()!=1;}

using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
#include <deque>
static std::deque<int>* countfx_owner;
static void* countfx_blocks[16];static int countfx_collect,countfx_armed,countfx_block_count,countfx_releases,countfx_bad;
static int countfx_order[16];static unsigned countfx_expected_size;static int countfx_check_front;
void* operator new(Size n){void* p=malloc(n);if(countfx_collect&&n==4096){int i=countfx_block_count++;if(i<16)countfx_blocks[i]=p;}return p;}
void countfx_release(void* p){if(countfx_armed)for(int i=0;i!=countfx_block_count&&i<16;++i)if(p==countfx_blocks[i]){
int n=countfx_releases++;if(n<16)countfx_order[n]=i;
if(countfx_owner->size()!=countfx_expected_size)countfx_bad=1;
if(countfx_check_front&&countfx_owner->front()!=99)countfx_bad=2;}free(p);}
void operator delete(void* p) noexcept {countfx_release(p);}
void operator delete(void* p,Size) noexcept {countfx_release(p);}
int observable_count_contract(){countfx_collect=1;std::deque<int> a(4100);countfx_collect=0;
if(countfx_block_count!=5)return 1;countfx_owner=&a;countfx_expected_size=0;countfx_armed=1;
a.resize(0);countfx_armed=0;
if(countfx_bad||!a.empty()||countfx_releases!=3)return 2;
for(int i=0;i!=3;++i)if(countfx_order[i]!=4-i)return 3;
a.resize(1);return a.front()!=0;}

int main(){int r=basic_count_contract();return r?r:observable_count_contract();}
)r19dqcount");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-count-resize-default" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeCountResizeFillRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-count-resize-fill.cpp");
  const auto Output = tmpFile("deque-count-resize-fill.nc");
  writeFile(Source, R"r19dqcount(#include <deque>
int basic_count_contract(){std::deque<int> a;for(int i=0;i!=2050;++i)a.push_back(i);
for(int i=1;i!=18;++i)a.push_front(-i);
int* first=&a.front();int* middle=&a[1041];int value=-71;
a.resize(4101,value);value=9;
if(a.size()!=4101||&a.front()!=first||&a[1041]!=middle)return 1;
for(unsigned i=0;i!=2067;++i)if(a[i]!=int(i)-17)return 2;
for(unsigned i=2067;i!=4101;++i)if(a[i]!=-71)return 3;
a.resize(1025,a[1041]);if(a.size()!=1025||&a.front()!=first)return 4;
a.resize(3074,a[1024]);
for(unsigned i=1025;i!=3074;++i)if(a[i]!=1007)return 5;
a.resize(0,33);a.resize(2,44);
return a.size()!=2||a.front()!=44||a.back()!=44;}

using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
#include <deque>
static std::deque<int>* countfx_owner;static int countfx_armed,countfx_calls,countfx_bad;
void* operator new(Size n){void* p=malloc(n);if(countfx_armed){
if(countfx_owner->size()!=1||countfx_owner->front()!=(countfx_calls?47:5))countfx_bad=1;
++countfx_calls;countfx_owner->front()=47;}return p;}
void operator delete(void* p) noexcept {free(p);}void operator delete(void* p,Size) noexcept {free(p);}
int observable_count_contract(){std::deque<int> a;a.push_back(5);int* first=&a.front();countfx_owner=&a;countfx_armed=1;
a.resize(2050,a.front());countfx_armed=0;
if(countfx_bad||!countfx_calls||a.size()!=2050||&a.front()!=first)return 1;
for(unsigned i=0;i!=a.size();++i)if(a[i]!=47)return 2;
return 0;}

int main(){int r=basic_count_contract();return r?r:observable_count_contract();}
)r19dqcount");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-count-resize-fill" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeCountAssignFillRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-count-assign-fill.cpp");
  const auto Output = tmpFile("deque-count-assign-fill.nc");
  writeFile(Source, R"r19dqcount(#include <deque>
int basic_count_contract(){std::deque<int> a;
a.assign(2050,13);if(a.size()!=2050)return 1;
for(unsigned i=0;i!=a.size();++i)if(a[i]!=13)return 2;
for(int i=0;i!=1031;++i)a.pop_front();
a.assign(3073,-19);if(a.size()!=3073)return 3;
for(unsigned i=0;i!=a.size();++i)if(a[i]!=-19)return 4;
a[1024]=23;a.assign(1025,a[1024]);
for(unsigned i=0;i!=a.size();++i)if(a[i]!=23)return 5;
a.assign(0,77);if(!a.empty())return 6;
int value=31;a.assign(1,value);value=9;
return a.size()!=1||a.front()!=31;}

using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
#include <deque>
static std::deque<int>* countfx_owner;static int countfx_armed,countfx_calls,countfx_bad,countfx_fill=7;
void* operator new(Size n){void* p=malloc(n);if(countfx_armed){
if(countfx_owner->size()!=1||countfx_owner->front()!=7)countfx_bad=1;
++countfx_calls;countfx_fill=41;}return p;}
void operator delete(void* p) noexcept {free(p);}void operator delete(void* p,Size) noexcept {free(p);}
int observable_count_contract(){std::deque<int> a;a.push_back(5);int* first=&a.front();countfx_owner=&a;countfx_armed=1;
a.assign(2050,countfx_fill);countfx_armed=0;
if(countfx_bad||!countfx_calls||a.size()!=2050||&a.front()!=first||a.front()!=7)return 1;
for(unsigned i=1;i!=a.size();++i)if(a[i]!=41)return 2;
return 0;}

int main(){int r=basic_count_contract();return r?r:observable_count_contract();}
)r19dqcount");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-count-assign-fill" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeCountCallSequencingRunAtBothOptimizations) {
  const auto Source = tmpFile("deque-count-call-sequencing.cpp");
  const auto Output = tmpFile("deque-count-call-sequencing.nc");
  writeFile(Source, R"r19dqcount(#include <deque>
std::deque<int>* owner;int receivers,counts,values,bad,next_count=1025,fill=17;
std::deque<int>& receive(){++receivers;return *owner;}
unsigned count(){if(receivers!=1)bad=1;++counts;return unsigned(next_count);}
const int& value(){if(receivers!=1)bad=2;++values;return fill;}
int basic_count_contract(){std::deque<int> a;owner=&a;
receive().resize(count(),value());
if(bad||receivers!=1||counts!=1||values!=1||a.size()!=1025)return 1;
for(unsigned i=0;i!=a.size();++i)if(a[i]!=17)return 2;
receivers=counts=values=0;next_count=0;
receive().resize(count(),value());
if(bad||receivers!=1||counts!=1||values!=1||!a.empty())return 3;
receivers=counts=values=0;next_count=2049;fill=-9;
receive().assign(count(),value());
if(bad||receivers!=1||counts!=1||values!=1||a.size()!=2049)return 4;
for(unsigned i=0;i!=a.size();++i)if(a[i]!=-9)return 5;
receivers=counts=values=0;next_count=0;
receive().assign(count(),value());
return bad||receivers!=1||counts!=1||values!=1||!a.empty();}

using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);extern "C" void free(void*);
#include <new>
#include <deque>
static std::deque<int>* countfx_owner;
static void* countfx_blocks[16];static int countfx_collect,countfx_armed,countfx_block_count,countfx_releases,countfx_bad;
static int countfx_order[16];static unsigned countfx_expected_size;static int countfx_check_front;
void* operator new(Size n){void* p=malloc(n);if(countfx_collect&&n==4096){int i=countfx_block_count++;if(i<16)countfx_blocks[i]=p;}return p;}
void countfx_release(void* p){if(countfx_armed)for(int i=0;i!=countfx_block_count&&i<16;++i)if(p==countfx_blocks[i]){
int n=countfx_releases++;if(n<16)countfx_order[n]=i;
if(countfx_owner->size()!=countfx_expected_size)countfx_bad=1;
if(countfx_check_front&&countfx_owner->front()!=99)countfx_bad=2;}free(p);}
void operator delete(void* p) noexcept {countfx_release(p);}
void operator delete(void* p,Size) noexcept {countfx_release(p);}
int observable_count_contract(){countfx_collect=1;std::deque<int> a(4100,7);countfx_collect=0;
if(countfx_block_count!=5)return 1;int* first=&a.front();countfx_owner=&a;
countfx_expected_size=1;countfx_check_front=1;countfx_armed=1;
a.assign(1,99);countfx_armed=0;
if(countfx_bad||a.size()!=1||a.front()!=99||&a.front()!=first||countfx_releases!=3)return 2;
for(int i=0;i!=3;++i)if(countfx_order[i]!=4-i)return 3;
return 0;}

int main(){int r=basic_count_contract();return r?r:observable_count_contract();}
)r19dqcount");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("deque-count-call-sequencing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2DequeCountConstructorOwnedBodyRejected) {
  const auto Source = tmpFile("deque-count-constructor-owned-body.cpp");
  writeFile(Source, R"r19dqcount(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::deque(decltype(sizeof(0)) n) : deque() {}
}}
int main(){std::deque<int> a(3);return a.size()!=0;}
)r19dqcount");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeCountFillConstructorOwnedBodyRejected) {
  const auto Source = tmpFile("deque-count-fill-constructor-owned-body.cpp");
  writeFile(Source, R"r19dqcount(#include <deque>
namespace std { inline namespace __1 {
template<> deque<int>::deque(decltype(sizeof(0)) n,const int& value) : deque() {}
}}
int main(){std::deque<int> a(3,7);return a.size()!=0;}
)r19dqcount");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeCountResizeOwnedBodiesRejected) {
  const auto Source = tmpFile("deque-count-resize-owned-bodies.cpp");
  writeFile(Source, R"r19dqcount(#include <deque>
namespace std { inline namespace __1 {
template<> void deque<int>::resize(decltype(sizeof(0)) n) {}
template<> void deque<int>::resize(decltype(sizeof(0)) n,const int& value) {}
}}
int main(){std::deque<int> a;a.resize(3);a.resize(5,7);return a.size()!=0;}
)r19dqcount");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2DequeCountAssignOwnedBodyRejected) {
  const auto Source = tmpFile("deque-count-assign-owned-body.cpp");
  writeFile(Source, R"r19dqcount(#include <deque>
namespace std { inline namespace __1 {
template<> void deque<int>::assign(decltype(sizeof(0)) n,const int& value) {}
}}
int main(){std::deque<int> a;a.assign(3,7);return a.size()!=0;}
)r19dqcount");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
