
TEST_F(TranslateTest, CoreV2AssociativeMapIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-map-identity.cpp");
  writeFile(Source, R"r19assoc(#include <map>
#include <type_traits>
using Container = std::map<int, int>;
int main() { return !std::is_same_v<Container, std::map<int, int>>; }
)r19assoc");
  const auto Output = tmpFile("associative-map-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-map-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeMapAliasesRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-map-aliases.cpp");
  writeFile(Source, R"r19assoc(#include <map>
#include <type_traits>
using Container = std::map<int, int>;
int main() { return !std::is_same_v<Container::key_type, int> || !std::is_same_v<Container::value_type, std::pair<const int, int>>; }
)r19assoc");
  const auto Output = tmpFile("associative-map-aliases.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-map-aliases" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeMapSizeofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-map-sizeof.cpp");
  writeFile(Source, R"r19assoc(#include <map>
#include <type_traits>
using Container = std::map<int, int>;
int main() { return sizeof(Container) != 3 * sizeof(void*); }
)r19assoc");
  const auto Output = tmpFile("associative-map-sizeof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-map-sizeof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeMapAlignofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-map-alignof.cpp");
  writeFile(Source, R"r19assoc(#include <map>
#include <type_traits>
using Container = std::map<int, int>;
int main() { return alignof(const Container&) != alignof(void*); }
)r19assoc");
  const auto Output = tmpFile("associative-map-alignof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-map-alignof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeSetIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-set-identity.cpp");
  writeFile(Source, R"r19assoc(#include <set>
#include <type_traits>
using Container = std::set<int>;
int main() { return !std::is_same_v<Container, std::set<int>>; }
)r19assoc");
  const auto Output = tmpFile("associative-set-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-set-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeSetAliasesRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-set-aliases.cpp");
  writeFile(Source, R"r19assoc(#include <set>
#include <type_traits>
using Container = std::set<int>;
int main() { return !std::is_same_v<Container::key_type, int> || !std::is_same_v<Container::value_type, int>; }
)r19assoc");
  const auto Output = tmpFile("associative-set-aliases.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-set-aliases" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeSetSizeofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-set-sizeof.cpp");
  writeFile(Source, R"r19assoc(#include <set>
#include <type_traits>
using Container = std::set<int>;
int main() { return sizeof(Container) != 3 * sizeof(void*); }
)r19assoc");
  const auto Output = tmpFile("associative-set-sizeof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-set-sizeof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeSetAlignofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-set-alignof.cpp");
  writeFile(Source, R"r19assoc(#include <set>
#include <type_traits>
using Container = std::set<int>;
int main() { return alignof(const Container&) != alignof(void*); }
)r19assoc");
  const auto Output = tmpFile("associative-set-alignof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-set-alignof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedSetIdentityRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-unordered_set-identity.cpp");
  writeFile(Source, R"r19assoc(#include <unordered_set>
#include <type_traits>
using Container = std::unordered_set<int>;
int main() { return !std::is_same_v<Container, std::unordered_set<int>>; }
)r19assoc");
  const auto Output = tmpFile("associative-unordered_set-identity.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-unordered_set-identity" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedSetAliasesRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-unordered_set-aliases.cpp");
  writeFile(Source, R"r19assoc(#include <unordered_set>
#include <type_traits>
using Container = std::unordered_set<int>;
int main() { return !std::is_same_v<Container::key_type, int> || !std::is_same_v<Container::value_type, int>; }
)r19assoc");
  const auto Output = tmpFile("associative-unordered_set-aliases.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-unordered_set-aliases" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedSetSizeofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-unordered_set-sizeof.cpp");
  writeFile(Source, R"r19assoc(#include <unordered_set>
#include <type_traits>
using Container = std::unordered_set<int>;
int main() { return sizeof(Container) != 5 * sizeof(void*); }
)r19assoc");
  const auto Output = tmpFile("associative-unordered_set-sizeof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-unordered_set-sizeof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedSetAlignofRunAtBothOptimizations) {
  const auto Source = tmpFile("associative-unordered_set-alignof.cpp");
  writeFile(Source, R"r19assoc(#include <unordered_set>
#include <type_traits>
using Container = std::unordered_set<int>;
int main() { return alignof(const Container&) != alignof(void*); }
)r19assoc");
  const auto Output = tmpFile("associative-unordered_set-alignof.nc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("associative-unordered_set-alignof" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization);
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2AssociativeMapCustomTypeOrPolicyRejected) {
  const auto Source = tmpFile("associative-map-custom-type-or-policy.cpp");
  writeFile(Source, R"r19assoc(#include <map>
#include <type_traits>
using Container = std::map<int, double>;
int main() { return sizeof(Container) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeMapRenamedContainerStorageRejected) {
  const auto Source = tmpFile("associative-map-renamed-container-storage.cpp");
  writeFile(Source, R"r19assoc(#define __tree_ altered_owner_storage
#include <map>
#include <type_traits>
#undef __tree_
using Container = std::map<int, int>;
int main() { return sizeof(Container) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeMapRuntimeStorageGuardRejected) {
  const auto Source = tmpFile("associative-map-runtime-storage-guard.cpp");
  writeFile(Source, R"r19assoc(#include <map>
#include <type_traits>
using Container = std::map<int, int>;
int main() { Container value; return sizeof(value) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeSetCustomTypeOrPolicyRejected) {
  const auto Source = tmpFile("associative-set-custom-type-or-policy.cpp");
  writeFile(Source, R"r19assoc(#include <set>
#include <type_traits>
using Container = std::set<int, std::greater<int>>;
int main() { return sizeof(Container) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeSetRenamedContainerStorageRejected) {
  const auto Source = tmpFile("associative-set-renamed-container-storage.cpp");
  writeFile(Source, R"r19assoc(#define __tree_ altered_owner_storage
#include <set>
#include <type_traits>
#undef __tree_
using Container = std::set<int>;
int main() { return sizeof(Container) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeSetRuntimeStorageGuardRejected) {
  const auto Source = tmpFile("associative-set-runtime-storage-guard.cpp");
  writeFile(Source, R"r19assoc(#include <set>
#include <type_traits>
using Container = std::set<int>;
int main() { Container value; return sizeof(value) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedSetCustomTypeOrPolicyRejected) {
  const auto Source = tmpFile("associative-unordered_set-custom-type-or-policy.cpp");
  writeFile(Source, R"r19assoc(#include <unordered_set>
#include <type_traits>
struct CustomHash { unsigned long long operator()(int key) const { return static_cast<unsigned int>(key); } };
using Container = std::unordered_set<int, CustomHash>;
int main() { return sizeof(Container) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedSetRenamedContainerStorageRejected) {
  const auto Source = tmpFile("associative-unordered_set-renamed-container-storage.cpp");
  writeFile(Source, R"r19assoc(#define __table_ altered_owner_storage
#include <unordered_set>
#include <type_traits>
#undef __table_
using Container = std::unordered_set<int>;
int main() { return sizeof(Container) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedSetRuntimeStorageGuardRejected) {
  const auto Source = tmpFile("associative-unordered_set-runtime-storage-guard.cpp");
  writeFile(Source, R"r19assoc(#include <unordered_set>
#include <type_traits>
using Container = std::unordered_set<int>;
int main() { Container value; return sizeof(value) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}

TEST_F(TranslateTest, CoreV2AssociativeUnorderedMapNonDefaultKeyRejected) {
  const auto Source = tmpFile("associative-unordered-map-non-default-key.cpp");
  writeFile(Source, R"r19assoc(#include <unordered_map>
using Map = std::unordered_map<long long, int>;
int main() { return sizeof(Map) == 0; }
)r19assoc");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  ASSERT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_TRUE(Result.err.find("TR0201") != std::string::npos ||
              Result.err.find("TR0203") != std::string::npos) << Result.err;
}
