TEST_F(TranslateTest, CoreV2ScopedAllocatorIntRunAtBothOptimizations) {
  const auto Source = tmpFile("scoped-allocator-basic.cpp");
  const auto Output = tmpFile("scoped-allocator-basic.nc");
  writeFile(Source, R"scoped(#include <memory>
#include <scoped_allocator>
int main(){std::scoped_allocator_adaptor<std::allocator<int>> a;int *p=a.allocate(2);
a.construct(p,9);a.construct(p+1,4);int n=p[0]+p[1];
a.destroy(p+1);a.destroy(p);a.deallocate(p,2);return n!=13;}
)scoped");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("scoped-allocator-basic" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}
