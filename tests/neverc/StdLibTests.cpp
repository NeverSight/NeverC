#include "NeverCTestFixture.h"
#include <cstdlib>
#include <filesystem>

class StdLibTest : public NeverCTest {
protected:
  static std::string stdTestDir() {
    return (testDir() / "std").string();
  }

  static std::string stdSrcDir() {
    return (testDir().parent_path().parent_path() / "std").string();
  }

  CmdResult compileAndRunStdTest(const std::string &testName,
                                  const std::vector<std::string> &srcs = {},
                                  const std::vector<std::string> &extraFlags = {}) {
    fs::path testFile = fs::path(stdTestDir()) / ("test_" + testName + ".c");
    if (!fs::exists(testFile))
      return {1, "", "test file not found: " + testFile.string()};

    fs::path outBin = tmp() / ("test_" + testName);
    std::string sd = stdSrcDir();

    std::vector<std::string> args;
    args.push_back("-I" + sd + "/include");
    args.push_back("-I" + sd + "/src/net");
    args.push_back("-Wall");
    args.push_back("-Wextra");
    args.push_back("-Wno-unused-parameter");
    args.push_back("-Wno-unused-function");
    args.push_back("-O1");
    for (const auto &f : extraFlags)
      args.push_back(f);

    if (!srcs.empty())
      args.push_back("-fno-builtin-std");

    args.push_back("-o");
    args.push_back(outBin.string());
    args.push_back(testFile.string());
    for (const auto &s : srcs)
      args.push_back(sd + "/" + s);
#ifndef _WIN32
    args.push_back("-lm");
    args.push_back("-lpthread");
#else
    args.push_back("-Wno-deprecated-declarations");
#endif

    CmdResult compile = ncc(args);
    if (!compile.ok())
      return compile;

    return exec(outBin.string(), {});
  }
};

TEST_F(StdLibTest, EmbeddedFunctionOnlyConsumer) {
  auto src = tmpFile("std_function_only.c");
  writeFile(src,
            "extern double neverc_math_sqrt(double);\n"
            "int main(void) { return neverc_math_sqrt(81.0) != 9.0; }\n");
  compileRunAndCheck("std_function_only", src.string(),
                     "-std=c11 -fbuiltin-std", 0);
}

TEST_F(StdLibTest, WindowsModulesCompileWithBundledSdk) {
  const std::string sd = stdSrcDir();
  for (const char *target : {"x86_64-pc-windows-msvc",
                             "aarch64-pc-windows-msvc"}) {
    for (const char *source : {"src/net/resolve/resolve.c",
                               "src/os/user/user.c",
                               "src/crypto/x509/x509_system.c"}) {
      SCOPED_TRACE(std::string(target) + ": " + source);
      fs::path bitcode = tmp() / (fs::path(source).stem().string() + ".bc");
      auto result = ncc({
          "-c",
          "-emit-llvm",
          "-O2",
          "-gline-tables-only",
          "-fno-builtin-std",
          "-fno-lto",
          "-ffreestanding",
          "-std=gnu11",
          "-Wall",
          "-Wextra",
          "-Werror",
          "-Wno-deprecated-declarations",
          std::string("--target=") + target,
          "-I" + sd + "/include",
          sd + "/" + source,
          "-o",
          bitcode.string(),
      });
      EXPECT_EQ(result.exitCode, 0) << result.out << result.err;
    }
  }
}

TEST_F(StdLibTest, NetIocpCompilesWithBundledSdk) {
  const std::string sd = stdSrcDir();
  const fs::path source = fs::path(stdTestDir()) / "test_net_iocp.c";
  for (const char *target : {"x86_64-pc-windows-msvc",
                             "aarch64-pc-windows-msvc"}) {
    SCOPED_TRACE(target);
    const fs::path object =
        tmp() / (std::string("test_net_iocp_") +
                 (target[0] == 'x' ? "x64.obj" : "arm64.obj"));
    auto result = ncc({
        "-c",
        "-fno-builtin-std",
        "-ffreestanding",
        "-std=gnu11",
        "-Wall",
        "-Wextra",
        "-Werror",
        std::string("--target=") + target,
        "-I" + sd + "/include",
        "-I" + sd + "/src/net",
        source.string(),
        "-o",
        object.string(),
    });
    EXPECT_EQ(result.exitCode, 0) << result.out << result.err;
  }
}

TEST_F(StdLibTest, EmbeddedRuntimePreservesUserLocalProvenance) {
  auto src = tmpFile("std_user_local.c");
  auto ir = tmpFile("std_user_local.ll");
  writeFile(src,
            "typedef unsigned long long u64;\n"
            "__attribute__((used, noinline))\n"
            "static u64 neverc_rand_seed(void) { return 7; }\n"
            "extern u64 neverc_rand_uint64(void);\n"
            "u64 read_user_seed(void) {\n"
            "  return neverc_rand_seed() + neverc_rand_uint64();\n"
            "}\n");

  std::vector<std::string> args = {
      "-std=c11", "-fbuiltin-std", "-flto=full", "-O0",
      "-S", "-emit-llvm", src.string(), "-o", ir.string(),
  };
  for (auto &f : sysrootFlags())
    args.push_back(f);
  for (auto &f : archFlags())
    args.push_back(f);

  auto r = ncc(args);
  ASSERT_EQ(r.exitCode, 0) << r.err;
  EXPECT_NE(readFile(ir).find("define internal i64 @neverc_rand_seed()"),
            std::string::npos)
      << "the embedded runtime must not change a user-local symbol's linkage";
}

TEST_F(StdLibTest, EmbeddedRuntimeSharesStateAcrossTranslationUnits) {
  auto owner = tmpFile("std_state_owner.c");
  auto consumer = tmpFile("std_state_consumer.c");
  writeFile(owner,
            "typedef unsigned long long u64;\n"
            "extern void neverc_rand_seed(u64);\n"
            "extern u64 neverc_rand_uint64(void);\n"
            "extern u64 consume_next(void);\n"
            "int main(void) {\n"
            "  neverc_rand_seed(1);\n"
            "  u64 from_consumer = consume_next();\n"
            "  neverc_rand_seed(1);\n"
            "  return from_consumer != neverc_rand_uint64();\n"
            "}\n");
  writeFile(consumer,
            "typedef unsigned long long u64;\n"
            "extern u64 neverc_rand_uint64(void);\n"
            "u64 consume_next(void) { return neverc_rand_uint64(); }\n");

  for (const char *mode : {"", "-flto=full", "-fno-lto"}) {
    SCOPED_TRACE(mode[0] ? mode : "auto-lto");
    auto exe = tmpFile(std::string("std_state_") +
                       (mode[0] ? mode + 1 : "auto"));
    std::vector<std::string> args = {"-std=c11", "-fbuiltin-std"};
    if (mode[0])
      args.push_back(mode);
    for (auto &f : sysrootFlags())
      args.push_back(f);
    for (auto &f : archFlags())
      args.push_back(f);
    args.insert(args.end(),
                {owner.string(), consumer.string(), "-o", exe.string()});

    auto compile = ncc(args);
    ASSERT_EQ(compile.exitCode, 0) << compile.err;
    auto run = exec(exe.string(), {});
    EXPECT_EQ(run.exitCode, 0) << run.out << run.err;
  }
}

TEST_F(StdLibTest, EmbeddedRuntimeKeepsModuleLocalsDistinct) {
  auto owner = tmpFile("std_local_owner.c");
  auto base32 = tmpFile("std_local_base32.c");
  auto base64 = tmpFile("std_local_base64.c");
  writeFile(owner,
            "extern int check_base32(void);\n"
            "extern int check_base64(void);\n"
            "int main(void) { return check_base32() || check_base64(); }\n");
  writeFile(base32,
            "typedef __SIZE_TYPE__ size_t;\n"
            "typedef unsigned char u8;\n"
            "extern size_t neverc_base32_encode(char *, const u8 *, size_t);\n"
            "int check_base32(void) {\n"
            "  const u8 input[3] = {'f', 'o', 'o'};\n"
            "  char output[9];\n"
            "  size_t n = neverc_base32_encode(output, input, 3);\n"
            "  const char expected[8] = {'M','Z','X','W','6','=','=','='};\n"
            "  if (n != 8) return 1;\n"
            "  for (size_t i = 0; i < 8; ++i)\n"
            "    if (output[i] != expected[i]) return 2;\n"
            "  return 0;\n"
            "}\n");
  writeFile(base64,
            "typedef __SIZE_TYPE__ size_t;\n"
            "typedef unsigned char u8;\n"
            "extern size_t neverc_base64_encode(char *, const u8 *, size_t);\n"
            "int check_base64(void) {\n"
            "  const u8 input[3] = {'f', 'o', 'o'};\n"
            "  char output[5];\n"
            "  size_t n = neverc_base64_encode(output, input, 3);\n"
            "  const char expected[4] = {'Z','m','9','v'};\n"
            "  if (n != 4) return 1;\n"
            "  for (size_t i = 0; i < 4; ++i)\n"
            "    if (output[i] != expected[i]) return 2;\n"
            "  return 0;\n"
            "}\n");

  for (const char *mode : {"", "-flto=full", "-fno-lto"}) {
    SCOPED_TRACE(mode[0] ? mode : "auto-lto");
    auto exe = tmpFile(std::string("std_local_symbols_") +
                       (mode[0] ? mode + 1 : "auto"));
    std::vector<std::string> args = {"-std=c11", "-fbuiltin-std"};
    if (mode[0])
      args.push_back(mode);
    for (auto &f : sysrootFlags())
      args.push_back(f);
    for (auto &f : archFlags())
      args.push_back(f);
    args.insert(args.end(),
                {owner.string(), base32.string(), base64.string(),
                 "-o", exe.string()});

    auto compile = ncc(args);
    ASSERT_EQ(compile.exitCode, 0) << compile.err;
    auto run = exec(exe.string(), {});
    EXPECT_EQ(run.exitCode, 0) << run.out << run.err;
  }
}

#define STD_TEST(name, ...)                                     \
  TEST_F(StdLibTest, name) {                                    \
    auto r = compileAndRunStdTest(#name, {__VA_ARGS__});         \
    ASSERT_TRUE(r.ok()) << "stdout: " << r.out                 \
                        << "\nstderr: " << r.err;               \
    EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;  \
  }

// ===== Math =====
STD_TEST(math, "src/math/abs.c", "src/math/acos.c", "src/math/acosh.c", "src/math/asin.c", "src/math/asinh.c", "src/math/atan.c", "src/math/atan2.c", "src/math/atanh.c", "src/math/cbrt.c", "src/math/ceil.c", "src/math/copysign.c", "src/math/cos.c", "src/math/cosh.c", "src/math/dim.c", "src/math/erf.c", "src/math/erfc.c", "src/math/erfcinv.c", "src/math/erfinv.c", "src/math/exp.c", "src/math/exp2.c", "src/math/expm1.c", "src/math/float32bits.c", "src/math/float64bits.c", "src/math/floor.c", "src/math/fma.c", "src/math/fmod.c", "src/math/frexp.c", "src/math/gamma.c", "src/math/hypot.c", "src/math/ilogb.c", "src/math/inf.c", "src/math/isinf.c", "src/math/isnan.c", "src/math/j0.c", "src/math/j1.c", "src/math/jn.c", "src/math/ldexp.c", "src/math/lgamma.c", "src/math/log.c", "src/math/log10.c", "src/math/log1p.c", "src/math/log2.c", "src/math/logb.c", "src/math/max.c", "src/math/min.c", "src/math/modf.c", "src/math/nan.c", "src/math/nextafter.c", "src/math/nextafter32.c", "src/math/pow.c", "src/math/pow10.c", "src/math/remainder.c", "src/math/round.c", "src/math/roundtoeven.c", "src/math/signbit.c", "src/math/sin.c", "src/math/sincos.c", "src/math/sinh.c", "src/math/sqrt.c", "src/math/tan.c", "src/math/tanh.c", "src/math/trunc.c")
STD_TEST(strconv, "src/strconv/format_bool.c", "src/strconv/format_float.c", "src/strconv/format_int.c", "src/strconv/parse_bool.c", "src/strconv/parse_float.c", "src/strconv/parse_int.c", "src/strconv/quote.c", "src/unicode/utf8/utf8.c", "src/unicode/unicode.c")
TEST_F(StdLibTest, StrconvAllocationFailure) {
  auto r = compileAndRunStdTest(
      "strconv_oom",
      {"src/strconv/format_bool.c", "src/strconv/format_float.c",
       "src/strconv/format_int.c", "src/strconv/parse_float.c",
       "src/unicode/utf8/utf8.c", "src/unicode/unicode.c"},
      {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(path, "src/path/base.c", "src/path/dir.c", "src/path/ext.c", "src/path/isabs.c", "src/path/clean.c", "src/path/join.c", "src/path/split.c", "src/path/match.c")
STD_TEST(sort, "src/sort/sort.c")
// Differential fuzz over the shared sort + substring-search engines (catches
// structural regressions like the Timsort merge-invariant overflow).
STD_TEST(sortsearch_fuzz, "src/sort/sort.c", "src/bytes/bytes.c")
STD_TEST(rand, "src/math/rand/rand.c", "src/math/log.c", "src/math/exp.c", "src/math/frexp.c", "src/math/ldexp.c", "src/math/floor.c", "src/math/modf.c", "src/math/trunc.c", "src/math/sqrt.c")
STD_TEST(bits, "src/math/bits/bits.c")
STD_TEST(cmplx, "src/math/cmplx/cmplx.c", "src/math/abs.c", "src/math/acos.c", "src/math/acosh.c", "src/math/asin.c", "src/math/asinh.c", "src/math/atan.c", "src/math/atan2.c", "src/math/atanh.c", "src/math/cbrt.c", "src/math/ceil.c", "src/math/copysign.c", "src/math/cos.c", "src/math/cosh.c", "src/math/dim.c", "src/math/erf.c", "src/math/erfc.c", "src/math/erfcinv.c", "src/math/erfinv.c", "src/math/exp.c", "src/math/exp2.c", "src/math/expm1.c", "src/math/float32bits.c", "src/math/float64bits.c", "src/math/floor.c", "src/math/fma.c", "src/math/fmod.c", "src/math/frexp.c", "src/math/gamma.c", "src/math/hypot.c", "src/math/ilogb.c", "src/math/inf.c", "src/math/isinf.c", "src/math/isnan.c", "src/math/j0.c", "src/math/j1.c", "src/math/jn.c", "src/math/ldexp.c", "src/math/lgamma.c", "src/math/log.c", "src/math/log10.c", "src/math/log1p.c", "src/math/log2.c", "src/math/logb.c", "src/math/max.c", "src/math/min.c", "src/math/modf.c", "src/math/nan.c", "src/math/nextafter.c", "src/math/nextafter32.c", "src/math/pow.c", "src/math/pow10.c", "src/math/remainder.c", "src/math/round.c", "src/math/roundtoeven.c", "src/math/signbit.c", "src/math/sin.c", "src/math/sincos.c", "src/math/sinh.c", "src/math/sqrt.c", "src/math/tan.c", "src/math/tanh.c", "src/math/trunc.c")
STD_TEST(big, "src/math/big/big.c")
TEST_F(StdLibTest, MathBigAllocationFailure) {
  auto r = compileAndRunStdTest("big_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== Encoding =====
STD_TEST(hex, "src/encoding/hex/decode.c", "src/encoding/hex/encode.c")
STD_TEST(base64, "src/encoding/base64/base64.c")
STD_TEST(base32, "src/encoding/base32/base32.c")
STD_TEST(binary, "src/encoding/binary/binary.c")
STD_TEST(ascii85, "src/encoding/ascii85/ascii85.c")
STD_TEST(protobuf, "src/encoding/protobuf/protobuf.c",
         "src/encoding/protobuf/protobuf_message.c")
STD_TEST(pem, "src/encoding/pem/pem.c", "src/encoding/base64/base64.c")
STD_TEST(json, "src/encoding/json/json.c", "src/strconv/format_float.c", "src/strconv/parse_float.c")
STD_TEST(csv, "src/encoding/csv/csv.c")
STD_TEST(xml, "src/encoding/xml/xml.c")
TEST_F(StdLibTest, XmlAllocationFailure) {
  auto r = compileAndRunStdTest("xml_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(asn1, "src/encoding/asn1/asn1.c")
TEST_F(StdLibTest, Asn1AllocationFailure) {
  auto r = compileAndRunStdTest("asn1_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== Hash =====
STD_TEST(fnv, "src/hash/fnv/fnv.c")
STD_TEST(crc32, "src/hash/crc32/crc32.c")
STD_TEST(crc64, "src/hash/crc64/crc64.c")
STD_TEST(xxhash, "src/hash/xxhash/xxhash.c")
#ifndef _WIN32
TEST_F(StdLibTest, crc_concurrency) {
  auto r = compileAndRunStdTest("crc_concurrency",
                                {"src/hash/crc32/crc32.c", "src/hash/crc64/crc64.c"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
#endif
STD_TEST(adler32, "src/hash/adler32/adler32.c")
STD_TEST(maphash, "src/hash/maphash/maphash.c")

// ===== Crypto =====
STD_TEST(sha256, "src/crypto/sha256/sha256.c")
STD_TEST(sha1, "src/crypto/sha1/sha1.c")
STD_TEST(sha512, "src/crypto/sha512/sha512.c")
STD_TEST(sha384, "src/crypto/sha384/sha384.c", "src/crypto/sha512/sha512.c")
STD_TEST(sha224, "src/crypto/sha224/sha224.c", "src/crypto/sha256/sha256.c")
STD_TEST(sha3, "src/crypto/sha3/sha3.c")
STD_TEST(sha512_variants, "src/crypto/sha512_224/sha512_224.c", "src/crypto/sha512_256/sha512_256.c", "src/crypto/sha512/sha512.c", "src/crypto/sha256/sha256.c")
STD_TEST(md5, "src/crypto/md5/md5.c")
STD_TEST(aes, "src/crypto/aes/aes.c")
STD_TEST(des, "src/crypto/des/des.c")
STD_TEST(rc4, "src/crypto/rc4/rc4.c")
STD_TEST(chacha20, "src/crypto/chacha20/chacha20.c")
STD_TEST(poly1305, "src/crypto/poly1305/poly1305.c")
STD_TEST(chacha20poly1305, "src/crypto/chacha20poly1305/chacha20poly1305.c", "src/crypto/chacha20/chacha20.c", "src/crypto/poly1305/poly1305.c")
STD_TEST(gcm, "src/crypto/gcm/gcm.c", "src/crypto/aes/aes.c")
STD_TEST(cipher, "src/crypto/cipher/cipher.c", "src/crypto/aes/aes.c")
STD_TEST(hmac, "src/crypto/hmac/hmac.c", "src/crypto/sha256/sha256.c", "src/crypto/sha512/sha512.c", "src/crypto/sha1/sha1.c", "src/crypto/md5/md5.c", "src/crypto/subtle/subtle.c")
STD_TEST(subtle, "src/crypto/subtle/subtle.c")
STD_TEST(hkdf, "src/crypto/hkdf/hkdf.c", "src/crypto/hmac/hmac.c", "src/crypto/sha256/sha256.c", "src/crypto/sha512/sha512.c", "src/crypto/sha1/sha1.c", "src/crypto/md5/md5.c", "src/crypto/subtle/subtle.c")
STD_TEST(pbkdf2, "src/crypto/pbkdf2/pbkdf2.c", "src/crypto/hmac/hmac.c", "src/crypto/sha256/sha256.c", "src/crypto/sha512/sha512.c", "src/crypto/sha1/sha1.c", "src/crypto/md5/md5.c", "src/crypto/subtle/subtle.c")
STD_TEST(crypto_rand, "src/crypto/rand/rand.c")
STD_TEST(elliptic, "src/crypto/elliptic/elliptic.c", "src/math/big/big.c")
STD_TEST(rsa, "src/crypto/rsa/rsa.c", "src/math/big/big.c", "src/crypto/rand/rand.c", "src/crypto/sha256/sha256.c", "src/crypto/sha384/sha384.c", "src/crypto/sha512/sha512.c")
TEST_F(StdLibTest, EmbeddedRsaPssProfilesDotSyntax) {
  auto r = compileAndRunStdTest("rsa_builtin", {}, {"-fbuiltin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
// A prime that leaves the public exponent non-invertible is a 1-in-65537 draw
// at the real exponent, so the generator's retry is only reachable with a
// small one. See test_rsa_retry.c.
TEST_F(StdLibTest, rsa_retry) {
  auto r = compileAndRunStdTest("rsa_retry",
                                {"src/crypto/rsa/rsa.c", "src/math/big/big.c",
                                 "src/crypto/rand/rand.c",
                                 "src/crypto/sha256/sha256.c",
                                 "src/crypto/sha384/sha384.c",
                                 "src/crypto/sha512/sha512.c"},
                                {"-DNCI_RSA_PUBLIC_EXPONENT=3"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(ecdsa, "src/crypto/ecdsa/ecdsa.c", "src/crypto/elliptic/elliptic.c", "src/math/big/big.c", "src/crypto/rand/rand.c", "src/crypto/sha256/sha256.c")
STD_TEST(dsa, "src/crypto/dsa/dsa.c", "src/math/big/big.c", "src/crypto/rand/rand.c", "src/crypto/sha256/sha256.c")
STD_TEST(ed25519, "src/crypto/ed25519/ed25519.c", "src/crypto/sha512/sha512.c", "src/crypto/rand/rand.c", "src/math/big/big.c")
STD_TEST(ecdh, "src/crypto/ecdh/ecdh.c", "src/crypto/elliptic/elliptic.c", "src/math/big/big.c", "src/crypto/rand/rand.c", "src/crypto/sha256/sha256.c")
STD_TEST(hpke, "src/crypto/hpke/hpke.c", "src/crypto/ecdh/ecdh.c",
    "src/crypto/elliptic/elliptic.c", "src/math/big/big.c",
    "src/crypto/hkdf/hkdf.c", "src/crypto/hmac/hmac.c",
    "src/crypto/sha256/sha256.c", "src/crypto/sha512/sha512.c",
    "src/crypto/sha1/sha1.c", "src/crypto/md5/md5.c",
    "src/crypto/aes/aes.c", "src/crypto/gcm/gcm.c",
    "src/crypto/chacha20/chacha20.c", "src/crypto/poly1305/poly1305.c",
    "src/crypto/chacha20poly1305/chacha20poly1305.c",
    "src/crypto/rand/rand.c", "src/crypto/subtle/subtle.c")
STD_TEST(mlkem, "src/crypto/mlkem/mlkem.c", "src/crypto/sha3/sha3.c",
    "src/crypto/rand/rand.c")
STD_TEST(mldsa, "src/crypto/mldsa/mldsa.c", "src/crypto/sha3/sha3.c",
    "src/crypto/rand/rand.c")

// ===== Unicode =====
STD_TEST(unicode, "src/unicode/unicode.c")
STD_TEST(utf8, "src/unicode/utf8/utf8.c")
STD_TEST(utf16, "src/unicode/utf16/utf16.c")

// ===== Core =====
STD_TEST(cmp, "src/cmp/cmp.c")
STD_TEST(bytes, "src/bytes/bytes.c")
TEST_F(StdLibTest, BytesAllocationFailure) {
  auto r = compileAndRunStdTest("bytes_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(errors, "src/errors/errors.c")
TEST_F(StdLibTest, ErrorsAllocationFailure) {
  auto r = compileAndRunStdTest("errors_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(html, "src/html/html.c")
STD_TEST(fmt, "src/fmt/fmt.c", "src/strconv/format_float.c", "src/strconv/parse_float.c")
TEST_F(StdLibTest, FmtAllocationFailure) {
  auto r = compileAndRunStdTest(
      "fmt_oom",
      {"src/strconv/format_float.c", "src/strconv/parse_float.c"},
      {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(io, "src/io/io.c")
TEST_F(StdLibTest, IoAllocationFailure) {
  auto r = compileAndRunStdTest("io_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(bufio, "src/bufio/bufio.c", "src/io/io.c")
TEST_F(StdLibTest, BufioAllocationFailure) {
  auto r = compileAndRunStdTest("bufio_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(flag, "src/flag/flag.c", "src/strconv/parse_int.c",
         "src/strconv/parse_float.c", "src/strconv/parse_bool.c")
STD_TEST(log, "src/log/log.c")
STD_TEST(slog, "src/log/slog/slog.c", "src/strconv/format_float.c")
STD_TEST(time, "src/time/time.c")
TEST_F(StdLibTest, TimeAllocationFailure) {
  auto r = compileAndRunStdTest("time_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(uuid, "src/uuid/uuid.c")
TEST_F(StdLibTest, UuidEntropyFailure) {
  auto r = compileAndRunStdTest(
      "uuid_entropy_failure", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(regexp, "src/regexp/regexp.c")
TEST_F(StdLibTest, RegexpAllocationFailure) {
  auto r = compileAndRunStdTest("regexp_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(mime, "src/mime/mime.c")
TEST_F(StdLibTest, MimeAllocationFailure) {
  auto r = compileAndRunStdTest("mime_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(context, "src/context/context.c")
TEST_F(StdLibTest, ContextAllocationFailure) {
  auto r = compileAndRunStdTest("context_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(maps, "src/maps/maps.c")
TEST_F(StdLibTest, MapsAllocationFailure) {
  auto r = compileAndRunStdTest("maps_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(slices, "src/slices/slices.c")
TEST_F(StdLibTest, SlicesAllocationFailure) {
  auto r = compileAndRunStdTest("slices_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== Container =====
STD_TEST(heap, "src/container/heap/heap.c")
STD_TEST(list, "src/container/list/list.c")
TEST_F(StdLibTest, ListAllocationFailure) {
  auto r = compileAndRunStdTest("list_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(ring, "src/container/ring/ring.c")
TEST_F(StdLibTest, RingAllocationFailure) {
  auto r = compileAndRunStdTest("ring_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(vector, "src/container/vector/vector.c")
TEST_F(StdLibTest, VectorAllocationFailure) {
  auto r = compileAndRunStdTest("vector_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== Compress =====
STD_TEST(lzw, "src/compress/lzw/lzw.c")
STD_TEST(flate, "src/compress/flate/flate.c")
STD_TEST(gzip, "src/compress/gzip/gzip.c", "src/compress/flate/flate.c", "src/hash/crc32/crc32.c")
STD_TEST(zlib, "src/compress/zlib/zlib.c", "src/compress/flate/flate.c", "src/hash/adler32/adler32.c")
STD_TEST(bzip2, "src/compress/bzip2/bzip2.c")

// ===== Archive =====
STD_TEST(tar, "src/archive/tar/tar.c")
TEST_F(StdLibTest, TarAllocationFailure) {
  auto r = compileAndRunStdTest("tar_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(zip, "src/archive/zip/zip.c", "src/hash/crc32/crc32.c")
TEST_F(StdLibTest, ZipAllocationFailure) {
  auto r = compileAndRunStdTest(
      "zip_oom", {"src/hash/crc32/crc32.c"}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== Text =====
// Note: 'template' is a C++ keyword, use template_ prefix
TEST_F(StdLibTest, text_template) {
  auto r = compileAndRunStdTest("template", {"src/text/template/template.c"});
  ASSERT_TRUE(r.ok()) << "stderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
TEST_F(StdLibTest, TextTemplateAllocationFailure) {
  auto r = compileAndRunStdTest("template_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(scanner, "src/text/scanner/scanner.c")
STD_TEST(tabwriter, "src/text/tabwriter/tabwriter.c")
TEST_F(StdLibTest, TabwriterAllocationFailure) {
  auto r = compileAndRunStdTest("tabwriter_oom", {},
                                {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== Index =====
STD_TEST(suffixarray, "src/index/suffixarray/suffixarray.c")

// ===== Sync =====
STD_TEST(sync, "src/sync/sync.c")
STD_TEST(thread, "src/thread/thread.c", "src/context/context.c")
TEST_F(StdLibTest, SyncMapAllocationFailure) {
  auto r = compileAndRunStdTest("sync_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
TEST_F(StdLibTest, EmbeddedThreadDotSyntax) {
  auto r = compileAndRunStdTest("thread_builtin", {}, {"-fbuiltin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
TEST_F(StdLibTest, ThreadAllocationAndCreationFailure) {
  auto r = compileAndRunStdTest("thread_failures",
                                {"src/context/context.c"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(atomic, "src/sync/atomic/atomic.c")

// ===== Net =====
#define TCP_DEPS \
    "src/net/tcp/tcp.c", "src/net/tcp/tcp_context.c", \
    "src/context/context.c"

STD_TEST(tcp, TCP_DEPS)
STD_TEST(udp, "src/net/udp/udp.c", "src/net/udp/udp_context.c",
         "src/context/context.c")
STD_TEST(net_transport,
         TCP_DEPS,
         "src/net/udp/udp.c", "src/net/udp/udp_context.c",
         "src/thread/thread.c")
TEST_F(StdLibTest, NetBufferFailurePaths) {
  auto r = compileAndRunStdTest("net_buffer", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// http.c embeds HTTPS support; the COFF linker (Windows) requires all
// referenced TLS symbols to be present at link time even when the test
// only exercises plain HTTP.  Include the full TLS dependency chain for
// every target that compiles http.c.
#define HTTP_TLS_DEPS \
    "src/compress/gzip/gzip.c", "src/compress/flate/flate.c", \
    "src/hash/crc32/crc32.c", "src/thread/thread.c", \
    "src/crypto/tls/tls.c", "src/crypto/tls/tls_config.c", \
    "src/crypto/tls/tls_record.c", "src/crypto/tls/tls_handshake.c", \
    "src/crypto/tls/tls_verify.c", \
    "src/crypto/tls/tls_key.c", "src/crypto/tls/tls_key_schedule.c", \
    "src/crypto/ecdh/ecdh.c", \
    "src/crypto/aes/aes.c", "src/crypto/gcm/gcm.c", \
    "src/crypto/chacha20/chacha20.c", "src/crypto/poly1305/poly1305.c", \
    "src/crypto/chacha20poly1305/chacha20poly1305.c", \
    "src/crypto/sha256/sha256.c", "src/crypto/sha512/sha512.c", \
    "src/crypto/sha384/sha384.c", "src/crypto/sha1/sha1.c", "src/crypto/md5/md5.c", \
    "src/crypto/hmac/hmac.c", "src/crypto/hkdf/hkdf.c", \
    "src/crypto/rand/rand.c", "src/crypto/subtle/subtle.c", \
    "src/crypto/x509/x509.c", "src/crypto/x509/x509_verify.c", \
    "src/crypto/x509/x509_pool.c", "src/crypto/x509/x509_system.c", \
    "src/crypto/rsa/rsa.c", "src/crypto/ecdsa/ecdsa.c", \
    "src/crypto/ed25519/ed25519.c", "src/crypto/elliptic/elliptic.c", \
    "src/math/big/big.c", "src/encoding/base64/base64.c", \
    "src/encoding/pem/pem.c"

STD_TEST(http, "src/net/http/http.c", "src/net/http/http_client.c", "src/net/http/http2/http2.c", "src/net/http/http2/http2_server.c", "src/net/http/http2/http2_client.c", TCP_DEPS, HTTP_TLS_DEPS)
STD_TEST(http_stage5,
         "src/net/http/http.c", "src/net/http/http_client.c",
         "src/net/http/http2/http2.c",
         "src/net/http/http2/http2_server.c",
         "src/net/http/http2/http2_client.c",
         "src/time/time.c", TCP_DEPS, HTTP_TLS_DEPS)
TEST_F(StdLibTest, HttpRouteAllocationFailure) {
  auto r = compileAndRunStdTest(
      "http_route_oom",
      {"src/net/http/http_client.c", "src/net/http/http2/http2.c",
       "src/net/http/http2/http2_server.c",
       "src/net/http/http2/http2_client.c", TCP_DEPS, HTTP_TLS_DEPS});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
TEST_F(StdLibTest, HttpStripPrefixAllocationFailure) {
  auto r = compileAndRunStdTest(
      "http_strip_prefix_oom",
      {"src/net/http/http.c", "src/net/http/http2/http2.c",
       "src/net/http/http2/http2_server.c",
       "src/net/http/http2/http2_client.c", TCP_DEPS, HTTP_TLS_DEPS});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
#ifndef _WIN32
TEST_F(StdLibTest, HttpClientAllocationFailure) {
  auto r = compileAndRunStdTest(
      "http_client_oom",
      {"src/net/http/http.c", "src/net/http/http2/http2.c",
       "src/net/http/http2/http2_server.c",
       "src/net/http/http2/http2_client.c", TCP_DEPS, HTTP_TLS_DEPS});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
#endif
STD_TEST(websocket, "src/net/websocket/websocket.c", TCP_DEPS,
    "src/net/http/http.c", "src/net/http/http_client.c", "src/net/http/http2/http2.c", "src/net/http/http2/http2_server.c", "src/net/http/http2/http2_client.c", HTTP_TLS_DEPS)
STD_TEST(url, "src/net/url/url.c")
STD_TEST(netip, "src/net/netip/netip.c")
STD_TEST(mail, "src/net/mail/mail.c")
STD_TEST(textproto, "src/net/textproto/textproto.c")
TEST_F(StdLibTest, TextprotoAllocationFailure) {
  auto r = compileAndRunStdTest("textproto_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(httptest, "src/net/http/httptest/httptest.c",
    "src/net/http/http.c", "src/net/http/http_client.c", "src/net/http/http2/http2.c", "src/net/http/http2/http2_server.c", "src/net/http/http2/http2_client.c", TCP_DEPS, HTTP_TLS_DEPS)

// ===== io_uring =====
STD_TEST(io_uring, TCP_DEPS, "src/net/http/http.c", "src/net/http/http_client.c", "src/net/http/http2/http2.c", "src/net/http/http2/http2_server.c", "src/net/http/http2/http2_client.c", HTTP_TLS_DEPS)

// ===== HTTP/2 =====
STD_TEST(http2, "src/net/http/http2/http2.c", "src/net/http/http2/http2_server.c", "src/net/http/http2/http2_client.c", "src/net/http/http.c", "src/net/http/http_client.c", TCP_DEPS,
    HTTP_TLS_DEPS)
STD_TEST(http2_oom, "src/net/http/http2/http2.c", "src/net/http/http.c",
    "src/net/http/http_client.c", TCP_DEPS, HTTP_TLS_DEPS)
TEST_F(StdLibTest, HpackAllocationFailure) {
  auto r = compileAndRunStdTest("hpack_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== Native RPC / gRPC =====
STD_TEST(rpc)
STD_TEST(grpc)

// ===== QUIC / HTTP/3 experimental components =====
STD_TEST(quic_frame)
STD_TEST(quic_loss)
STD_TEST(quic_conn, "src/crypto/rand/rand.c")
STD_TEST(http3_frame)
STD_TEST(http3_server)

TEST_F(StdLibTest, EmbeddedNetworkDotSyntax) {
  auto r = compileAndRunStdTest("network_builtin", {}, {"-fbuiltin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

TEST_F(StdLibTest, EmbeddedContextCancelHandleDotSyntax) {
  auto r = compileAndRunStdTest("context_builtin", {}, {"-fbuiltin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== HTTP Benchmark =====
STD_TEST(http_bench, "src/net/http/http.c", "src/net/http/http_client.c", "src/net/http/http2/http2.c", "src/net/http/http2/http2_server.c", "src/net/http/http2/http2_client.c", TCP_DEPS, HTTP_TLS_DEPS)

// ===== HTTP Util =====
STD_TEST(httputil, "src/net/http/httputil/httputil.c",
    "src/net/http/http.c", "src/net/http/http_client.c", "src/net/http/http2/http2.c", "src/net/http/http2/http2_server.c", "src/net/http/http2/http2_client.c", TCP_DEPS, HTTP_TLS_DEPS)

// ===== Cookie Jar =====
STD_TEST(cookiejar, "src/net/http/cookiejar/cookiejar.c")
#ifndef _WIN32
TEST_F(StdLibTest, CookieJarConcurrency) {
  auto r = compileAndRunStdTest(
      "cookiejar_concurrency", {"src/net/http/cookiejar/cookiejar.c"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
#endif
TEST_F(StdLibTest, CookieJarAllocationFailure) {
  auto r = compileAndRunStdTest("cookiejar_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== SMTP =====
STD_TEST(smtp, "src/net/smtp/smtp.c", TCP_DEPS)

// ===== Net Core (DNS, Pipe, SplitHostPort) =====
STD_TEST(resolve, "src/net/resolve/resolve.c")
STD_TEST(net_interface, "src/net/interface/interface.c")

// ===== Net Internals (Timer Wheel, Buffer Pool, Poller, Event Loop) =====
STD_TEST(net_internals, TCP_DEPS)
#ifndef _WIN32
TEST_F(StdLibTest, NetPollFallback) {
  auto r = compileAndRunStdTest("net_internals",
                                {TCP_DEPS},
                                {"-DNC_FORCE_POLL=1"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("ALL PASSED")) << "stdout: " << r.out;
}
#endif
TEST_F(StdLibTest, NetInternalHeadersAreStandalone) {
  auto r = compileAndRunStdTest("net_internal_headers", {},
                                {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
TEST_F(StdLibTest, NetIocpCompletionLifecycle) {
  auto r = compileAndRunStdTest("net_iocp", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
TEST_F(StdLibTest, NetThreadPoolAllocationFailure) {
  auto r = compileAndRunStdTest("net_threadpool_oom", {},
                                {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
TEST_F(StdLibTest, NetEventLoopAllocationFailure) {
  auto r = compileAndRunStdTest("net_eventloop_oom", {},
                                {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== Path =====
STD_TEST(filepath, "src/path/filepath/filepath.c")

// ===== Image =====
STD_TEST(color, "src/image/color/color.c")
STD_TEST(image, "src/image/image/image.c", "src/image/color/color.c")
STD_TEST(draw, "src/image/draw/draw.c", "src/image/image/image.c", "src/image/color/color.c")
STD_TEST(png, "src/image/png/png.c", "src/image/image/image.c", "src/image/color/color.c", "src/compress/flate/flate.c", "src/hash/crc32/crc32.c", "src/hash/adler32/adler32.c")
STD_TEST(jpeg, "src/image/jpeg/jpeg.c", "src/image/image/image.c", "src/image/color/color.c")
TEST_F(StdLibTest, JpegAllocationFailure) {
  auto r = compileAndRunStdTest("jpeg_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(gif, "src/image/gif/gif.c", "src/image/image/image.c", "src/image/color/color.c", "src/compress/lzw/lzw.c")
TEST_F(StdLibTest, GifAllocationFailure) {
  auto r = compileAndRunStdTest("gif_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
TEST_F(StdLibTest, GifResourceBounds) {
  auto r = compileAndRunStdTest("gif_bounds", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
// Locks in the Wu-quantizer quality win in neverc_gif_from_rgba (lower error
// than the old 216-color web-safe cube; near-lossless on small off-grid palettes).
STD_TEST(gif_quant, "src/image/gif/gif.c", "src/image/image/image.c", "src/image/color/color.c", "src/compress/lzw/lzw.c")
STD_TEST(palette, "src/image/color/palette/palette.c")

// ===== OS =====
STD_TEST(os, "src/os/os.c")
TEST_F(StdLibTest, OsAllocationFailure) {
  auto r = compileAndRunStdTest("os_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(exec, "src/os/exec/exec.c")
TEST_F(StdLibTest, ExecAllocationFailure) {
  auto r = compileAndRunStdTest("exec_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
STD_TEST(signal, "src/os/signal/signal.c")
STD_TEST(user, "src/os/user/user.c")

// ===== HTML =====
STD_TEST(html_template, "src/html/template/template.c", "src/html/html.c")
TEST_F(StdLibTest, HtmlTemplateAllocationFailure) {
  auto r = compileAndRunStdTest("html_template_oom", {},
                                {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== MIME =====
STD_TEST(quotedprintable, "src/mime/quotedprintable/quotedprintable.c")
STD_TEST(multipart, "src/mime/multipart/multipart.c")

// ===== IO =====
STD_TEST(fs, "src/io/fs/fs.c")
TEST_F(StdLibTest, FsAllocationFailure) {
  auto r = compileAndRunStdTest("fs_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== Log =====
STD_TEST(syslog, "src/log/syslog/syslog.c")

// ===== Debug =====
STD_TEST(elf, "src/debug/elf/elf.c")
STD_TEST(pe, "src/debug/pe/pe.c")
STD_TEST(macho, "src/debug/macho/macho.c")
STD_TEST(dwarf, "src/debug/dwarf/dwarf.c")
STD_TEST(plan9obj, "src/debug/plan9obj/plan9obj.c")

// ===== Crypto (x509) =====
#define X509_VERIFY_DEPS \
    "src/crypto/x509/x509.c", "src/crypto/x509/x509_verify.c", \
    "src/crypto/x509/x509_pool.c", "src/crypto/x509/x509_system.c", \
    "src/crypto/rsa/rsa.c", "src/crypto/ecdsa/ecdsa.c", \
    "src/crypto/ed25519/ed25519.c", "src/crypto/rand/rand.c", \
    "src/crypto/elliptic/elliptic.c", \
    "src/crypto/sha256/sha256.c", "src/crypto/sha384/sha384.c", \
    "src/crypto/sha512/sha512.c", "src/math/big/big.c", \
    "src/encoding/pem/pem.c", "src/encoding/base64/base64.c"

STD_TEST(x509, X509_VERIFY_DEPS)
STD_TEST(x509_chain, X509_VERIFY_DEPS)
TEST_F(StdLibTest, EmbeddedX509SignatureDotSyntax) {
  auto r = compileAndRunStdTest("x509_builtin", {}, {"-fbuiltin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
#undef X509_VERIFY_DEPS
STD_TEST(tls, HTTP_TLS_DEPS, TCP_DEPS)
TEST_F(StdLibTest, TlsExperimentalTransport) {
  auto r = compileAndRunStdTest(
      "tls",
      {HTTP_TLS_DEPS, TCP_DEPS},
      {"-DNEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT=1",
       "-DNEVERC_TLS_TESTING=1"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
TEST_F(StdLibTest, TlsOpenSslBidirectionalInterop) {
#if defined(_WIN32)
  GTEST_SKIP() << "OpenSSL interop harness is POSIX-only for now";
#else
  // LibreSSL's s_server rejects host:port -accept and lacks -ciphersuites.
  if (std::system(
          "bash -c 'for c in \"${OPENSSL_BIN:-}\" "
          "/opt/homebrew/opt/openssl@3/bin/openssl "
          "/usr/local/opt/openssl@3/bin/openssl "
          "/opt/homebrew/bin/openssl /usr/local/bin/openssl "
          "$(command -v openssl 2>/dev/null); do "
          "[ -n \"$c\" ] && [ -x \"$c\" ] || continue; "
          "case \"$($c version 2>/dev/null)\" in "
          "OpenSSL\\ 3*|OpenSSL\\ 1.1*) exit 0;; esac; "
          "done; exit 1'") != 0)
    GTEST_SKIP() << "OpenSSL 1.1+/3.x not available";

  fs::path peer = tmp() / "tls_openssl_interop_peer";
  fs::path cert = tmp() / "tls_openssl_interop_cert.pem";
  fs::path key = tmp() / "tls_openssl_interop_key.pem";
  std::string sd = stdSrcDir();

  std::vector<std::string> args = {
      "-I" + sd + "/include",
      "-I" + sd + "/src/net",
      "-Wall",
      "-Wextra",
      "-Wno-unused-parameter",
      "-Wno-unused-function",
      "-O1",
      "-fno-builtin-std",
      "-DNEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT=1",
      "-DNEVERC_TLS_TESTING=1",
      "-o",
      peer.string(),
      (fs::path(stdTestDir()) / "test_tls_interop.c").string(),
  };
  for (const char *src : {HTTP_TLS_DEPS, TCP_DEPS})
    args.push_back(sd + "/" + src);
  args.push_back("-lm");
  args.push_back("-lpthread");

  CmdResult compile = ncc(args);
  ASSERT_TRUE(compile.ok()) << "stdout: " << compile.out
                            << "\nstderr: " << compile.err;

  fs::path script =
      fs::path(stdTestDir()) / "run_tls_openssl_interop.sh";
  auto run = exec("/bin/bash",
                  {script.string(), peer.string(), cert.string(),
                   key.string()});
  ASSERT_TRUE(run.ok()) << "stdout: " << run.out << "\nstderr: " << run.err;
  EXPECT_TRUE(run.contains("openssl interop client: ok"))
      << "stdout: " << run.out;
  EXPECT_TRUE(run.contains("openssl interop server: ok"))
      << "stdout: " << run.out;
  EXPECT_TRUE(run.contains("openssl interop client resumption: ok"))
      << "stdout: " << run.out;
  EXPECT_TRUE(run.contains("openssl interop server resumption: ok"))
      << "stdout: " << run.out;
#endif
}
TEST_F(StdLibTest, TlsBoringSslBidirectionalInterop) {
#if defined(_WIN32)
  GTEST_SKIP() << "BoringSSL interop harness is POSIX-only for now";
#else
  fs::path peer = tmp() / "tls_boringssl_interop_peer";
  fs::path cert = tmp() / "tls_boringssl_interop_cert.pem";
  fs::path key = tmp() / "tls_boringssl_interop_key.pem";
  std::string sd = stdSrcDir();

  std::vector<std::string> args = {
      "-I" + sd + "/include",
      "-I" + sd + "/src/net",
      "-Wall",
      "-Wextra",
      "-Wno-unused-parameter",
      "-Wno-unused-function",
      "-O1",
      "-fno-builtin-std",
      "-DNEVERC_TLS_ENABLE_EXPERIMENTAL_TRANSPORT=1",
      "-DNEVERC_TLS_TESTING=1",
      "-o",
      peer.string(),
      (fs::path(stdTestDir()) / "test_tls_interop.c").string(),
  };
  for (const char *src : {HTTP_TLS_DEPS, TCP_DEPS})
    args.push_back(sd + "/" + src);
  args.push_back("-lm");
  args.push_back("-lpthread");

  CmdResult compile = ncc(args);
  ASSERT_TRUE(compile.ok()) << "stdout: " << compile.out
                            << "\nstderr: " << compile.err;

  fs::path script =
      fs::path(stdTestDir()) / "run_tls_boringssl_interop.sh";
  auto run = exec("/bin/bash",
                  {script.string(), peer.string(), cert.string(),
                   key.string()});
  if (run.contains("skip: bssl not available") ||
      run.contains("skip: OpenSSL") ||
      run.contains("skip: openssl"))
    GTEST_SKIP() << "boringssl interop prerequisites unavailable";
  ASSERT_TRUE(run.ok()) << "stdout: " << run.out << "\nstderr: " << run.err;
  EXPECT_TRUE(run.contains("boringssl interop client: ok"))
      << "stdout: " << run.out;
  EXPECT_TRUE(run.contains("boringssl interop server: ok"))
      << "stdout: " << run.out;
  EXPECT_TRUE(run.contains("boringssl interop client resumption: ok"))
      << "stdout: " << run.out;
  EXPECT_TRUE(run.contains("boringssl interop server resumption: ok"))
      << "stdout: " << run.out;
#endif
}
TEST_F(StdLibTest, EmbeddedTlsCertificateVerifyDotSyntax) {
  auto r = compileAndRunStdTest("tls_builtin", {}, {"-fbuiltin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== Regexp =====
STD_TEST(regexp_syntax, "src/regexp/syntax/syntax.c")
TEST_F(StdLibTest, RegexpSyntaxAllocationFailure) {
  auto r = compileAndRunStdTest("regexp_syntax_oom", {},
                                {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== Time =====
STD_TEST(tzdata, "src/time/tzdata/tzdata.c")

// ===== CString =====
STD_TEST(cstring, "src/cstring/cstring.c")
TEST_F(StdLibTest, CStringAllocationFailure) {
  auto r = compileAndRunStdTest("cstring_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== Arena =====
STD_TEST(arena, "src/arena/arena.c")
TEST_F(StdLibTest, ArenaAllocationFailure) {
  auto r = compileAndRunStdTest("arena_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== Unique =====
STD_TEST(unique, "src/unique/unique.c")
TEST_F(StdLibTest, UniqueAllocationFailure) {
  auto r = compileAndRunStdTest("unique_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== Weak =====
STD_TEST(weak, "src/weak/weak.c")
TEST_F(StdLibTest, WeakRetainLifecycle) {
  auto r = compileAndRunStdTest("weak_retain", {"src/weak/weak.c"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}
TEST_F(StdLibTest, WeakAllocationFailure) {
  auto r = compileAndRunStdTest("weak_oom", {}, {"-fno-builtin-std"});
  ASSERT_TRUE(r.ok()) << "stdout: " << r.out << "\nstderr: " << r.err;
  EXPECT_TRUE(r.contains("passed")) << "stdout: " << r.out;
}

// ===== Dot-syntax (comprehensive) =====
STD_TEST(dot_syntax,
    "src/math/abs.c", "src/math/acos.c", "src/math/acosh.c", "src/math/asin.c",
    "src/math/asinh.c", "src/math/atan.c", "src/math/atan2.c", "src/math/atanh.c",
    "src/math/cbrt.c", "src/math/ceil.c", "src/math/copysign.c", "src/math/cos.c",
    "src/math/cosh.c", "src/math/dim.c", "src/math/erf.c", "src/math/erfc.c",
    "src/math/erfcinv.c", "src/math/erfinv.c", "src/math/exp.c", "src/math/exp2.c",
    "src/math/expm1.c", "src/math/float32bits.c", "src/math/float64bits.c",
    "src/math/floor.c", "src/math/fma.c", "src/math/fmod.c", "src/math/frexp.c",
    "src/math/gamma.c", "src/math/hypot.c", "src/math/ilogb.c", "src/math/inf.c",
    "src/math/isinf.c", "src/math/isnan.c", "src/math/j0.c", "src/math/j1.c",
    "src/math/jn.c", "src/math/ldexp.c", "src/math/lgamma.c", "src/math/log.c",
    "src/math/log10.c", "src/math/log1p.c", "src/math/log2.c", "src/math/logb.c",
    "src/math/max.c", "src/math/min.c", "src/math/modf.c", "src/math/nan.c",
    "src/math/nextafter.c", "src/math/nextafter32.c", "src/math/pow.c",
    "src/math/pow10.c", "src/math/remainder.c", "src/math/round.c",
    "src/math/roundtoeven.c", "src/math/signbit.c", "src/math/sin.c",
    "src/math/sincos.c", "src/math/sinh.c", "src/math/sqrt.c", "src/math/tan.c",
    "src/math/tanh.c", "src/math/trunc.c",
    "src/math/rand/rand.c",
    "src/math/bits/bits.c",
    "src/strconv/format_int.c", "src/strconv/parse_int.c", "src/strconv/format_bool.c",
    "src/strconv/format_float.c", "src/strconv/parse_float.c",
    "src/strconv/parse_bool.c",
    "src/encoding/hex/encode.c", "src/encoding/hex/decode.c",
    "src/hash/fnv/fnv.c",
    "src/hash/adler32/adler32.c",
    "src/container/vector/vector.c",
    "src/sort/sort.c",
    "src/cmp/cmp.c",
    "src/errors/errors.c",
    "src/bytes/bytes.c",
    "src/unicode/unicode.c",
    "src/html/html.c",
    "src/path/base.c", "src/path/dir.c", "src/path/ext.c", "src/path/isabs.c", "src/path/clean.c",
    "src/slices/slices.c",
    "src/maps/maps.c",
    "src/regexp/regexp.c",
    "src/uuid/uuid.c",
    "src/fmt/fmt.c",
    "src/cstring/cstring.c",
    "src/io/io.c",
    "src/time/time.c",
    "src/compress/zlib/zlib.c",
    "src/sync/sync.c",
    "src/sync/atomic/atomic.c",
    "src/os/os.c",
    "src/log/log.c",
    "src/encoding/json/json.c",
    "src/encoding/binary/binary.c",
    "src/container/list/list.c",
    "src/container/ring/ring.c",
    "src/path/filepath/filepath.c",
    "src/hash/crc64/crc64.c",
    "src/image/color/color.c",
    "src/flag/flag.c",
    "src/arena/arena.c",
    "src/unique/unique.c",
    "src/weak/weak.c",
    TCP_DEPS,
    "src/net/http/http.c",
    "src/net/http/http_client.c",
    "src/net/http/http2/http2.c",
    "src/net/http/http2/http2_server.c", "src/net/http/http2/http2_client.c",
    "src/net/url/url.c",
    HTTP_TLS_DEPS)
