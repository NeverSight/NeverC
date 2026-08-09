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

// ---- Intermediate IR: encrypted, opaque until final codegen/LTO ----

TEST_F(XorStrTest, Codegen_IntermediateKeepsOpaqueDecoder) {
  auto src = xorStrDir() / "nc_xorstr_codegen.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  // This assertion covers the xorstr transformation's input module.  The
  // hosted allocator is a separate embedded module and legitimately contains
  // Windows API names, including the plaintext used by this fixture.
  auto ir = emitIR(src.string(), "xorstr_codegen", "-O2 -fno-builtin-mimalloc");
  if (ir.empty())
    return;

  EXPECT_NE(ir.find("private"), std::string::npos)
      << "expected encrypted constant in IR (private global)";

  EXPECT_EQ(ir.find("GetProcAddress"), std::string::npos)
      << "plaintext 'GetProcAddress' should not appear in IR";

  const size_t CallerBegin = ir.find("@test_xorstr(");
  ASSERT_NE(CallerBegin, std::string::npos);
  const size_t CallerEnd = ir.find("\n}", CallerBegin);
  ASSERT_NE(CallerEnd, std::string::npos);
  const std::string Caller = ir.substr(CallerBegin, CallerEnd - CallerBegin);
  EXPECT_NE(Caller.find("@__neverc_xorstr_decrypt("), std::string::npos)
      << "intermediate IR must retain the call for final-link rekeying\n"
      << Caller;
  const size_t DecoderName = ir.find("@__neverc_xorstr_decrypt(");
  ASSERT_NE(DecoderName, std::string::npos);
  const size_t DecoderEnd = ir.find("\n}", DecoderName);
  ASSERT_NE(DecoderEnd, std::string::npos);
  const std::string Decoder = ir.substr(DecoderName, DecoderEnd - DecoderName);
  const bool HasVolatileLoad =
      Decoder.find("load volatile") != std::string::npos ||
      Decoder.find("load atomic volatile") != std::string::npos;
  EXPECT_TRUE(HasVolatileLoad)
      << "ordinary optimization must not specialize decoder inputs\n"
      << Decoder;
}

TEST_F(XorStrTest, Codegen_NativeObjectHasNoSharedDecoder) {
  auto src = xorStrDir() / "nc_xorstr_codegen.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto object = tmpFile("xorstr_no_shared_decoder.o");
  std::vector<std::string> args = {"-c",       "-O2",
                                   "-fno-lto", "-fno-builtin-mimalloc",
                                   "-include", "neverc/xorstr/xorstr.h"};
  for (auto &f : sysrootFlags())
    args.push_back(f);
  for (auto &f : archFlags())
    args.push_back(f);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(object.string());

  auto r = ncc(args);
  ASSERT_EQ(r.exitCode, 0) << "native object compilation failed\n" << r.err;
  const std::string Bytes = readFile(object);
  EXPECT_EQ(Bytes.find("__neverc_xorstr_"), std::string::npos)
      << "machine-code output must not retain a shared decoder identity";
  EXPECT_EQ(Bytes.find(".rekey"), std::string::npos)
      << "machine-code output must not identify re-keyed ciphertext globals";
  EXPECT_EQ(Bytes.find("GetProcAddress"), std::string::npos)
      << "machine-code output must not contain plaintext";
}

TEST_F(XorStrTest, Runtime_StatefulDecryptRoundTrip) {
  auto src = xorStrDir() / "nc_xorstr_runtime.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  compileRunAndCheck("xorstr_runtime", src.string(),
                     "-std=c11 -O2 -fno-builtin-mimalloc");
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

  EXPECT_NE(irContent.find("load volatile i8"), std::string::npos)
      << "auto-encrypted ciphertext loads must resist later folding";

  EXPECT_EQ(irContent.find("\"hello auto\""), std::string::npos)
      << "plaintext 'hello auto' should not appear in IR";
}

TEST_F(XorStrTest, EncryptCallStrings_NativeObjectHasNoSemanticMarkers) {
  auto src = xorStrDir() / "encrypt_call_strings.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";

  auto object = tmpFile("encrypt_call_strings_anonymous.o");
  std::vector<std::string> args = {"-c", "-O2", "-fno-lto",
                                   "-fencrypt-call-strings"};
  for (auto &f : sysrootFlags())
    args.push_back(f);
  for (auto &f : archFlags())
    args.push_back(f);
  args.push_back(src.string());
  args.push_back("-o");
  args.push_back(object.string());

  auto r = ncc(args);
  ASSERT_EQ(r.exitCode, 0) << "native object compilation failed\n" << r.err;
  const std::string Bytes = readFile(object);
  EXPECT_EQ(Bytes.find(".xorstr.enc"), std::string::npos)
      << "ciphertext globals must not expose an xorstr marker";
  EXPECT_EQ(Bytes.find("hello auto"), std::string::npos)
      << "machine-code output must not contain plaintext";
}

TEST_F(XorStrTest, EncryptCallStrings_RuntimeRoundTrip) {
  auto src = xorStrDir() / "encrypt_call_strings_runtime.c";
  if (!fs::exists(src))
    GTEST_SKIP() << src << " not found";
  compileRunAndCheck("encrypt_call_strings_runtime", src.string(),
                     "-std=c11 -O1 -fencrypt-call-strings "
                     "-fno-builtin-mimalloc");
}
