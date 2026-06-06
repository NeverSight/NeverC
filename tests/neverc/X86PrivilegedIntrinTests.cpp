#include "NeverCTestFixture.h"

class X86PrivilegedIntrinTest : public NeverCTest {};

TEST_F(X86PrivilegedIntrinTest, MsrIntrinsicsLowerCorrectly) {
  auto src = tmpFile("msr_intrin.c");
  writeFile(src,
      "unsigned __int64 read_msr(unsigned int addr) {\n"
      "  return __readmsr(addr);\n"
      "}\n"
      "void write_msr(unsigned int addr, unsigned __int64 val) {\n"
      "  __writemsr(addr, val);\n"
      "}\n");
  auto out = tmpFile("msr_intrin.s");
  auto r = ncc({"--target=x86_64-pc-windows-msvc", "-O2", "-S",
                src.string(), "-o", out.string()});
  ASSERT_EQ(r.exitCode, 0) << "compile failed\n" << r.err;

  std::string s = readFile(out);
  auto has = [&](const char *m) { return s.find(m) != std::string::npos; };
  EXPECT_TRUE(has("rdmsr")) << "expected rdmsr instruction\n" << s;
  EXPECT_TRUE(has("wrmsr")) << "expected wrmsr instruction\n" << s;
  EXPECT_FALSE(has("__asm")) << "should not contain inline asm remnants\n" << s;
}

TEST_F(X86PrivilegedIntrinTest, CrRegisterIntrinsicsLowerCorrectly) {
  auto src = tmpFile("cr_intrin.c");
  writeFile(src,
      "unsigned __int64 read_cr0(void) { return __readcr0(); }\n"
      "unsigned __int64 read_cr3(void) { return __readcr3(); }\n"
      "unsigned __int64 read_cr4(void) { return __readcr4(); }\n"
      "void write_cr0(unsigned __int64 v) { __writecr0(v); }\n"
      "void write_cr3(unsigned __int64 v) { __writecr3(v); }\n"
      "void write_cr4(unsigned __int64 v) { __writecr4(v); }\n");
  auto out = tmpFile("cr_intrin.s");
  auto r = ncc({"--target=x86_64-pc-windows-msvc", "-O2", "-S",
                src.string(), "-o", out.string()});
  ASSERT_EQ(r.exitCode, 0) << "compile failed\n" << r.err;

  std::string s = readFile(out);
  auto has = [&](const char *m) { return s.find(m) != std::string::npos; };
  EXPECT_TRUE(has("cr0")) << "expected cr0 reference\n" << s;
  EXPECT_TRUE(has("cr3")) << "expected cr3 reference\n" << s;
  EXPECT_TRUE(has("cr4")) << "expected cr4 reference\n" << s;
}

TEST_F(X86PrivilegedIntrinTest, PortIoIntrinsicsLowerCorrectly) {
  auto src = tmpFile("portio_intrin.c");
  writeFile(src,
      "unsigned char  in8 (unsigned short p) { return __inbyte(p); }\n"
      "unsigned short in16(unsigned short p) { return __inword(p); }\n"
      "unsigned int   in32(unsigned short p) { return __indword(p); }\n"
      "void out8 (unsigned short p, unsigned char  v) { __outbyte(p, v); }\n"
      "void out16(unsigned short p, unsigned short v) { __outword(p, v); }\n"
      "void out32(unsigned short p, unsigned int   v) { __outdword(p, v); }\n");
  auto out = tmpFile("portio_intrin.s");
  auto r = ncc({"--target=x86_64-pc-windows-msvc", "-O2", "-S",
                src.string(), "-o", out.string()});
  ASSERT_EQ(r.exitCode, 0) << "compile failed\n" << r.err;

  std::string s = readFile(out);
  auto has = [&](const char *m) { return s.find(m) != std::string::npos; };
  EXPECT_TRUE(has("in\tal, dx") || has("in al, dx"))
      << "expected in al, dx instruction\n" << s;
  EXPECT_TRUE(has("in\tax, dx") || has("in ax, dx"))
      << "expected in ax, dx instruction\n" << s;
  EXPECT_TRUE(has("in\teax, dx") || has("in eax, dx"))
      << "expected in eax, dx instruction\n" << s;
  EXPECT_TRUE(has("out\tdx, al") || has("out dx, al"))
      << "expected out dx, al instruction\n" << s;
  EXPECT_TRUE(has("out\tdx, ax") || has("out dx, ax"))
      << "expected out dx, ax instruction\n" << s;
  EXPECT_TRUE(has("out\tdx, eax") || has("out dx, eax"))
      << "expected out dx, eax instruction\n" << s;
  EXPECT_FALSE(has("__asm")) << "should not contain inline asm remnants\n" << s;
}

TEST_F(X86PrivilegedIntrinTest, InvlpgIntrinsicLowersCorrectly) {
  auto src = tmpFile("invlpg_intrin.c");
  writeFile(src,
      "void flush_page(void *addr) { __invlpg(addr); }\n");
  auto out = tmpFile("invlpg_intrin.s");
  auto r = ncc({"--target=x86_64-pc-windows-msvc", "-O2", "-S",
                src.string(), "-o", out.string()});
  ASSERT_EQ(r.exitCode, 0) << "compile failed\n" << r.err;

  std::string s = readFile(out);
  auto has = [&](const char *m) { return s.find(m) != std::string::npos; };
  EXPECT_TRUE(has("invlpg")) << "expected invlpg instruction\n" << s;
}
