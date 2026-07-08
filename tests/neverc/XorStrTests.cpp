#include "NeverCTestFixture.h"

class XorStrTest : public NeverCTest {
protected:
  fs::path xorStrDir() { return testDir() / "xorstr"; }

  CmdResult syntaxOnly(const std::string &src,
                       const std::string &extraFlags = "") {
    std::vector<std::string> args = {"-fsyntax-only", "-include",
                                     "neverc/xorstr/xorstr.h"};
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
                                     "neverc/xorstr/xorstr.h"};
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

TEST_F(XorStrTest, Basic_CompileClean) {
  auto src = xorStrDir() / "nc_xorstr_basic.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  auto r = syntaxOnly(src.string());
  EXPECT_EQ(r.exitCode, 0) << "basic xorstr failed\n" << r.err;
}

TEST_F(XorStrTest, Basic_NonLiteralError) {
  auto src = xorStrDir() / "nc_xorstr_basic.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  auto r = syntaxOnly(src.string(), "-DTEST_BAD_ARG");
  EXPECT_NE(r.exitCode, 0);
  EXPECT_TRUE(r.stderrContains("not a string literal"))
      << "expected 'not a string literal' error\n" << r.err;
}

// ---- Wide / u8 / u16 / u32 strings ----

TEST_F(XorStrTest, Wide_AllEncodings) {
  auto src = xorStrDir() / "nc_xorstr_wide.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  auto r = syntaxOnly(src.string());
  EXPECT_EQ(r.exitCode, 0)
      << "wide/u8 xorstr failed\n" << r.err;
}

// ---- Codegen: encrypted in IR, decrypt call present, no plaintext ----

TEST_F(XorStrTest, Codegen_EncryptedAndDecryptCall) {
  auto src = xorStrDir() / "nc_xorstr_codegen.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  auto ir = emitIR(src.string(), "xorstr_codegen", "-O1");
  if (ir.empty())
    return;

  EXPECT_NE(ir.find("__neverc_xorstr_decrypt"), std::string::npos)
      << "expected call to __neverc_xorstr_decrypt in IR";

  EXPECT_EQ(ir.find("GetProcAddress"), std::string::npos)
      << "plaintext 'GetProcAddress' should not appear in IR";
}

// ---- -fencrypt-call-strings auto-encryption ----

TEST_F(XorStrTest, EncryptCallStrings_AutoEncrypt) {
  auto src = xorStrDir() / "encrypt_call_strings.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto ir = tmpFile("encrypt_call_strings.ll");
  std::vector<std::string> args = {"-S", "-emit-llvm", "-O1",
                                   "-fencrypt-call-strings"};
  for (auto &f : sysrootFlags())
    args.push_back(f);
  for (auto &f : archFlags())
    args.push_back(f);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(ir.string());
  auto r = ncc(args);
  ASSERT_EQ(r.exitCode, 0) << "encrypt-call-strings failed\n" << r.err;

  auto irContent = readFile(ir);

  EXPECT_NE(irContent.find("xorstr"), std::string::npos)
      << "expected xorstr pattern in auto-encrypted IR";

  EXPECT_EQ(irContent.find("\"hello auto\""), std::string::npos)
      << "plaintext 'hello auto' should not appear in IR";
}
