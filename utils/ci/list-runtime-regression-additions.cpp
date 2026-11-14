
TEST_F(TranslateTest, CoreV2ListIntEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("list-empty-layout.cpp");
  const auto Output = tmpFile("list-empty-layout.nc");
  writeFile(Source, R"r19list(#include <list>
int main(){std::list<int> v;std::list<int>::iterator i,j;
std::list<int>::const_iterator a,b;const std::list<int>& c=v;
return sizeof(v)!=24||alignof(std::list<int>)!=8||sizeof(i)!=8||!v.empty()||v.size()!=0||i!=j||a!=b||c.begin()!=c.end();}
)r19list");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-empty-layout" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListIntIteratorsRunAtBothOptimizations) {
  const auto Source = tmpFile("list-iterator-layout.cpp");
  const auto Output = tmpFile("list-iterator-layout.nc");
  writeFile(Source, R"r19list(#include <list>
int main(){std::list<int> v{1,2,3};auto i=v.begin();*i=4;
auto a=i++;auto b=v.end();--b;--i;std::list<int>::const_iterator c=i;
auto d=c++;auto e=c--;auto j=i;j=b;
return sizeof(c)!=8||alignof(decltype(c))!=8||*a!=4||*b!=3||*d!=4||*e!=2||*c!=4||*j!=3;}
)r19list");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-iterator-layout" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListIntNodeOperationsRunAtBothOptimizations) {
  const auto Source = tmpFile("list-original-runtime.cpp");
  const auto Output = tmpFile("list-original-runtime.nc");
  writeFile(Source, R"r19list(#include <list>
int main(){std::list<int> v{2,3};v.push_front(1);v.push_back(4);v.pop_front();v.pop_back();
return v.size()!=2||v.front()!=2||v.back()!=3;}
)r19list");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-original-runtime" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListIntReferencesRunAtBothOptimizations) {
  const auto Source = tmpFile("list-reference-runtime.cpp");
  const auto Output = tmpFile("list-reference-runtime.nc");
  writeFile(Source, R"r19list(#include <list>
int main(){std::list<int> v{2,3};auto i=v.insert(v.cbegin(),1);auto n=v.erase(i);
if(*n!=2||v.size()!=2)return 1;v.push_front(v.front());v.back()=4;
const std::list<int>& c=v;if(c.front()!=2||c.back()!=4)return 2;
v.clear();v.push_back(8);return v.size()!=1||v.front()!=8||v.back()!=8;}
)r19list");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-reference-runtime" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListIntAllocationReferencesRunAtBothOptimizations) {
  const auto Source = tmpFile("list-live-allocation-reference.cpp");
  const auto Output = tmpFile("list-live-allocation-reference.nc");
  writeFile(Source, R"r19list(using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <new>
#include <list>
static int* watched;
static int allocations,releases,changed,bad_size;
void* operator new(Size n){++allocations;if(n!=24)bad_size=1;
if(watched){*watched=7;changed=1;}return ::malloc(n);}
void operator delete(void* p) noexcept {++releases;::free(p);}
int main(){int bad=0;{std::list<int> v{1,2};
watched=&v.front();v.push_front(v.front());watched=nullptr;
int expected=changed?7:1;auto i=v.begin();
if(*i++!=expected||*i++!=expected||*i!=2||v.size()!=3)bad=1;
}return bad||bad_size||allocations!=releases||allocations>3;}
)r19list");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-live-allocation-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListIntDeleteOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("list-selected-delete-order.cpp");
  const auto Output = tmpFile("list-selected-delete-order.nc");
  writeFile(Source, R"r19list(using Size=decltype(sizeof(0));
extern "C" void* malloc(Size);
extern "C" void free(void*);
#include <new>
#include <list>
static std::list<int>* observed;
static int mode,bad,allocations,releases,reinserted,clear_inserted;
void* operator new(Size n){++allocations;if(n!=24)bad=1;return ::malloc(n);}
void operator delete(void* p) noexcept {++releases;
if(observed){
if(mode==1&&(observed->size()!=2||observed->front()!=2))bad=1;
if(mode==2){if(observed->size()!=1||observed->front()!=3)bad=1;
observed->push_front(9);reinserted=1;}
if(mode==3){if(!clear_inserted){if(!observed->empty()||observed->size()!=0)bad=1;
observed->push_back(8);clear_inserted=1;}
else if(observed->size()!=1||observed->front()!=8)bad=1;}
}::free(p);}
int main(){{std::list<int> v{1,2,3};observed=&v;
mode=1;v.pop_front();mode=2;auto i=v.erase(v.cbegin());
if(*i!=3||v.front()!=(reinserted?9:3))bad=1;
mode=3;v.clear();if(clear_inserted){if(v.size()!=1||v.front()!=8||v.back()!=8)bad=1;}
else if(!v.empty())bad=1;observed=nullptr;mode=0;
}return bad||allocations!=releases||allocations>5;}
)r19list");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("list-selected-delete-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2ListOwnedPushRejected) {
  const auto Source = tmpFile("list-owned-push-body.cpp");
  writeFile(Source, R"r19list(#include <list>
namespace std { inline namespace __1 {
template<> inline void list<int>::push_front(const int&) {}
}}
int main(){std::list<int> v;int x=1;v.push_front(x);return v.empty();}
)r19list");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListOwnedFrontMacroRejected) {
  const auto Source = tmpFile("list-owned-front-macro.cpp");
  writeFile(Source, R"r19list(#define front owned_front
#include <list>
int main(){std::list<int> v{1};return v.front()!=1;}
)r19list");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListOwnedNodeMacroRejected) {
  const auto Source = tmpFile("list-owned-next-macro.cpp");
  writeFile(Source, R"r19list(#define __next_ owned_next
#include <list>
int main(){std::list<int> v{1};return v.front()!=1;}
)r19list");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2ListOwnedEmptyRejected) {
  const auto Source = tmpFile("list-owned-empty-body.cpp");
  writeFile(Source, R"r19list(#include <list>
namespace std { inline namespace __1 {
template<> inline bool list<int>::empty() const noexcept {return true;}
}}
int main(){std::list<int> v;return !v.empty();}
)r19list");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
