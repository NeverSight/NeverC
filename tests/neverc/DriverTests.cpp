#include "NeverCTestFixture.h"

class DriverTest : public NeverCTest {};

TEST_F(DriverTest, KernelStyleC) {
  syntaxCheck("kernel_style_c_test",
              (testDir() / "kernel/kernel_style_c_test.c").string(), "gnu11",
              "x86_64-linux-gnu");
}

TEST_F(DriverTest, CrossAArch64) {
  syntaxCheck("test_cross_aarch64",
              (testDir() / "platform/test_cross_aarch64.c").string(), "c11",
              "aarch64-linux-gnu", "-fneverc-types");
}

TEST_F(DriverTest, CrossAppleIOS) {
  syntaxCheck("test_cross_apple_ios",
              (testDir() / "test_basic.c").string(), "c11",
              "aarch64-apple-ios");
}

TEST_F(DriverTest, CrossAndroid) {
  syntaxCheck("test_cross_android",
              (testDir() / "platform/test_android_target_min.c").string(),
              "c11", "aarch64-linux-android29");
}

// Builtin string in kernel-style code on multiple targets
TEST_F(DriverTest, StringKernelMultiTarget) {
  auto src = (testDir() / "string/test_neverc_string_kernel.c").string();
  std::string alloc =
      "-fbuiltin-string -DNEVERC_STRING_ALLOC=kernel_alloc "
      "-DNEVERC_STRING_FREE=kernel_free";
  syntaxCheck("neverc_string_kernel_linux", src, "c23", "x86_64-linux-gnu",
              alloc);
  syntaxCheck("neverc_string_kernel_android", src, "c23",
              "aarch64-linux-android29", alloc);
  syntaxCheck("neverc_string_kernel_ios", src, "c23", "aarch64-apple-ios",
              alloc);
  syntaxCheck("neverc_string_kernel_macos", src, "c23", hostTriple(), alloc);
  syntaxCheck("neverc_string_kernel_windows", src, "c23",
              "x86_64-windows-msvc", alloc);
}

TEST_F(DriverTest, InlineAsmMS) {
  syntaxCheck("test_inline_asm_ms",
              (testDir() / "asm/test_inline_asm_ms.c").string(), "c11",
              "x86_64-windows-msvc", "-fms-extensions");
}

TEST_F(DriverTest, InlineAsmMSCompile) {
  auto obj = tmpFile("inline_asm_ms.obj");
  auto r = ncc({"-std=c11", "-target", "x86_64-windows-msvc", "-fms-extensions",
                "-c", (testDir() / "asm/test_inline_asm_ms.c").string(), "-o",
                obj.string()});
  EXPECT_EQ(r.exitCode, 0) << r.err;
}

TEST_F(DriverTest, SEHTryC) {
  syntaxCheck("seh_try_c",
              (testDir() / "platform/seh_try_c.c").string(), "c11",
              "x86_64-windows-msvc", "-fms-extensions");
}

TEST_F(DriverTest, RejectShellcodeSEH) {
  auto src = (testDir() / "platform/seh_try_c.c").string();
  auto grep =
      "SEH '__try'/'__except'/'__finally' is not supported under -fshellcode";
  expectCommandFail("reject_shellcode_seh_x64", grep,
                    {"-fshellcode", "--target=x86_64-pc-windows-msvc",
                     "-fms-extensions", src, "-o", tmpFile("seh.bin").string()});
  expectCommandFail("reject_shellcode_seh_arm64", grep,
                    {"-fshellcode", "--target=aarch64-pc-windows-msvc",
                     "-fms-extensions", src, "-o", tmpFile("seh.bin").string()});
}

TEST_F(DriverTest, AdvancedFlagsRejected) {
  syntaxCheck("advanced_flags_rejected",
              (testDir() / "advanced_flags_rejected.c").string(), "c11",
              hostTriple());
}

// Reject unsupported architectures
TEST_F(DriverTest, RejectUnsupportedTargets) {
  auto src = (testDir() / "test_basic.c").string();
  struct Case { const char *name; const char *triple; };
  Case cases[] = {
      {"armv7", "armv7-unknown-linux-gnueabi"},
      {"riscv64", "riscv64-unknown-linux-gnu"},
      {"mips64", "mips64-unknown-linux-gnu"},
      {"i386", "i386-pc-linux-gnu"},
      {"arm64ec", "arm64ec-windows-msvc"},
      {"arm64e", "arm64e-apple-darwin"},
      {"aarch64_be", "aarch64_be-linux-gnu"},
      {"wasm32", "wasm32-unknown-unknown"},
      {"wasm64", "wasm64-unknown-unknown"},
  };
  for (auto &c : cases) {
    SCOPED_TRACE(c.name);
    expectCommandFail(std::string("reject_target_") + c.name,
                      "unsupported target architecture",
                      {"-target", c.triple, "-c", src, "-o", "/dev/null"});
  }
}

TEST_F(DriverTest, RejectDarwinArch) {
  if (!isDarwin()) {
    GTEST_SKIP() << "Darwin -arch tests";
    return;
  }
  auto src = (testDir() / "test_basic.c").string();
  auto sf = sysrootFlags();
  for (auto *arch : {"armv7", "arm64e", "arm64_32"}) {
    SCOPED_TRACE(arch);
    std::vector<std::string> args = {"-arch", arch, "-c", src, "-o",
                                     "/dev/null"};
    for (auto &f : sf) args.push_back(f);
    expectCommandFail(std::string("reject_arch_") + arch, "invalid arch name",
                      args);
  }
}

TEST_F(DriverTest, RejectCxxMode) {
  auto src = (testDir() / "test_basic.c").string();
  auto sf = sysrootFlags();
  auto af = archFlags();
  {
    std::vector<std::string> args = {"-x", "c++", "-fsyntax-only", src};
    for (auto &f : sf) args.push_back(f);
    for (auto &f : af) args.push_back(f);
    expectCommandFail("reject_x_cxx", "c++", args);
  }
  {
    std::vector<std::string> args = {"-x", "objective-c", "-fsyntax-only", src};
    for (auto &f : sf) args.push_back(f);
    for (auto &f : af) args.push_back(f);
    expectCommandFail("reject_x_objc", "objective-c", args);
  }
}

TEST_F(DriverTest, SingleDriverEntrypoint) {
  auto buildDir = neverc().parent_path();
  std::vector<std::string> forbidden = {
      "neverc++",  "neverc-cl",  "neverc-cpp",  "neverc-dxc",
      "clang",     "clang++",    "clang-cl",    "clang-cpp",
      "clang-dxc", "cl",         "gcc",         "g++",
      "cpp",       "flang",      "dxc",         "lld",
      "ld.lld",    "lld-link",   "ld64.lld",    "ld-lld",
      "lld-gnu",   "lld-darwin",
  };
  for (auto &name : forbidden)
    EXPECT_FALSE(fs::exists(buildDir / name))
        << "unexpected driver: " << name;
}

// Reject C++ / ObjC / OpenMP flags
TEST_F(DriverTest, RejectUnsupportedFlags) {
  auto src = (testDir() / "test_basic.c").string();
  auto sf = sysrootFlags();
  auto af = archFlags();

  struct Case { const char *flag; const char *grep; };
  Case cases[] = {
      {"-fcoroutines", "unknown argument: '-fcoroutines'"},
      {"-fcxx-exceptions", "unknown argument: '-fcxx-exceptions'"},
      {"-emit-module-interface", "unknown argument"},
      {"-emit-header-unit", "unknown argument"},
      {"-fobjc-arc", "unknown argument"},
      {"-fblocks", "unknown argument"},
      {"-fapinotes", "unknown argument"},
      {"-fopenmp", "unknown argument"},
      {"-fopenacc", "unknown argument"},
  };
  for (auto &c : cases) {
    SCOPED_TRACE(c.flag);
    std::vector<std::string> args = {c.flag, "-fsyntax-only", src};
    for (auto &f : sf) args.push_back(f);
    for (auto &f : af) args.push_back(f);
    expectCommandFail(std::string("reject_") + c.flag, c.grep, args);
  }
}

// Reject driver-mode overrides
TEST_F(DriverTest, RejectDriverModes) {
  auto src = (testDir() / "test_basic.c").string();
  for (auto *mode : {"neverc", "cl", "g++", "cpp", "flang", "dxc"}) {
    SCOPED_TRACE(mode);
    std::string flag = std::string("--driver-mode=") + mode;
    expectCommandFail(std::string("reject_driver_mode_") + mode,
                      "unknown argument", {flag, "-###", src});
  }
}

// Windows MSVC default runtime
TEST_F(DriverTest, WindowsMSVCDefaultRuntime) {
  auto src = (testDir() / "test_basic.c").string();
  auto r = ncc({"-###", "--target=x86_64-pc-windows-msvc", "-c", src});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.stderrContains("--dependent-lib=libcmt") ||
              r.contains("--dependent-lib=libcmt"))
      << "MSVC default runtime missing libcmt\n" << r.err << r.out;
}

// Windows MSVC LTO compatibility
TEST_F(DriverTest, WindowsMSVCLTO) {
  auto src = (testDir() / "test_basic.c").string();
  auto r = ncc(
      {"-###", "--target=x86_64-pc-windows-msvc", "-flto", "-c", "--", src});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.stderrContains("-flto=full") || r.contains("-flto=full"));
}

// Windows GNU no MSVC runtime
TEST_F(DriverTest, WindowsGNUNoMSVCRuntime) {
  auto src = (testDir() / "test_basic.c").string();
  auto r = ncc({"-###", "--target=x86_64-w64-windows-gnu", "-c", src});
  EXPECT_EQ(r.exitCode, 0);
  auto all = r.err + r.out;
  EXPECT_EQ(all.find("--dependent-lib=libcmt"), std::string::npos);
}
