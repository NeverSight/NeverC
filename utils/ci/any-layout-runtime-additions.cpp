
TEST_F(TranslateTest, CoreV2AnyLayoutDirectSizeAlignRunAtBothOptimizations) {
  const auto Source = tmpFile("any-layout-direct-size-align.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() {
  return sizeof(std::any) != 4 * sizeof(void*) || alignof(std::any) != alignof(void*);
}
)r19any");
  const auto Output = tmpFile("any-layout-direct-size-align.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-layout-direct-size-align" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyLayoutConstReferenceSizeRunAtBothOptimizations) {
  const auto Source = tmpFile("any-layout-const-reference-size.cpp");
  writeFile(Source, R"r19any(#include <any>
using A = std::any;
int main() { return sizeof(const A&) != sizeof(A) || alignof(const A&) != alignof(void*); }
)r19any");
  const auto Output = tmpFile("any-layout-const-reference-size.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-layout-const-reference-size" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyLayoutWrappedPointerTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("any-layout-wrapped-pointer-type.cpp");
  writeFile(Source, R"r19any(#include <any>
#include <type_traits>
using A = const std::any;
static_assert(std::is_same_v<A*, const std::any*>);
int main() { return sizeof(A) != 4 * sizeof(void*); }
)r19any");
  const auto Output = tmpFile("any-layout-wrapped-pointer-type.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-layout-wrapped-pointer-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyLayoutConstantArrayBoundRunAtBothOptimizations) {
  const auto Source = tmpFile("any-layout-constant-array-bound.cpp");
  writeFile(Source, R"r19any(#include <any>
constexpr auto Count = sizeof(std::any);
int main() { unsigned char bytes[Count]{}; return sizeof(bytes) != 4 * sizeof(void*); }
)r19any");
  const auto Output = tmpFile("any-layout-constant-array-bound.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-layout-constant-array-bound" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyLayoutVolatileTypeRejected) {
  const auto Source = tmpFile("any-layout-volatile-type.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() { return sizeof(volatile std::any) == 0; }
)r19any");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyLayoutRuntimeConstructionRejected) {
  const auto Source = tmpFile("any-layout-runtime-construction.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() { std::any value{7}; return !value.has_value(); }
)r19any");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyLayoutUnevaluatedObjectRejected) {
  const auto Source = tmpFile("any-layout-unevaluated-object.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() { return sizeof(std::any{}) == 0; }
)r19any");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyLayoutAlteredHandlerFieldRejected) {
  const auto Source = tmpFile("any-layout-altered-handler-field.cpp");
  writeFile(Source, R"r19any(#define __h_ altered_handler
#include <any>
#undef __h_
int main() { return sizeof(std::any) == 0; }
)r19any");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
