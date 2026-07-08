#include "NeverCTestFixture.h"

#include "neverc/Linker/Core/Driver/LTOCacheContract.h"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <sstream>

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

// Per-partition object cache (LTOCache.cpp + ParallelCodeGenMerge.cpp):
// partition assignment is a stable name hash, and each partition's object
// is cached keyed on its post-IPO bitcode.  Editing one function must
// invalidate only the full-link entry plus the single partition that
// contains the function; the relink mixing cached and fresh partitions
// must be byte-identical to a cache-disabled clean relink.
TEST_F(LTOTest, LtoPartitionCache) {
  auto cacheDir = tmpFile("pcache_dir");
  setEnvVar(linker::ltoCacheDirEnvVar, cacheDir.string().c_str());

  // Generate a project that crosses the partitioned-codegen thresholds
  // (>= 8 surviving functions, >= 10000 merged IR instructions) and
  // stays partition-stable: noinline bodies seeded from a volatile
  // global, no cross-file calls.
  constexpr int NFiles = 16, NFuncs = 4, NStmts = 100;
  auto srcDir = tmpFile("pcache_src");
  fs::create_directories(srcDir);

  auto fnBody = [&](int fi, int fj, int extra) {
    std::string b;
    b += "__attribute__((noinline)) unsigned f_" + std::to_string(fi) + "_" +
         std::to_string(fj) + "(unsigned a) {\n";
    b += "  unsigned x = g_seed + a;\n";
    for (int s = 0; s < NStmts + extra; ++s) {
      unsigned mul = (2654435761u + 2654435761u * unsigned(s) +
                      97u * unsigned(fi) + 31u * unsigned(fj)) |
                     1u;
      b += "  x ^= x >> " + std::to_string(5 + (s % 11)) + "; x *= " +
           std::to_string(mul) + "u; x ^= x << " +
           std::to_string(3 + (s % 7)) + ";\n";
    }
    b += "  return x;\n}\n";
    return b;
  };
  auto writeUnit = [&](int fi, int extraInLastFn) {
    std::string src = "extern volatile unsigned g_seed;\n";
    for (int fj = 0; fj < NFuncs; ++fj)
      src += fnBody(fi, fj, fj == NFuncs - 1 ? extraInLastFn : 0);
    writeFile(srcDir / ("u" + std::to_string(fi) + ".c"), src);
  };
  for (int fi = 0; fi < NFiles; ++fi)
    writeUnit(fi, 0);
  {
    std::string m = "#include <stdio.h>\nvolatile unsigned g_seed = "
                    "0x12345678u;\n";
    for (int fi = 0; fi < NFiles; ++fi)
      for (int fj = 0; fj < NFuncs; ++fj)
        m += "extern unsigned f_" + std::to_string(fi) + "_" +
             std::to_string(fj) + "(unsigned);\n";
    m += "int main(void) {\n  unsigned acc = 0;\n";
    for (int fi = 0; fi < NFiles; ++fi)
      for (int fj = 0; fj < NFuncs; ++fj)
        m += "  acc ^= f_" + std::to_string(fi) + "_" + std::to_string(fj) +
             "(" + std::to_string(fi * NFuncs + fj) + "u);\n";
    m += "  printf(\"CK=%08x\\n\", acc);\n  return 0;\n}\n";
    writeFile(srcDir / "main.c", m);
  }

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  // Default driver mode = auto-LTO: objects carry bitcode, the link runs
  // the partitioned LTO pipeline this test exercises.
  auto compileUnit = [&](const std::string &stem) {
    auto c = base;
    c.insert(c.end(), {"-c", (srcDir / (stem + ".c")).string(), "-o",
                       (srcDir / (stem + ".o")).string()});
    ASSERT_EQ(ncc(c).exitCode, 0) << stem;
  };
  for (int fi = 0; fi < NFiles; ++fi)
    compileUnit("u" + std::to_string(fi));
  compileUnit("main");

  std::vector<std::string> link;
  for (auto &f : sysrootFlags()) link.push_back(f);
  for (auto &f : archFlags()) link.push_back(f);
  for (int fi = 0; fi < NFiles; ++fi)
    link.push_back((srcDir / ("u" + std::to_string(fi) + ".o")).string());
  link.push_back((srcDir / "main.o").string());
  if (isWindows())
    link.push_back("-mno-incremental-linker-compatible");
  auto exe = tmpFile("pcache_exe");
  link.insert(link.end(), {"-o", exe.string()});

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

  // Cold link: one full-link entry + one entry per partition.
  ASSERT_EQ(ncc(link).exitCode, 0);
  size_t afterCold = countEntries();
  ASSERT_GE(afterCold, 3u) << "expected partitioned codegen (>= 2 partitions)";
  auto r1 = exec(exe.string(), {});
  ASSERT_EQ(r1.exitCode, 0);
  ASSERT_TRUE(r1.contains("CK=")) << r1.out;

  // Edit one function body in one unit: only that partition plus the
  // full-link key may miss.
  writeUnit(3, 2);
  compileUnit("u3");
  ASSERT_EQ(ncc(link).exitCode, 0);
  size_t afterEdit = countEntries();
  EXPECT_EQ(afterEdit, afterCold + 2)
      << "an edit to one function must add exactly one full-link entry and "
         "one partition entry; more means partition assignment is unstable";
  std::string mixed = readFile(exe);

  // The mixed cached/fresh link must equal a cache-disabled clean link.
  setEnvVar(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
  ASSERT_EQ(ncc(link).exitCode, 0);
  unsetEnvVar(linker::ltoCacheEnvVar);
  std::string clean = readFile(exe);
  EXPECT_TRUE(mixed == clean)
      << "cached-partition relink differs from clean relink";

  auto r2 = exec(exe.string(), {});
  EXPECT_EQ(r2.exitCode, 0);
  EXPECT_TRUE(r2.contains("CK=")) << r2.out;
  EXPECT_NE(r1.out, r2.out) << "edit must change the checksum";

  unsetEnvVar(linker::ltoCacheDirEnvVar);
}

// Auto-LTO compile-time cliff guard for the two cooperating valves that tame
// it: the inline cap (Inliner.cpp's NevercInlineMaxCallerLoops) and the
// full-unroll cap (LoopUnrollPass.cpp's NevercFullUnrollMaxLoopsPerFunc).  When
// main calls many small loop-bearing leaves exactly once, last-call-to-static
// inlining wants to fold them all into main -- a single function with hundreds
// of fully-unrollable constant-trip loops -- and full unrolling then makes
// ScalarEvolution's trip-count / exit-value machinery superlinear in the loop
// count (measured ~O(N^2); N=360 used to time out entirely).
//
// The two caps are complementary, which is *why this test must disable both* to
// see the cliff: the inline cap alone already stops main growing past its loop
// limit (NevercInlineMaxCallerLoops, default 32), so toggling only the unroll
// cap barely moves the needle (measured 0.6s vs 0.6s -- a coin-flip timing
// assertion, the historical flake here).  With both caps off the collapse and
// the unroll blowup both fire and the link detonates (measured ~0.7s vs ~6s+, a
// ~10x gap), giving the guard a wide, non-flaky margin on any hardware.
//
// This pins both halves of the contract: (1) the same program links far faster
// with the caps at their defaults than with both disabled, and (2) the two
// binaries produce identical output -- the caps may only withdraw an
// optimization, never change program semantics.
TEST_F(LTOTest, AutoLtoLoopDenseNoCompileCliff) {
  // Cold, comparable links: disable both cache layers so neither timing is a
  // cache hit of the other.
  setEnvVar(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
  setEnvVar(linker::ltoPartitionCacheEnvVar, linker::ltoCacheDisableValue);

  constexpr int NFiles = 15, NFuncsPerFile = 10; // 150 single-call leaves
  auto srcDir = tmpFile("cliff_src");
  fs::create_directories(srcDir);

  std::vector<std::string> names;
  for (int fi = 0; fi < NFiles; ++fi) {
    std::string src = "#include <stdint.h>\n";
    for (int fj = 0; fj < NFuncsPerFile; ++fj) {
      std::string nm = "fn_" + std::to_string(fi) + "_" + std::to_string(fj);
      names.push_back(nm);
      // Distinct odd constants per function so nothing folds them together;
      // a constant-trip (7) loop makes each a full-unroll candidate.
      unsigned c1 = (2654435761u * unsigned(fi * 131 + fj + 1)) | 1u;
      unsigned c2 = (40503u * unsigned(fi + 7) +
                     2246822519u * unsigned(fj + 3)) | 1u;
      unsigned c3 = (2166136261u ^ (16777619u * unsigned(fi * 17 + fj))) | 1u;
      src += "uint64_t " + nm + "(uint64_t x){ uint64_t a=x^" +
             std::to_string(c1) + "ULL; for(int i=0;i<7;i++){ a=a*" +
             std::to_string(c2) + "ULL+(a>>13)+i; if(a&1) a^=" +
             std::to_string(c3) + "ULL; } return a; }\n";
    }
    writeFile(srcDir / ("m" + std::to_string(fi) + ".c"), src);
  }
  {
    std::string m = "#include <stdint.h>\n#include <stdio.h>\n";
    for (auto &n : names)
      m += "uint64_t " + n + "(uint64_t);\n";
    m += "int main(void){ uint64_t acc=1;\n for(int r=0;r<3;r++){\n";
    for (auto &n : names)
      m += "  acc=acc*1000003ULL+" + n + "(acc);\n";
    m += " }\n printf(\"CK=%llu\\n\",(unsigned long long)acc); return 0; }\n";
    writeFile(srcDir / "main.c", m);
  }

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  // Default driver mode = auto-LTO: objects carry bitcode and the whole-program
  // optimizer (inliner + unroller) runs at link time, which is where the cliff
  // lives.
  std::vector<std::string> objs;
  auto compileUnit = [&](const std::string &stem) {
    auto c = base;
    auto o = (srcDir / (stem + ".o")).string();
    c.insert(c.end(),
             {"-c", (srcDir / (stem + ".c")).string(), "-o", o});
    ASSERT_EQ(ncc(c).exitCode, 0) << stem;
    objs.push_back(o);
  };
  for (int fi = 0; fi < NFiles; ++fi)
    compileUnit("m" + std::to_string(fi));
  compileUnit("main");

  auto linkArgs = [&](const std::string &exe, bool capsOff) {
    std::vector<std::string> l;
    for (auto &f : sysrootFlags()) l.push_back(f);
    for (auto &f : archFlags()) l.push_back(f);
    for (auto &o : objs) l.push_back(o);
    if (capsOff) {
      // Reproduce the pre-fix pathology *in full*.  Both caps must be off:
      // disabling only the unroll cap leaves the inline cap holding main at
      // ~12 loops, so the superlinear blowup never forms and the timing arms
      // become indistinguishable (the historical flake).  Off together, main
      // collapses to one giant function and the unroller detonates SCEV.
      l.push_back("-mllvm");
      l.push_back("-neverc-full-unroll-max-loops-per-function=0");
      l.push_back("-mllvm");
      l.push_back("-neverc-inline-max-caller-loops=0");
    }
    if (isWindows())
      l.push_back("-mno-incremental-linker-compatible");
    l.insert(l.end(), {"-o", exe});
    return l;
  };

  auto exeOn = tmpFile("cliff_on");
  auto t0 = std::chrono::steady_clock::now();
  auto rOn = ncc(linkArgs(exeOn.string(), /*capsOff=*/false));
  auto t1 = std::chrono::steady_clock::now();
  ASSERT_EQ(rOn.exitCode, 0) << rOn.err;
  double tOn = std::chrono::duration<double>(t1 - t0).count();

  auto exeOff = tmpFile("cliff_off");
  auto t2 = std::chrono::steady_clock::now();
  auto rOff = ncc(linkArgs(exeOff.string(), /*capsOff=*/true));
  auto t3 = std::chrono::steady_clock::now();
  ASSERT_EQ(rOff.exitCode, 0) << rOff.err;
  double tOff = std::chrono::duration<double>(t3 - t2).count();

  // (1) Semantics must be unchanged by the caps.
  auto outOn = exec(exeOn.string(), {});
  auto outOff = exec(exeOff.string(), {});
  EXPECT_EQ(outOn.exitCode, 0);
  EXPECT_EQ(outOff.exitCode, 0);
  EXPECT_TRUE(outOn.contains("CK=")) << outOn.out;
  EXPECT_EQ(outOn.out, outOff.out)
      << "a cap changed program output (caps must be semantics-preserving)";

  // (2) The caps must mitigate the superlinear blowup.  The real separation
  // with both caps off is ~10x (measured ~0.6s vs ~6s on a 16-core host), so
  // requiring the capped link to be under half the uncapped link is a wide,
  // non-flaky margin that still fails loudly if either cap regresses (then the
  // collapse/unroll fires in the "on" arm too and the times converge).
  EXPECT_LT(tOn, tOff * 0.5)
      << "loop-density caps gave no link-time benefit (tOn=" << tOn
      << "s tOff=" << tOff << "s): NevercInlineMaxCallerLoops or "
         "NevercFullUnrollMaxLoopsPerFunc may have regressed";

  unsetEnvVar(linker::ltoPartitionCacheEnvVar);
  unsetEnvVar(linker::ltoCacheEnvVar);
}

// Auto-LTO determinism contract: the parallel-codegen + merge pipeline must be a
// pure function of its inputs, independent of how many worker threads happen to
// run it.  The partition count is derived only from the module (instruction /
// loop / function counts), never from hardware_concurrency(), and partition
// results are collected by index, not completion order -- so a 1-thread build, a
// 4-thread build and a 16-thread build of the same sources must emit a
// byte-identical object.  Pinning this guards two things at once: that execution
// parallelism never leaks into the output (e.g. a future change collecting
// results in finish order), and that the object is reproducible across machines
// with different core counts (the same property, since NEVERC_PCG_THREADS here
// stands in for a different host's core count).
//
// The artifact compared is the relocatable (`-r`) merge -- the merger's direct
// output and exactly the shape a kernel module (.ko) ships -- not a final
// executable, whose linker-generated UUID / ad-hoc code signature legitimately
// vary run to run and would mask the property under test.
TEST_F(LTOTest, AutoLtoMergeIsThreadCountIndependent) {
  // Disable both cache layers so every link genuinely re-runs parallel codegen
  // rather than restoring a previous link's stored object (which would make the
  // comparison trivially pass without exercising codegen at all).
  setEnvVar(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
  setEnvVar(linker::ltoPartitionCacheEnvVar, linker::ltoCacheDisableValue);

  // 128 loop-bearing leaves: well above the parallel-codegen engagement floors
  // (>= 8 functions, >= 56 loops) so the path is exercised, and enough loops
  // that the work estimate asks for several partitions (a multi-partition merge
  // is what could expose a thread-order dependency).
  constexpr int NFiles = 16, NFuncsPerFile = 8;
  auto srcDir = tmpFile("det_src");
  fs::create_directories(srcDir);

  std::vector<std::string> names;
  for (int fi = 0; fi < NFiles; ++fi) {
    std::string src = "#include <stdint.h>\n";
    for (int fj = 0; fj < NFuncsPerFile; ++fj) {
      std::string nm = "fn_" + std::to_string(fi) + "_" + std::to_string(fj);
      names.push_back(nm);
      unsigned c1 = (2654435761u * unsigned(fi * 131 + fj + 1)) | 1u;
      unsigned c2 = (40503u * unsigned(fi + 7) +
                     2246822519u * unsigned(fj + 3)) | 1u;
      src += "uint64_t " + nm + "(uint64_t x){ uint64_t a=x^" +
             std::to_string(c1) + "ULL; for(int i=0;i<7;i++){ a=a*" +
             std::to_string(c2) + "ULL+(a>>13)+i; } return a; }\n";
    }
    writeFile(srcDir / ("m" + std::to_string(fi) + ".c"), src);
  }
  {
    std::string m = "#include <stdint.h>\n#include <stdio.h>\n";
    for (auto &n : names)
      m += "uint64_t " + n + "(uint64_t);\n";
    m += "int main(void){ uint64_t acc=1;\n";
    for (auto &n : names)
      m += "  acc=acc*1000003ULL+" + n + "(acc);\n";
    m += " printf(\"CK=%llu\\n\",(unsigned long long)acc); return 0; }\n";
    writeFile(srcDir / "main.c", m);
  }

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  std::vector<std::string> objs;
  auto compileUnit = [&](const std::string &stem) {
    auto c = base;
    auto o = (srcDir / (stem + ".o")).string();
    c.insert(c.end(), {"-c", (srcDir / (stem + ".c")).string(), "-o", o});
    ASSERT_EQ(ncc(c).exitCode, 0) << stem;
    objs.push_back(o);
  };
  for (int fi = 0; fi < NFiles; ++fi)
    compileUnit("m" + std::to_string(fi));
  compileUnit("main");

  // Relocatable (`-r`) merge under a given worker-thread count.
  auto mergeWithThreads = [&](const char *threads) -> std::string {
    setEnvVar("NEVERC_PCG_THREADS", threads);
    std::vector<std::string> l;
    for (auto &f : sysrootFlags()) l.push_back(f);
    for (auto &f : archFlags()) l.push_back(f);
    // COFF stamps the PE header with the wall-clock second by default
    // (incremental-linker compatibility); the 1-, 4- and 16-thread merges run
    // seconds apart, so that timestamp byte alone differs and masquerades as a
    // parallelism leak.  Request reproducible output (timestamp = content hash)
    // so this comparison only measures codegen determinism, matching the
    // LtoLinkCache test above.
    if (isWindows())
      l.push_back("-mno-incremental-linker-compatible");
    l.push_back("-r");
    for (auto &o : objs) l.push_back(o);
    auto out = tmpFile(std::string("det_merge_") + threads + ".o");
    l.insert(l.end(), {"-o", out.string()});
    auto r = ncc(l);
    EXPECT_EQ(r.exitCode, 0) << "threads=" << threads << ": " << r.err;
    return readFile(out);
  };

  std::string o1 = mergeWithThreads("1");
  std::string o4 = mergeWithThreads("4");
  std::string o16 = mergeWithThreads("16");
  unsetEnvVar("NEVERC_PCG_THREADS");

  ASSERT_FALSE(o1.empty()) << "relocatable merge produced no object";
  EXPECT_EQ(o1, o4) << "auto-LTO object differs between 1 and 4 worker threads "
                       "-- execution parallelism leaked into the emitted bytes "
                       "(non-reproducible build)";
  EXPECT_EQ(o1, o16) << "auto-LTO object differs between 1 and 16 worker threads "
                        "-- execution parallelism leaked into the emitted bytes";

  unsetEnvVar(linker::ltoPartitionCacheEnvVar);
  unsetEnvVar(linker::ltoCacheEnvVar);
}

// The auto-LTO loop-density inline cap (Inliner.cpp's
// NevercInlineMaxCallerLoops) withdraws *cost-driven* inlining of loop-bearing
// callees into an already-loop-dense caller; it must never change program
// semantics.  main() calls every leaf exactly once, so last-call-to-static
// inlining would otherwise fold them all into main -- the very shape the cap
// targets.  Build the same program twice, once with the cap at a deliberately
// low value (so it engages hard) and once disabled, and require byte-identical
// program *output*: the cap may only trade code shape / compile time, never
// results.
TEST_F(LTOTest, AutoLtoInlineCapSemanticsPreserved) {
  setEnvVar(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
  setEnvVar(linker::ltoPartitionCacheEnvVar, linker::ltoCacheDisableValue);

  constexpr int NFiles = 10, NFuncsPerFile = 8; // 80 single-call leaves
  auto srcDir = tmpFile("inlinecap_src");
  fs::create_directories(srcDir);

  std::vector<std::string> names;
  for (int fi = 0; fi < NFiles; ++fi) {
    std::string src = "#include <stdint.h>\n";
    for (int fj = 0; fj < NFuncsPerFile; ++fj) {
      std::string nm = "lf_" + std::to_string(fi) + "_" + std::to_string(fj);
      names.push_back(nm);
      unsigned c1 = (2246822519u * unsigned(fi * 71 + fj + 1)) | 1u;
      unsigned c2 = (3266489917u * unsigned(fi + 5) +
                     668265263u * unsigned(fj + 2)) | 1u;
      // A constant-trip loop with a data-dependent branch: a loop-bearing leaf
      // the cap can choose to hold back from main.
      src += "uint64_t " + nm + "(uint64_t x){ uint64_t a=x^" +
             std::to_string(c1) + "ULL; for(int i=0;i<9;i++){ a=a*" +
             std::to_string(c2) + "ULL+(a>>11)+i; if(a&2) a+=" +
             std::to_string(c1) + "ULL; } return a; }\n";
    }
    writeFile(srcDir / ("m" + std::to_string(fi) + ".c"), src);
  }
  {
    std::string m = "#include <stdint.h>\n#include <stdio.h>\n";
    for (auto &n : names)
      m += "uint64_t " + n + "(uint64_t);\n";
    m += "int main(void){ uint64_t acc=1;\n for(int r=0;r<2;r++){\n";
    for (auto &n : names)
      m += "  acc=acc*1000003ULL+" + n + "(acc);\n";
    m += " }\n printf(\"CK=%llu\\n\",(unsigned long long)acc); return 0; }\n";
    writeFile(srcDir / "main.c", m);
  }

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  std::vector<std::string> objs;
  auto compileUnit = [&](const std::string &stem) {
    auto c = base;
    auto o = (srcDir / (stem + ".o")).string();
    c.insert(c.end(), {"-c", (srcDir / (stem + ".c")).string(), "-o", o});
    ASSERT_EQ(ncc(c).exitCode, 0) << stem;
    objs.push_back(o);
  };
  for (int fi = 0; fi < NFiles; ++fi)
    compileUnit("m" + std::to_string(fi));
  compileUnit("main");

  auto linkExe = [&](const std::string &exe, int capValue) {
    std::vector<std::string> l;
    for (auto &f : sysrootFlags()) l.push_back(f);
    for (auto &f : archFlags()) l.push_back(f);
    for (auto &o : objs) l.push_back(o);
    l.push_back("-mllvm");
    l.push_back("-neverc-inline-max-caller-loops=" + std::to_string(capValue));
    if (isWindows())
      l.push_back("-mno-incremental-linker-compatible");
    l.insert(l.end(), {"-o", exe});
    return ncc(l);
  };

  auto exeCap = tmpFile("inlinecap_on");
  ASSERT_EQ(linkExe(exeCap.string(), /*capValue=*/4).exitCode, 0);
  auto exeNoCap = tmpFile("inlinecap_off");
  ASSERT_EQ(linkExe(exeNoCap.string(), /*capValue=*/0).exitCode, 0);

  auto outCap = exec(exeCap.string(), {});
  auto outNoCap = exec(exeNoCap.string(), {});
  EXPECT_EQ(outCap.exitCode, 0) << outCap.err;
  EXPECT_EQ(outNoCap.exitCode, 0) << outNoCap.err;
  EXPECT_TRUE(outCap.contains("CK=")) << outCap.out;
  EXPECT_EQ(outCap.out, outNoCap.out)
      << "the loop-density inline cap changed program output -- it must be "
         "purely an optimization withdrawal, never a semantic change";

  unsetEnvVar(linker::ltoPartitionCacheEnvVar);
  unsetEnvVar(linker::ltoCacheEnvVar);
}

// The auto-LTO SCEV huge-expression bound (ParallelCodeGenMerge's
// PcgScevHugeExprThreshold, which lowers ScalarEvolution's HugeExprThreshold for
// the per-partition optimization) must be a pure compile-cost knob: making SCEV
// fall back to its conservative *unsimplified* form on oversized expressions --
// exactly what the MaxArithDepth check beside it already does -- can change code
// shape and compile time, never the computed result.  Build the same program
// with the bound set deliberately tiny (so it fires on essentially every
// expression the whole-program functions produce) and with it disabled (stock
// ScalarEvolution), and require byte-identical program output.  This is a timing
// -free invariant, so it can never flake.
TEST_F(LTOTest, AutoLtoScevHugeThresholdSemanticsPreserved) {
  setEnvVar(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
  setEnvVar(linker::ltoPartitionCacheEnvVar, linker::ltoCacheDisableValue);

  constexpr int NFiles = 10, NFuncsPerFile = 8; // 80 loop+select leaves
  auto srcDir = tmpFile("scevsem_src");
  fs::create_directories(srcDir);

  std::vector<std::string> names;
  for (int fi = 0; fi < NFiles; ++fi) {
    std::string src = "#include <stdint.h>\n";
    for (int fj = 0; fj < NFuncsPerFile; ++fj) {
      std::string nm = "sf_" + std::to_string(fi) + "_" + std::to_string(fj);
      names.push_back(nm);
      unsigned c1 = (2246822519u * unsigned(fi * 71 + fj + 1)) | 1u;
      unsigned c2 = (3266489917u * unsigned(fi + 5) +
                     668265263u * unsigned(fj + 2)) | 1u;
      // A constant-trip loop with a data-dependent branch: inlined into main it
      // helps build the large SCEV expressions the bound targets.
      src += "uint64_t " + nm + "(uint64_t x){ uint64_t a=x^" +
             std::to_string(c1) + "ULL; for(int i=0;i<9;i++){ a=a*" +
             std::to_string(c2) + "ULL+(a>>11)+i; if(a&2) a+=" +
             std::to_string(c1) + "ULL; } return a; }\n";
    }
    writeFile(srcDir / ("m" + std::to_string(fi) + ".c"), src);
  }
  {
    std::string m = "#include <stdint.h>\n#include <stdio.h>\n";
    for (auto &n : names)
      m += "uint64_t " + n + "(uint64_t);\n";
    m += "int main(void){ uint64_t acc=1;\n for(int r=0;r<2;r++){\n";
    for (auto &n : names)
      m += "  acc=acc*1000003ULL+" + n + "(acc);\n";
    m += " }\n printf(\"CK=%llu\\n\",(unsigned long long)acc); return 0; }\n";
    writeFile(srcDir / "main.c", m);
  }

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  std::vector<std::string> objs;
  auto compileUnit = [&](const std::string &stem) {
    auto c = base;
    auto o = (srcDir / (stem + ".o")).string();
    c.insert(c.end(), {"-c", (srcDir / (stem + ".c")).string(), "-o", o});
    ASSERT_EQ(ncc(c).exitCode, 0) << stem;
    objs.push_back(o);
  };
  for (int fi = 0; fi < NFiles; ++fi)
    compileUnit("m" + std::to_string(fi));
  compileUnit("main");

  auto linkExe = [&](const std::string &exe, unsigned scevThreshold) {
    std::vector<std::string> l;
    for (auto &f : sysrootFlags()) l.push_back(f);
    for (auto &f : archFlags()) l.push_back(f);
    for (auto &o : objs) l.push_back(o);
    l.push_back("-mllvm");
    l.push_back("-neverc-auto-lto-scev-huge-expr-threshold=" +
                std::to_string(scevThreshold));
    if (isWindows())
      l.push_back("-mno-incremental-linker-compatible");
    l.insert(l.end(), {"-o", exe});
    return ncc(l);
  };

  // Tiny bound (4): SCEV gives up simplifying almost immediately.
  auto exeTiny = tmpFile("scevsem_tiny");
  ASSERT_EQ(linkExe(exeTiny.string(), /*scevThreshold=*/4).exitCode, 0);
  // Disabled (0): ScalarEvolution's stock threshold.
  auto exeOff = tmpFile("scevsem_off");
  ASSERT_EQ(linkExe(exeOff.string(), /*scevThreshold=*/0).exitCode, 0);

  auto outTiny = exec(exeTiny.string(), {});
  auto outOff = exec(exeOff.string(), {});
  EXPECT_EQ(outTiny.exitCode, 0) << outTiny.err;
  EXPECT_EQ(outOff.exitCode, 0) << outOff.err;
  EXPECT_TRUE(outTiny.contains("CK=")) << outTiny.out;
  EXPECT_EQ(outTiny.out, outOff.out)
      << "the auto-LTO SCEV huge-expression bound changed program output -- it "
         "must only withdraw simplification, never change a result";

  unsetEnvVar(linker::ltoPartitionCacheEnvVar);
  unsetEnvVar(linker::ltoCacheEnvVar);
}

// Real auto-LTO + mergeSections E2E: compile the in-tree Android kernel multifile
// example (per-function .text.* sections folded into .text) and assert every
// exported function lands at a distinct, non-zero offset.  This is the exact
// shape that bit us when PartOffsets lookup collapsed every symbol to 0 —
// syntactic mergeTests cover the math, but only a neverc-emitted .ko exercises
// the full IPO → parallel-codegen → mergeSections → verify chain on real codegen.
TEST_F(LTOTest, AndroidKernelMultifileMergeSectionOffsets) {
  auto exDir = fs::canonical(testDir() / "../../examples/android-kernel-multifile");
  if (!fs::exists(exDir / "main.c"))
    GTEST_SKIP() << "android-kernel-multifile example not found";

  std::string llvmNm = "llvm-nm";
  if (exec("which", {"llvm-nm"}).exitCode != 0) {
    if (exec("/opt/homebrew/opt/llvm/bin/llvm-nm", {"--version"}).exitCode == 0)
      llvmNm = "/opt/homebrew/opt/llvm/bin/llvm-nm";
    else if (exec("/opt/homebrew/opt/llvm@22/bin/llvm-nm", {"--version"})
                 .exitCode == 0)
      llvmNm = "/opt/homebrew/opt/llvm@22/bin/llvm-nm";
    else
      GTEST_SKIP() << "llvm-nm not available";
  }

  auto ko = tmpFile("nvk_multi.ko");
  std::vector<std::string> args = {
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=510",
      "-r",
      "-nostdlib",
      "-o",
      ko.string(),
      (exDir / "main.c").string(),
      (exDir / "interposes.c").string(),
      (exDir / "utils.c").string(),
  };
  auto link = ncc(args);
  ASSERT_EQ(link.exitCode, 0) << link.err;

  auto symtab = exec(llvmNm, {ko.string()});
  ASSERT_EQ(symtab.exitCode, 0) << symtab.err;

  auto parseOffset = [&](const char *Name) -> uint64_t {
    std::string needle = std::string(" ") + Name;
    std::istringstream in(symtab.out);
    std::string line;
    while (std::getline(in, line)) {
      if (line.find(needle) == std::string::npos)
        continue;
      uint64_t off = std::strtoull(line.c_str(), nullptr, 16);
      if (off != 0 || line[0] == '0')
        return off;
    }
    ADD_FAILURE() << "symbol not found: " << Name;
    return 0;
  };

  uint64_t interposesInit = parseOffset("interposes_init");
  uint64_t interposesCleanup = parseOffset("interposes_cleanup");
  uint64_t initMod = parseOffset("init_module");
  uint64_t cleanupMod = parseOffset("cleanup_module");
  ASSERT_NE(interposesInit, 0u) << "interposes_init collapsed to .text+0 (SecOff regression)";
  ASSERT_NE(interposesCleanup, 0u);
  ASSERT_NE(initMod, 0u);
  ASSERT_NE(cleanupMod, 0u);
  EXPECT_NE(interposesInit, interposesCleanup);
  EXPECT_NE(interposesInit, initMod);
  EXPECT_NE(initMod, cleanupMod);
}

// NVK_KERNEL=618 must select the 6.18 preset (vermagic + file_operations layout).
TEST_F(LTOTest, AndroidKernel618PresetFromNvkKernel) {
  auto exDir = fs::canonical(testDir() / "../../examples/android-kernel-chardev");
  if (!fs::exists(exDir / "main.c"))
    GTEST_SKIP() << "android-kernel-chardev example not found";

  auto ko = tmpFile("nvk_chardev_618.ko");
  std::vector<std::string> args = {
      "--target=aarch64-linux-android",
      "-fandroid-kernel-driver-mode",
      "-DNVK_KERNEL=618",
      "-r",
      "-nostdlib",
      "-o",
      ko.string(),
      (exDir / "main.c").string(),
  };
  auto link = ncc(args);
  ASSERT_EQ(link.exitCode, 0) << link.err;

  auto stringsOut = exec("strings", {ko.string()});
  ASSERT_EQ(stringsOut.exitCode, 0) << stringsOut.err;
  EXPECT_NE(stringsOut.out.find("vermagic=6.18.24-android17-5"),
            std::string::npos)
      << "618 preset vermagic missing; NVK_KERNEL may not map to NEVERC_KRT_KERNEL";
}

// Auto-LTO vs clang full-LTO cold-link regression gate.  neverc's auto-LTO link
// (whole-program IPO with the loop-density inline cap + SCEV bound, then
// PARTITIONED PARALLEL codegen, then in-process merge) must stay at least as
// fast as clang's monolithic full-LTO link (serial codegen) on the multi-file
// loop-dense C that is auto-LTO's whole reason to exist -- and must produce a
// program with byte-identical observable output.  This welds the headline
// compile-speed balance shut against silent regression: if a future change makes
// neverc's link slower than a stock full-LTO link, or diverges its result, this
// goes red.
//
// Only the LINK (LTO codegen) step is timed -- both toolchains compile the same
// sources to bitcode first -- so the comparison is purely parallel-vs-serial LTO
// codegen on identical input, with no parallel-frontend fairness caveat.
//
// Robust by construction: if no comparison clang is found, or it cannot
// compile/link the workload in this environment (different sysroot, no LTO
// plugin, ...), the test SKIPS rather than failing -- it can only go red on a
// genuine neverc regression.  Set NEVERC_BENCH_CLANG to pin the comparator
// (e.g. a real clang-22); otherwise PATH `clang` is used.
TEST_F(LTOTest, AutoLtoLinkBeatsClangFullLTO) {
  std::string clang = "clang";
  if (const char *e = getenv("NEVERC_BENCH_CLANG"); e && *e)
    clang = e;
  if (exec(clang, {"--version"}).exitCode != 0)
    GTEST_SKIP() << "no usable comparison clang (set NEVERC_BENCH_CLANG)";

  // Many small single-call loop leaves: last-call-to-static inlining folds them
  // into main, the shape that makes a naive full-LTO link go SCEV-superlinear.
  // Integer-only (uint64 xorshift) so the checksum is bit-identical across
  // compilers -- no FP contraction/reassociation to legitimately diverge.
  constexpr int NFiles = 12, NFuncsPerFile = 12; // 144 leaves
  auto srcDir = tmpFile("clangcmp_src");
  fs::create_directories(srcDir);
  std::vector<std::string> names;
  for (int fi = 0; fi < NFiles; ++fi) {
    std::string src = "#include <stdint.h>\n";
    for (int fj = 0; fj < NFuncsPerFile; ++fj) {
      std::string nm = "fn_" + std::to_string(fi) + "_" + std::to_string(fj);
      names.push_back(nm);
      unsigned c1 = (2654435761u * unsigned(fi * 131 + fj + 1)) | 1u;
      unsigned c2 =
          (40503u * unsigned(fi + 7) + 2246822519u * unsigned(fj + 3)) | 1u;
      unsigned c3 = (2166136261u ^ (16777619u * unsigned(fi * 17 + fj))) | 1u;
      src += "uint64_t " + nm + "(uint64_t x){ uint64_t a=x^" +
             std::to_string(c1) + "ULL; for(int i=0;i<7;i++){ a=a*" +
             std::to_string(c2) + "ULL+(a>>13)+i; if(a&1) a^=" +
             std::to_string(c3) + "ULL; } return a; }\n";
    }
    writeFile(srcDir / ("m" + std::to_string(fi) + ".c"), src);
  }
  {
    std::string m = "#include <stdint.h>\n#include <stdio.h>\n";
    for (auto &n : names)
      m += "uint64_t " + n + "(uint64_t);\n";
    m += "int main(void){ uint64_t acc=1;\n for(int r=0;r<3;r++){\n";
    for (auto &n : names)
      m += "  acc=acc*1000003ULL+" + n + "(acc);\n";
    m += " }\n printf(\"CK=%llu\\n\",(unsigned long long)acc); return 0; }\n";
    writeFile(srcDir / "main.c", m);
  }

  std::vector<std::string> units;
  for (int fi = 0; fi < NFiles; ++fi)
    units.push_back("m" + std::to_string(fi));
  units.push_back("main");

  std::vector<std::string> base = {"-std=c11"};
  for (auto &f : sysrootFlags()) base.push_back(f);
  for (auto &f : archFlags()) base.push_back(f);

  // Compile each unit to bitcode with both toolchains (not timed).  A clang
  // compile failure means a toolchain/sysroot mismatch on this host, not a
  // neverc bug -> skip.
  std::vector<std::string> nvObjs, clObjs;
  for (auto &u : units) {
    auto srcp = (srcDir / (u + ".c")).string();
    auto nvo = (srcDir / (u + ".nv.o")).string();
    auto c = base;
    c.insert(c.end(), {"-c", srcp, "-o", nvo});
    ASSERT_EQ(ncc(c).exitCode, 0) << "neverc compile " << u;
    nvObjs.push_back(nvo);

    auto clo = (srcDir / (u + ".cl.o")).string();
    auto cr = exec(clang, {"-O2", "-flto", "-c", srcp, "-o", clo});
    if (cr.exitCode != 0)
      GTEST_SKIP() << "comparison clang cannot compile workload: " << cr.err;
    clObjs.push_back(clo);
  }

  // Cold neverc link: disable both cache layers so the timing is real codegen,
  // not a restored prior object.  Set late (after the clang work above that can
  // skip) and restored on every exit path below.
  setEnvVar(linker::ltoCacheEnvVar, linker::ltoCacheDisableValue);
  setEnvVar(linker::ltoPartitionCacheEnvVar, linker::ltoCacheDisableValue);

  // Time neverc auto-LTO link (IPO + partitioned parallel codegen + merge).
  auto nvExe = tmpFile("clangcmp_nv");
  std::vector<std::string> nvLink;
  for (auto &f : sysrootFlags()) nvLink.push_back(f);
  for (auto &f : archFlags()) nvLink.push_back(f);
  for (auto &o : nvObjs) nvLink.push_back(o);
  if (isWindows())
    nvLink.push_back("-mno-incremental-linker-compatible");
  nvLink.insert(nvLink.end(), {"-o", nvExe.string()});
  auto a0 = std::chrono::steady_clock::now();
  auto rNv = ncc(nvLink);
  auto a1 = std::chrono::steady_clock::now();
  double tNv = std::chrono::duration<double>(a1 - a0).count();
  ASSERT_EQ(rNv.exitCode, 0) << rNv.err;

  // Time clang monolithic full-LTO link (serial codegen).
  auto clExe = tmpFile("clangcmp_cl");
  std::vector<std::string> clLink = {"-O2", "-flto"};
  for (auto &o : clObjs) clLink.push_back(o);
  clLink.insert(clLink.end(), {"-o", clExe.string()});
  auto b0 = std::chrono::steady_clock::now();
  auto rCl = exec(clang, clLink);
  auto b1 = std::chrono::steady_clock::now();
  double tCl = std::chrono::duration<double>(b1 - b0).count();
  if (rCl.exitCode != 0) {
    unsetEnvVar(linker::ltoPartitionCacheEnvVar);
    unsetEnvVar(linker::ltoCacheEnvVar);
    GTEST_SKIP() << "comparison clang cannot link workload: " << rCl.err;
  }

  // (1) Identical observable result: neverc's parallel/merged LTO must agree
  // with a monolithic full-LTO compile of the same C, bit-for-bit on stdout.
  auto outNv = exec(nvExe.string(), {});
  auto outCl = exec(clExe.string(), {});
  EXPECT_EQ(outNv.exitCode, 0) << outNv.err;
  EXPECT_EQ(outCl.exitCode, 0) << outCl.err;
  EXPECT_TRUE(outNv.contains("CK=")) << outNv.out;
  EXPECT_EQ(outNv.out, outCl.out)
      << "neverc auto-LTO output diverged from clang full-LTO on identical C";

  // (2) Balance: neverc's auto-LTO link must not be slower than clang's
  // monolithic full-LTO link.  Asserted only once clang's link is clearly above
  // the per-invocation noise floor (a host that links this trivially fast on
  // both sides checks correctness only and never flakes); where it does engage,
  // neverc's measured margin is large (~5-25x), so the bare `<` is wide.
  if (tCl > 0.8)
    EXPECT_LT(tNv, tCl)
        << "neverc auto-LTO link (" << tNv
        << "s) is slower than clang full-LTO (" << tCl
        << "s) on loop-dense multi-file C -- the compile-speed balance regressed";

  unsetEnvVar(linker::ltoPartitionCacheEnvVar);
  unsetEnvVar(linker::ltoCacheEnvVar);
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
