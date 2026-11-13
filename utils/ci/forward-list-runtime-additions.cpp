
TEST_F(TranslateTest, CoreV2ForwardListIntEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-empty-layout.cpp");
  const auto Output = tmpFile("forward-list-empty-layout.nc");
  writeFile(Source, R"r19forwardlist(#include <forward_list>
int main(){std::forward_list<int> v;std::forward_list<int>::iterator i,j;
const std::forward_list<int>& c=v;
return sizeof(v)!=8||alignof(std::forward_list<int>)!=8||sizeof(i)!=8||!v.empty()||i!=j||c.begin()!=c.end()||c.before_begin()==c.end();}
)r19forwardlist");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-empty-layout" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListIntIteratorsRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-iterator-layout.cpp");
  const auto Output = tmpFile("forward-list-iterator-layout.nc");
  writeFile(Source, R"r19forwardlist(#include <forward_list>
int main(){std::forward_list<int> v{1,2};auto i=v.begin();
std::forward_list<int>::const_iterator c=i;auto d=c++;
auto j=i;++j;i=j;
return sizeof(c)!=8||alignof(decltype(c))!=8||*d!=1||*c!=2||*i!=2||c==v.cend();}
)r19forwardlist");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-iterator-layout" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListIntNodeOperationsRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-original-runtime.cpp");
  const auto Output = tmpFile("forward-list-original-runtime.nc");
  writeFile(Source, R"r19forwardlist(#include <forward_list>
int main(){std::forward_list<int> v{2,3};v.push_front(1);auto p=v.before_begin();
v.insert_after(p,0);v.erase_after(p);v.pop_front();
auto i=v.begin();int a=*i++;int b=*i;return a!=2||b!=3;}
)r19forwardlist");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-original-runtime" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListIntReferencesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-reference-runtime.cpp");
  const auto Output = tmpFile("forward-list-reference-runtime.nc");
  writeFile(Source, R"r19forwardlist(#include <forward_list>
int main(){std::forward_list<int> v{1,2};v.push_front(v.front());
v.front()=3;auto p=v.cbefore_begin();auto i=v.insert_after(p,4);
if(*i!=4||v.front()!=4)return 1;
auto n=v.erase_after(p);if(*n!=3)return 2;
const std::forward_list<int>& c=v;if(c.front()!=3)return 3;
v.clear();v.push_front(9);return v.front()!=9||v.empty();}
)r19forwardlist");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-reference-runtime" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListIntAllocationReferencesRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-live-allocation-reference.cpp");
  const auto Output = tmpFile("forward-list-live-allocation-reference.nc");
  writeFile(Source, R"r19forwardlist(using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <new>
#include <forward_list>
static int* watched;
static int allocations,releases,changed,bad_size;
void* operator new(Size n){++allocations;if(n!=16)bad_size=1;
if(watched){*watched=7;changed=1;}return ::malloc(n);}
void operator delete(void* p) noexcept {++releases;::free(p);}
int main(){int bad=0;{std::forward_list<int> v{1,2};
watched=&v.front();v.push_front(v.front());watched=nullptr;
int expected=changed?7:1;auto i=v.begin();
if(*i++!=expected||*i++!=expected||*i!=2)bad=1;
}return bad||bad_size||allocations!=releases||allocations>3;}
)r19forwardlist");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-live-allocation-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListIntDeleteOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("forward-list-selected-delete-order.cpp");
  const auto Output = tmpFile("forward-list-selected-delete-order.nc");
  writeFile(Source, R"r19forwardlist(using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <new>
#include <forward_list>
static std::forward_list<int>* observed;
static int mode,bad,allocations,releases,reinserted;
void* operator new(Size n){++allocations;if(n!=16)bad=1;return ::malloc(n);}
void operator delete(void* p) noexcept {++releases;
if(observed){
if(mode==1&&(observed->empty()||observed->front()!=2))bad=1;
if(mode==2){if(observed->empty()||observed->front()!=3)bad=1;
observed->push_front(9);reinserted=1;}
if(mode==3&&observed->empty())bad=1;
}::free(p);}
int main(){{std::forward_list<int> v{1,2,3};observed=&v;
mode=1;v.pop_front();mode=2;auto i=v.erase_after(v.cbefore_begin());
int expected=reinserted?9:3;if(*i!=expected||v.front()!=expected)bad=1;
mode=3;v.clear();if(!v.empty())bad=1;
observed=nullptr;mode=0;v.push_front(4);
}return bad||allocations!=releases||allocations>5;}
)r19forwardlist");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("forward-list-selected-delete-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ForwardListOwnedPushRejected) {
  const auto Source = tmpFile("forward-list-owned-push-body.cpp");
  writeFile(Source, R"r19forwardlist(#include <forward_list>
namespace std { inline namespace __1 {
template<> inline void forward_list<int>::push_front(const int&) {}
}}
int main(){std::forward_list<int> v;int x=1;v.push_front(x);return v.empty();}
)r19forwardlist");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListOwnedFrontMacroRejected) {
  const auto Source = tmpFile("forward-list-owned-front-macro.cpp");
  writeFile(Source, R"r19forwardlist(#define front owned_front
#include <forward_list>
int main(){std::forward_list<int> v{1};return v.front()!=1;}
)r19forwardlist");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListOwnedNodeMacroRejected) {
  const auto Source = tmpFile("forward-list-owned-next-macro.cpp");
  writeFile(Source, R"r19forwardlist(#define __next_ owned_next
#include <forward_list>
int main(){std::forward_list<int> v{1};return v.front()!=1;}
)r19forwardlist");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ForwardListOwnedEmptyRejected) {
  const auto Source = tmpFile("forward-list-owned-empty-body.cpp");
  writeFile(Source, R"r19forwardlist(#include <forward_list>
namespace std { inline namespace __1 {
template<> inline bool forward_list<int>::empty() const noexcept {return true;}
}}
int main(){std::forward_list<int> v;return !v.empty();}
)r19forwardlist");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
