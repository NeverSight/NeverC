#include "NeverCTestFixture.h"

class X86PrivilegedIntrinTest : public NeverCTest {
protected:
  std::pair<int, std::string> compileToAsm(const char *src,
                                           const char *stem) {
    auto srcFile = tmpFile(std::string(stem) + ".c");
    writeFile(srcFile, src);
    auto outFile = tmpFile(std::string(stem) + ".s");
    // -fno-builtin-mimalloc: these tests scan the whole assembly listing for
    // the presence or absence of specific instructions and of "#APP", and the
    // allocator that is otherwise injected by default contributes hundreds of
    // functions with inline asm of their own.
    auto r = ncc({"--target=x86_64-pc-windows-msvc", "-fno-builtin-mimalloc",
                  "-O2", "-S", srcFile.string(), "-o", outFile.string()});
    if (r.exitCode != 0) return {r.exitCode, r.err};
    return {0, readFile(outFile)};
  }
};

// ---------------------------------------------------------------------------
// MSR: rdmsr / wrmsr — true intrinsic lowering (no InlineAsm markers)
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, MsrIntrinsicsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    unsigned __int64 read_msr(unsigned int addr) { return __readmsr(addr); }
    void write_msr(unsigned int addr, unsigned __int64 val) { __writemsr(addr, val); }
  )", "msr");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("rdmsr"), std::string::npos) << "expected rdmsr\n" << s;
  EXPECT_NE(s.find("wrmsr"), std::string::npos) << "expected wrmsr\n" << s;
  EXPECT_EQ(s.find("#APP"), std::string::npos) << "intrinsic path must not use InlineAsm\n" << s;
}

// ---------------------------------------------------------------------------
// CR registers: read + write — write must NOT be DCE'd
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, CrRegisterReadsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    unsigned __int64 read_cr0(void) { return __readcr0(); }
    unsigned __int64 read_cr3(void) { return __readcr3(); }
    unsigned __int64 read_cr4(void) { return __readcr4(); }
    unsigned __int64 read_cr8(void) { return __readcr8(); }
  )", "cr_read");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("mov\trax, cr0"), std::string::npos) << "expected mov rax, cr0\n" << s;
  EXPECT_NE(s.find("mov\trax, cr3"), std::string::npos) << "expected mov rax, cr3\n" << s;
  EXPECT_NE(s.find("mov\trax, cr4"), std::string::npos) << "expected mov rax, cr4\n" << s;
  EXPECT_NE(s.find("mov\trax, cr8"), std::string::npos) << "expected mov rax, cr8\n" << s;
}

TEST_F(X86PrivilegedIntrinTest, CrRegisterWritesNotDCEd) {
  auto [rc, s] = compileToAsm(R"(
    void write_cr0(unsigned __int64 v) { __writecr0(v); }
    void write_cr3(unsigned __int64 v) { __writecr3(v); }
    void write_cr4(unsigned __int64 v) { __writecr4(v); }
    void write_cr8(unsigned __int64 v) { __writecr8(v); }
  )", "cr_write");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("mov\tcr0, rcx"), std::string::npos) << "write_cr0 DCE'd or wrong\n" << s;
  EXPECT_NE(s.find("mov\tcr3, rcx"), std::string::npos) << "write_cr3 DCE'd or wrong\n" << s;
  EXPECT_NE(s.find("mov\tcr4, rcx"), std::string::npos) << "write_cr4 DCE'd or wrong\n" << s;
  EXPECT_NE(s.find("mov\tcr8, rcx"), std::string::npos) << "write_cr8 DCE'd or wrong\n" << s;
}

// ---------------------------------------------------------------------------
// DR registers: read + write — write must NOT be DCE'd
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, DrRegisterOpsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    unsigned __int64 read_dr0(void) { return __readdr(0); }
    unsigned __int64 read_dr7(void) { return __readdr(7); }
    void write_dr0(unsigned __int64 v) { __writedr(0, v); }
    void write_dr7(unsigned __int64 v) { __writedr(7, v); }
  )", "dr");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("mov\trax, dr0"), std::string::npos) << "expected mov rax, dr0\n" << s;
  EXPECT_NE(s.find("mov\trax, dr7"), std::string::npos) << "expected mov rax, dr7\n" << s;
  EXPECT_NE(s.find("mov\tdr0, rcx"), std::string::npos) << "write_dr0 DCE'd or wrong\n" << s;
  EXPECT_NE(s.find("mov\tdr7, rcx"), std::string::npos) << "write_dr7 DCE'd or wrong\n" << s;
}

// ---------------------------------------------------------------------------
// Port I/O: in/out 8/16/32 — true intrinsic lowering
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, PortIoIntrinsicsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    unsigned char  in8 (unsigned short p) { return __inbyte(p); }
    unsigned short in16(unsigned short p) { return __inword(p); }
    unsigned int   in32(unsigned short p) { return __indword(p); }
    void out8 (unsigned short p, unsigned char  v) { __outbyte(p, v); }
    void out16(unsigned short p, unsigned short v) { __outword(p, v); }
    void out32(unsigned short p, unsigned int   v) { __outdword(p, v); }
  )", "portio");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("in\tal, dx"), std::string::npos) << "expected in al, dx\n" << s;
  EXPECT_NE(s.find("in\tax, dx"), std::string::npos) << "expected in ax, dx\n" << s;
  EXPECT_NE(s.find("in\teax, dx"), std::string::npos) << "expected in eax, dx\n" << s;
  EXPECT_NE(s.find("out\tdx, al"), std::string::npos) << "expected out dx, al\n" << s;
  EXPECT_NE(s.find("out\tdx, ax"), std::string::npos) << "expected out dx, ax\n" << s;
  EXPECT_NE(s.find("out\tdx, eax"), std::string::npos) << "expected out dx, eax\n" << s;
  EXPECT_EQ(s.find("#APP"), std::string::npos) << "intrinsic path must not use InlineAsm\n" << s;
}

// ---------------------------------------------------------------------------
// TLB flush: invlpg
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, InvlpgLowersCorrectly) {
  auto [rc, s] = compileToAsm(
      "void flush_page(void *addr) { __invlpg(addr); }\n", "invlpg");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("invlpg"), std::string::npos) << "expected invlpg\n" << s;
}

// ---------------------------------------------------------------------------
// CLI / STI
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, CliStiLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    void do_cli(void) { _disable(); }
    void do_sti(void) { _enable(); }
  )", "clisti");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("cli"), std::string::npos) << "expected cli\n" << s;
  EXPECT_NE(s.find("sti"), std::string::npos) << "expected sti\n" << s;
}

// ---------------------------------------------------------------------------
// Descriptor table ops: sidt/lidt/sgdt/lgdt
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, DescriptorTableOpsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    void do_sidt(void *p) { __sidt(p); }
    void do_lidt(void *p) { __lidt(p); }
    void do_sgdt(void *p) { _sgdt(p); }
    void do_lgdt(void *p) { _lgdt(p); }
  )", "desctable");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("sidt"), std::string::npos) << "expected sidt\n" << s;
  EXPECT_NE(s.find("lidt"), std::string::npos) << "expected lidt\n" << s;
  EXPECT_NE(s.find("sgdt"), std::string::npos) << "expected sgdt\n" << s;
  EXPECT_NE(s.find("lgdt"), std::string::npos) << "expected lgdt\n" << s;
}

// ---------------------------------------------------------------------------
// WBINVD
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, WbinvdLowersCorrectly) {
  auto [rc, s] = compileToAsm(
      "void do_wbinvd(void) { __wbinvd(); }\n", "wbinvd");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("wbinvd"), std::string::npos) << "expected wbinvd\n" << s;
}

// ---------------------------------------------------------------------------
// Segment limit (LSL)
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, SegmentLimitLowersCorrectly) {
  auto [rc, s] = compileToAsm(
      "unsigned int do_lsl(unsigned int sel) { return __segmentlimit(sel); }\n",
      "seglimit");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("lsl"), std::string::npos) << "expected lsl instruction\n" << s;
}

// ---------------------------------------------------------------------------
// VMX: vmxoff, vmlaunch, vmresume, vmptrst (simple ops)
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, VmxSimpleOpsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    void do_vmxoff(void) { __vmx_off(); }
    unsigned char do_vmlaunch(void) { return __vmx_vmlaunch(); }
    unsigned char do_vmresume(void) { return __vmx_vmresume(); }
    void do_vmptrst(void *p) { __vmx_vmptrst((__int64 *)p); }
  )", "vmx_simple");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("vmxoff"), std::string::npos) << "expected vmxoff\n" << s;
  EXPECT_NE(s.find("vmlaunch"), std::string::npos) << "expected vmlaunch\n" << s;
  EXPECT_NE(s.find("vmresume"), std::string::npos) << "expected vmresume\n" << s;
  EXPECT_NE(s.find("vmptrst"), std::string::npos) << "expected vmptrst\n" << s;
}

// ---------------------------------------------------------------------------
// VMX: vmwrite, vmread, vmclear, vmptrld, vmxon (parameterized ops)
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, VmxParameterizedOpsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    unsigned char do_vmwrite(unsigned __int64 f, unsigned __int64 v) { return __vmx_vmwrite(f, v); }
    unsigned char do_vmread(unsigned __int64 f, unsigned __int64 *v) { return __vmx_vmread(f, v); }
    unsigned char do_vmclear(unsigned __int64 *p) { return __vmx_vmclear(p); }
    unsigned char do_vmptrld(unsigned __int64 *p) { return __vmx_vmptrld(p); }
    unsigned char do_vmxon(unsigned __int64 *p) { return __vmx_on(p); }
  )", "vmx_param");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("vmwrite"), std::string::npos) << "expected vmwrite\n" << s;
  EXPECT_NE(s.find("vmread"), std::string::npos) << "expected vmread\n" << s;
  EXPECT_NE(s.find("vmclear"), std::string::npos) << "expected vmclear\n" << s;
  EXPECT_NE(s.find("vmptrld"), std::string::npos) << "expected vmptrld\n" << s;
  EXPECT_NE(s.find("vmxon"), std::string::npos) << "expected vmxon\n" << s;
}

// ---------------------------------------------------------------------------
// VMX status flag encoding: vmlaunch returns 0/1/2 via sete+setb
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, VmxStatusFlagEncoding) {
  auto [rc, s] = compileToAsm(R"(
    unsigned char do_vmlaunch(void) { return __vmx_vmlaunch(); }
  )", "vmx_flags");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("sete"), std::string::npos) << "expected sete for ZF\n" << s;
  EXPECT_NE(s.find("setb"), std::string::npos) << "expected setb for CF\n" << s;
}

// ---------------------------------------------------------------------------
// GS segment: read/write/inc/add for all sizes
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, GsSegmentOpsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    unsigned char     read_gs8 (unsigned long o) { return __readgsbyte(o); }
    unsigned short    read_gs16(unsigned long o) { return __readgsword(o); }
    unsigned long     read_gs32(unsigned long o) { return __readgsdword(o); }
    unsigned __int64  read_gs64(unsigned long o) { return __readgsqword(o); }
    void write_gs8 (unsigned long o, unsigned char     v) { __writegsbyte(o, v); }
    void write_gs16(unsigned long o, unsigned short    v) { __writegsword(o, v); }
    void write_gs32(unsigned long o, unsigned long     v) { __writegsdword(o, v); }
    void write_gs64(unsigned long o, unsigned __int64  v) { __writegsqword(o, v); }
    void inc_gs8 (unsigned long o) { __incgsbyte(o); }
    void inc_gs16(unsigned long o) { __incgsword(o); }
    void inc_gs32(unsigned long o) { __incgsdword(o); }
    void inc_gs64(unsigned long o) { __incgsqword(o); }
    void add_gs8 (unsigned long o, unsigned char     v) { __addgsbyte(o, v); }
    void add_gs16(unsigned long o, unsigned short    v) { __addgsword(o, v); }
    void add_gs32(unsigned long o, unsigned long     v) { __addgsdword(o, v); }
    void add_gs64(unsigned long o, unsigned __int64  v) { __addgsqword(o, v); }
  )", "gsops");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("gs:"), std::string::npos) << "expected gs: segment prefix\n" << s;
}

// ---------------------------------------------------------------------------
// FS segment: read for all sizes
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, FsSegmentReadsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    unsigned char     read_fs8 (unsigned long o) { return __readfsbyte(o); }
    unsigned short    read_fs16(unsigned long o) { return __readfsword(o); }
    unsigned long     read_fs32(unsigned long o) { return __readfsdword(o); }
    unsigned __int64  read_fs64(unsigned long o) { return __readfsqword(o); }
  )", "fsread");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("fs:"), std::string::npos) << "expected fs: segment prefix\n" << s;
}

// ---------------------------------------------------------------------------
// Rep string ops: movsb/w/d/q, stosb/w/d/q
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, RepStringOpsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    void do_movsb(void *d, const void *s, unsigned __int64 n) { __movsb((unsigned char*)d, (const unsigned char*)s, n); }
    void do_movsw(void *d, const void *s, unsigned __int64 n) { __movsw((unsigned short*)d, (const unsigned short*)s, n); }
    void do_movsd(void *d, const void *s, unsigned __int64 n) { __movsd((unsigned long*)d, (const unsigned long*)s, n); }
    void do_movsq(void *d, const void *s, unsigned __int64 n) { __movsq((unsigned __int64*)d, (const unsigned __int64*)s, n); }
    void do_stosb(unsigned char     *d, unsigned char     v, unsigned __int64 n) { __stosb(d, v, n); }
    void do_stosw(unsigned short    *d, unsigned short    v, unsigned __int64 n) { __stosw(d, v, n); }
    void do_stosd(unsigned long     *d, unsigned long     v, unsigned __int64 n) { __stosd(d, v, n); }
    void do_stosq(unsigned __int64  *d, unsigned __int64  v, unsigned __int64 n) { __stosq(d, v, n); }
  )", "repstring");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("movsb"), std::string::npos) << "expected movsb\n" << s;
  EXPECT_NE(s.find("movsw"), std::string::npos) << "expected movsw\n" << s;
  EXPECT_NE(s.find("movsd"), std::string::npos) << "expected movsd\n" << s;
  EXPECT_NE(s.find("movsq"), std::string::npos) << "expected movsq\n" << s;
  EXPECT_NE(s.find("stosb"), std::string::npos) << "expected stosb\n" << s;
  EXPECT_NE(s.find("stosw"), std::string::npos) << "expected stosw\n" << s;
  EXPECT_NE(s.find("stosd"), std::string::npos) << "expected stosd\n" << s;
  EXPECT_NE(s.find("stosq"), std::string::npos) << "expected stosq\n" << s;
}

// ---------------------------------------------------------------------------
// Port I/O string ops: inbytestring, outbytestring
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, PortIoStringOpsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    void do_inbytes(unsigned short p, unsigned char *b, unsigned long n) { __inbytestring(p, b, n); }
    void do_outbytes(unsigned short p, unsigned char *b, unsigned long n) { __outbytestring(p, b, n); }
  )", "portiostr");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("rep"), std::string::npos) << "expected rep prefix\n" << s;
  EXPECT_NE(s.find("insb"), std::string::npos) << "expected insb\n" << s;
  EXPECT_NE(s.find("outsb"), std::string::npos) << "expected outsb\n" << s;
}

// ---------------------------------------------------------------------------
// CPUID
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, CpuidLowersCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    void do_cpuid(int info[4], int func, int subfunc) { __cpuidex(info, func, subfunc); }
  )", "cpuid");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("cpuid"), std::string::npos) << "expected cpuid\n" << s;
}

// ---------------------------------------------------------------------------
// INT 2C
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, Int2cLowersCorrectly) {
  auto [rc, s] = compileToAsm(
      "void do_int2c(void) { __int2c(); }\n", "int2c");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("int\t44"), std::string::npos) << "expected int 44 (0x2c)\n" << s;
}

// ---------------------------------------------------------------------------
// EFLAGS read/write
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, EflagsReadWriteLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    unsigned __int64 read_flags(void) { return __readeflags(); }
    void write_flags(unsigned __int64 v) { __writeeflags(v); }
  )", "eflags");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("pushfq"), std::string::npos) << "expected pushfq\n" << s;
  EXPECT_NE(s.find("popfq"), std::string::npos) << "expected popfq\n" << s;
}

// ---------------------------------------------------------------------------
// INVPCID
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, InvpcidLowersCorrectly) {
  auto [rc, s] = compileToAsm(
      "void do_invpcid(unsigned int t, void *d) { _invpcid(t, d); }\n",
      "invpcid");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("invpcid"), std::string::npos) << "expected invpcid\n" << s;
}

// ---------------------------------------------------------------------------
// XSETBV
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, XsetbvLowersCorrectly) {
  auto [rc, s] = compileToAsm(
      "void do_xsetbv(unsigned int xcr, unsigned __int64 val) { _xsetbv(xcr, val); }\n",
      "xsetbv");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("xsetbv"), std::string::npos) << "expected xsetbv\n" << s;
}

// ---------------------------------------------------------------------------
// TSX: xbegin, xabort, xend, xtest
// ---------------------------------------------------------------------------
TEST_F(X86PrivilegedIntrinTest, TsxOpsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    void do_xbegin(void) { _xbegin(); }
    void do_xend(void)   { _xend(); }
    void do_xabort(void) { _xabort(0x42); }
    unsigned char do_xtest(void) { return _xtest(); }
  )", "tsx");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("xbegin"), std::string::npos) << "expected xbegin\n" << s;
  EXPECT_NE(s.find("xend"), std::string::npos) << "expected xend\n" << s;
  EXPECT_NE(s.find("xabort"), std::string::npos) << "expected xabort\n" << s;
  EXPECT_NE(s.find("xtest"), std::string::npos) << "expected xtest\n" << s;
}
