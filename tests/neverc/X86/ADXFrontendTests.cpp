#include "NativeBuiltinFixture.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/TargetParser/Host.h"

class X86ADXTest : public X86NativeBuiltinTest {};

TEST_F(X86ADXTest, NativeHeadersGenerateBothChainsAcrossObjectFormats) {
  const auto Source = tmpFile("adx_headers.c");
  std::string Code = R"(
#include <x86intrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#endif
#if defined(__ADX__)
#error "Use function targets without a translation-unit ADX feature"
#endif
)";
  for (unsigned Bits : {32u, 64u}) {
    const std::string W = std::to_string(Bits);
    const std::string Type = Bits == 32 ? "unsigned" : "unsigned long long";
    for (const char *Name : {"adcx", "adox", "addcarryx"})
      Code += "__attribute__((target(\"adx\"))) unsigned char " +
              std::string(Name) + W + "(unsigned char c, " + Type + " a, " +
              Type + " b, " + Type + " *p) { return _" + Name + "_u" + W +
              "(c, a, b, p); }\n";
    // The baseline ADC API shares the old addcarryx builtin spelling and must
    // continue to work without ADX after adding the explicit native APIs.
    Code += "unsigned char baseline" + W + "(unsigned char c, " + Type +
            " a, " + Type + " b, " + Type + " *p) { return _addcarry_u" + W +
            "(c, a, b, p); }\n";
  }
  writeFile(Source, Code);
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      const auto AssemblyFile = tmpFile("adx_headers.s");
      auto Result =
          ncc({Target, Opt, "-mno-adx", "-fno-lto", "-fno-builtin-mimalloc",
               "-ffreestanding", "-Werror=implicit-function-declaration",
               "-mllvm", "-verify-machineinstrs", "-masm=att", "-S",
               Source.string(), "-o", AssemblyFile.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Assembly = readFile(AssemblyFile);
      EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
      EXPECT_EQ(Assembly.find("\tcall"), std::string::npos) << Assembly;
      for (const char *Op : {"adcx", "adox"})
        for (const char *Suffix : {"l", "q"})
          EXPECT_NE(Assembly.find("\t" + std::string(Op) + Suffix + "\t"),
                    std::string::npos)
              << Assembly;
      const auto IRFile = tmpFile("adx_headers.ll");
      Result = ncc({Target, Opt, "-mno-adx", "-fno-lto",
                    "-fno-builtin-mimalloc", "-ffreestanding", "-S",
                    "-emit-llvm", Source.string(), "-o", IRFile.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto IR = readFile(IRFile);
      EXPECT_EQ(IR.find("asm "), std::string::npos) << IR;
      for (const char *Op : {"adcx", "adox", "addcarry"})
        for (const char *Bits : {"32", "64"})
          EXPECT_NE(IR.find("@llvm.x86." + std::string(Op) + "." + Bits),
                    std::string::npos)
              << IR;
    }
}

TEST_F(X86ADXTest, NativeBuiltinsAndHeadersRequireAdx) {
  for (const char *Name : {"adcx", "adox"})
    for (unsigned Bits : {32u, 64u})
      for (bool Builtin : {false, true}) {
        const std::string W = std::to_string(Bits);
        const std::string Type = Bits == 32 ? "unsigned" : "unsigned long long";
        const auto Source = tmpFile("adx_feature.c");
        writeFile(Source, "#include <immintrin.h>\nunsigned char f(" + Type +
                              " a, " + Type + " b, " + Type + " *p) { return " +
                              (Builtin ? "__builtin_ia32_" : "_") + Name +
                              "_u" + W + "(0, a, b, p); }\n");
        const auto Output = tmpFile("adx_feature.o");
        const auto Result =
            ncc({"--target=x86_64-unknown-linux-gnu", "-mno-adx", "-O2", "-c",
                 Source.string(), "-o", Output.string()});
        EXPECT_NE(Result.exitCode, 0) << Name << W;
        EXPECT_NE(Result.err.find("adx"), std::string::npos) << Result.err;
      }
}

TEST_F(X86ADXTest, EveryCarryByteAliasesAndInterleavedChainsMatchBitAddition) {
  llvm::StringMap<bool, llvm::MallocAllocator> Features;
  if (!llvm::sys::getHostCPUFeatures(Features) || !Features.lookup("adx"))
    GTEST_SKIP() << "native execution requires ADX";
  const auto Source = tmpFile("adx_values.c");
  writeFile(Source, R"(
#include <immintrin.h>
#include <stdint.h>
__attribute__((noinline))
unsigned char reference(unsigned char carry, uint64_t a, uint64_t b,
                        unsigned bits, uint64_t *sum) {
  uint64_t result = 0;
  carry = carry != 0;
  for (unsigned bit = 0; bit < bits; ++bit) {
    unsigned digit = ((a >> bit) & 1) + ((b >> bit) & 1) + carry;
    result |= (uint64_t)(digit & 1) << bit;
    carry = digit >> 1;
  }
  *sum = result;
  return carry;
}
__attribute__((target("adx"), noinline))
int verify(unsigned char c, unsigned long long a, unsigned long long b) {
  uint64_t expected;
  unsigned char want = reference(c, a, b, 64, &expected);
  unsigned long long x = a;
  if (_adcx_u64(c, x, b, &x) != want || x != expected) return 1;
  x = a;
  if (_adox_u64(c, x, b, &x) != want || x != expected) return 2;
  if (_addcarryx_u64(c, a, b, &x) != want || x != expected) return 3;
  if (_addcarry_u64(c, a, b, &x) != want || x != expected) return 4;
  want = reference(c, a, b, 32, &expected);
  unsigned y = a;
  if (_adcx_u32(c, y, b, &y) != want || y != expected) return 5;
  y = a;
  if (_adox_u32(c, y, b, &y) != want || y != expected) return 6;
  if (_addcarryx_u32(c, a, b, &y) != want || y != expected) return 7;
  if (_addcarry_u32(c, a, b, &y) != want || y != expected) return 8;
  return 0;
}
__attribute__((target("adx"), noinline))
int chains(unsigned char cf, unsigned char of) {
  unsigned long long a = ~0ULL, b = 0x7fffffffffffffffULL;
  unsigned long long c = 1, d = 0x8000000000000001ULL;
  uint64_t x = a, y = b;
  unsigned char xc = cf, yc = of;
  for (unsigned step = 0; step < 4; ++step) {
    cf = _adcx_u64(cf, a, c, &a);
    of = _adox_u64(of, b, d, &b);
    xc = reference(xc, x, c, 64, &x);
    yc = reference(yc, y, d, 64, &y);
    if (a != x || b != y || cf != xc || of != yc) return 9;
  }
  unsigned aa = ~0U, bb = 0x7fffffffU;
  x = aa; y = bb; xc = cf; yc = of;
  for (unsigned step = 0; step < 4; ++step) {
    cf = _adcx_u32(cf, aa, 1, &aa);
    of = _adox_u32(of, bb, 0x80000001U, &bb);
    xc = reference(xc, x, 1, 32, &x);
    yc = reference(yc, y, 0x80000001U, 32, &y);
    if (aa != x || bb != y || cf != xc || of != yc) return 10;
  }
  return 0;
}
__attribute__((target("adx"))) int once(void) {
  unsigned a = 1, b = 2, c = 0, out[2], *p = out;
  if (_adcx_u32(c++, a++, b++, p++) != 0 || out[0] != 3 || c != 1 || a != 2 || b != 3 || p != out + 1) return 11;
  unsigned long long x = 1, y = 2, result[2], *q = result;
  c = 3;
  if (_adox_u64(c++, x++, y++, q++) != 0 || result[0] != 4 || c != 4 || x != 2 || y != 3 || q != result + 1) return 12;
  return 0;
}
int main(void) {
  const uint64_t fixed[] = {0, 1, UINT64_MAX, 0x8000000000000000ULL,
    0x7fffffffffffffffULL, 0xffffffffULL, 0x80000000ULL, 0xfedcba9876543210ULL};
  for (unsigned carry = 0; carry < 256; ++carry) {
    for (unsigned i = 0; i < 8; ++i) for (unsigned j = 0; j < 8; ++j) {
      int result = verify(carry, fixed[i], fixed[j]);
      if (result) return result;
    }
    for (unsigned other = 0; other < 256; ++other) {
      int result = chains(carry, other);
      if (result) return result;
    }
  }
  return once();
}
)");
  for (const char *Opt : {"-O0", "-O2"})
    compileRunAndCheck(
        std::string("adx_values") + Opt, Source.string(),
        std::string("-std=c23 -mno-adx -fno-lto -fno-builtin-mimalloc ") + Opt);
}
