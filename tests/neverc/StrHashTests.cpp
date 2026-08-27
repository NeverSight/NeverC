#include "NeverCTestFixture.h"

class StrHashTest : public NeverCTest {
protected:
  fs::path strHashDir() { return testDir() / "strhash"; }

  CmdResult syntaxOnly(const std::string &src,
                       const std::string &extraFlags = "") {
    std::vector<std::string> args = {"-fsyntax-only", "-include",
                                     "neverc/strhash/strhash.h"};
    for (auto &f : splitFlags(extraFlags))
      args.push_back(f);
    for (auto &f : sysrootFlags())
      args.push_back(f);
    for (auto &f : archFlags())
      args.push_back(f);
    args.push_back(src);
    return ncc(args);
  }

  std::string emitIR(const std::string &src, const std::string &name,
                     const std::string &extraFlags = "") {
    auto ir = tmpFile(name + ".ll");
    std::vector<std::string> args = {"-S", "-emit-llvm", "-include",
                                     "neverc/strhash/strhash.h"};
    for (auto &f : splitFlags(extraFlags))
      args.push_back(f);
    for (auto &f : sysrootFlags())
      args.push_back(f);
    for (auto &f : archFlags())
      args.push_back(f);
    args.push_back(src);
    args.push_back("-o");
    args.push_back(ir.string());
    auto r = ncc(args);
    EXPECT_EQ(r.exitCode, 0) << name << ": emit-llvm failed\n" << r.err;
    if (r.exitCode != 0)
      return "";
    return readFile(ir);
  }
};

// ---- Basic tests ----

TEST_F(StrHashTest, Basic_CompileClean) {
  auto src = strHashDir() / "nc_strhash_basic.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  auto r = syntaxOnly(src.string());
  EXPECT_EQ(r.exitCode, 0) << "basic syntax-only failed\n" << r.err;
}

TEST_F(StrHashTest, Basic_NonLiteralError) {
  auto src = strHashDir() / "nc_strhash_basic.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  auto r = syntaxOnly(src.string(), "-DTEST_BAD_ARG");
  EXPECT_NE(r.exitCode, 0);
  EXPECT_TRUE(r.stderrContains("not a string literal"))
      << "expected 'not a string literal' error\n" << r.err;
}

// ---- Algorithm selection ----

TEST_F(StrHashTest, Algo_FNV32a) {
  auto src = strHashDir() / "nc_strhash_algo.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  auto r = syntaxOnly(src.string(), "-fstrhash-algo=fnv32a");
  EXPECT_EQ(r.exitCode, 0) << "fnv32a algo failed\n" << r.err;
}

TEST_F(StrHashTest, Algo_FNV64a) {
  auto src = strHashDir() / "nc_strhash_algo.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  auto r = syntaxOnly(src.string(), "-fstrhash-algo=fnv64a");
  EXPECT_EQ(r.exitCode, 0) << "fnv64a algo failed\n" << r.err;
}

TEST_F(StrHashTest, Algo_XXHash64) {
  auto src = strHashDir() / "nc_strhash_algo.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  auto r = syntaxOnly(src.string(), "-fstrhash-algo=xxhash64");
  EXPECT_EQ(r.exitCode, 0) << "xxhash64 algo failed\n" << r.err;
}

TEST_F(StrHashTest, Algo_InvalidReject) {
  auto src = strHashDir() / "nc_strhash_algo.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  auto r = syntaxOnly(src.string(), "-fstrhash-algo=invalid");
  EXPECT_NE(r.exitCode, 0);
  EXPECT_TRUE(r.stderrContains("invalid value"))
      << "expected 'invalid value' error\n" << r.err;
}

// ---- Wide / u8 string support ----

TEST_F(StrHashTest, Wide_AllEncodings) {
  auto src = strHashDir() / "nc_strhash_wide.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  auto r = syntaxOnly(src.string());
  EXPECT_EQ(r.exitCode, 0)
      << "wide/u8 string hash failed\n" << r.err;
}

// ---- Custom hash function ----

TEST_F(StrHashTest, CustomHash_CompileClean) {
  auto src = strHashDir() / "nc_strhash_custom_hash.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  auto r = syntaxOnly(src.string());
  EXPECT_EQ(r.exitCode, 0) << "custom hash failed\n" << r.err;
}

TEST_F(StrHashTest, CustomHash_NoBuiltinCallInIR) {
  auto src = strHashDir() / "nc_strhash_custom_hash.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto ir = tmpFile("custom_hash_ir.ll");
  std::vector<std::string> args = {"-S", "-emit-llvm", "-O1", "-fstrhash-fold"};
  for (auto &f : sysrootFlags())
    args.push_back(f);
  for (auto &f : archFlags())
    args.push_back(f);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(ir.string());
  auto r = ncc(args);
  ASSERT_EQ(r.exitCode, 0) << "custom hash emit-llvm failed\n" << r.err;

  auto irContent = readFile(ir);
  EXPECT_EQ(irContent.find("neverc_fnv"), std::string::npos)
      << "custom hash should not call neverc_fnv functions";
}

// ---- Codegen: NC_STRHASH folded at Sema level (O0) ----

TEST_F(StrHashTest, Codegen_SemaFold) {
  auto src = strHashDir() / "nc_strhash_codegen.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  // -fno-builtin-mimalloc: the assertion below reads the whole module for the
  // absence of any call, and the allocator that is otherwise injected by
  // default brings hundreds of its own.  What is under test is whether Sema
  // folded NC_STRHASH, which has nothing to do with the allocator.
  auto ir = emitIR(src.string(), "codegen_default", "-O0 -fno-builtin-mimalloc");
  if (ir.empty())
    return;
  EXPECT_EQ(ir.find("call"), std::string::npos)
      << "NC_STRHASH should be folded to constant at O0 (no calls)\n"
      << "IR excerpt:\n"
      << ir.substr(0, 500);
}

// ---- FoldPass: runtime calls folded with -fstrhash-fold ----

TEST_F(StrHashTest, FoldPass_ConstArgFolded) {
  auto src = strHashDir() / "nc_strhash_fold.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  auto ir = emitIR(src.string(), "fold_pass", "-O1 -fstrhash-fold");
  if (ir.empty())
    return;

  // test_fold_fnv64a: constant arg should be folded (no call to neverc_fnv_sum64a)
  auto fnPos = ir.find("@test_fold_fnv64a");
  auto noFoldPos = ir.find("@test_no_fold");
  ASSERT_NE(fnPos, std::string::npos);
  ASSERT_NE(noFoldPos, std::string::npos);

  auto foldedSection = ir.substr(fnPos, noFoldPos - fnPos);
  EXPECT_EQ(foldedSection.find("call"), std::string::npos)
      << "test_fold_fnv64a should have no calls after folding";

  // test_no_fold: non-constant arg must NOT be folded (call still present)
  auto noFoldSection = ir.substr(noFoldPos);
  EXPECT_NE(noFoldSection.find("neverc_fnv_sum64a"), std::string::npos)
      << "test_no_fold should still contain neverc_fnv_sum64a call";
}

// ---- Runtime match pattern ----

TEST_F(StrHashTest, RuntimeMatch_CompileClean) {
  auto src = strHashDir() / "nc_strhash_runtime_match.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  auto r = syntaxOnly(src.string());
  EXPECT_EQ(r.exitCode, 0)
      << "runtime match pattern failed\n" << r.err;
}

TEST_F(StrHashTest, RuntimeMatchesCompileTimeForBuiltinAlgorithms) {
  auto Src = strHashDir() / "nc_strhash_runtime_parity.c";
  if (!fs::exists(Src))
    GTEST_SKIP() << Src << " not found";

  for (const char *Algorithm : {"fnv32a", "fnv64a", "xxhash64"}) {
    SCOPED_TRACE(Algorithm);
    compileRunAndCheck(
        std::string("strhash_runtime_parity_") + Algorithm, Src.string(),
        std::string("-std=c11 -fstrhash-algo=") + Algorithm, 0);
  }
}
