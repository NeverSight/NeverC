
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

TEST_F(TranslateTest, CoreV2VariantMetadataRuntimeValueRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-metadata-runtime-value.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using V = std::variant<int,double>;
int main(){V value(7);return value.index();}
)r19variant");
  const auto Output = tmpFile("variant-metadata-runtime-value.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-metadata-runtime-value" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsDefaultRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-default.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  std::variant<int, double> value;
  return value.index() != 0 || value.valueless_by_exception();
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-default.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-default" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsSecondRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-second.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  std::variant<int, double> value = 2.5;
  return value.index() != 1 || value.valueless_by_exception();
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-second.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-second" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsThirdLvalueRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-third-lvalue.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  int value = 7;
  const std::variant<char, double, int> item = value;
  return item.index() != 2 || item.valueless_by_exception();
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-third-lvalue.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-third-lvalue" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsCopyRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-copy.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  std::variant<int, double> source = 3.0;
  const std::variant<int, double> copy = source;
  return copy.index() != 1 || copy.valueless_by_exception();
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-copy.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-copy" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsDefaultPointerRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-default-pointer.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  std::variant<int*, bool> value{};
  return (&value)->index() != 0 || (&value)->valueless_by_exception();
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-default-pointer.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-default-pointer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsRefAndSideEffectRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-ref-and-side-effect.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
using Choice = std::variant<int, double>;
int attempts;
const Choice& once(const Choice& choice) { ++attempts; return choice; }
extern "C" int check() {
  Choice choice = 4.0;
  return once(choice).index() != 1 || attempts != 1 ||
         once(choice).valueless_by_exception() || attempts != 2;
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-ref-and-side-effect.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-ref-and-side-effect" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsTemporaryRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-temporary.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  return std::variant<int, double>(1).index() != 0 ||
         std::variant<int, double>(2.5).valueless_by_exception();
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-temporary.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-temporary" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsGetIfAliasRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-get-if-alias.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  std::variant<int, double> value = 3;
  auto *first = std::get_if<0>(&value);
  if (!first || std::get_if<1>(&value) || *first != 3) return 1;
  *first = 11;
  auto *again = std::get_if<int>(&value);
  return again != first || *again != 11;
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-get-if-alias.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-get-if-alias" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsGetIfConstRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-get-if-const.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  using Choice = std::variant<int, double>;
  const Choice value = 2.5;
  const auto *p = std::get_if<double>(&value);
  const Choice *empty = nullptr;
  return !p || *p != 2.5 || std::get_if<0>(&value) ||
         std::get_if<1>(empty) || std::get_if<int>(empty);
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-get-if-const.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-get-if-const" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsGetIfDuplicateRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-get-if-duplicate.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  std::variant<int, int> value;
  const auto *p = std::get_if<0>(&value);
  return !p || *p != 0 || std::get_if<1>(&value);
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-get-if-duplicate.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-get-if-duplicate" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsGetIfCopyAndEvaluateOnceRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-get-if-copy-and-evaluate-once.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
using Choice = std::variant<char, double, int>;
int attempts;
Choice *once(Choice *owner) { ++attempts; return owner; }
extern "C" int check() {
  Choice value = 42;
  Choice copy = value;
  auto *p = std::get_if<2>(once(&copy));
  if (!p || *p != 42 || attempts != 1) return 2;
  *p = 19;
  auto *original = std::get_if<2>(&value);
  return !original || original == p || *original != 42 || *p != 19;
}
int main() { return check(); }
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-get-if-copy-and-evaluate-once.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-get-if-copy-and-evaluate-once" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsHoldsConstRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-holds-const.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
int main() {
  const std::variant<int, double> value = 3.0;
  return !std::holds_alternative<double>(value) || std::holds_alternative<int>(value);
}
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-holds-const.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-holds-const" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsCopyChangingAlternativeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-copy-changing-alternative.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
int main() {
  std::variant<int, double> first = 3;
  const std::variant<int, double> second = 9.0;
  auto& receiver = (first = second);
  auto* payload = std::get_if<1>(&first);
  return &receiver != &first || !payload || *payload != 9.0 || std::get_if<0>(&first);
}
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-copy-changing-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-copy-changing-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsMoveAndSelfAssignmentRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-move-and-self-assignment.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
int main() {
  using V = std::variant<int, double>;
  V original = 6;
  V copy = 3.0;
  copy = static_cast<V&&>(original);
  copy = copy;
  auto* result = std::get_if<int>(&copy);
  return !result || *result != 6 || !std::holds_alternative<int>(original);
}
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-move-and-self-assignment.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-move-and-self-assignment" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsConstGetByIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-const-get-by-index.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
int main() {
  const std::variant<int, double> value{5};
  const auto &item = std::get<0>(value);
  return item != 5;
}
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-const-get-by-index.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-const-get-by-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantObjectsConstGetByTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-objects-const-get-by-type.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
int main() {
  const std::variant<int, double> value{2.5};
  return std::get<double>(value) != 2.5;
}
)r19variantsclr");
  const auto Output = tmpFile("variant-objects-const-get-by-type.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-objects-const-get-by-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-identity.cpp");
  writeFile(Source, R"r19monostate(#include <variant>
#include <type_traits>
using M = std::monostate;
using V = std::variant<M, int>;
int main() {
  return !std::is_same_v<M, std::variant_alternative_t<0, V>> ||
         !std::is_same_v<const M, std::variant_alternative_t<0, const V>> ||
         std::variant_size_v<V> != 2;
}
)r19monostate");
  const auto Output = tmpFile("variant-monostate-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateStandaloneLayoutRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-standalone-layout.cpp");
  writeFile(Source, R"r19monostate(#include <variant>
using M = std::monostate;
int main() { return sizeof(M) != 1 || alignof(const M&) != 1 || sizeof(M[5]) != 5; }
)r19monostate");
  const auto Output = tmpFile("variant-monostate-standalone-layout.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-standalone-layout" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateVariantLayoutRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-variant-layout.cpp");
  writeFile(Source, R"r19monostate(#include <variant>
using V = std::variant<std::monostate, int>;
using R = std::variant<unsigned char, int>;
int main() { return sizeof(V) != sizeof(R) || alignof(const V&) != alignof(R); }
)r19monostate");
  const auto Output = tmpFile("variant-monostate-variant-layout.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-variant-layout" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateMonostateOnlyVariantRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-monostate-only-variant.cpp");
  writeFile(Source, R"r19monostate(#include <variant>
using V = std::variant<std::monostate, std::monostate>;
using R = std::variant<unsigned char, unsigned char>;
int main() { return sizeof(V) != sizeof(R) || alignof(V) != alignof(R); }
)r19monostate");
  const auto Output = tmpFile("variant-monostate-monostate-only-variant.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-monostate-only-variant" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateValueActiveEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-value-active-empty.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
using V = std::variant<std::monostate, int>;
int main() {
  V value;
  return value.index() != 0 || value.valueless_by_exception() ||
         !std::holds_alternative<std::monostate>(value) ||
         !std::get_if<0>(&value) || !std::get_if<std::monostate>(&value) ||
         std::get_if<1>(&value);
}
)r19varvalue");
  const auto Output = tmpFile("variant-monostate-value-active-empty.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-value-active-empty" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateValueConstAddressRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-value-const-address.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
using V = std::variant<std::monostate, int>;
int main() {
  const V value;
  const V* absent = nullptr;
  const auto* first = std::get_if<std::monostate>(&value);
  return first == nullptr || first != &std::get<std::monostate>(value) ||
         std::get_if<0>(absent) != nullptr || std::get_if<1>(&value) != nullptr;
}
)r19varvalue");
  const auto Output = tmpFile("variant-monostate-value-const-address.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-value-const-address" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateValueCopyAndAssignRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-value-copy-and-assign.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
using V = std::variant<std::monostate, int>;
int main() {
  V source;
  V copy(source);
  const V scalar(37);
  source = scalar;
  const auto* number = std::get_if<int>(&source);
  return !number || *number != 37 || std::get_if<0>(&source) != nullptr ||
         !std::get_if<0>(&copy) || copy.index() != 0;
}
)r19varvalue");
  const auto Output = tmpFile("variant-monostate-value-copy-and-assign.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-value-copy-and-assign" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantMonostateValueDuplicateEmptyRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-monostate-value-duplicate-empty.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
using V = std::variant<std::monostate, std::monostate>;
int main() {
  V value;
  return value.index() != 0 || !std::get_if<0>(&value) || std::get_if<1>(&value);
}
)r19varvalue");
  const auto Output = tmpFile("variant-monostate-value-duplicate-empty.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-monostate-value-duplicate-empty" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantIndexConstructionRepeatedTypeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-index-construction-repeated-type.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int main() {
  std::variant<int, int> value(std::in_place_index<1>, 31);
  auto* selected = std::get_if<1>(&value);
  return value.index() != 1 || !selected || *selected != 31 || std::get_if<0>(&value);
}
)r19varvalue");
  const auto Output = tmpFile("variant-index-construction-repeated-type.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-index-construction-repeated-type" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantIndexConstructionZeroInitializedRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-index-construction-zero-initialized.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int main() {
  std::variant<int, long> value(std::in_place_index<1>);
  auto* selected = std::get_if<1>(&value);
  return value.index() != 1 || !selected || *selected != 0;
}
)r19varvalue");
  const auto Output = tmpFile("variant-index-construction-zero-initialized.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-index-construction-zero-initialized" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantIndexConstructionArgumentEffectRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-index-construction-argument-effect.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
long payload;
long& next() { ++payload; return payload; }
int main() {
  const std::variant<int, long> value(std::in_place_index<1>, next());
  return payload != 1 || std::get<1>(value) != 1;
}
)r19varvalue");
  const auto Output = tmpFile("variant-index-construction-argument-effect.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-index-construction-argument-effect" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantIndexConstructionEmptyNonfirstRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-index-construction-empty-nonfirst.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int main() {
  std::variant<int, std::monostate> value(std::in_place_index<1>);
  return !std::holds_alternative<std::monostate>(value) || !std::get_if<1>(&value) ||
         std::get_if<0>(&value) != nullptr;
}
)r19varvalue");
  const auto Output = tmpFile("variant-index-construction-empty-nonfirst.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-index-construction-empty-nonfirst" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantTypeConstructionValueAndEffectRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-type-construction-value-and-effect.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
long count;
long& next() { ++count; return count; }
int main() {
  using Selected = long;
  const std::variant<int, Selected> value(std::in_place_type<Selected>, next());
  return count != 1 || value.index() != 1 || std::get<Selected>(value) != 1;
}
)r19varvalue");
  const auto Output = tmpFile("variant-type-construction-value-and-effect.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-type-construction-value-and-effect" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantTypeConstructionValueInitializePointerRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-type-construction-value-initialize-pointer.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int main() {
  std::variant<int, int*> value(std::in_place_type<int*>);
  auto** selected = std::get_if<int*>(&value);
  return value.index() != 1 || selected == nullptr || *selected != nullptr;
}
)r19varvalue");
  const auto Output = tmpFile("variant-type-construction-value-initialize-pointer.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-type-construction-value-initialize-pointer" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantTypeConstructionEmptyAlternativeRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-type-construction-empty-alternative.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int main() {
  std::variant<int, std::monostate> value(std::in_place_type<std::monostate>);
  return value.index() != 1 || !std::holds_alternative<std::monostate>(value) ||
         std::get_if<int>(&value) != nullptr;
}
)r19varvalue");
  const auto Output = tmpFile("variant-type-construction-empty-alternative.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-type-construction-empty-alternative" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2VariantTypeConstructionZeroIndexRunAtBothOptimizations) {
  const auto Source = tmpFile("variant-type-construction-zero-index.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int main() {
  std::variant<unsigned, long> value(std::in_place_type<unsigned>, 19u);
  auto* selected = std::get_if<unsigned>(&value);
  return value.index() != 0 || !selected || *selected != 19u;
}
)r19varvalue");
  const auto Output = tmpFile("variant-type-construction-zero-index.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("variant-type-construction-zero-index" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
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

TEST_F(TranslateTest, CoreV2VariantObjectsClassAlternativeRejected) {
  const auto Source = tmpFile("variant-objects-class-alternative.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
struct Payload { int field; };
extern "C" int check() { std::variant<Payload, int> value; return int(value.index()); }
int main() { return check(); }
)r19variantsclr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantObjectsConstAlternativeRejected) {
  const auto Source = tmpFile("variant-objects-const-alternative.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  std::variant<const int, double> value;
  return int(value.index());
}
int main() { return check(); }
)r19variantsclr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantObjectsAlteredDiscriminantRejected) {
  const auto Source = tmpFile("variant-objects-altered-discriminant.cpp");
  writeFile(Source, R"r19variantsclr(#define __index renamed_variant_discriminant
#include <variant>
#undef __index
extern "C" int check() { std::variant<int, double> value; return int(value.index()); }
int main() { return check(); }
)r19variantsclr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantObjectsUnprovedConversionRejected) {
  const auto Source = tmpFile("variant-objects-unproved-conversion.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  short original = 7;
  std::variant<int, double> value = original;
  return int(value.index());
}
int main() { return check(); }
)r19variantsclr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantObjectsUncheckedGetRejected) {
  const auto Source = tmpFile("variant-objects-unchecked-get.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
extern "C" int check() {
  std::variant<int, double> value;
  return std::get<0>(value);
}
int main() { return check(); }
)r19variantsclr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantObjectsConstGetInactiveRejected) {
  const auto Source = tmpFile("variant-objects-const-get-inactive.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
int main() {
  const std::variant<int, double> value{2.5};
  return std::get<0>(value);
}
)r19variantsclr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantObjectsConstGetMutableAliasRejected) {
  const auto Source = tmpFile("variant-objects-const-get-mutable-alias.cpp");
  writeFile(Source, R"r19variantsclr(#include <variant>
int main() {
  std::variant<int, double> value{3};
  const auto &alias = value;
  value = std::variant<int, double>{2.5};
  return std::get<0>(alias);
}
)r19variantsclr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantObjectsConstGetAlteredHelperRejected) {
  const auto Source = tmpFile("variant-objects-const-get-altered-helper.cpp");
  writeFile(Source, R"r19variantsclr(#define __generic_get replaced_generic_get
#include <variant>
#undef __generic_get
int main() {
  const std::variant<int, double> value{7};
  return std::get<int>(value);
}
)r19variantsclr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantObjectsGetIfReplacedAddressBodyRejected) {
  const auto Source = tmpFile("variant-objects-get-if-replaced-address-body.cpp");
  writeFile(Source, R"r19variantsclr(#define __builtin_addressof(x) (&(x))
#include <variant>
#undef __builtin_addressof
int main() {
  std::variant<int, double> value = 3;
  const int *item = std::get_if<0>(&value);
  return !item || *item != 3;
}
)r19variantsclr");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMonostateSourceRedeclarationRejected) {
  const auto Source = tmpFile("variant-monostate-source-redeclaration.cpp");
  writeFile(Source, R"r19monostate(#include <variant>
_LIBCPP_BEGIN_NAMESPACE_STD
struct monostate;
_LIBCPP_END_NAMESPACE_STD
using V = std::variant<std::monostate, int>;
int main() { return sizeof(V) == 0; }
)r19monostate");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMonostateVolatileLayoutRejected) {
  const auto Source = tmpFile("variant-monostate-volatile-layout.cpp");
  writeFile(Source, R"r19monostate(#include <variant>
using M = volatile std::monostate;
int main() { return sizeof(M) == 0; }
)r19monostate");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMonostateValueForeignEmptyRejected) {
  const auto Source = tmpFile("variant-monostate-value-foreign-empty.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
struct Empty {};
int main() {
  std::variant<Empty, int> value;
  return value.index();
}
)r19varvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantMonostateValueRuntimeRedeclarationRejected) {
  const auto Source = tmpFile("variant-monostate-value-runtime-redeclaration.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
_LIBCPP_BEGIN_NAMESPACE_STD
struct monostate;
_LIBCPP_END_NAMESPACE_STD
int main() {
  std::variant<std::monostate, int> value;
  return value.index();
}
)r19varvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantIndexConstructionWrittenTagObjectRejected) {
  const auto Source = tmpFile("variant-index-construction-written-tag-object.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int f() {
  std::variant<int, long> value(std::in_place_index_t<1>{}, 12L);
  return value.index();
}
)r19varvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantIndexConstructionEscapingTagRejected) {
  const auto Source = tmpFile("variant-index-construction-escaping-tag.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int f() {
  const auto* tag = &std::in_place_index<1>;
  return tag != nullptr;
}
)r19varvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantTypeConstructionWrittenTagObjectRejected) {
  const auto Source = tmpFile("variant-type-construction-written-tag-object.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int f() {
  std::variant<int, long> value(std::in_place_type_t<long>{}, 19L);
  return value.index();
}
)r19varvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2VariantTypeConstructionEffectfulTagRejected) {
  const auto Source = tmpFile("variant-type-construction-effectful-tag.cpp");
  writeFile(Source, R"r19varvalue(#include <variant>
int effects;
std::in_place_type_t<long> select_type() {
  ++effects;
  return std::in_place_type<long>;
}
int f() {
  std::variant<int, long> value(select_type(), 19L);
  return value.index();
}
)r19varvalue");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
