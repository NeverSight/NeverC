#pragma once

#include <gtest/gtest.h>
#include <filesystem>
#include <string>
#include <vector>

namespace fs = std::filesystem;

struct CmdResult {
  int exitCode = -1;
  std::string out;
  std::string err;
  bool ok() const { return exitCode == 0; }
  bool contains(const std::string &needle) const;
  bool stderrContains(const std::string &needle) const;
};

class NeverCTest : public ::testing::Test {
protected:
  void SetUp() override;
  void TearDown() override;

  // ---- Platform detection ----
  static bool isDarwin();
  static bool isLinux();
  static bool isWindows();
  static bool isArm64();
  static bool isX86_64();
  static std::string hostArch();
  static std::string hostTriple();

  // ---- Paths ----
  static fs::path neverc();
  static fs::path testDir();
  static fs::path cTestDir();
  fs::path tmp() const;
  fs::path tmpFile(const std::string &name) const;

  // ---- SDK / sysroot ----
  static std::string sdkPath();
  std::vector<std::string> sysrootFlags() const;
  std::vector<std::string> archFlags() const;
  std::vector<std::string> linkFlags() const;

  // ---- Low-level execution ----
  CmdResult exec(const std::string &program,
                 const std::vector<std::string> &args) const;
  CmdResult ncc(const std::vector<std::string> &args) const;

  // ---- High-level test patterns ----

  // Compile src -> obj, src -> exe, run exe, check exit + stdout.
  void compileRunAndCheck(const std::string &name, const std::string &src,
                          const std::string &std = "-std=c11",
                          int expectedExit = 0,
                          const std::string &expectedStdoutGrep = "");

  // Syntax-only check for a given target triple.
  void syntaxCheck(const std::string &name, const std::string &src,
                   const std::string &std, const std::string &target,
                   const std::string &extra = "");

  // Expect a command to fail and stderr to contain needle.
  void expectCommandFail(const std::string &name,
                         const std::string &expectedStderrGrep,
                         const std::vector<std::string> &args);

  // Compile-only: src -> obj. Returns true on success.
  bool compileOnly(const std::string &name, const std::string &src,
                   const std::string &flags = "");

  // ---- Shellcode patterns (macOS arm64) ----

  // Build + sign the shellcode loader. Returns loader path.
  fs::path buildShellcodeLoader();

  // Compile to shellcode .bin, load, check exit code.
  void shellcodeTest(const std::string &name, const std::string &src,
                     int arg0 = 0, int arg1 = 0, int expectedExit = 0);

  // Compile to shellcode .bin only (no loader run).
  bool shellcodeCompileOnly(const std::string &name, const std::string &src,
                            const std::vector<std::string> &extraFlags = {});

  // Expect shellcode compilation to fail with a specific error.
  void shellcodeExpectFail(const std::string &name, const std::string &src,
                           const std::string &expectedError,
                           const std::vector<std::string> &extraFlags = {});

  // User-mode + kernel-mode shellcode string pair.
  void shellcodeStringPair(const std::string &tag);

  // Cross-target shellcode compile (all 8 triples).
  void shellcodeCrossCompile(const std::string &label, const std::string &src,
                             const std::vector<std::string> &extra = {});
  void shellcodeCrossCompileKernel(const std::string &label,
                                   const std::string &src,
                                   const std::vector<std::string> &extra = {});

  // ---- File utilities ----
  void writeFile(const fs::path &path, const std::string &content);
  std::string readFile(const fs::path &path) const;
  size_t fileSize(const fs::path &path) const;

  // ---- String utilities ----
  static std::vector<std::string> splitFlags(const std::string &flags);

private:
  fs::path tmpDir_;
  fs::path loaderPath_;
  bool loaderBuilt_ = false;

  static std::string cachedSdkPath_;
  static bool sdkCached_;
};
