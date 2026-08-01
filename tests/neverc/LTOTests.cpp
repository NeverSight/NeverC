#include "NeverCTestFixture.h"

#include "neverc/Linker/Core/Driver/LTOCacheContract.h"

#include "llvm/Bitcode/BitcodeWriter.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"

#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <optional>
#include <sstream>
#include <utility>

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

class ScopedEnvVar {
  std::string Name;
  std::optional<std::string> OldValue;

public:
  ScopedEnvVar(const char *Name, const char *Value) : Name(Name) {
    if (const char *Old = std::getenv(Name))
      OldValue = Old;
    setEnvVar(Name, Value);
  }

  ScopedEnvVar(const ScopedEnvVar &) = delete;
  ScopedEnvVar &operator=(const ScopedEnvVar &) = delete;

  ~ScopedEnvVar() {
    if (OldValue)
      setEnvVar(Name.c_str(), OldValue->c_str());
    else
      unsetEnvVar(Name.c_str());
  }
};

static double medianSeconds(std::vector<double> Values) {
  assert(!Values.empty());
  std::sort(Values.begin(), Values.end());
  return Values[Values.size() / 2];
}

class LTOTest : public NeverCTest {
protected:
  std::vector<std::string>
  writeAutoLtoLoopDenseProject(const std::string &Stem, bool RuntimeSeed) {
    constexpr int NFiles = 12;
    constexpr int NFuncsPerFile = 12;
    auto SrcDir = tmpFile(Stem);
    fs::create_directories(SrcDir);

    std::vector<std::string> Names;
    std::vector<std::string> Sources;
    for (int FI = 0; FI < NFiles; ++FI) {
      std::string Src = "#include <stdint.h>\n";
      for (int FJ = 0; FJ < NFuncsPerFile; ++FJ) {
        std::string Name =
            "fn_" + std::to_string(FI) + "_" + std::to_string(FJ);
        Names.push_back(Name);
        unsigned C1 =
            (2654435761u * unsigned(FI * 131 + FJ + 1)) | 1u;
        unsigned C2 =
            (40503u * unsigned(FI + 7) +
             2246822519u * unsigned(FJ + 3)) |
            1u;
        unsigned C3 =
            (2166136261u ^ (16777619u * unsigned(FI * 17 + FJ))) | 1u;
        Src += "uint64_t " + Name + "(uint64_t x){ uint64_t a=x^" +
               std::to_string(C1) +
               "ULL; for(int i=0;i<7;i++){ a=a*" + std::to_string(C2) +
               "ULL+(a>>13)+i; if(a&1) a^=" + std::to_string(C3) +
               "ULL; } return a; }\n";
      }
      auto Path = SrcDir / ("m" + std::to_string(FI) + ".c");
      writeFile(Path, Src);
      Sources.push_back(Path.string());
    }

    std::string Main = "#include <stdint.h>\n#include <stdio.h>\n";
    for (const auto &Name : Names)
      Main += "uint64_t " + Name + "(uint64_t);\n";
    if (RuntimeSeed)
      Main += "int main(int argc, char **argv){ (void)argv; "
              "uint64_t acc=(uint64_t)argc;\n";
    else
      Main += "int main(void){ uint64_t acc=1;\n";
    Main += "for(int r=0;r<3;r++){\n";
    for (const auto &Name : Names)
      Main += "acc=acc*1000003ULL+" + Name + "(acc);\n";
    Main += "}\nprintf(\"CK=%llu\\n\",(unsigned long long)acc);"
            " return 0; }\n";

    auto MainPath = SrcDir / "main.c";
    writeFile(MainPath, Main);
    Sources.push_back(MainPath.string());
    return Sources;
  }
};

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

TEST_F(LTOTest, AArch64UnalignedCrossCcTailCallFallsBack) {
  auto src = tmpFile("aarch64_unaligned_cross_cc_tail.bc");
  auto obj = tmpFile("aarch64_unaligned_cross_cc_tail.o");
  llvm::LLVMContext context;
  llvm::Module module("aarch64_unaligned_cross_cc_tail", context);
  module.setTargetTriple("aarch64-unknown-linux-gnu");
  llvm::Type *ptrTy = llvm::PointerType::getUnqual(context);
  llvm::Function *unlock = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getInt32Ty(context), {ptrTy}, false),
      llvm::Function::ExternalLinkage, "unlock", module);

  std::vector<llvm::Type *> storeArgs = {ptrTy};
  storeArgs.insert(storeArgs.end(), 8, llvm::Type::getInt64Ty(context));
  llvm::Function *store = llvm::Function::Create(
      llvm::FunctionType::get(llvm::Type::getVoidTy(context), storeArgs, false),
      llvm::Function::ExternalLinkage, "store", module);
  store->setCallingConv(llvm::CallingConv::Fast);
  llvm::IRBuilder<> builder(
      llvm::BasicBlock::Create(context, "entry", store));
  llvm::CallInst *call = builder.CreateCall(unlock, {store->getArg(0)});
  call->setTailCallKind(llvm::CallInst::TCK_Tail);
  builder.CreateRetVoid();

  llvm::SmallVector<char, 0> bitcode;
  llvm::raw_svector_ostream bitcodeStream(bitcode);
  llvm::WriteBitcodeToFile(module, bitcodeStream);
  writeFile(src, std::string(bitcode.begin(), bitcode.end()));

  auto result = ncc({"--target=aarch64-unknown-linux-gnu", "-fno-lto", "-c",
                     src.string(), "-o", obj.string()});
  EXPECT_TRUE(result.ok()) << "AArch64 codegen rejected a valid tail-call "
                             "candidate instead of lowering it as a normal "
                             "call:\n"
                          << result.err;
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
  ScopedEnvVar CacheDir(linker::ltoCacheDirEnvVar,
                        cacheDir.string().c_str());

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
  {
    ScopedEnvVar Disabled(linker::ltoCacheEnvVar,
                          linker::ltoCacheDisableValue);
    auto off = link;
    off.insert(off.end(), {"-o", exeOff.string()});
    ASSERT_EQ(ncc(off).exitCode, 0);
  }
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
}

// Per-partition object cache (LTOCache.cpp + ParallelCodeGenMerge.cpp):
// partition assignment is a stable name hash, and each partition's object
// is cached keyed on its post-IPO bitcode.  Editing one function must
// invalidate only the full-link entry plus the single partition that
// contains the function; the relink mixing cached and fresh partitions
// must be byte-identical to a cache-disabled clean relink.
TEST_F(LTOTest, LtoPartitionCache) {
  auto cacheDir = tmpFile("pcache_dir");
  ScopedEnvVar CacheDir(linker::ltoCacheDirEnvVar,
                        cacheDir.string().c_str());

  // Generate a project that crosses the partitioned-codegen thresholds
  // (>= 8 surviving functions, >= 10000 merged IR instructions) and
  // stays partition-stable: noinline bodies seeded from a volatile
  // global, no cross-file calls.
  constexpr int NFiles = 16, NFuncs = 4, NStmts = 100;
  auto srcDir = tmpFile("pcache_src");
  fs::create_directories(srcDir);

  auto fnBody = [&](int fi, int fj, int extra, bool emitCodegenError) {
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
    if (emitCodegenError)
      b += "  __asm__ volatile(\".error\");\n";
    b += "  return x;\n}\n";
    return b;
  };
  auto writeUnit = [&](int fi, int extraInLastFn,
                       bool emitCodegenError = false) {
    std::string src = "extern volatile unsigned g_seed;\n";
    for (int fj = 0; fj < NFuncs; ++fj)
      src += fnBody(fi, fj, fj == NFuncs - 1 ? extraInLastFn : 0,
                    emitCodegenError && fj == NFuncs - 1);
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
  {
    ScopedEnvVar Disabled(linker::ltoCacheEnvVar,
                          linker::ltoCacheDisableValue);
    ASSERT_EQ(ncc(link).exitCode, 0);
  }
  std::string clean = readFile(exe);
  EXPECT_TRUE(mixed == clean)
      << "cached-partition relink differs from clean relink";

  auto r2 = exec(exe.string(), {});
  EXPECT_EQ(r2.exitCode, 0);
  EXPECT_TRUE(r2.contains("CK=")) << r2.out;
  EXPECT_NE(r1.out, r2.out) << "edit must change the checksum";

  // A backend diagnostic can still leave bytes in the partition object
  // buffer.  That object is invalid and must not be committed to the cache.
  writeUnit(3, 3, /*emitCodegenError=*/true);
  compileUnit("u3");
  CmdResult firstFailure = ncc(link);
  EXPECT_NE(firstFailure.exitCode, 0);
  EXPECT_TRUE(firstFailure.stderrContains(".error directive invoked"))
      << "link did not reach the intentional backend diagnostic:\n"
      << firstFailure.err;

  // Other successful partitions may legitimately populate the fallback
  // pipeline's cache.  On retry those are hits, while the failed partition
  // must miss and reproduce its diagnostic.  Caching the failed object would
  // instead let the retry consume a condemned artifact.
  size_t afterFirstFailure = countEntries();
  CmdResult secondFailure = ncc(link);
  EXPECT_NE(secondFailure.exitCode, 0)
      << "a cached failed partition made an invalid link succeed";
  EXPECT_TRUE(secondFailure.stderrContains(".error directive invoked"))
      << "retry did not regenerate the failed partition:\n"
      << secondFailure.err;
  EXPECT_EQ(countEntries(), afterFirstFailure)
      << "retry added another entry for a partition that cannot codegen";
}

TEST_F(LTOTest, ParallelCodegenPreservesAliasUsers) {
  auto cacheDir = tmpFile("pcg_alias_cache");
  ScopedEnvVar CacheDir(linker::ltoCacheDirEnvVar,
                        cacheDir.string().c_str());
  ScopedEnvVar Strict("NEVERC_PCG_STRICT", "1");
  ScopedEnvVar Debug("NEVERC_PCG_DEBUG", "1");

  auto buildAndRun = [&](const std::string &Tag,
                         bool DisablePartitionCache) {
    auto src = tmpFile("pcg_alias_" + Tag + ".c");
    auto obj = tmpFile("pcg_alias_" + Tag + ".o");
    std::string code =
        "typedef unsigned long long u64;\n"
        "__attribute__((noinline)) u64 alias_target(u64 x) {\n"
        "  return x * 3ULL + 1ULL;\n"
        "}\n"
        "extern u64 public_alias(u64) "
        "__attribute__((alias(\"alias_target\")));\n";
    for (unsigned I = 0; I < 32; ++I)
      code += "__attribute__((noinline)) u64 alias_user_" +
              std::to_string(I) +
              "(u64 x) { return public_alias(x + " + std::to_string(I) +
              "ULL); }\n";
    code += "int main(void) {\n";
    for (unsigned I = 0; I < 32; ++I)
      code += "  if (alias_user_" + std::to_string(I) +
              "(1) != ((1ULL + " + std::to_string(I) +
              "ULL) * 3ULL + 1ULL)) return " + std::to_string(I + 1) + ";\n";
    code += "  return 0;\n}\n";
    writeFile(src, code);

    std::vector<std::string> args = {
        "--target=x86_64-unknown-linux-gnu",
        "-O0",
        "-std=gnu11",
        "-fno-lto",
        "-c",
        "-mllvm",
        "-neverc-pcg-min-funcs=2",
        "-mllvm",
        "-neverc-pcg-weight-floor=1",
        "-mllvm",
        "-neverc-pcg-cg-weight-div=1",
    };
    args.insert(args.end(), {src.string(), "-o", obj.string()});

    std::optional<ScopedEnvVar> PartitionCache;
    if (DisablePartitionCache)
      PartitionCache.emplace(linker::ltoPartitionCacheEnvVar,
                             linker::ltoCacheDisableValue);
    else
      PartitionCache.emplace(linker::ltoPartitionCacheEnvVar, "1");

    CmdResult compile = ncc(args);
    ASSERT_EQ(compile.exitCode, 0) << Tag << ":\n" << compile.err;
    EXPECT_TRUE(compile.stderrContains("[pcg] SUCCESS"))
        << Tag << " did not exercise merged parallel codegen:\n"
        << compile.err;
    EXPECT_FALSE(readFile(obj).empty()) << Tag << " emitted an empty object";
  };

  buildAndRun("cached", false);
  buildAndRun("uncached", true);
}

TEST_F(LTOTest, ParallelCodegenPreservesLinkerOptionsExactlyOnce) {
  auto src = tmpFile("pcg_linker_options.c");
  auto obj = tmpFile("pcg_linker_options.obj");
  std::string code =
      "#pragma comment(lib, \"advapi32.lib\")\n"
      "#pragma comment(linker, \"/INCLUDE:pcg_metadata_anchor\")\n"
      "int pcg_metadata_anchor(void) { return 7; }\n";
  for (unsigned I = 0; I < 32; ++I)
    code += "__attribute__((noinline)) int pcg_metadata_user_" +
            std::to_string(I) + "(int x) { return x * " +
            std::to_string(I + 3) + " + pcg_metadata_anchor(); }\n";
  writeFile(src, code);

  ScopedEnvVar Strict("NEVERC_PCG_STRICT", "1");
  ScopedEnvVar Debug("NEVERC_PCG_DEBUG", "1");
  std::vector<std::string> args = {
      "--target=x86_64-pc-windows-msvc",
      "-O0",
      "-std=gnu11",
      "-fno-lto",
      "-c",
      "-mllvm",
      "-neverc-pcg-min-funcs=2",
      "-mllvm",
      "-neverc-pcg-weight-floor=1",
      "-mllvm",
      "-neverc-pcg-cg-weight-div=1",
      src.string(),
      "-o",
      obj.string(),
  };
  CmdResult compile = ncc(args);
  ASSERT_EQ(compile.exitCode, 0) << compile.err;
  ASSERT_TRUE(compile.stderrContains("[pcg] SUCCESS"))
      << "test did not exercise merged parallel codegen:\n"
      << compile.err;

  const std::string bytes = readFile(obj);
  auto count = [&](const std::string &needle) {
    size_t result = 0;
    for (size_t pos = 0;
         (pos = bytes.find(needle, pos)) != std::string::npos;
         pos += needle.size())
      ++result;
    return result;
  };
  EXPECT_EQ(count("--defaultlib=advapi32.lib"), 1u);
  EXPECT_EQ(count("/INCLUDE:pcg_metadata_anchor"), 1u);
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
  ScopedEnvVar NoLtoCache(linker::ltoCacheEnvVar,
                          linker::ltoCacheDisableValue);
  ScopedEnvVar NoPartitionCache(linker::ltoPartitionCacheEnvVar,
                                linker::ltoCacheDisableValue);

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
}

// Each auto-LTO compile-cost control must be independently overrideable without
// changing observable program behavior. Keep this normal regression free of
// wall-clock assertions: timing acceptance is covered by the explicitly
// enabled benchmark below.
TEST_F(LTOTest, AutoLtoBoundedIndVarWideningSemanticsPreserved) {
  ScopedEnvVar NoLtoCache(linker::ltoCacheEnvVar,
                          linker::ltoCacheDisableValue);
  ScopedEnvVar NoPartitionCache(linker::ltoPartitionCacheEnvVar,
                                linker::ltoCacheDisableValue);

  const auto Sources =
      writeAutoLtoLoopDenseProject("bounded_indvars_src", false);

  struct BuildArm {
    const char *Tag;
    std::vector<std::string> Extra;
  };
  const std::vector<BuildArm> Arms = {
      {"default", {}},
      {"bounded_widening",
       {"-mllvm",
        "-neverc-auto-lto-indvars-widen-max-function-loops=31"}},
      {"former_behavior",
       {"-mllvm", "-neverc-auto-lto-scev-huge-expr-threshold=512"}},
      {"bounded_old_scev",
       {"-mllvm",
        "-neverc-auto-lto-indvars-widen-max-function-loops=31",
        "-mllvm",
        "-neverc-auto-lto-scev-huge-expr-threshold=512"}},
  };

  auto build = [&](const std::string &Tag,
                   const std::vector<std::string> &Extra) {
    fs::path Output = tmpFile("bounded_indvars_" + Tag);
    std::vector<std::string> Args = {"-O2", "-std=c11"};
    for (const auto &Flag : sysrootFlags())
      Args.push_back(Flag);
    for (const auto &Flag : archFlags())
      Args.push_back(Flag);
    Args.insert(Args.end(), Extra.begin(), Extra.end());
    Args.insert(Args.end(), Sources.begin(), Sources.end());
    if (isWindows())
      Args.push_back("-mno-incremental-linker-compatible");
    Args.insert(Args.end(), {"-o", Output.string()});

    CmdResult Result = ncc(Args);
    return std::make_pair(std::move(Result), std::move(Output));
  };

  {
    ScopedEnvVar Debug("NEVERC_PCG_DEBUG", "1");
    auto [Result, Output] = build("probe", {});
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    EXPECT_TRUE(Result.stderrContains("[pcg] p-opt engaged")) << Result.err;
  }

  std::vector<fs::path> Outputs;
  for (const BuildArm &Arm : Arms) {
    auto [Result, Output] = build(Arm.Tag, Arm.Extra);
    ASSERT_EQ(Result.exitCode, 0) << Arm.Tag << ":\n" << Result.err;
    Outputs.push_back(std::move(Output));
  }

  std::optional<std::string> ExpectedOutput;
  for (size_t I = 0; I < Arms.size(); ++I) {
    CmdResult Run = exec(Outputs[I].string(), {});
    ASSERT_EQ(Run.exitCode, 0) << Arms[I].Tag << ":\n" << Run.err;
    EXPECT_TRUE(Run.contains("CK=")) << Arms[I].Tag << ":\n" << Run.out;
    if (!ExpectedOutput)
      ExpectedOutput = Run.out;
    else
      EXPECT_EQ(Run.out, *ExpectedOutput) << Arms[I].Tag;
  }

  ASSERT_EQ(Outputs.size(), 4u);
  EXPECT_LE(fileSize(Outputs[0]),
            static_cast<size_t>(fileSize(Outputs[2]) * 1.01) + 1);
}

// Keep the quantitative 25% target for explicitly bounded widening as an
// acceptance benchmark rather than a normal unit-test gate. This pathological
// mode is intentionally not the production default. Run with
// NEVERC_RUN_PERF_BENCHMARKS=1.
TEST_F(LTOTest, AutoLtoBoundedIndVarWideningCompileBenchmark) {
  const char *RunBenchmarks = std::getenv("NEVERC_RUN_PERF_BENCHMARKS");
  if (!RunBenchmarks || std::string(RunBenchmarks) == "0")
    GTEST_SKIP() << "set NEVERC_RUN_PERF_BENCHMARKS=1 to run timing acceptance";

  ScopedEnvVar NoLtoCache(linker::ltoCacheEnvVar,
                          linker::ltoCacheDisableValue);
  ScopedEnvVar NoPartitionCache(linker::ltoPartitionCacheEnvVar,
                                linker::ltoCacheDisableValue);

  const auto Sources =
      writeAutoLtoLoopDenseProject("bounded_indvars_bench_src", false);

  struct TimedBuild {
    CmdResult Result;
    double Seconds;
    fs::path Output;
  };
  auto build = [&](const std::string &Tag, unsigned Run,
                   const std::vector<std::string> &Extra) {
    fs::path Output =
        tmpFile("bounded_indvars_bench_" + Tag + "_" + std::to_string(Run));
    std::vector<std::string> Args = {"-O2", "-std=c11"};
    for (const auto &Flag : sysrootFlags())
      Args.push_back(Flag);
    for (const auto &Flag : archFlags())
      Args.push_back(Flag);
    Args.insert(Args.end(), Extra.begin(), Extra.end());
    Args.insert(Args.end(), Sources.begin(), Sources.end());
    if (isWindows())
      Args.push_back("-mno-incremental-linker-compatible");
    Args.insert(Args.end(), {"-o", Output.string()});

    auto Start = std::chrono::steady_clock::now();
    CmdResult Result = ncc(Args);
    double Seconds = std::chrono::duration<double>(
                         std::chrono::steady_clock::now() - Start)
                         .count();
    return TimedBuild{std::move(Result), Seconds, std::move(Output)};
  };

  const std::vector<std::string> BoundedBehavior = {
      "-mllvm",
      "-neverc-auto-lto-indvars-widen-max-function-loops=31"};
  const std::vector<std::string> OldBehavior = {
      "-mllvm",
      "-neverc-auto-lto-indvars-widen-max-function-loops=0",
      "-mllvm",
      "-neverc-auto-lto-scev-huge-expr-threshold=512"};

  std::vector<double> BoundedTimes;
  std::vector<double> FormerTimes;
  fs::path BoundedOutput;
  fs::path FormerOutput;
  for (unsigned Run = 0; Run < 5; ++Run) {
    auto runBounded = [&] {
      TimedBuild Build = build("bounded", Run, BoundedBehavior);
      if (Build.Result.exitCode != 0) {
        ADD_FAILURE() << Build.Result.err;
        return false;
      }
      BoundedTimes.push_back(Build.Seconds);
      BoundedOutput = std::move(Build.Output);
      return true;
    };
    auto runFormer = [&] {
      TimedBuild Build = build("former", Run, OldBehavior);
      if (Build.Result.exitCode != 0) {
        ADD_FAILURE() << Build.Result.err;
        return false;
      }
      FormerTimes.push_back(Build.Seconds);
      FormerOutput = std::move(Build.Output);
      return true;
    };

    if ((Run & 1) == 0) {
      ASSERT_TRUE(runBounded());
      ASSERT_TRUE(runFormer());
    } else {
      ASSERT_TRUE(runFormer());
      ASSERT_TRUE(runBounded());
    }
  }

  const double BoundedMedian = medianSeconds(BoundedTimes);
  const double FormerMedian = medianSeconds(FormerTimes);
  RecordProperty("bounded_median_seconds", BoundedMedian);
  RecordProperty("former_median_seconds", FormerMedian);
  EXPECT_LE(BoundedMedian, FormerMedian * 0.75)
      << "explicit bounded IV widening must improve the interleaved complete "
         "cold-build median by at least 25%";

  CmdResult BoundedRun = exec(BoundedOutput.string(), {});
  CmdResult FormerRun = exec(FormerOutput.string(), {});
  ASSERT_EQ(BoundedRun.exitCode, 0) << BoundedRun.err;
  ASSERT_EQ(FormerRun.exitCode, 0) << FormerRun.err;
  EXPECT_TRUE(BoundedRun.contains("CK=")) << BoundedRun.out;
  EXPECT_EQ(BoundedRun.out, FormerRun.out);
  EXPECT_LE(fileSize(BoundedOutput),
            static_cast<size_t>(fileSize(FormerOutput) * 1.01) + 1);
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
  ScopedEnvVar NoLtoCache(linker::ltoCacheEnvVar,
                          linker::ltoCacheDisableValue);
  ScopedEnvVar NoPartitionCache(linker::ltoPartitionCacheEnvVar,
                                linker::ltoCacheDisableValue);

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
    ScopedEnvVar WorkerThreads("NEVERC_PCG_THREADS", threads);
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

  ASSERT_FALSE(o1.empty()) << "relocatable merge produced no object";
  EXPECT_EQ(o1, o4) << "auto-LTO object differs between 1 and 4 worker threads "
                       "-- execution parallelism leaked into the emitted bytes "
                       "(non-reproducible build)";
  EXPECT_EQ(o1, o16) << "auto-LTO object differs between 1 and 16 worker threads "
                        "-- execution parallelism leaked into the emitted bytes";
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
  ScopedEnvVar NoLtoCache(linker::ltoCacheEnvVar,
                          linker::ltoCacheDisableValue);
  ScopedEnvVar NoPartitionCache(linker::ltoPartitionCacheEnvVar,
                                linker::ltoCacheDisableValue);

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
  ScopedEnvVar NoLtoCache(linker::ltoCacheEnvVar,
                          linker::ltoCacheDisableValue);
  ScopedEnvVar NoPartitionCache(linker::ltoPartitionCacheEnvVar,
                                linker::ltoCacheDisableValue);

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

// Compare complete cold builds from matching C sources. This benchmark is
// explicitly opt-in because it depends on an external clang-22 installation
// and takes long enough to be inappropriate for the normal unit-test suite.
TEST_F(LTOTest, AutoLtoCompleteBuildBeatsClang22FullLTO) {
  const char *ClangPath = std::getenv("NEVERC_BENCH_CLANG");
  if (!ClangPath || !*ClangPath)
    GTEST_SKIP() << "set NEVERC_BENCH_CLANG to a clang-22 executable";

  const std::string Clang = ClangPath;
  CmdResult Version = exec(Clang, {"--version"});
  if (Version.exitCode != 0)
    GTEST_SKIP() << "comparison clang is not executable: " << Version.err;
  const std::string VersionText = Version.out + Version.err;
  if (VersionText.find("clang version 22") == std::string::npos)
    GTEST_SKIP() << "comparison compiler is not clang-22: " << VersionText;

  ScopedEnvVar NoLtoCache(linker::ltoCacheEnvVar,
                          linker::ltoCacheDisableValue);
  ScopedEnvVar NoPartitionCache(linker::ltoPartitionCacheEnvVar,
                                linker::ltoCacheDisableValue);

  const auto Sources =
      writeAutoLtoLoopDenseProject("clang22_complete_src", true);

  struct TimedBuild {
    CmdResult Result;
    double Seconds;
    fs::path Output;
  };
  auto build = [&](bool UseNeverc, const std::string &Tag, unsigned Run) {
    fs::path Output =
        tmpFile("clang22_complete_" + Tag + "_" + std::to_string(Run));
    std::vector<std::string> Args = {"-O2", "-std=c11"};
    if (!UseNeverc)
      Args.push_back("-flto=full");
    for (const auto &Flag : sysrootFlags())
      Args.push_back(Flag);
    for (const auto &Flag : archFlags())
      Args.push_back(Flag);
    Args.insert(Args.end(), Sources.begin(), Sources.end());
    if (UseNeverc && isWindows())
      Args.push_back("-mno-incremental-linker-compatible");
    Args.insert(Args.end(), {"-o", Output.string()});

    auto Start = std::chrono::steady_clock::now();
    CmdResult Result = UseNeverc ? ncc(Args) : exec(Clang, Args);
    double Seconds = std::chrono::duration<double>(
                         std::chrono::steady_clock::now() - Start)
                         .count();
    return TimedBuild{std::move(Result), Seconds, std::move(Output)};
  };

  TimedBuild NevercProbe = build(true, "neverc_probe", 0);
  ASSERT_EQ(NevercProbe.Result.exitCode, 0) << NevercProbe.Result.err;
  TimedBuild ClangProbe = build(false, "clang_probe", 0);
  if (ClangProbe.Result.exitCode != 0)
    GTEST_SKIP() << "clang-22 cannot build the comparison workload: "
                 << ClangProbe.Result.err;

  std::vector<double> NevercTimes;
  std::vector<double> ClangTimes;
  fs::path NevercOutput;
  fs::path ClangOutput;
  for (unsigned Run = 0; Run < 5; ++Run) {
    auto runNeverc = [&] {
      TimedBuild Build = build(true, "neverc", Run);
      if (Build.Result.exitCode != 0) {
        ADD_FAILURE() << Build.Result.err;
        return false;
      }
      NevercTimes.push_back(Build.Seconds);
      NevercOutput = std::move(Build.Output);
      return true;
    };
    auto runClang = [&] {
      TimedBuild Build = build(false, "clang", Run);
      if (Build.Result.exitCode != 0) {
        ADD_FAILURE() << Build.Result.err;
        return false;
      }
      ClangTimes.push_back(Build.Seconds);
      ClangOutput = std::move(Build.Output);
      return true;
    };

    if ((Run & 1) == 0) {
      ASSERT_TRUE(runNeverc());
      ASSERT_TRUE(runClang());
    } else {
      ASSERT_TRUE(runClang());
      ASSERT_TRUE(runNeverc());
    }
  }

  const double NevercMedian = medianSeconds(NevercTimes);
  const double ClangMedian = medianSeconds(ClangTimes);
  RecordProperty("neverc_complete_median_seconds", NevercMedian);
  RecordProperty("clang22_complete_median_seconds", ClangMedian);
  EXPECT_LT(NevercMedian, ClangMedian);

  CmdResult NevercRun = exec(NevercOutput.string(), {});
  CmdResult ClangRun = exec(ClangOutput.string(), {});
  ASSERT_EQ(NevercRun.exitCode, 0) << NevercRun.err;
  ASSERT_EQ(ClangRun.exitCode, 0) << ClangRun.err;
  EXPECT_TRUE(NevercRun.contains("CK=")) << NevercRun.out;
  EXPECT_EQ(NevercRun.out, ClangRun.out);
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
