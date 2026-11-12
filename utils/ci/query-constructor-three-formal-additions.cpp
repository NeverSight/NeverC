
TEST_F(TranslateTest, CoreV2QueryOnlyConstructorDeclarationsRunAtBothOptimizations) {
  const auto Source = tmpFile("query-only-constructor.cpp");
  const auto Output = tmpFile("query-only-constructor.nc");
  writeFile(Source, R"cpp(struct C { C(int) noexcept; };
static_assert(__is_constructible(C, int));
int main() { return 0; }
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "-o", Output.string()});
  ASSERT_EQ(Result.exitCode, 0) << Result.out << Result.err;
  for (const std::string &Optimization : {"-O0", "-O2"}) {
    SCOPED_TRACE(Optimization);
    const auto Executable = tmpFile("query-only-constructor" + Optimization);
    auto Compile = compileGenerated(Output, Executable, Optimization, {"-fno-inline"});
    ASSERT_EQ(Compile.exitCode, 0) << Compile.out << Compile.err;
    auto Run = exec(Executable.string(), {});
    EXPECT_EQ(Run.exitCode, 0) << Run.out << Run.err;
  }
}

TEST_F(TranslateTest, CoreV2QueryOnlyConstructorDeclarationsRequireRuntimeDefinitions) {
  const char *Cases[] = {
      R"cpp(struct C { C(int) noexcept; };
static_assert(__is_constructible(C, int));
int main() { C value(1); return 0; }
)cpp",
      R"cpp(struct C { C(int) noexcept; };
int main() { return 0; }
)cpp",
      R"cpp(struct C { C(int) noexcept; };
static_assert(__is_constructible(C&, C&));
int main() { return 0; }
)cpp",
      R"cpp(struct C { C(int) noexcept; };
struct D { D(int) noexcept {} };
static_assert(__is_constructible(D, int));
int main() { return 0; }
)cpp"
  };
  for (unsigned I = 0; I != 4; ++I) {
    SCOPED_TRACE(I);
    const auto Source = tmpFile("constructor-definition-" + std::to_string(I) + ".cpp");
    writeFile(Source, Cases[I]);
    auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
    EXPECT_NE(Result.exitCode, 0) << Result.out << Result.err;
    EXPECT_NE(Result.err.find("TR0203"), std::string::npos) << Result.err;
  }
}

TEST_F(TranslateTest, CoreV2QueryOnlyConstructorDeclarationsKeepDefaultSourceChecks) {
  const auto Source = tmpFile("query-constructor-default-source.cpp");
  writeFile(Source, R"cpp(int unsupported_default() { __asm__("" : : : "memory"); return 1; }
struct C { C(int = unsupported_default()) noexcept; };
static_assert(__is_constructible(C));
int main() { return 0; }
)cpp");
  auto Result = translate(Source, {"--profile", "cpp-core-v2", "--check"});
  EXPECT_NE(Result.exitCode, 0) << Result.out << Result.err;
  EXPECT_NE(Result.err.find("TR0201"), std::string::npos) << Result.err;
}
