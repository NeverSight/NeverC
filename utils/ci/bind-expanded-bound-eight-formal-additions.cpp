
TEST_F(TranslateTest, CoreV2BindBoundPackThreeValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-bound-pack-three-values.cpp");
  const auto Output = tmpFile("bind-bound-pack-three-values.nc");
  writeFile(Source, R"cpp(#include <functional>
int sum(int a,int b,int c){return a+b+c;}
int main(){auto f=std::bind(&sum,2,3,5);return f()!=10;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-bound-pack-three-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindBoundPackFourMixedRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-bound-pack-four-mixed.cpp");
  const auto Output = tmpFile("bind-bound-pack-four-mixed.nc");
  writeFile(Source, R"cpp(#include <functional>
double combine(int a,double b,bool c,int* p){*p+=a;return b+*p+(c?1:0);}
int main(){int n=2;auto f=std::bind(&combine,3,0.5,true,&n);return f()!=6.5||n!=5;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-bound-pack-four-mixed" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindBoundPackEightValuesRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-bound-pack-eight-values.cpp");
  const auto Output = tmpFile("bind-bound-pack-eight-values.nc");
  writeFile(Source, R"cpp(#include <functional>
int sum(int a,int b,int c,int d,int e,int f,int g,int h){return a+b+c+d+e+f+g+h;}
int main(){auto f=std::bind(&sum,1,2,3,4,5,6,7,8);return f()!=36;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-bound-pack-eight-values" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindBoundPackThirdReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-bound-pack-third-reference.cpp");
  const auto Output = tmpFile("bind-bound-pack-third-reference.nc");
  writeFile(Source, R"cpp(#include <functional>
int add_to(int a,int b,int& n){n+=a+b;return n;}
int main(){int n=5;auto f=std::bind(&add_to,2,3,std::ref(n));const auto copy=f;return f()!=10||copy()!=15||f((n=20))!=25||n!=25;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-bound-pack-third-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindBoundPackCopiedStateRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-bound-pack-copied-state.cpp");
  const auto Output = tmpFile("bind-bound-pack-copied-state.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int advance(int a,int b,int& c){return a+b+ ++c;}
int main(){auto f=std::bind(&advance,4,5,6);auto copy=f;return f()!=16||std::move(f)()!=17||copy()!=16||f()!=18;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-bound-pack-copied-state" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindBoundPackCaptureEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-bound-pack-capture-effects.cpp");
  const auto Output = tmpFile("bind-bound-pack-capture-effects.nc");
  writeFile(Source, R"cpp(#include <functional>
int left_builds,middle_builds,right_builds,calls;
int left(){++left_builds;return 3;}int middle(){++middle_builds;return 5;}int right(){++right_builds;return 7;}
int sum(int a,int b,int c){++calls;return a+b+c;}
int main(){auto f=std::bind(&sum,left(),middle(),right());int unused=0;return left_builds!=1||middle_builds!=1||right_builds!=1||calls!=0||f(++unused,++unused)!=15||unused!=2||calls!=1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-bound-pack-capture-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindBoundPackCapturedPointerRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-bound-pack-captured-pointer.cpp");
  const auto Output = tmpFile("bind-bound-pack-captured-pointer.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int first(int a,int b,int c){return a+b+c;}int second(int,int,int){return -1;}
int main(){int(*pointer)(int,int,int)=&first;auto f=std::bind(pointer,2,3,5);pointer=&second;const auto copy=f;return f()!=10||copy()!=10||std::move(f)()!=10||pointer(2,3,5)!=-1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-bound-pack-captured-pointer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindBoundPackResultKindsRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-bound-pack-result-kinds.cpp");
  const auto Output = tmpFile("bind-bound-pack-result-kinds.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <type_traits>
void store(int* p,int a,int b){*p=a+b;}int& identity(int& n,int,int){return n;}
int main(){int n=0;auto write=std::bind(&store,&n,3,4);write();auto live=std::bind(&identity,std::ref(n),1,2);using F=decltype(live);static_assert(std::is_bind_expression<F>::value,"bind expression");static_assert(std::is_same<decltype(live()),int&>::value,"live reference");static_assert(!std::is_nothrow_invocable<F&>::value,"pinned bind exception signature");if(n!=7)return 1;live()=14;return n!=14||&live()!=&n;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-bound-pack-result-kinds" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}
