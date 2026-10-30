
TEST_F(TranslateTest, CoreV2BindZeroBoundValueRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-zero-bound-value.cpp");
  const auto Output = tmpFile("bind-zero-bound-value.nc");
  writeFile(Source, R"cpp(#include <functional>
int answer(){return 37;}
int main(){auto f=std::bind(&answer);return f()!=37;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-zero-bound-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindZeroBoundDesignatorRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-zero-bound-designator.cpp");
  const auto Output = tmpFile("bind-zero-bound-designator.nc");
  writeFile(Source, R"cpp(#include <functional>
int calls;int answer(){++calls;return 37;}
int main(){auto f=std::bind(answer);return f()!=37||calls!=1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-zero-bound-designator" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindZeroBoundCopyCvrefRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-zero-bound-copy-cvref.cpp");
  const auto Output = tmpFile("bind-zero-bound-copy-cvref.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int calls;int answer(){++calls;return 37;}
int main(){auto f=std::bind(&answer);auto copy=f;const auto fixed=copy;return f()!=37||copy()!=37||fixed()!=37||std::move(copy)()!=37||calls!=4;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-zero-bound-copy-cvref" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindZeroBoundFactoryOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-zero-bound-factory-once.cpp");
  const auto Output = tmpFile("bind-zero-bound-factory-once.nc");
  writeFile(Source, R"cpp(#include <functional>
using Callback=int(*)();int selections;int first(){return 7;}int second(){return 9;}
Callback selected=&first;Callback select(){++selections;return selected;}
int main(){auto f=std::bind(select());selected=&second;return f()!=7||f()!=7||selections!=1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-zero-bound-factory-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindZeroBoundVoidTargetRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-zero-bound-void-target.cpp");
  const auto Output = tmpFile("bind-zero-bound-void-target.nc");
  writeFile(Source, R"cpp(#include <functional>
int calls;void touch(){++calls;}
int main(){const auto f=std::bind(&touch);f();f();return calls!=2;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-zero-bound-void-target" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindZeroBoundReferenceResultRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-zero-bound-reference-result.cpp");
  const auto Output = tmpFile("bind-zero-bound-reference-result.nc");
  writeFile(Source, R"cpp(#include <functional>
int stored=5;int& access(){return stored;}
int main(){auto f=std::bind(&access);f()=17;const auto fixed=f;return &fixed()!=&stored||stored!=17;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-zero-bound-reference-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindZeroBoundUnusedEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-zero-bound-unused-effects.cpp");
  const auto Output = tmpFile("bind-zero-bound-unused-effects.nc");
  writeFile(Source, R"cpp(#include <functional>
int effects;int targets;int bump(){return ++effects;}int answer(){++targets;return 37;}
int main(){auto f=std::bind(&answer);int value=3;const int fixed=4;return f(bump(),bump())!=37||effects!=2||targets!=1||f(value,fixed)!=37||targets!=2||value!=3;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-zero-bound-unused-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindZeroBoundResultKindsRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-zero-bound-result-kinds.cpp");
  const auto Output = tmpFile("bind-zero-bound-result-kinds.nc");
  writeFile(Source, R"cpp(#include <functional>
int stored=5;int* address(){return &stored;}double half(){return 0.5;}
int main(){auto pointer=std::bind(&address);auto real=std::bind(&half);return pointer()!=&stored||*pointer()!=5||real()!=0.5;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-zero-bound-result-kinds" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}
