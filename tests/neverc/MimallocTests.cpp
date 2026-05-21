#include "NeverCTestFixture.h"

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
