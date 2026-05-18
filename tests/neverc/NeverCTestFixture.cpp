#include "NeverCTestFixture.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>

#ifdef _WIN32
#include <io.h>
#include <process.h>
#define popen _popen
#define pclose _pclose
#else
#include <sys/utsname.h>
#include <unistd.h>
#endif

std::string NeverCTest::cachedSdkPath_;
bool NeverCTest::sdkCached_ = false;

// ---- CmdResult helpers ----

bool CmdResult::contains(const std::string &needle) const {
  return out.find(needle) != std::string::npos;
}

bool CmdResult::stderrContains(const std::string &needle) const {
  return err.find(needle) != std::string::npos;
}

// ---- SetUp / TearDown ----

void NeverCTest::SetUp() {
  auto testName =
      ::testing::UnitTest::GetInstance()->current_test_info()->name();
  tmpDir_ = fs::temp_directory_path() / ("neverc_test_" + std::string(testName)
                                         + "_" + std::to_string(getpid()));
  fs::create_directories(tmpDir_);
}

void NeverCTest::TearDown() {
  if (!tmpDir_.empty() && fs::exists(tmpDir_)) {
    std::error_code ec;
    fs::remove_all(tmpDir_, ec);
  }
}

// ---- Platform detection ----

bool NeverCTest::isDarwin() {
#ifdef __APPLE__
  return true;
#else
  return false;
#endif
}

bool NeverCTest::isLinux() {
#ifdef __linux__
  return true;
#else
  return false;
#endif
}

bool NeverCTest::isWindows() {
#ifdef _WIN32
  return true;
#else
  return false;
#endif
}

bool NeverCTest::isArm64() {
#if defined(__aarch64__) || defined(_M_ARM64)
  return true;
#else
  return false;
#endif
}

bool NeverCTest::isX86_64() {
#if defined(__x86_64__) || defined(_M_X64)
  return true;
#else
  return false;
#endif
}

std::string NeverCTest::hostArch() {
#if defined(__aarch64__) || defined(_M_ARM64)
  return "arm64";
#elif defined(__x86_64__) || defined(_M_X64)
  return "x86_64";
#else
  return "unknown";
#endif
}

std::string NeverCTest::hostTriple() {
  if (isDarwin())
    return hostArch() + "-apple-macosx";
  if (isLinux())
    return hostArch() + "-linux-gnu";
  if (isWindows())
    return hostArch() + "-pc-windows-msvc";
  return hostArch() + "-unknown-unknown";
}

// ---- Paths ----

fs::path NeverCTest::neverc() { return fs::path(NEVERC_BINARY); }
fs::path NeverCTest::testDir() { return fs::path(TEST_SOURCE_DIR); }
fs::path NeverCTest::cTestDir() { return fs::path(TEST_C_DIR); }
fs::path NeverCTest::tmp() const { return tmpDir_; }
fs::path NeverCTest::tmpFile(const std::string &name) const {
  return tmpDir_ / name;
}

// ---- SDK / sysroot ----

std::string NeverCTest::sdkPath() {
  if (sdkCached_)
    return cachedSdkPath_;
  sdkCached_ = true;
#ifdef __APPLE__
  auto *fp = popen("xcrun --show-sdk-path 2>/dev/null", "r");
  if (fp) {
    char buf[512];
    if (fgets(buf, sizeof(buf), fp)) {
      cachedSdkPath_ = buf;
      while (!cachedSdkPath_.empty() && cachedSdkPath_.back() == '\n')
        cachedSdkPath_.pop_back();
    }
    pclose(fp);
  }
#endif
  return cachedSdkPath_;
}

std::vector<std::string> NeverCTest::sysrootFlags() const {
  std::vector<std::string> f;
  if (isDarwin()) {
    auto sdk = sdkPath();
    if (!sdk.empty()) {
      f.push_back("-isysroot");
      f.push_back(sdk);
    }
  }
  return f;
}

std::vector<std::string> NeverCTest::archFlags() const {
  std::vector<std::string> f;
  if (isDarwin()) {
    f.push_back("-target");
    f.push_back(hostTriple());
  }
  return f;
}

std::vector<std::string> NeverCTest::linkFlags() const {
  std::vector<std::string> f;
  if (isDarwin()) {
    auto sdk = sdkPath();
    if (!sdk.empty()) {
      f.push_back("-L" + sdk + "/usr/lib");
      f.push_back("-lSystem");
    }
  } else {
    f.push_back("-lc");
  }
  return f;
}

// ---- Low-level execution ----

static std::string shellEscape(const std::string &s) {
  std::string r = "'";
  for (char c : s) {
    if (c == '\'')
      r += "'\\''";
    else
      r += c;
  }
  r += "'";
  return r;
}

CmdResult NeverCTest::exec(const std::string &program,
                           const std::vector<std::string> &args) const {
  CmdResult result;
  auto errFile = tmpFile("_cmd_stderr.txt");

  std::string cmd = shellEscape(program);
  for (auto &a : args)
    cmd += " " + shellEscape(a);
  cmd += " 2>" + shellEscape(errFile.string());

  auto *fp = popen(cmd.c_str(), "r");
  if (!fp) {
    result.exitCode = -1;
    return result;
  }

  char buf[4096];
  while (fgets(buf, sizeof(buf), fp))
    result.out += buf;

  int status = pclose(fp);
#ifndef _WIN32
  result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#else
  result.exitCode = status;
#endif

  if (fs::exists(errFile)) {
    std::ifstream f(errFile);
    result.err.assign(std::istreambuf_iterator<char>(f), {});
  }
  return result;
}

CmdResult NeverCTest::ncc(const std::vector<std::string> &args) const {
  return exec(neverc().string(), args);
}

// ---- High-level test patterns ----

void NeverCTest::compileRunAndCheck(const std::string &name,
                                    const std::string &src,
                                    const std::string &stdFlag,
                                    int expectedExit,
                                    const std::string &expectedStdoutGrep) {
  SCOPED_TRACE("compileRunAndCheck: " + name);

  auto obj = tmpFile(name + ".o");
  auto exe = tmpFile(name);

  // Compile to .o
  {
    auto args = splitFlags(stdFlag);
    for (auto &f : sysrootFlags()) args.push_back(f);
    for (auto &f : archFlags()) args.push_back(f);
    args.push_back("-c");
    args.push_back(src);
    args.push_back("-o");
    args.push_back(obj.string());
    auto r = ncc(args);
    ASSERT_EQ(r.exitCode, 0)
        << name << ": compile failed\n" << r.err;
  }

  // Compile + link to exe
  {
    auto args = splitFlags(stdFlag);
    for (auto &f : sysrootFlags()) args.push_back(f);
    for (auto &f : archFlags()) args.push_back(f);
    args.push_back(src);
    args.push_back("-o");
    args.push_back(exe.string());
    auto r = ncc(args);
    ASSERT_EQ(r.exitCode, 0)
        << name << ": link failed\n" << r.err;
  }

  // Run
  {
    auto r = exec(exe.string(), {});
    EXPECT_EQ(r.exitCode, expectedExit)
        << name << ": run exit " << r.exitCode << ", expected " << expectedExit
        << "\n" << r.out << r.err;

    if (!expectedStdoutGrep.empty()) {
      EXPECT_TRUE(r.contains(expectedStdoutGrep))
          << name << ": stdout missing '" << expectedStdoutGrep << "'\n"
          << r.out;
    }
  }
}

void NeverCTest::syntaxCheck(const std::string &name, const std::string &src,
                              const std::string &std, const std::string &target,
                              const std::string &extra) {
  SCOPED_TRACE("syntaxCheck: " + name);

  std::vector<std::string> args;
  args.push_back("-std=" + std);
  args.push_back("-target");
  args.push_back(target);
  args.push_back("-Wno-unused-variable");
  args.push_back("-Wno-unused-function");
  args.push_back("-Wno-unused-value");
  args.push_back("-Wno-empty-body");
  args.push_back("-fsyntax-only");

  for (auto &f : splitFlags(extra))
    args.push_back(f);

  if (target.find("apple") != std::string::npos) {
    for (auto &f : sysrootFlags())
      args.push_back(f);
  }

  args.push_back(src);
  auto r = ncc(args);
  EXPECT_EQ(r.exitCode, 0) << name << ": syntax check failed\n" << r.err;
}

void NeverCTest::expectCommandFail(const std::string &name,
                                    const std::string &expectedStderrGrep,
                                    const std::vector<std::string> &args) {
  SCOPED_TRACE("expectCommandFail: " + name);
  auto r = ncc(args);
  EXPECT_NE(r.exitCode, 0) << name << ": expected failure but succeeded";

  if (!expectedStderrGrep.empty()) {
    EXPECT_TRUE(r.stderrContains(expectedStderrGrep) ||
                r.contains(expectedStderrGrep))
        << name << ": stderr missing '" << expectedStderrGrep << "'\n"
        << r.err << r.out;
  }
}

bool NeverCTest::compileOnly(const std::string &name, const std::string &src,
                              const std::string &flags) {
  auto obj = tmpFile(name + ".o");
  auto args = splitFlags(flags);
  for (auto &f : sysrootFlags()) args.push_back(f);
  for (auto &f : archFlags()) args.push_back(f);
  args.push_back("-c");
  args.push_back(src);
  args.push_back("-o");
  args.push_back(obj.string());
  auto r = ncc(args);
  EXPECT_EQ(r.exitCode, 0) << name << ": compile-only failed\n" << r.err;
  return r.exitCode == 0;
}

// ---- Shellcode patterns ----

fs::path NeverCTest::buildShellcodeLoader() {
  if (loaderBuilt_)
    return loaderPath_;

  if (!isDarwin() || !isArm64()) {
    loaderBuilt_ = true;
    return {};
  }

  auto loaderSrc = testDir() / "shellcode" / "loader_arm64_macos.c";
  loaderPath_ = tmpFile("shellcode_loader");

  auto r = exec("cc", {"-O2", "-o", loaderPath_.string(), loaderSrc.string()});
  EXPECT_EQ(r.exitCode, 0) << "shellcode loader build failed\n" << r.err;

  if (r.exitCode == 0) {
    auto entitlements = testDir() / "shellcode" / "jit.entitlements";
    if (fs::exists(entitlements)) {
      exec("codesign",
           {"-s", "-", "--entitlements", entitlements.string(), "-f",
            loaderPath_.string()});
    }
  }

  loaderBuilt_ = true;
  return loaderPath_;
}

void NeverCTest::shellcodeTest(const std::string &name, const std::string &src,
                                int arg0, int arg1, int expectedExit) {
  SCOPED_TRACE("shellcodeTest: " + name);

  if (!isDarwin() || !isArm64()) {
    GTEST_SKIP() << "shellcode loader requires arm64 macOS";
    return;
  }

  auto loader = buildShellcodeLoader();
  if (loader.empty()) {
    GTEST_SKIP() << "shellcode loader not available";
    return;
  }

  auto bin = tmpFile(name + ".bin");
  auto r = ncc({"-fshellcode", src, "-o", bin.string()});
  ASSERT_EQ(r.exitCode, 0) << "shellcode " << name << ": compile\n" << r.err;

  auto runR = exec(loader.string(),
                   {bin.string(), std::to_string(arg0), std::to_string(arg1)});
  EXPECT_EQ(runR.exitCode, expectedExit)
      << "shellcode " << name << ": run exit " << runR.exitCode << ", expected "
      << expectedExit << "\n" << runR.out;
}

bool NeverCTest::shellcodeCompileOnly(
    const std::string &name, const std::string &src,
    const std::vector<std::string> &extraFlags) {
  auto bin = tmpFile(name + ".bin");
  std::vector<std::string> args = {"-fshellcode"};
  for (auto &f : extraFlags) args.push_back(f);
  args.push_back(src);
  args.push_back("-o");
  args.push_back(bin.string());
  auto r = ncc(args);
  EXPECT_EQ(r.exitCode, 0) << "shellcode " << name << ": compile\n" << r.err;
  return r.exitCode == 0;
}

void NeverCTest::shellcodeExpectFail(
    const std::string &name, const std::string &src,
    const std::string &expectedError,
    const std::vector<std::string> &extraFlags) {
  SCOPED_TRACE("shellcodeExpectFail: " + name);

  auto bin = tmpFile(name + ".bin");
  std::vector<std::string> args = {"-fshellcode"};
  for (auto &f : extraFlags) args.push_back(f);
  args.push_back(src);
  args.push_back("-o");
  args.push_back(bin.string());
  auto r = ncc(args);
  EXPECT_NE(r.exitCode, 0) << "shellcode " << name << ": expected failure";

  if (!expectedError.empty()) {
    EXPECT_TRUE(r.stderrContains(expectedError) || r.contains(expectedError))
        << "shellcode " << name << ": missing '" << expectedError << "'\n"
        << r.err << r.out;
  }
}

void NeverCTest::shellcodeStringPair(const std::string &tag) {
  SCOPED_TRACE("shellcodeStringPair: " + (tag.empty() ? "<base>" : tag));

  auto shellDir = testDir() / "shellcode";
  std::string userSrc, kernSrc;
  if (tag.empty()) {
    userSrc = (shellDir / "test_shellcode_neverc_string.c").string();
    kernSrc = (shellDir / "test_shellcode_neverc_string_kernel.c").string();
  } else {
    userSrc =
        (shellDir / ("test_shellcode_neverc_string_" + tag + ".c")).string();
    kernSrc =
        (shellDir / ("test_shellcode_neverc_string_kernel_" + tag + ".c"))
            .string();
  }

  std::string userName = tag.empty() ? "neverc_string" : "neverc_string_" + tag;
  std::string kernName =
      tag.empty() ? "neverc_string_kernel" : "neverc_string_kernel_" + tag;

  shellcodeTest(userName, userSrc, 0, 0, 0);
  shellcodeCompileOnly(kernName, kernSrc, {"-mshellcode-context=kernel"});
}

void NeverCTest::shellcodeCrossCompile(const std::string &label,
                                        const std::string &src,
                                        const std::vector<std::string> &extra) {
  static const char *triples[] = {
      "arm64-apple-macos",          "x86_64-apple-macos",
      "aarch64-linux-gnu",          "x86_64-linux-gnu",
      "aarch64-linux-android29",    "x86_64-linux-android29",
      "aarch64-pc-windows-msvc",    "x86_64-pc-windows-msvc",
  };
  for (auto *triple : triples) {
    SCOPED_TRACE(std::string(triple));
    auto bin = tmpFile(label + "_" + triple + ".bin");
    std::vector<std::string> args = {"-fshellcode", "-target", triple};
    for (auto &f : extra) args.push_back(f);
    args.push_back(src);
    args.push_back("-o");
    args.push_back(bin.string());
    auto r = ncc(args);
    EXPECT_TRUE(r.exitCode == 0 && fs::exists(bin) && fileSize(bin) > 0)
        << label << " " << triple << ": compile failed\n" << r.err;
  }
}

void NeverCTest::shellcodeCrossCompileKernel(
    const std::string &label, const std::string &src,
    const std::vector<std::string> &extra) {
  static const char *triples[] = {
      "arm64-apple-macos",          "x86_64-apple-macos",
      "aarch64-linux-gnu",          "x86_64-linux-gnu",
      "aarch64-linux-android29",    "x86_64-linux-android29",
      "aarch64-pc-windows-msvc",    "x86_64-pc-windows-msvc",
  };
  for (auto *triple : triples) {
    SCOPED_TRACE(std::string(triple));
    auto bin = tmpFile(label + "_kern_" + triple + ".bin");
    std::vector<std::string> args = {"-fshellcode", "-mshellcode-context=kernel",
                                     "-target", triple};
    for (auto &f : extra) args.push_back(f);
    args.push_back(src);
    args.push_back("-o");
    args.push_back(bin.string());
    auto r = ncc(args);
    EXPECT_TRUE(r.exitCode == 0 && fs::exists(bin) && fileSize(bin) > 0)
        << label << " " << triple << " (kernel): compile failed\n" << r.err;
  }
}

// ---- File utilities ----

void NeverCTest::writeFile(const fs::path &path, const std::string &content) {
  std::ofstream f(path);
  f << content;
}

std::string NeverCTest::readFile(const fs::path &path) const {
  std::ifstream f(path);
  return {std::istreambuf_iterator<char>(f), {}};
}

size_t NeverCTest::fileSize(const fs::path &path) const {
  if (!fs::exists(path)) return 0;
  return fs::file_size(path);
}

// ---- String utilities ----

std::vector<std::string> NeverCTest::splitFlags(const std::string &flags) {
  std::vector<std::string> result;
  std::istringstream iss(flags);
  std::string token;
  while (iss >> token)
    result.push_back(token);
  return result;
}
