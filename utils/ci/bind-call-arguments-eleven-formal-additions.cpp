
TEST_F(TranslateTest, CoreV2BindCallUnusedPrvalueRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-prvalue.cpp");
  const auto Output = tmpFile("bind-call-unused-prvalue.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int twice(int n){return n*2;}
int main(){auto f=std::bind(&twice,21);return f(99)!=42;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-prvalue" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedCvLvaluesRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-cv-lvalues.cpp");
  const auto Output = tmpFile("bind-call-unused-cv-lvalues.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int plus(int a,int b){return a+b;}
int main(){int n=8;const int c=9;auto f=std::bind(&plus,3,4);return f(n,c)!=7||n!=8;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-cv-lvalues" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedEffectsOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-effects-once.cpp");
  const auto Output = tmpFile("bind-call-unused-effects-once.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int bump(int& n){return ++n;}
int plus(int a,int b){return a+b;}
int main(){int a=0,b=0;auto f=std::bind(&plus,3,4);int r=f(bump(a),bump(b));return r!=7||a!=1||b!=1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-effects-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedUpdatesReferentRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-updates-referent.cpp");
  const auto Output = tmpFile("bind-call-unused-updates-referent.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int plus(int a,int b){return a+b;}
int main(){int n=4;auto f=std::bind(&plus,std::ref(n),3);int r=f((n=9));return r!=12||n!=9;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-updates-referent" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedSameBindReentryRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-same-bind-reentry.cpp");
  const auto Output = tmpFile("bind-call-unused-same-bind-reentry.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int bump(int& n){return ++n;}
int main(){auto f=std::bind(&bump,4);int r=f(f());int s=f();return r!=6||s!=7;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-same-bind-reentry" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedConstReceiverRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-const-receiver.cpp");
  const auto Output = tmpFile("bind-call-unused-const-receiver.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int plus(int a,int b){return a+b;}
int main(){const auto f=std::bind(&plus,3,4);return f(9)!=7;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-const-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedRvalueReceiverRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-rvalue-receiver.cpp");
  const auto Output = tmpFile("bind-call-unused-rvalue-receiver.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int bump(int& n){return ++n;}
int main(){auto f=std::bind(&bump,4);int calls=0;int r=std::move(f)(++calls);int s=f();return r!=5||s!=6||calls!=1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-rvalue-receiver" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedVoidTargetRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-void-target.cpp");
  const auto Output = tmpFile("bind-call-unused-void-target.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
void add(int& n,int x){n+=x;}
int main(){int n=1,a=0,b=0;auto f=std::bind(&add,std::ref(n),4);f(++a,++b);return n!=5||a!=1||b!=1;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-void-target" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedReferenceResultRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-reference-result.cpp");
  const auto Output = tmpFile("bind-call-unused-reference-result.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int& identity(int& n){return n;}
int main(){int n=1;auto f=std::bind(&identity,std::ref(n));int& r=f((n=11));r=12;return n!=12;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-reference-result" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallReceiverBeforeUnusedEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-receiver-before-unused-effects.cpp");
  const auto Output = tmpFile("bind-call-receiver-before-unused-effects.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int bump(int& n){return ++n;}
int main(){auto a=std::bind(&bump,4);auto b=std::bind(&bump,10);auto* p=&a;int r=(*p)((p=&b,0));int s=a();int t=b();return r!=5||s!=6||t!=11||p!=&b;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-receiver-before-unused-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2BindCallUnusedRvalueScalarRunAtBothOptimizations) {
  const auto Source = tmpFile("bind-call-unused-rvalue-scalar.cpp");
  const auto Output = tmpFile("bind-call-unused-rvalue-scalar.nc");
  writeFile(Source, R"cpp(#include <functional>
#include <utility>
int twice(int n){return n*2;}
int main(){int n=9;auto f=std::bind(&twice,21);return f(std::move(n))!=42||n!=9;}
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("bind-call-unused-rvalue-scalar" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}
