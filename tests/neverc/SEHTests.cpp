// Windows Structured Exception Handling (SEH) validation suite.
//
// These are the C-only subset of Microsoft's public SEH test suite
// (github.com/microsoft/Windows-classic-samples-style xcpt4 / nested_collided /
// xframe), exercising __try/__except/__finally and setjmp/longjmp reliable
// stack unwinding. They are the end-to-end regression for the Windows ARM64
// unwind fix: before it, every function's .xdata FunctionLength was emitted as
// 0 (a spurious .p2align 0 per basic block inserted an MCAlignFragment that
// made the WinEH function-length walk bail), so RtlUnwindEx could not map a PC
// back to its function and any longjmp/SEH unwind faulted with 0xC00000FF.
// With the fix these run to completion.
//
// SEH is a Windows-only projection of the Windows ABI; these only build and run
// on a Windows target (they #include <windows.h> and use MS __try keywords), so
// they are skipped on non-Windows hosts. On the Windows ARM64 / x64 CI they
// compile natively for the host and run.
//
// XCPT4's C++ throw/catch sub-tests (Test82/Test90, xcpt4cxx.cpp) are skipped:
// neverc is a C-only compiler (see xcpt4ex.c's XCPT4_HAVE_CXX_EH guard).

#include "NeverCTestFixture.h"
#include <algorithm>

class SEHTest : public NeverCTest {
protected:
  // Compile `sources` (relative to tests/neverc/seh) together into one exe,
  // run it, and assert: clean compile, exit code 0 (no 0xC00000FF crash), every
  // string in `required` present in stdout, and no string in `forbidden`.
  void runSeh(const std::string &name, const std::vector<std::string> &sources,
              const std::string &opt,
              const std::vector<std::string> &required,
              const std::vector<std::string> &forbidden = {"failed"}) {
    if (!isWindows())
      GTEST_SKIP() << "SEH is a Windows-only mechanism; skipped on this host.";

    SCOPED_TRACE("runSeh: " + name);
    auto exe = tmpFile(name + ".exe");

    std::vector<std::string> args = splitFlags(opt);
    args.push_back("-w"); // silence the suite's benign %d/LONG -Wformat noise
    for (auto &f : sysrootFlags())
      args.push_back(f);
    for (auto &f : archFlags())
      args.push_back(f);
    for (auto &s : sources)
      args.push_back((testDir() / "seh" / s).string());
    args.push_back("-o");
    args.push_back(exe.string());

    auto c = ncc(args);
    ASSERT_EQ(c.exitCode, 0) << name << ": compile/link failed\n" << c.err;

    auto r = exec(exe.string(), {});
    EXPECT_EQ(r.exitCode, 0) << name << ": exit " << r.exitCode
                             << " (0xC00000FF == broken unwind)\n"
                             << r.out << r.err;
    for (auto &m : required)
      EXPECT_TRUE(r.contains(m))
          << name << ": stdout missing '" << m << "'\n" << r.out;
    for (auto &m : forbidden)
      EXPECT_FALSE(r.contains(m))
          << name << ": stdout contains '" << m << "'\n" << r.out;
  }
};

// NESTED_COLLIDED: nested-exception dispatch + collided-unwind. Prints "PASSED."
// for each of the two cases on success; a broken unwind crashes before that.
TEST_F(SEHTest, NestedCollided) {
  runSeh("nestcol", {"nestcol.c"}, "-O2",
         {"Collided Unwind test", "Nested Exception test", "PASSED."});
}
TEST_F(SEHTest, NestedCollided_NoLTO) {
  runSeh("nestcol_nolto", {"nestcol.c"}, "-O2 -fno-lto",
         {"Collided Unwind test", "Nested Exception test", "PASSED."});
}

// XCPT4: comprehensive local-frame SEH (__try/__except/__finally + setjmp).
// Runs to "End of exception test" with no "failed" line on success.
TEST_F(SEHTest, Xcpt4) {
  runSeh("xcpt4", {"xcpt4u.c", "xcpt4ex.c", "xcpt4pg.c"}, "-O2",
         {"Start of exception test", "End of exception test"});
}
TEST_F(SEHTest, Xcpt4_NoLTO) {
  runSeh("xcpt4_nolto", {"xcpt4u.c", "xcpt4ex.c", "xcpt4pg.c"}, "-O2 -fno-lto",
         {"Start of exception test", "End of exception test"});
}

// XFRAME: cross-module EH (the EXE LoadLibrary's the DLL and dispatches/unwinds
// across the module boundary). The DLL must sit next to the EXE so the runtime
// loader finds it; both are emitted into the same tmp dir.
TEST_F(SEHTest, XframeEh) {
  if (!isWindows())
    GTEST_SKIP() << "SEH is a Windows-only mechanism; skipped on this host.";

  SCOPED_TRACE("XframeEh");
  auto dll = tmpFile("xframe_eh_dll.dll");
  auto exe = tmpFile("xframe_eh_exe.exe");

  auto build = [&](const std::string &src, const fs::path &out, bool shared) {
    std::vector<std::string> args = splitFlags("-O2");
    args.push_back("-w");
    if (shared)
      args.push_back("-shared");
    for (auto &f : sysrootFlags())
      args.push_back(f);
    for (auto &f : archFlags())
      args.push_back(f);
    args.push_back((testDir() / "seh" / src).string());
    args.push_back("-o");
    args.push_back(out.string());
    return ncc(args);
  };

  auto cd = build("xframe_eh_dll.c", dll, /*shared=*/true);
  ASSERT_EQ(cd.exitCode, 0) << "xframe dll build failed\n" << cd.err;
  auto ce = build("xframe_eh_exe.c", exe, /*shared=*/false);
  ASSERT_EQ(ce.exitCode, 0) << "xframe exe build failed\n" << ce.err;

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0) << "xframe: exit " << r.exitCode
                           << " (0xC00000FF == broken unwind)\n"
                           << r.out << r.err;
  EXPECT_TRUE(r.contains("Caught Exceptions Test PASSED.")) << r.out;
  EXPECT_TRUE(r.contains("Resumed Exceptions Test PASSED.")) << r.out;
}

// Regression for the Windows-on-ARM64 enablement underlying the SEH suite
// above. <winnt.h>'s ReadAcquireN/WriteReleaseN call the MSVC load-acquire /
// store-release intrinsics __ldarN/__stlrN; without them <windows.h> (and thus
// every test above) fails to compile for aarch64-pc-windows-msvc. These checks
// are pure cross-compiles, so they run on every host (not Windows-only).
class WinArm64IntrinTest : public NeverCTest {};

// __ldarN must lower to LDAR(B/H) and __stlrN to STLR(B/H), across all widths.
TEST_F(WinArm64IntrinTest, LdarStlrLowerToAcquireRelease) {
  auto src = tmpFile("ldar_stlr.c");
  writeFile(
      src,
      "unsigned char       l8 (const volatile unsigned char      *p){return __ldar8(p);}\n"
      "unsigned short      l16(const volatile unsigned short     *p){return __ldar16(p);}\n"
      "unsigned int        l32(const volatile unsigned int       *p){return __ldar32(p);}\n"
      "unsigned long long  l64(const volatile unsigned long long *p){return __ldar64(p);}\n"
      "void s8 (volatile unsigned char      *p, unsigned char      v){__stlr8(p,v);}\n"
      "void s16(volatile unsigned short     *p, unsigned short     v){__stlr16(p,v);}\n"
      "void s32(volatile unsigned int       *p, unsigned int       v){__stlr32(p,v);}\n"
      "void s64(volatile unsigned long long *p, unsigned long long v){__stlr64(p,v);}\n");
  auto out = tmpFile("ldar_stlr.s");
  auto r = ncc({"--target=aarch64-pc-windows-msvc", "-O2", "-S", src.string(),
                "-o", out.string()});
  ASSERT_EQ(r.exitCode, 0) << "compile failed\n" << r.err;

  std::string s = readFile(out);
  std::replace(s.begin(), s.end(), '\t', ' '); // normalize mnemonic/operand gap
  auto has = [&](const char *m) { return s.find(m) != std::string::npos; };
  // Load-acquire: byte / half / word(32) / doubleword(64).
  EXPECT_TRUE(has("ldarb")) << s;
  EXPECT_TRUE(has("ldarh")) << s;
  EXPECT_TRUE(has("ldar w")) << s;
  EXPECT_TRUE(has("ldar x")) << s;
  // Store-release: byte / half / word(32) / doubleword(64).
  EXPECT_TRUE(has("stlrb")) << s;
  EXPECT_TRUE(has("stlrh")) << s;
  EXPECT_TRUE(has("stlr w")) << s;
  EXPECT_TRUE(has("stlr x")) << s;
}

// <windows.h> must parse cleanly for Windows on ARM64 (was blocked by the
// missing __ldar8/__stlr8 family).
TEST_F(WinArm64IntrinTest, WindowsHCompilesForArm64) {
  auto src = tmpFile("winh_arm64.c");
  writeFile(src, "#include <windows.h>\nint main(void){return 0;}\n");
  syntaxCheck("windows_h_arm64", src.string(), "c11",
              "aarch64-pc-windows-msvc");
}
