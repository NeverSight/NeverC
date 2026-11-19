
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

TEST_F(TranslateTest, CoreV2VariantMetadataLayoutQueryRejected) {
  const auto Source = tmpFile("variant-metadata-layout-query.cpp");
  writeFile(Source, R"r19variant(#include <variant>
using V = std::variant<int,double>;
int main(){return sizeof(V)==0;}
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
