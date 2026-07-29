#include "NeverCTestFixture.h"
#include <sstream>
#include <string_view>

class MimallocTest : public NeverCTest {};

// -fbuiltin-mimalloc should define __NEVERC_MIMALLOC__
TEST_F(MimallocTest, MacroDefined) {
  compileOnly("mimalloc_macro",
              (testDir() / "mimalloc/test_mimalloc_macro.c").string(),
              "-fbuiltin-mimalloc");
}

// mimalloc is on by default wherever there is a libc heap to replace, so a
// plain compile defines __NEVERC_MIMALLOC__ with no flag asking for it.
TEST_F(MimallocTest, MacroDefinedByDefault) {
  compileOnly("mimalloc_default",
              (testDir() / "mimalloc/test_mimalloc_default.c").string(), "");
}

// -fno-builtin-mimalloc opts back out
TEST_F(MimallocTest, MacroNotDefined) {
  compileOnly("mimalloc_no_macro",
              (testDir() / "mimalloc/test_mimalloc_no_macro.c").string(),
              "-fno-builtin-mimalloc");
}

// A kernel image has no userspace heap, so every kernel mode suppresses the
// default injection.  -fms-kernel and -fandroid-kernel-driver-mode carry this
// test: they are how the Windows and Android driver examples build, and unlike
// -mkernel neither implies -fno-builtin, so neither is covered by the
// suppression tests above.  The source #errors if the macro survives, which
// makes compiling it the assertion.
TEST_F(MimallocTest, KernelModesSuppressDefault) {
  const std::string Source =
      (testDir() / "mimalloc/test_mimalloc_kernel.c").string();

  struct Mode {
    const char *Label;
    std::vector<std::string> Args;
  };
  const Mode Modes[] = {
      {"mkernel", {"-mkernel"}},
      {"ms_kernel", {"--target=x86_64-pc-windows-msvc", "-fms-kernel"}},
      {"android_kernel",
       {"--target=aarch64-linux-android", "-fandroid-kernel-driver-mode"}},
      // A driver build that picks up -fbuiltin-mimalloc from shared flags has
      // to come out clean too: the suppression wins over the explicit request.
      {"ms_kernel_explicit_request",
       {"--target=x86_64-pc-windows-msvc", "-fms-kernel",
        "-fbuiltin-mimalloc"}},
  };

  for (const Mode &M : Modes) {
    SCOPED_TRACE(M.Label);
    // ncc() rather than compileOnly(): the latter appends the host -target,
    // which would undo the cross triples these modes need.
    auto Obj = tmpFile(std::string("mimalloc_kernel_") + M.Label + ".o");
    std::vector<std::string> Args = M.Args;
    Args.insert(Args.end(), {"-c", Source, "-o", Obj.string()});
    auto R = ncc(Args);
    EXPECT_EQ(R.exitCode, 0) << R.err;
  }
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

TEST_F(MimallocTest, RuntimeOverrideAliasesRemainCoalescibleOnELF) {
  auto src = tmpFile("mimalloc_weak_aliases.c");
  auto ir = tmpFile("mimalloc_weak_aliases.ll");
  writeFile(src,
            "typedef __SIZE_TYPE__ size_t;\n"
            "extern void *malloc(size_t);\n"
            "extern void free(void *);\n"
            "void use_allocator(void) {\n"
            "  void *p = malloc(32);\n"
            "  free(p);\n"
            "}\n");

  auto r = ncc({"--target=x86_64-unknown-linux-gnu", "-std=c11",
                "-fbuiltin-mimalloc", "-fno-lto", "-O0", "-S",
                "-emit-llvm", src.string(), "-o", ir.string()});
  ASSERT_EQ(r.exitCode, 0) << r.err;

  const std::string text = readFile(ir);
  EXPECT_NE(text.find("@malloc = weak_odr"), std::string::npos)
      << "the malloc override alias must remain weak across native multi-TU "
         "links";
  EXPECT_NE(text.find("@free = weak_odr"), std::string::npos)
      << "the free override alias must remain weak across native multi-TU "
         "links";
  EXPECT_NE(text.find("@__libc_malloc = weak_odr"), std::string::npos)
      << "libc interceptor aliases must also be weak; strong aliases duplicate "
         "across -fno-lto TUs";
  EXPECT_NE(text.find("@strdup = weak_odr"), std::string::npos)
      << "strdup must be a weak alias, not a strong external definition";
}

TEST_F(MimallocTest, RuntimeUsesSafeThreadPointerOnLinux) {
  auto src = tmpFile("mimalloc_thread_pointer.c");
  writeFile(src, "typedef __SIZE_TYPE__ size_t;\n"
                 "extern void *malloc(size_t);\n"
                 "void *allocate(void) { return malloc(32); }\n");

  for (const char *triple :
       {"x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu"}) {
    SCOPED_TRACE(triple);
    auto ir = tmpFile(std::string("mimalloc_thread_pointer_") + triple + ".ll");
    auto r = ncc({std::string("--target=") + triple, "-std=c11",
                  "-fbuiltin-mimalloc", "-fno-lto", "-O0", "-S", "-emit-llvm",
                  src.string(), "-o", ir.string()});
    ASSERT_EQ(r.exitCode, 0) << r.err;

    const std::string text = readFile(ir);
    EXPECT_NE(text.find("@llvm.thread.pointer()"), std::string::npos)
        << "the Linux runtime must use LLVM's thread-pointer intrinsic";
    EXPECT_EQ(text.find("load ptr, ptr null"), std::string::npos)
        << "the FS-slot asm fallback eagerly dereferences address zero";
    if (std::string_view(triple).substr(0, 7) == "aarch64")
      EXPECT_EQ(text.find("={di}"), std::string::npos)
          << "the ARM64 runtime must not contain x86 register constraints";
  }
}

TEST_F(MimallocTest, RuntimeUsesMatchingDarwinArchitecture) {
  auto src = tmpFile("mimalloc_darwin_arch.c");
  writeFile(src, "typedef __SIZE_TYPE__ size_t;\n"
                 "extern void *malloc(size_t);\n"
                 "void *allocate(void) { return malloc(32); }\n");

  for (const char *triple :
       {"x86_64-apple-macosx11.0", "arm64-apple-macosx11.0"}) {
    SCOPED_TRACE(triple);
    auto ir = tmpFile(std::string("mimalloc_darwin_arch_") + triple + ".ll");
    auto r = ncc({std::string("--target=") + triple, "-std=c11",
                  "-fbuiltin-mimalloc", "-fno-lto", "-O0", "-S", "-emit-llvm",
                  src.string(), "-o", ir.string()});
    ASSERT_EQ(r.exitCode, 0) << r.err;

    const std::string text = readFile(ir);
    EXPECT_NE(text.find("@mi_malloc"), std::string::npos)
        << "the target must receive an embedded mimalloc runtime";
    if (std::string_view(triple).substr(0, 6) == "x86_64")
      EXPECT_NE(text.find("={di}"), std::string::npos)
          << "the x64 runtime must contain x86 register constraints";
    else
      EXPECT_EQ(text.find("={di}"), std::string::npos)
          << "the ARM64 runtime must not contain x86 register constraints";
  }
}

TEST_F(MimallocTest, RuntimeCrossCompilesWithPcgOnCOFF) {
  const auto src =
      (testDir() / "string" / "test_neverc_string_fuzz.c").string();

  for (const char *triple :
       {"x86_64-pc-windows-msvc", "aarch64-pc-windows-msvc"}) {
    SCOPED_TRACE(triple);
    auto obj = tmpFile(std::string("mimalloc_pcg_") + triple + ".obj");
    auto r =
        ncc({std::string("--target=") + triple, "-std=c23", "-fbuiltin-string",
             "-fbuiltin-mimalloc", "-fno-lto", "-O0", "-c", "-mllvm",
             "-neverc-pcg-min-funcs=2", "-mllvm", "-neverc-pcg-weight-floor=1",
             "-mllvm", "-neverc-pcg-cg-weight-div=1", src, "-o", obj.string()});
    ASSERT_EQ(r.exitCode, 0) << r.err;

    const std::string bytes = readFile(obj);
    EXPECT_FALSE(bytes.empty());
    EXPECT_NE(bytes.find(".__pcg"), std::string::npos)
        << "the regression must exercise merged parallel codegen";
    size_t count = 0;
    for (size_t pos = 0; (pos = bytes.find("/DEFAULTLIB:advapi32.lib", pos)) !=
                         std::string::npos;
         pos += sizeof("/DEFAULTLIB:advapi32.lib") - 1)
      ++count;
    EXPECT_EQ(count, 1u)
        << "the embedded runtime dependency must survive PCG exactly once";
  }
}

// A constructor runs only if the object format's structor list still names
// it.  Parallel codegen keeps that list in one partition while binning bodies
// across all of them, and codegen drops any record whose associated symbol is
// a declaration where the list lives -- so the record survives only if the
// two stay together.  The association also has to remain expressible in a
// plain relocatable object: an ELF section group would send the partition
// merge into its serial fallback instead.  Losing the record leaves the
// runtime's process-attach hook unregistered, and the first libc call into
// malloc then hands out memory from a heap that was never brought up.
TEST_F(MimallocTest, RuntimeConstructorSurvivesParallelCodegen) {
  auto src = tmpFile("mimalloc_pcg_ctor.c");
  writeFile(src,
            "typedef __SIZE_TYPE__ size_t;\n"
            "extern void *malloc(size_t);\n"
            "extern void free(void *);\n"
            "static int scale(int v) { return v * 3; }\n"
            "static int bias(int v) { return scale(v) + 1; }\n"
            "int main(void) {\n"
            "  void *p = malloc(32);\n"
            "  free(p);\n"
            "  return bias(0) - 1;\n"
            "}\n");

  for (const char *triple :
       {"x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu"}) {
    SCOPED_TRACE(triple);
    auto obj = tmpFile(std::string("mimalloc_pcg_ctor_") + triple + ".o");
    auto r =
        ncc({std::string("--target=") + triple, "-std=c11",
             "-fbuiltin-mimalloc", "-fno-lto", "-O0", "-c", "-mllvm",
             "-neverc-pcg-min-funcs=2", "-mllvm", "-neverc-pcg-weight-floor=1",
             "-mllvm", "-neverc-pcg-cg-weight-div=1", src.string(), "-o",
             obj.string()});
    ASSERT_EQ(r.exitCode, 0) << r.err;

    const std::string bytes = readFile(obj);
    EXPECT_NE(bytes.find(".__pcg"), std::string::npos)
        << "the regression must exercise merged parallel codegen, not the "
           "serial fallback";
    EXPECT_NE(bytes.find(".init_array"), std::string::npos)
        << "the runtime constructor must still be registered after the merge";
  }
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
