
TEST_F(TranslateTest, CoreV2VariantMetadataSizeAndSentinelRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-metadata-size-and-sentinel.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using V = std::variant<int,double,unsigned long long>;
int main() {
  return std::variant_size_v<V> != 3 ||
         std::variant_npos != static_cast<decltype(sizeof(int))>(-1);
}
)r19variant");
  const auto Output = tmpFile("variant-metadata-size-and-sentinel.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-metadata-size-and-sentinel" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMetadataAlternativeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-metadata-alternative.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using V = std::variant<int,double>;
using R = std::variant_alternative_t<1,V>;
R answer(){return 3.5;}
int main(){return answer()!=3.5;}
)r19variant");
  const auto Output = tmpFile("variant-metadata-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-metadata-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMetadataConstAlternativeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-metadata-const-alternative.cpp");
  writeFile(Source, R"r19variant(#include <variant>
#include <type_traits>
using V = std::variant<int,int,float>;
using R = std::variant_alternative_t<1,const V>;
static_assert(std::is_same_v<R,const int>);
int main(){R value=17;return value!=17;}
)r19variant");
  const auto Output = tmpFile("variant-metadata-const-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-metadata-const-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMetadataNestedAlternativeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-metadata-nested-alternative.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using Inner = std::variant<int,float>;
using Outer = std::variant<Inner,char>;
using Selected = std::variant_alternative_t<0,Outer>;
int main(){return std::variant_size_v<Selected>!=2;}
)r19variant");
  const auto Output = tmpFile("variant-metadata-nested-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-metadata-nested-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMetadataSizeQualificationRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-metadata-size-qualification.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using V = std::variant<int,char>;
int main(){return std::variant_size_v<const V>!=2;}
)r19variant");
  const auto Output = tmpFile("variant-metadata-size-qualification.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-metadata-size-qualification" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMetadataWrittenIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-metadata-written-index.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using V = std::variant<int,float,unsigned long>;
constexpr unsigned last(){return 3-1;}
using R = std::variant_alternative_t<last(),V>;
int main(){R v=19UL;return v!=19UL;}
)r19variant");
  const auto Output = tmpFile("variant-metadata-written-index.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-metadata-written-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantLayoutMixedScalarRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-layout-mixed-scalar.cpp");
  writeFile(Source, R"r19variantlayout(#include <variant>
using V = std::variant<int, double>;
int main() { return sizeof(V) != 2 * sizeof(double) || alignof(V) != alignof(double); }
)r19variantlayout");
  const auto Output = tmpFile("variant-layout-mixed-scalar.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-layout-mixed-scalar" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantLayoutReferenceAndConstRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-layout-reference-and-const.cpp");
  writeFile(Source, R"r19variantlayout(#include <variant>
using V = std::variant<long long, char>;
int main() { return sizeof(const V&) != sizeof(V) || alignof(const V&) != alignof(long long); }
)r19variantlayout");
  const auto Output = tmpFile("variant-layout-reference-and-const.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-layout-reference-and-const" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantLayoutPointerAlternativeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-layout-pointer-alternative.cpp");
  writeFile(Source, R"r19variantlayout(#include <variant>
using V = std::variant<int*, bool>;
int main() { return sizeof(V) != 2 * sizeof(int*) || alignof(V) != alignof(int*); }
)r19variantlayout");
  const auto Output = tmpFile("variant-layout-pointer-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-layout-pointer-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantLayoutDuplicateTypesRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-layout-duplicate-types.cpp");
  writeFile(Source, R"r19variantlayout(#include <variant>
using V = std::variant<double, int, double>;
int main() { return sizeof(V) != 2 * sizeof(double) || alignof(V) != alignof(double); }
)r19variantlayout");
  const auto Output = tmpFile("variant-layout-duplicate-types.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-layout-duplicate-types" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantLayoutDeclarationIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-layout-declaration-index.cpp");
  writeFile(Source, R"r19variantlayout(#include <variant>
using V = std::variant<int, double>;
constexpr auto Width = sizeof(V);
int main() { unsigned char bytes[Width]{}; return sizeof(bytes) != 2 * sizeof(double); }
)r19variantlayout");
  const auto Output = tmpFile("variant-layout-declaration-index.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-layout-declaration-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

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

TEST_F(TranslateTest, CoreV2AnyEmptyDefaultLocalRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-default-local.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() {
  std::any a;
  if (a.has_value()) return 1;
  a.reset();
  a.reset();
  return a.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-default-local.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-default-local" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyReceiverEvaluationRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-receiver-evaluation.cpp");
  writeFile(Source, R"r19any(#include <any>
int visits;
std::any* receive(std::any* p) { ++visits; return p; }
int main() {
  std::any a;
  receive(&a)->reset();
  const std::any* p = receive(&a);
  return visits != 2 || p->has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-receiver-evaluation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-receiver-evaluation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyTemporaryRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-temporary.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() {
  return std::any{}.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-temporary.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-temporary" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyMemberLifetimeRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-member-lifetime.cpp");
  writeFile(Source, R"r19any(#include <any>
struct Owner { std::any any; int marker; };
int main() {
  Owner object{{}, 2};
  object.any.reset();
  return object.marker != 2 || object.any.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-member-lifetime.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-member-lifetime" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyArrayLifetimeRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-array-lifetime.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() {
  std::any values[2];
  values[1].reset();
  return values[0].has_value() || values[1].has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-array-lifetime.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-array-lifetime" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyUnprovedCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-unproved-copy.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() {
  std::any a;
  std::any b(a);
  return b.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-unproved-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-unproved-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyPointerCastMutableRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-pointer-cast-mutable.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() {
  std::any value;
  int* payload = std::any_cast<int>(&value);
  return payload != nullptr || value.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-pointer-cast-mutable.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-pointer-cast-mutable" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyPointerCastConstRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-pointer-cast-const.cpp");
  writeFile(Source, R"r19any(#include <any>
#include <type_traits>
int main() {
  const std::any value;
  using T = unsigned;
  const auto* pointer = std::any_cast<T>(&value);
  static_assert(std::is_same_v<decltype(pointer), const T*>);
  return pointer != nullptr || std::any_cast<const T>(&value) != nullptr;
}
)r19any");
  const auto Output = tmpFile("any-empty-pointer-cast-const.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-pointer-cast-const" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyPointerCastNullAndEvaluationRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-pointer-cast-null-and-evaluation.cpp");
  writeFile(Source, R"r19any(#include <any>
int calls;
std::any* choose(std::any* value) { ++calls; return value; }
int main() {
  std::any value;
  auto* first = std::any_cast<int>(choose(&value));
  auto* second = std::any_cast<int>(choose(nullptr));
  return first != nullptr || second != nullptr || calls != 2;
}
)r19any");
  const auto Output = tmpFile("any-empty-pointer-cast-null-and-evaluation.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-pointer-cast-null-and-evaluation" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyPointerCastPointerTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-pointer-cast-pointer-type.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() {
  const std::any* value = nullptr;
  int* const* pointer = std::any_cast<int*>(value);
  return pointer != nullptr;
}
)r19any");
  const auto Output = tmpFile("any-empty-pointer-cast-pointer-type.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-pointer-cast-pointer-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyCopyMoveRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-copy-move.cpp");
  writeFile(Source, R"r19any(#include <any>
#include <utility>
int main() {
  std::any original;
  std::any moved(std::move(original));
  return original.has_value() || moved.has_value() || std::any_cast<int>(&moved);
}
)r19any");
  const auto Output = tmpFile("any-empty-copy-move.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-copy-move" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyCopyCopyEffectsRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-copy-copy-effects.cpp");
  writeFile(Source, R"r19any(#include <any>
int calls;
const std::any& observe(const std::any& value) { ++calls; return value; }
int main() {
  std::any original;
  std::any copy(observe(original));
  return calls != 1 || copy.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-copy-copy-effects.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-copy-copy-effects" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyAssignmentCopyIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-assignment-copy-identity.cpp");
  writeFile(Source, R"r19any(#include <any>
int main() {
  std::any target;
  const std::any source;
  return &(target = source) != &target || target.has_value() || source.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-assignment-copy-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-assignment-copy-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyAssignmentOperatorNotationOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-assignment-operator-notation-order.cpp");
  writeFile(Source, R"r19any(#include <any>
int effects;
std::any& select(std::any& value, int number) { effects = effects * 10 + number; return value; }
int main() {
  std::any first, second;
  auto& result = (select(first, 1) = select(second, 2));
  return effects != 21 || &result != &first || first.has_value() || second.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-assignment-operator-notation-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-assignment-operator-notation-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyAssignmentExplicitAssignmentOrderRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-assignment-explicit-assignment-order.cpp");
  writeFile(Source, R"r19any(#include <any>
int effects;
std::any* select(std::any* value, int number) { effects = effects * 10 + number; return value; }
int main() {
  std::any first, second;
  auto& result = select(&first, 1)->operator=(*select(&second, 2));
  return effects != 12 || &result != &first || first.has_value() || second.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-assignment-explicit-assignment-order.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-assignment-explicit-assignment-order" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyAssignmentMoveAndSelfRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-assignment-move-and-self.cpp");
  writeFile(Source, R"r19any(#include <any>
#include <utility>
int main() {
  std::any first, second;
  return &(first = std::move(second)) != &first || &(first = std::move(first)) != &first ||
         first.has_value() || second.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-assignment-move-and-self.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-assignment-move-and-self" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AnyEmptyAssignmentSwapSequencingRunAtBothOptimizations) {
  const auto Source = tmpFile("any-empty-assignment-swap-sequencing.cpp");
  writeFile(Source, R"r19any(#include <any>
int effects;
std::any& select(std::any& value, int number) { effects = effects * 10 + number; return value; }
int main() {
  std::any first, second;
  select(first, 1).swap(select(second, 2));
  first.swap(first);
  return effects != 12 || first.has_value() || second.has_value();
}
)r19any");
  const auto Output = tmpFile("any-empty-assignment-swap-sequencing.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("any-empty-assignment-swap-sequencing" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMetadataRuntimeValueRejected) {
  const auto Source = tmpFile("variant-metadata-runtime-value.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using V = std::variant<int,double>;
int main(){V value(7);return value.index();}
)r19variant");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMetadataVariantSizeStorageRejected) {
  const auto Source = tmpFile("variant-metadata-variant-size-storage.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using V=std::variant<int,double>;
int main(){const auto *p=&std::variant_size_v<V>;return *p!=2;}
)r19variant");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMetadataSentinelStorageRejected) {
  const auto Source = tmpFile("variant-metadata-sentinel-storage.cpp");
  writeFile(Source, R"r19variant(#include <variant>
int main(){auto p=&std::variant_npos;return *p==0;}
)r19variant");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMetadataSourceSpecializationRejected) {
  const auto Source = tmpFile("variant-metadata-source-specialization.cpp");
  writeFile(Source, R"r19variant(#include <variant>
struct Record {};
namespace std { template<> struct variant_size<Record> : integral_constant<size_t,1> {}; }
int main(){return std::variant_size_v<Record>!=1;}
)r19variant");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMetadataVolatileAlternativeRejected) {
  const auto Source = tmpFile("variant-metadata-volatile-alternative.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using V = std::variant<int,volatile int>;
int main(){return std::variant_size_v<V>!=2;}
)r19variant");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMetadataDirectVolatileAlternativeRejected) {
  const auto Source = tmpFile("variant-metadata-direct-volatile-alternative.cpp");
  writeFile(Source, R"r19variant(#include <variant>
int main() { return std::variant_size_v<std::variant<int, volatile int>> != 2; }
)r19variant");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantLayoutAlteredIndexMemberRejected) {
  const auto Source = tmpFile("variant-layout-altered-index-member.cpp");
  writeFile(Source, R"r19variantlayout(#define __index substituted_index
#include <variant>
#undef __index
using V = std::variant<int, double>;
int main() { return sizeof(V) == 0; }
)r19variantlayout");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantLayoutUnsupportedLargeFloatRejected) {
  const auto Source = tmpFile("variant-layout-unsupported-large-float.cpp");
  writeFile(Source, R"r19variantlayout(#include <variant>
using V = std::variant<int, long double>;
int main() { return sizeof(V) == 0; }
)r19variantlayout");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantLayoutRuntimeRecordRejected) {
  const auto Source = tmpFile("variant-layout-runtime-record.cpp");
  writeFile(Source, R"r19variantlayout(#include <variant>
using V = std::variant<int, double>;
int main() { V value{9}; return sizeof(value) == 0; }
)r19variantlayout");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
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

TEST_F(TranslateTest, CoreV2AnyEmptyAlteredObserverRejected) {
  const auto Source = tmpFile("any-empty-altered-observer.cpp");
  writeFile(Source, R"r19any(#define has_value changed_observer
#include <any>
#undef has_value
int main() {
  std::any a;
  return a.changed_observer();
}
)r19any");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyEmptyAlteredResetRejected) {
  const auto Source = tmpFile("any-empty-altered-reset.cpp");
  writeFile(Source, R"r19any(#define reset changed_reset
#include <any>
#undef reset
int main() {
  std::any a;
  a.changed_reset();
  return a.has_value();
}
)r19any");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyEmptyPointerCastValueFormRejected) {
  const auto Source = tmpFile("any-empty-pointer-cast-value-form.cpp");
  writeFile(Source, R"r19any(#include <any>
int f() {
  std::any value;
  return std::any_cast<int>(value);
}
)r19any");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyEmptyPointerCastUserRedeclarationRejected) {
  const auto Source = tmpFile("any-empty-pointer-cast-user-redeclaration.cpp");
  writeFile(Source, R"r19any(#include <any>
_LIBCPP_BEGIN_NAMESPACE_STD
template <class T> add_pointer_t<T> any_cast(any*) noexcept;
_LIBCPP_END_NAMESPACE_STD
int f() {
  std::any value;
  return std::any_cast<int>(&value) != nullptr;
}
)r19any");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AnyEmptyAssignmentRenamedSwapRejected) {
  const auto Source = tmpFile("any-empty-assignment-renamed-swap.cpp");
  writeFile(Source, R"r19any(#define swap changed_swap
#include <any>
#undef swap
int f() {
  std::any first, second;
  first = second;
  return first.has_value();
}
)r19any");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
