#include "NeverCTestFixture.h"

#include "neverc/Linker/Core/Driver/LTOCacheContract.h"

#include <cstdlib>

// MSVC has no POSIX setenv/unsetenv; _putenv_s keeps the CRT and Win32
// environment blocks in sync so spawned neverc children inherit the value.
static void setEnvVar(const char *Name, const char *Value) {
#ifdef _WIN32
  _putenv_s(Name, Value);
#else
  setenv(Name, Value, 1);
#endif
}

static void unsetEnvVar(const char *Name) {
#ifdef _WIN32
  _putenv_s(Name, "");
#else
  unsetenv(Name);
#endif
}

class LTOTest : public NeverCTest {};

TEST_F(LTOTest, HelloLTO) {
  auto src = (testDir() / "lto/hello_lto.c").string();
  auto obj = tmpFile("hello_lto.o");
  auto exe = tmpFile("hello_lto");

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  auto c = base;
  c.insert(c.end(), {"-flto", "-c", src, "-o", obj.string()});
  ASSERT_EQ(ncc(c).exitCode, 0);

  auto l = base;
  l.erase(l.begin()); // remove -std=c11 for link
  l.insert(l.end(), {"-flto", obj.string(), "-o", exe.string()});
  ASSERT_EQ(ncc(l).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 3) << "hello_lto should exit 3";
}

TEST_F(LTOTest, MultiTU_AB) {
  auto ltoDir = testDir() / "lto";
  auto objA = tmpFile("lto_a.o");
  auto objB = tmpFile("lto_b.o");
  auto exe = tmpFile("lto_ab");

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  auto a1 = base;
  a1.insert(a1.end(),
            {"-flto", "-c", (ltoDir / "test_lto_a.c").string(), "-o",
             objA.string()});
  ASSERT_EQ(ncc(a1).exitCode, 0);

  auto a2 = base;
  a2.insert(a2.end(),
            {"-flto", "-c", (ltoDir / "test_lto_b.c").string(), "-o",
             objB.string()});
  ASSERT_EQ(ncc(a2).exitCode, 0);

  std::vector<std::string> link;
  for (auto &f : sysrootFlags()) link.push_back(f);
  for (auto &f : archFlags()) link.push_back(f);
  link.insert(link.end(),
              {"-flto", objA.string(), objB.string(), "-o", exe.string()});
  ASSERT_EQ(ncc(link).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.contains("add(3,4)=7"));
}

// Guards the driver forwarding of user -mllvm flags into the link job
// (populateLinkerDriverConfig -> LinkerDriverConfig::mllvmOpts ->
// parseMllvmOptions).  Under (auto-)LTO the optimizer runs at link time,
// so flags like -neverc-module-inliner-threshold are meaningless unless
// they reach the linker's cl::opt parsing.
TEST_F(LTOTest, MllvmReachesLinkJob) {
  auto ltoDir = testDir() / "lto";
  auto objA = tmpFile("mllvm_a.o");
  auto objB = tmpFile("mllvm_b.o");

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  auto a1 = base;
  a1.insert(a1.end(),
            {"-flto", "-c", (ltoDir / "test_lto_a.c").string(), "-o",
             objA.string()});
  ASSERT_EQ(ncc(a1).exitCode, 0);

  auto a2 = base;
  a2.insert(a2.end(),
            {"-flto", "-c", (ltoDir / "test_lto_b.c").string(), "-o",
             objB.string()});
  ASSERT_EQ(ncc(a2).exitCode, 0);

  std::vector<std::string> link;
  for (auto &f : sysrootFlags()) link.push_back(f);
  for (auto &f : archFlags()) link.push_back(f);
  link.insert(link.end(), {"-flto", objA.string(), objB.string()});

  // A valid link-stage LLVM option must be accepted and produce a working
  // binary.
  auto good = link;
  auto exeGood = tmpFile("mllvm_good");
  good.insert(good.end(), {"-mllvm", "-neverc-module-inliner-threshold=0",
                           "-o", exeGood.string()});
  ASSERT_EQ(ncc(good).exitCode, 0);
  auto r = exec(exeGood.string(), {});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.contains("add(3,4)=7"));

  // An unknown option must make the link fail: this proves the flag was
  // actually parsed by the link job instead of being silently dropped
  // (the pre-fix behavior).
  auto bad = link;
  auto exeBad = tmpFile("mllvm_bad");
  bad.insert(bad.end(), {"-mllvm", "-neverc-no-such-option-guard", "-o",
                         exeBad.string()});
  auto br = ncc(bad);
  EXPECT_NE(br.exitCode, 0)
      << "link must fail on unknown -mllvm option; succeeding means the "
         "flag was dropped before reaching the linker";
  EXPECT_TRUE(br.stderrContains("Unknown command line argument"))
      << "stderr: " << br.err;
}

// LTO link cache (LTOCache.cpp): a second link with identical inputs and
// flags must hit the cache and produce a bit-identical binary; disabling
// via NEVERC_LTO_CACHE=0 must not write entries; changing a flag that
// affects codegen must miss.
TEST_F(LTOTest, LtoLinkCache) {
  auto ltoDir = testDir() / "lto";
  auto cacheDir = tmpFile("ltocache_dir");
  setEnvVar(linker::ltoCacheDirEnvVar, cacheDir.string().c_str());

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  auto objA = tmpFile("ltocache_a.o");
  auto objB = tmpFile("ltocache_b.o");
  auto a1 = base;
  a1.insert(a1.end(), {"-flto", "-c", (ltoDir / "test_lto_a.c").string(), "-o",
                       objA.string()});
  ASSERT_EQ(ncc(a1).exitCode, 0);
  auto a2 = base;
  a2.insert(a2.end(), {"-flto", "-c", (ltoDir / "test_lto_b.c").string(), "-o",
                       objB.string()});
  ASSERT_EQ(ncc(a2).exitCode, 0);

  std::vector<std::string> link;
  for (auto &f : sysrootFlags()) link.push_back(f);
  for (auto &f : archFlags()) link.push_back(f);
  link.insert(link.end(), {"-flto", objA.string(), objB.string()});
  // COFF stamps the PE header with the wall-clock second by default
  // (incremental-linker compatibility); two otherwise identical links
  // differ whenever that second ticks over.  Request reproducible output
  // (timestamp = content hash) so the cold/warm byte comparison below
  // only measures cache correctness.
  if (isWindows())
    link.push_back("-mno-incremental-linker-compatible");

  auto countEntries = [&] {
    size_t n = 0;
    std::error_code ec;
    for (fs::directory_iterator it(cacheDir, ec), e; !ec && it != e;
         it.increment(ec))
      if (it->path().filename().string().rfind(linker::ltoCacheEntryPrefix,
                                               0) == 0 &&
          it->path().extension() != linker::ltoCacheTmpSuffix)
        ++n;
    return n;
  };

  // Disabled: no entries may be written.
  auto exeOff = tmpFile("ltocache_off");
  setEnvVar(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
  auto off = link;
  off.insert(off.end(), {"-o", exeOff.string()});
  ASSERT_EQ(ncc(off).exitCode, 0);
  unsetEnvVar(linker::ltoCacheEnvVar);
  EXPECT_EQ(countEntries(), 0u);

  // Cold link populates the cache; warm link must be bit-identical.
  auto exe = tmpFile("ltocache_exe");
  auto l1 = link;
  l1.insert(l1.end(), {"-o", exe.string()});
  ASSERT_EQ(ncc(l1).exitCode, 0);
  size_t afterCold = countEntries();
  EXPECT_GE(afterCold, 1u);
  std::string cold = readFile(exe);

  ASSERT_EQ(ncc(l1).exitCode, 0);
  std::string warm = readFile(exe);
  EXPECT_EQ(countEntries(), afterCold) << "warm link must not add entries";
  EXPECT_TRUE(cold == warm) << "cache hit produced a different binary";
  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.contains("add(3,4)=7"));

  // A codegen-relevant flag change must miss (new entry).
  auto exeO0 = tmpFile("ltocache_o0");
  auto l2 = link;
  l2.insert(l2.end(), {"-O0", "-o", exeO0.string()});
  ASSERT_EQ(ncc(l2).exitCode, 0);
  EXPECT_GT(countEntries(), afterCold) << "flag change must be a cache miss";

  unsetEnvVar(linker::ltoCacheDirEnvVar);
}

TEST_F(LTOTest, InlineAsmLTO) {
  auto asmDir = testDir() / "asm";
  auto objMain = tmpFile("asm_lto_main.o");
  auto objHelper = tmpFile("asm_lto_helper.o");
  auto exe = tmpFile("asm_lto");

  std::vector<std::string> base = {"-std=gnu11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  auto a1 = base;
  a1.insert(a1.end(),
            {"-flto", "-c", (asmDir / "test_inline_asm_lto_main.c").string(),
             "-o", objMain.string()});
  ASSERT_EQ(ncc(a1).exitCode, 0);

  auto a2 = base;
  a2.insert(a2.end(),
            {"-flto", "-c", (asmDir / "test_inline_asm_lto_helper.c").string(),
             "-o", objHelper.string()});
  ASSERT_EQ(ncc(a2).exitCode, 0);

  std::vector<std::string> link;
  for (auto &f : sysrootFlags()) link.push_back(f);
  for (auto &f : archFlags()) link.push_back(f);
  link.insert(link.end(), {"-flto", objMain.string(), objHelper.string(), "-o",
                           exe.string()});
  ASSERT_EQ(ncc(link).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.contains("test_inline_asm_lto: ALL PASSED"));
}

TEST_F(LTOTest, InlineAsmGCCWithLTO) {
  auto src = (testDir() / "asm/test_inline_asm_gcc.c").string();
  auto obj = tmpFile("inline_asm_gcc_lto.o");
  auto exe = tmpFile("inline_asm_gcc_lto");

  std::vector<std::string> base = {"-std=gnu11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  auto c = base;
  c.insert(c.end(), {"-flto", "-c", src, "-o", obj.string()});
  ASSERT_EQ(ncc(c).exitCode, 0);

  std::vector<std::string> link;
  for (auto &f : sysrootFlags()) link.push_back(f);
  for (auto &f : archFlags()) link.push_back(f);
  link.insert(link.end(), {"-flto", obj.string(), "-o", exe.string()});
  ASSERT_EQ(ncc(link).exitCode, 0);

  auto r = exec(exe.string(), {});
  EXPECT_EQ(r.exitCode, 0);
  EXPECT_TRUE(r.contains("test_inline_asm_gcc: ALL PASSED"));
}
