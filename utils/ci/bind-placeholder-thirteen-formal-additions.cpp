
// Placeholder runtime goldens: proposed until current source and generated executions pass.

TEST_F(TranslateTest, CoreV2FactoryPlaceholderOneRunAtBothOptimizations) {
  const auto Source = tmpFile("factory-placeholder-one.cpp");
  const auto Output = tmpFile("factory-placeholder-one.nc");
  writeFile(Source, R"cpp(#include <functional>
int identity(int n){return n;}
int main(){auto f=std::bind(&identity,std::placeholders::_1);return 0;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("factory-placeholder-one" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2FactoryPlaceholderParenthesesRunAtBothOptimizations) {
  const auto Source = tmpFile("factory-placeholder-parentheses.cpp");
  const auto Output = tmpFile("factory-placeholder-parentheses.nc");
  writeFile(Source, R"cpp(#include <functional>
int sum(int a,int b){return a+b;}
int main(){auto f=std::bind(&sum,(std::placeholders::_2),((std::placeholders::_1)));return 0;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("factory-placeholder-parentheses" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2FactoryPlaceholderRepeatedRunAtBothOptimizations) {
  const auto Source = tmpFile("factory-placeholder-repeated.cpp");
  const auto Output = tmpFile("factory-placeholder-repeated.nc");
  writeFile(Source, R"cpp(#include <functional>
int sum(int a,int b){return a+b;}
int main(){auto f=std::bind(&sum,std::placeholders::_1,std::placeholders::_1);return 0;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("factory-placeholder-repeated" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2FactoryPlaceholderMixedReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("factory-placeholder-mixed-reference.cpp");
  const auto Output = tmpFile("factory-placeholder-mixed-reference.nc");
  writeFile(Source, R"cpp(#include <functional>
int combine(int& n,int a,int b){return n+a+b;}
int main(){int n=2;auto f=std::bind(&combine,std::ref(n),4,std::placeholders::_1);return n!=2;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("factory-placeholder-mixed-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2FactoryPlaceholderEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("factory-placeholder-effects.cpp");
  const auto Output = tmpFile("factory-placeholder-effects.nc");
  writeFile(Source, R"cpp(#include <functional>
using Callback=int(*)(int,int);int selections,captures;int sum(int a,int b){return a+b;}Callback select(){++selections;return &sum;}int capture(){++captures;return 7;}
int main(){auto f=std::bind(select(),capture(),std::placeholders::_1);return selections!=1||captures!=1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("factory-placeholder-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindPlaceholderOneRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-placeholder-one.cpp");
  const auto Output = tmpFile("bind-placeholder-one.nc");
  writeFile(Source, R"cpp(#include <functional>
int twice(int n){return n*2;}
int main(){auto f=std::bind(&twice,std::placeholders::_1);return f(9)!=18;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-placeholder-one" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindPlaceholderReorderedRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-placeholder-reordered.cpp");
  const auto Output = tmpFile("bind-placeholder-reordered.nc");
  writeFile(Source, R"cpp(#include <functional>
int encode(int a,int b){return 10*a+b;}
int main(){auto f=std::bind(&encode,std::placeholders::_2,std::placeholders::_1);const auto copy=f;return f(3,7)!=73||copy(4,8)!=84;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-placeholder-reordered" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindPlaceholderRepeatedOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-placeholder-repeated-once.cpp");
  const auto Output = tmpFile("bind-placeholder-repeated-once.nc");
  writeFile(Source, R"cpp(#include <functional>
int sum(int a,int b){return a+b;}
int main(){int n=5;auto f=std::bind(&sum,std::placeholders::_1,std::placeholders::_1);return f(++n)!=12||n!=6;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-placeholder-repeated-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindPlaceholderIndependentBoundStateRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-placeholder-independent-bound-state.cpp");
  const auto Output = tmpFile("bind-placeholder-independent-bound-state.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int advance(int& n,int step){n+=step;return n;}
int main(){auto f=std::bind(&advance,1,std::placeholders::_1);auto copy=f;return f(2)!=3||std::move(f)(3)!=6||copy(2)!=3;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-placeholder-independent-bound-state" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindPlaceholderBorrowedStateRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-placeholder-borrowed-state.cpp");
  const auto Output = tmpFile("bind-placeholder-borrowed-state.nc");
  writeFile(Source, R"cpp(#include <functional>
int advance(int& n,int step){n+=step;return n;}
int main(){int n=5;auto f=std::bind(&advance,std::ref(n),std::placeholders::_1);const auto copy=f;return f(2)!=7||copy(3)!=10||f((n=20,4))!=24||n!=24;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-placeholder-borrowed-state" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindPlaceholderUnusedEffectsBeforeReadRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-placeholder-unused-effects-before-read.cpp");
  const auto Output = tmpFile("bind-placeholder-unused-effects-before-read.nc");
  writeFile(Source, R"cpp(#include <functional>
int read(int n){return n;}
int main(){int n=0;auto f=std::bind(&read,std::placeholders::_1);return f(++n,++n)!=2||n!=2;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-placeholder-unused-effects-before-read" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindPlaceholderLiveReferenceRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-placeholder-live-reference.cpp");
  const auto Output = tmpFile("bind-placeholder-live-reference.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int& identity(int& n){return n;}
int main(){int n=5;auto f=std::bind(&identity,std::placeholders::_1);const auto copy=f;f(n)=9;return &copy(n)!=&n||std::move(f)(n)!=9;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-placeholder-live-reference" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindPlaceholderCaptureAndQueriesRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-placeholder-capture-and-queries.cpp");
  const auto Output = tmpFile("bind-placeholder-capture-and-queries.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <type_traits>
using Callback=int(*)(int);int selections,calls;int first(int n){++calls;return n+1;}int second(int){return -1;}Callback target=&first;Callback select(){++selections;return target;}
int main(){auto f=std::bind(select(),std::placeholders::_2);using F=decltype(f);static_assert(!std::is_invocable<F&,int>::value,"second call argument required");static_assert(std::is_invocable<F&,int,int>::value,"two call arguments");static_assert(std::is_same<decltype(f(3,4)),int>::value,"scalar result");static_assert(!std::is_nothrow_invocable<F&,int,int>::value,"pinned exception signature");target=&second;return selections!=1||calls!=0||f(3,4)!=5||calls!=1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-placeholder-capture-and-queries" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}
