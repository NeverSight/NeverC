#include "NeverCTestFixture.h"
#include <sstream>

class MimallocTest : public NeverCTest {};

// -fbuiltin-mimalloc should define __NEVERC_MIMALLOC__
TEST_F(MimallocTest, MacroDefined) {
  compileOnly("mimalloc_macro",
              (testDir() / "mimalloc/test_mimalloc_macro.c").string(),
              "-fbuiltin-mimalloc");
}

// Without -fbuiltin-mimalloc, __NEVERC_MIMALLOC__ should not be defined
TEST_F(MimallocTest, MacroNotDefined) {
  compileOnly("mimalloc_no_macro",
              (testDir() / "mimalloc/test_mimalloc_no_macro.c").string(), "");
}

// Basic malloc/free/calloc/realloc should work with -fbuiltin-mimalloc
TEST_F(MimallocTest, BasicAllocations) {
  compileRunAndCheck(
      "mimalloc_basic",
      (testDir() / "mimalloc/test_mimalloc_basic.c").string(),
      "-fbuiltin-mimalloc", 0, "test_mimalloc_basic: ALL PASSED");
}

TEST_F(MimallocTest, FunctionOnlyConsumer) {
  auto src = tmpFile("mimalloc_function_only.c");
  writeFile(src,
            "typedef __SIZE_TYPE__ size_t;\n"
            "extern void *malloc(size_t);\n"
            "extern void free(void *);\n"
            "extern int mi_version(void);\n"
            "int main(void) {\n"
            "  if (mi_version() <= 0) return 2;\n"
            "  void *p = malloc(32);\n"
            "  if (!p) return 1;\n"
            "  free(p);\n"
            "  return 0;\n"
            "}\n");
  compileRunAndCheck("mimalloc_function_only", src.string(),
                     "-std=c11 -fbuiltin-mimalloc", 0);
}

TEST_F(MimallocTest, RuntimePreservesUserLocalProvenance) {
  auto src = tmpFile("mimalloc_user_local.c");
  auto ir = tmpFile("mimalloc_user_local.ll");
  writeFile(src,
            "typedef __SIZE_TYPE__ size_t;\n"
            "static volatile unsigned long long mi_process_start = 7;\n"
            "extern void *malloc(size_t);\n"
            "unsigned long long read_user_state(void) {\n"
            "  void *p = malloc(16);\n"
            "  return mi_process_start + (p != (void *)0);\n"
            "}\n");

  std::vector<std::string> args = {
      "-std=c11", "-fbuiltin-mimalloc", "-flto=full", "-O0",
      "-S", "-emit-llvm", src.string(), "-o", ir.string(),
  };
  for (auto &f : sysrootFlags())
    args.push_back(f);
  for (auto &f : archFlags())
    args.push_back(f);

  auto r = ncc(args);
  ASSERT_EQ(r.exitCode, 0) << r.err;
  EXPECT_NE(readFile(ir).find("@mi_process_start = internal"),
            std::string::npos)
      << "the embedded runtime must not change a user-local symbol's linkage";
}

TEST_F(MimallocTest, RuntimeWorksAcrossTranslationUnits) {
  auto owner = tmpFile("mimalloc_owner.c");
  auto consumer = tmpFile("mimalloc_consumer.c");
  writeFile(owner,
            "typedef __SIZE_TYPE__ size_t;\n"
            "static volatile unsigned long long mi_process_start = 7;\n"
            "extern void *malloc(size_t);\n"
            "extern void free(void *);\n"
            "extern void *consumer_alloc(size_t);\n"
            "extern void consumer_free(void *);\n"
            "int main(void) {\n"
            "  void *a = consumer_alloc(48);\n"
            "  void *b = malloc(64);\n"
            "  if (!a || !b) return 1;\n"
            "  free(a);\n"
            "  consumer_free(b);\n"
            "  if (mi_process_start != 7) return 2;\n"
            "  return 0;\n"
            "}\n");
  writeFile(consumer,
            "typedef __SIZE_TYPE__ size_t;\n"
            "extern void *malloc(size_t);\n"
            "extern void free(void *);\n"
            "void *consumer_alloc(size_t n) { return malloc(n); }\n"
            "void consumer_free(void *p) { free(p); }\n");

  for (const char *mode : {"", "-flto=full", "-fno-lto"}) {
    SCOPED_TRACE(mode[0] ? mode : "auto-lto");
    auto exe = tmpFile(std::string("mimalloc_multitu_") +
                       (mode[0] ? mode + 1 : "auto"));
    std::vector<std::string> args = {"-std=c11", "-fbuiltin-mimalloc"};
    if (mode[0])
      args.push_back(mode);
    for (auto &f : sysrootFlags())
      args.push_back(f);
    for (auto &f : archFlags())
      args.push_back(f);
    args.insert(args.end(),
                {owner.string(), consumer.string(), "-o", exe.string()});

    auto compile = ncc(args);
    ASSERT_EQ(compile.exitCode, 0) << compile.err;
    auto run = exec(exe.string(), {});
    EXPECT_EQ(run.exitCode, 0) << run.out << run.err;

    if (!isWindows() && std::string(mode) == "-fno-lto") {
      auto nm = exec("nm", {"-a", exe.string()});
      ASSERT_EQ(nm.exitCode, 0) << nm.err;

      unsigned stateCopies = 0;
      std::istringstream lines(nm.out);
      for (std::string line; std::getline(lines, line);) {
        auto pos = line.find_last_of(" \t");
        std::string name = pos == std::string::npos ? line : line.substr(pos + 1);
        if (isDarwin() && !name.empty() && name.front() == '_')
          name.erase(name.begin());
        constexpr const char *RuntimeState =
            "__neverc_mimalloc_local.mi_options";
        if (name == RuntimeState ||
            name.rfind(std::string(RuntimeState) + ".", 0) == 0)
          ++stateCopies;
      }
      EXPECT_EQ(stateCopies, 1u)
          << "non-LTO multi-TU output must contain one coalesced mimalloc "
             "state";
    }
  }
}

// -fno-builtin should suppress -fbuiltin-mimalloc
TEST_F(MimallocTest, SuppressedByNoBuiltin) {
  compileOnly("mimalloc_suppress_nobuiltin",
              (testDir() / "mimalloc/test_mimalloc_suppression.c").string(),
              "-fbuiltin-mimalloc -fno-builtin");
}

// -ffreestanding should suppress -fbuiltin-mimalloc
TEST_F(MimallocTest, SuppressedByFreestanding) {
  compileOnly("mimalloc_suppress_freestanding",
              (testDir() / "mimalloc/test_mimalloc_freestanding.c").string(),
              "-fbuiltin-mimalloc -ffreestanding");
}

// -fno-builtin-mimalloc should disable the feature
TEST_F(MimallocTest, ExplicitDisable) {
  auto src = tmpFile("mimalloc_disabled.c");
  writeFile(src,
            "#ifdef __NEVERC_MIMALLOC__\n"
            "#error should not be defined\n"
            "#endif\n"
            "int main(void) { return 0; }\n");
  compileOnly("mimalloc_disabled", src.string(), "-fno-builtin-mimalloc");
}

// -fbuiltin-mimalloc should be accepted by the driver
TEST_F(MimallocTest, DriverAcceptsFlag) {
  auto src = tmpFile("mimalloc_driver.c");
  writeFile(src, "int main(void) { return 0; }\n");
  auto r = ncc({"-fbuiltin-mimalloc", "-fsyntax-only", src.string()});
  EXPECT_EQ(r.exitCode, 0) << "driver rejected -fbuiltin-mimalloc\n" << r.err;
}

// -fno-builtin-mimalloc should be accepted by the driver
TEST_F(MimallocTest, DriverAcceptsNoFlag) {
  auto src = tmpFile("mimalloc_driver_no.c");
  writeFile(src, "int main(void) { return 0; }\n");
  auto r = ncc({"-fno-builtin-mimalloc", "-fsyntax-only", src.string()});
  EXPECT_EQ(r.exitCode, 0)
      << "driver rejected -fno-builtin-mimalloc\n" << r.err;
}

// With -fbuiltin-mimalloc, emit-llvm should succeed
TEST_F(MimallocTest, EmitLLVM) {
  auto src = tmpFile("mimalloc_emit.c");
  auto bc = tmpFile("mimalloc_emit.bc");
  writeFile(src, "#include <stdlib.h>\n"
                 "int main(void) {\n"
                 "  void *p = malloc(42);\n"
                 "  free(p);\n"
                 "  return 0;\n"
                 "}\n");
  auto args = sysrootFlags();
  for (auto &f : archFlags()) args.push_back(f);
  args.insert(args.end(),
              {"-fbuiltin-mimalloc", "-c", "-emit-llvm", src.string(), "-o",
               bc.string()});
  auto r = ncc(args);
  EXPECT_EQ(r.exitCode, 0) << "emit-llvm with mimalloc failed\n" << r.err;
}
