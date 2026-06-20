// GoogleTest suite for CallingConv::NeverC_Custom (data-driven custom calling
// convention). Each test compiles a small program to x86-64 assembly via the
// CustomCallConvPlugin under a given "neverc-callconv" spec, then asserts the
// resulting register/stack placement. Pure -S (no execution), so it runs on any
// host -- the custom convention is currently an x86-64 backend feature.

#include "NeverCTestFixture.h"

namespace {

// External-linkage callees (kept by -O2) with noinline so caller3 keeps a real
// call site; this lets us check both caller-side setup and callee-side receipt.
const char *kCases = R"C(
#define NI __attribute__((noinline))
NI int  add3 (int a, int b, int c)            { return a + b + c; }
NI long add3l(long a, long b, long c)         { return a + b + c; }
NI float fmix(float x, float y)               { return x + y; }
NI int  spill5(int a,int b,int c,int d,int e) { return a+b+c+d+e; }
int caller3(void) { return add3(10, 20, 30); }
)C";

std::string detab(std::string s) {
  for (char &c : s)
    if (c == '\t')
      c = ' ';
  return s;
}

} // namespace

class CustomCallConvTest : public NeverCTest {
protected:
  fs::path plugin_;
  fs::path casesSrc_;

  void SetUp() override {
    NeverCTest::SetUp();

    // Build the plugin for the host so neverc can load it.
    fs::path pluginSrc =
        fs::path(NEVERC_PLUGINSDK_DIR) / "examples" / "CustomCallConvPlugin.c";
    plugin_ = tmpFile(pluginLeaf());
    CmdResult r = ncc({"-shared", "-I" + std::string(NEVERC_NEVERC_INCLUDE_DIR),
                       "-o", plugin_.string(), pluginSrc.string()});
    ASSERT_TRUE(r.ok()) << "plugin build failed:\n" << r.err;

    casesSrc_ = tmpFile("cases.c");
    writeFile(casesSrc_, kCases);
  }

  static std::string pluginLeaf() {
    if (isWindows())
      return "ccplugin.dll";
    if (isDarwin())
      return "ccplugin.dylib";
    return "ccplugin.so";
  }

  // Compile the cases to x86-64 assembly under `spec`; return the (de-tabbed)
  // assembly text. `extraArg` allows passing another -fplugin-pass-arg.
  std::string asmForTriple(const std::string &triple, const std::string &spec,
                           const std::string &extraArg = "") {
    fs::path out = tmpFile("out.s");
    std::vector<std::string> args = {"-fplugin-pass=" + plugin_.string(),
                                     "-fplugin-pass-arg=cc-all=1",
                                     "-fplugin-pass-arg=ccspec=" + spec};
    if (!extraArg.empty())
      args.push_back(extraArg);
    args.push_back("-S");
    args.push_back("-O2");
    args.push_back("--target=" + triple);
    args.push_back(casesSrc_.string());
    args.push_back("-o");
    args.push_back(out.string());

    CmdResult r = ncc(args);
    EXPECT_TRUE(r.ok()) << "compile failed for spec '" << spec << "' (target "
                        << triple << "):\n"
                        << r.err;
    return detab(readFile(out));
  }

  std::string asmFor(const std::string &spec, const std::string &extraArg = "") {
    return asmForTriple("x86_64-unknown-linux-gnu", spec, extraArg);
  }

  static bool has(const std::string &hay, const std::string &needle) {
    return hay.find(needle) != std::string::npos;
  }
};

// Pool mode, GPRs: 3rd integer arg lands in r8d (std SysV would use edx).
TEST_F(CustomCallConvTest, PoolGPR) {
  std::string s = asmFor("gpr:rcx,rdx,r8;ret:rax");
  EXPECT_TRUE(has(s, "$30, %r8d")); // caller passes 3rd arg in r8d
  EXPECT_TRUE(has(s, "$10, %ecx")); // 1st arg in ecx
}

// Pool mode, XMMs: float args use the listed XMMs (std would use xmm0/xmm1).
TEST_F(CustomCallConvTest, PoolXMM) {
  std::string s = asmFor("gpr:rcx;xmm:xmm5,xmm4;ret:rax;ret_xmm:xmm0");
  EXPECT_TRUE(has(s, "%xmm5"));
  EXPECT_TRUE(has(s, "%xmm4"));
}

// Positional mode: 2nd argument forced to the stack while 1st/3rd stay in regs.
TEST_F(CustomCallConvTest, PositionalForcesStack) {
  std::string s = asmFor("args:rcx,stack,r8;ret:rax");
  EXPECT_TRUE(has(s, "$20, (%rsp)")); // caller writes 2nd arg to the stack
  EXPECT_TRUE(has(s, "%r8d"));        // 3rd arg still in r8d
}

// Positional mode, all stack: caller pushes every argument.
TEST_F(CustomCallConvTest, PositionalAllStack) {
  std::string s = asmFor("args:stack,stack,stack");
  EXPECT_TRUE(has(s, "pushq $10"));
  EXPECT_TRUE(has(s, "pushq $20"));
  EXPECT_TRUE(has(s, "pushq $30"));
}

// Pool exhaustion: only one GPR listed; extra integer args spill to the stack.
TEST_F(CustomCallConvTest, PoolExhaustionSpills) {
  std::string s = asmFor("gpr:rcx;ret:rax");
  EXPECT_TRUE(has(s, "$10, %ecx")); // 1st arg in the only listed reg
  EXPECT_TRUE(has(s, "pushq $20")); // 2nd arg spilled
}

// i64 arguments must use 64-bit sub-registers (add3l adds via %r8, not %r8d).
TEST_F(CustomCallConvTest, I64UsesWideRegister) {
  std::string s = asmFor("gpr:rcx,rdx,r8;ret:rax");
  EXPECT_TRUE(has(s, "addq %r8,"));
}

// No function matches the plugin's prefix -> standard SysV convention is kept.
TEST_F(CustomCallConvTest, FallbackToStandard) {
  std::string s =
      asmFor("gpr:rcx,rdx,r8;ret:rax", "-fplugin-pass-arg=ccprefix=nomatch_");
  EXPECT_TRUE(has(s, "$10, %edi"));   // standard 1st integer arg register
  EXPECT_FALSE(has(s, "$30, %r8d"));  // custom layout must NOT appear
}

// Struct return (sret): the frontend lowers it to a hidden pointer argument
// plus a pointer return, both scalars -- so it works with no special handling.
// The hidden pointer takes the 1st GPR (rcx) and is returned in ret (rax).
TEST_F(CustomCallConvTest, StructReturnSret) {
  writeFile(casesSrc_,
            "struct P{long a,b,c;};\n"
            "__attribute__((noinline)) struct P mk(long x){"
            "struct P p={x,x+1,x+2};return p;}\n");
  std::string s = asmFor("gpr:rcx,rdx,r8;ret:rax");
  EXPECT_TRUE(has(s, "(%rcx)"));      // result written through the sret pointer
  EXPECT_TRUE(has(s, "%rcx, %rax"));  // sret pointer returned in rax
}

// Byval aggregate: passed as a pointer in the IR, so caller and callee both use
// the 1st GPR (rcx) for the struct address -- consistent and correct.
TEST_F(CustomCallConvTest, ByvalAggregate) {
  writeFile(casesSrc_,
            "struct Q{long a,b,c;};\n"
            "__attribute__((noinline)) long use(struct Q q){"
            "return q.a+q.b+q.c;}\n"
            "long mk2(void){struct Q q={1,2,3};return use(q);}\n");
  std::string s = asmFor("gpr:rcx,rdx,r8;ret:rax");
  EXPECT_TRUE(has(s, "(%rcx)"));      // callee reads the struct via the ptr
  EXPECT_TRUE(has(s, "%rsp, %rcx"));  // caller passes the struct address in rcx
}

//===----------------------------------------------------------------------===//
// AArch64 -- same plugin API / spec format; x0-x28 GPRs, v0-v31 SIMD, stack.
//===----------------------------------------------------------------------===//

static constexpr const char *kA64 = "aarch64-unknown-linux-gnu";

// Pool GPRs: caller passes args in the listed registers (std AAPCS: w0,w1,w2).
TEST_F(CustomCallConvTest, AArch64PoolGPR) {
  std::string s = asmForTriple(kA64, "gpr:x9,x10,x11;ret:x0");
  EXPECT_TRUE(has(s, "mov w9, #10"));  // 1st arg -> w9
  EXPECT_TRUE(has(s, "mov w11, #30")); // 3rd arg -> w11
}

// FP/SIMD pool: float args use the listed v-registers (std AAPCS: s0,s1).
TEST_F(CustomCallConvTest, AArch64FPR) {
  std::string s = asmForTriple(kA64, "gpr:x9;fpr:v5,v6;ret:x0;ret_fpr:v0");
  EXPECT_TRUE(has(s, "s5")); // fmix x -> s5
  EXPECT_TRUE(has(s, "s6")); // fmix y -> s6
}

// Positional + stack: 2nd arg forced to the stack; 1st/3rd stay in regs.
TEST_F(CustomCallConvTest, AArch64PositionalStack) {
  std::string s = asmForTriple(kA64, "args:x9,stack,x11;ret:x0");
  EXPECT_TRUE(has(s, "mov w9, #10")); // 1st arg -> w9
  EXPECT_TRUE(has(s, "[sp]"));        // 2nd arg on the stack (no offset)
}

// csr on AArch64: work preserves only x28 (csr:x28) and clobbers the other
// callee-saved regs, so the caller (standard CC) must keep a value live across
// the call in x28 -- not x19, which work would clobber. This also exercises the
// mixed case (standard caller calling a custom callee), which on AArch64 is
// handled via the SelectionDAG fallback that the custom convention requires.
TEST_F(CustomCallConvTest, AArch64CalleeSavedCsr) {
  writeFile(casesSrc_,
            "__attribute__((noinline)) int work(int a){return a*a;}\n"
            "int caller(int x,int y){int w=work(x);return w+y;}\n");
  std::string s = asmForTriple(kA64, "gpr:x9;ret:x0;csr:x28",
                               "-fplugin-pass-arg=ccprefix=work");
  EXPECT_TRUE(has(s, "w9, w0"));   // x passed to work in the custom arg reg w9
  EXPECT_TRUE(has(s, "w28, w1"));  // y kept in x28 (work's only preserved reg)
  EXPECT_TRUE(has(s, "w0, w28"));  // and read back from x28 after the call
}

// Two functions with *different* custom specs where one calls the other. The
// caller must pass the argument using the *callee's* spec (w11), not its own
// (w12/w13). A uniform spec previously masked this on AArch64; this locks the
// fix (caller-side spec injection via the SelectionDAG fallback).
TEST_F(CustomCallConvTest, AArch64MixedSpecCrossCall) {
  writeFile(
      casesSrc_,
      "__attribute__((custom_attr(\"neverc-callconv\",\"gpr:x11;ret:x0\")))"
      " __attribute__((noinline)) int callee(int a){return a+1;}\n"
      "__attribute__((custom_attr(\"neverc-callconv\",\"gpr:x12,x13;ret:x0\")))"
      " int caller(int x,int y){return callee(x)+y;}\n");
  fs::path out = tmpFile("mix.s");
  CmdResult r = ncc({"-fplugin-pass=" + plugin_.string(), "-S", "-O2",
                     "--target=aarch64-unknown-linux-gnu", casesSrc_.string(),
                     "-o", out.string()});
  ASSERT_TRUE(r.ok()) << r.err;
  std::string s = detab(readFile(out));
  EXPECT_TRUE(has(s, "w11, w12")); // x (caller's w12) moved to callee's arg w11
}

//===----------------------------------------------------------------------===//
// Frontend: custom_attr(...) source attribute -> function string attribute.
//===----------------------------------------------------------------------===//

// GNU __attribute__((custom_attr("k","v"))) lowers to a clean function
// attribute: no warning, no global annotation.
TEST_F(CustomCallConvTest, CustomAttrLowersToFunctionAttribute) {
  fs::path src = tmpFile("ca.c");
  writeFile(src, "__attribute__((custom_attr(\"neverc-callconv\","
                 "\"gpr:rcx;ret:rax\"))) int f(int a){return a;}\n");
  fs::path out = tmpFile("ca.ll");
  CmdResult r = ncc({"-emit-llvm", "-S", "--target=x86_64-unknown-linux-gnu",
                     src.string(), "-o", out.string()});
  ASSERT_TRUE(r.ok()) << r.err;
  EXPECT_FALSE(r.stderrContains("unknown attribute")); // no warning
  std::string ir = readFile(out);
  EXPECT_NE(ir.find("\"neverc-callconv\"=\"gpr:rcx;ret:rax\""),
            std::string::npos);
  EXPECT_EQ(ir.find("llvm.global.annotations"), std::string::npos);
}

// Microsoft __declspec(custom_attr(...)) works too.
TEST_F(CustomCallConvTest, CustomAttrDeclspec) {
  fs::path src = tmpFile("ms.c");
  writeFile(src, "__declspec(custom_attr(\"neverc-callconv\",\"gpr:rcx;ret:rax\"))"
                 " int f(int a){return a;}\n");
  fs::path out = tmpFile("ms.ll");
  CmdResult r = ncc({"-emit-llvm", "-S", "--target=x86_64-pc-windows-msvc",
                     src.string(), "-o", out.string()});
  ASSERT_TRUE(r.ok()) << r.err;
  EXPECT_FALSE(r.stderrContains("unknown attribute"));
  EXPECT_NE(readFile(out).find("\"neverc-callconv\"=\"gpr:rcx;ret:rax\""),
            std::string::npos);
}

// Full pipeline: source custom_attr -> plugin reads the attr -> custom CC asm.
// No cc-all=1, so the plugin only processes functions with custom_attr.
TEST_F(CustomCallConvTest, CustomAttrEndToEnd) {
  fs::path src = tmpFile("e2e.c");
  writeFile(src,
            "__attribute__((custom_attr(\"neverc-callconv\","
            "\"gpr:rcx,rdx,r8;ret:rax\"))) __attribute__((noinline)) "
            "int add3(int a,int b,int c){return a+b+c;}\n"
            "int caller(void){return add3(10,20,30);}\n");
  fs::path out = tmpFile("e2e.s");
  CmdResult r = ncc({"-fplugin-pass=" + plugin_.string(), "-S", "-O2",
                     "--target=x86_64-unknown-linux-gnu", src.string(), "-o",
                     out.string()});
  ASSERT_TRUE(r.ok()) << r.err;
  std::string s = detab(readFile(out));
  EXPECT_TRUE(has(s, "$10, %ecx"));  // caller passes args via the custom regs
  EXPECT_TRUE(has(s, "$30, %r8d"));
}

//===----------------------------------------------------------------------===//
// Hardening: configurable callee-saved (csr), varargs rejection, indirect calls.
//===----------------------------------------------------------------------===//

// csr: a custom callee-saved set. The callee preserves only r12 (csr:r12) and
// clobbers the other callee-saved regs, so a value live across the call must
// land in r12 in the caller (not rbx, which the callee would clobber).
TEST_F(CustomCallConvTest, CalleeSavedCsr) {
  writeFile(casesSrc_,
            "__attribute__((noinline)) int work(int a){return a*a;}\n"
            "int caller(int x,int y){int w=work(x);return w+y;}\n");
  std::string s =
      asmFor("gpr:rcx;ret:rax;csr:r12", "-fplugin-pass-arg=ccprefix=work");
  EXPECT_TRUE(has(s, "%esi, %r12d")); // y kept in r12 across the call to work
  EXPECT_TRUE(has(s, "%r12d, %eax")); // and read back from r12 afterwards
}

// vararg has no defined custom-CC ABI: compiling a variadic function under the
// custom convention must fail with a clear diagnostic instead of mis-passing
// the variadic part silently.
TEST_F(CustomCallConvTest, VarArgRejected) {
  writeFile(casesSrc_,
            "#include <stdarg.h>\n"
            "int sum(int n,...){va_list ap;va_start(ap,n);int s=0;"
            "for(int i=0;i<n;i++)s+=va_arg(ap,int);va_end(ap);return s;}\n");
  fs::path out = tmpFile("va.s");
  CmdResult r = ncc({"-fplugin-pass=" + plugin_.string(),
                     "-fplugin-pass-arg=cc-all=1",
                     "-fplugin-pass-arg=ccspec=gpr:rcx,rdx;ret:rax",
                     "-fplugin-pass-arg=ccprefix=sum", "-S", "-O2",
                     "--target=x86_64-unknown-linux-gnu", casesSrc_.string(),
                     "-o", out.string()});
  EXPECT_FALSE(r.ok()); // compilation must fail
  EXPECT_TRUE(r.stderrContains("does not support variadic"));
}

// Same rejection on the AArch64 backend: the custom convention has no vararg
// ABI on either target, so a variadic function must fail with the same clear
// diagnostic (a clean error, not a fatal abort).
TEST_F(CustomCallConvTest, VarArgRejectedAArch64) {
  writeFile(casesSrc_,
            "#include <stdarg.h>\n"
            "int sum(int n,...){va_list ap;va_start(ap,n);int s=0;"
            "for(int i=0;i<n;i++)s+=va_arg(ap,int);va_end(ap);return s;}\n");
  fs::path out = tmpFile("va_a64.s");
  CmdResult r = ncc({"-fplugin-pass=" + plugin_.string(),
                     "-fplugin-pass-arg=cc-all=1",
                     "-fplugin-pass-arg=ccspec=gpr:x9,x10;ret:x0",
                     "-fplugin-pass-arg=ccprefix=sum", "-S", "-O2",
                     "--target=aarch64-unknown-linux-gnu", casesSrc_.string(),
                     "-o", out.string()});
  EXPECT_FALSE(r.ok()); // compilation must fail
  EXPECT_TRUE(r.stderrContains("does not support variadic"));
}

// An address-taken custom-CC function can't carry its convention to indirect
// call sites; the plugin warns and compilation still succeeds (indirect calls
// fall back to the standard CC, no crash).
TEST_F(CustomCallConvTest, IndirectAddressTakenWarns) {
  writeFile(casesSrc_,
            "__attribute__((noinline)) int target(int a){return a+1;}\n"
            "typedef int(*fn_t)(int);\n"
            "fn_t get(void){return target;}\n"
            "int call_indirect(fn_t fp,int x){return fp(x);}\n");
  fs::path out = tmpFile("ind.s");
  CmdResult r = ncc({"-fplugin-pass=" + plugin_.string(),
                     "-fplugin-pass-arg=cc-all=1",
                     "-fplugin-pass-arg=ccspec=gpr:rcx;ret:rax",
                     "-fplugin-pass-arg=ccprefix=target", "-S", "-O2",
                     "--target=x86_64-unknown-linux-gnu", casesSrc_.string(),
                     "-o", out.string()});
  EXPECT_TRUE(r.ok()) << r.err;                      // must not crash
  EXPECT_TRUE(r.stderrContains("address is taken")); // warned
}

// The stack pointer must never be assignable as an argument register: a spec
// naming rsp skips it (the arg goes to the next valid register) instead of
// clobbering rsp.
TEST_F(CustomCallConvTest, StackPointerRejectedAsArgReg) {
  writeFile(casesSrc_, "int f(int a){return a;}\n");
  std::string s = asmFor("gpr:rsp,rcx;ret:rax");
  EXPECT_TRUE(has(s, "%ecx, %eax")); // arg uses rcx (rsp was skipped)
  EXPECT_FALSE(has(s, "%esp"));      // nothing is passed in the stack pointer
}

// A register listed as both a return register and callee-saved (csr) is an ABI
// mistake (the callee would restore it, overwriting the return value); the
// bridge warns but compilation still succeeds.
TEST_F(CustomCallConvTest, CsrConflictWarns) {
  writeFile(casesSrc_,
            "__attribute__((noinline)) int work(int a){return a*a;}\n"
            "int caller(int x){return work(x);}\n");
  fs::path out = tmpFile("conf.s");
  CmdResult r = ncc({"-fplugin-pass=" + plugin_.string(),
                     "-fplugin-pass-arg=cc-all=1",
                     "-fplugin-pass-arg=ccspec=gpr:rcx;ret:rax;csr:rax",
                     "-fplugin-pass-arg=ccprefix=work", "-S", "-O2",
                     "--target=x86_64-unknown-linux-gnu", casesSrc_.string(),
                     "-o", out.string()});
  EXPECT_TRUE(r.ok()) << r.err;                       // still compiles
  EXPECT_TRUE(r.stderrContains("both callee-saved")); // warned
}
