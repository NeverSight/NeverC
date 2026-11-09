//===- RepFrontendTests.cpp - Native legacy REP source interfaces --------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//
/// \file
/// Check code-mode signatures, explicit controls and complete state outputs.
//===----------------------------------------------------------------------===//

#include "NativeBuiltinFixture.h"

#include <iterator>
#include <regex>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

class X86RepFrontendTest : public X86NativeBuiltinTest {};

TEST_F(X86RepFrontendTest, LegacyPointerCountsMatchNativeAndMicrosoftCalls) {
  const auto Source = tmpFile("rep-pointer32.c");
  const auto IR = tmpFile("rep-pointer32.ll");
  writeFile(Source, R"C(
#include <intrin.h>
void probe(void *d, const void *s, unsigned n, unsigned v) {
  __movsb(d,s,n); __movsw(d,s,n); __movsd(d,s,n);
  __stosb(d,v,n); __stosw(d,v,n); __stosd(d,v,n);
  __builtin_ia32_movsb32(d,s,n);
  __builtin_ia32_movsw32(d,s,n);
  __builtin_ia32_movsd_rep32(d,s,n);
  __builtin_ia32_stosb32(d,v,n);
  __builtin_ia32_stosw32(d,v,n);
  __builtin_ia32_stosd32(d,v,n);
}
)C");
  for (const char *Opt : {"-O0", "-O2"}) {
    const auto R = ncc({"--target=i386-unknown-linux-gnu", "-fms-extensions",
                        "-D_MSC_VER=1930", Opt, "-Werror", "-ffreestanding",
                        "-fno-lto", "-fno-builtin-mimalloc", "-S", "-emit-llvm",
                        Source.string(), "-o", IR.string()});
    ASSERT_EQ(R.exitCode, 0) << R.err;
    const auto Text = readFile(IR);
    for (const char *Name :
         {"movsb", "movsw", "movsd.rep", "stosb", "stosw", "stosd"}) {
      const std::string API = std::string("@llvm.x86.") + Name + "32";
      const std::regex Call("call void " + API + "\\([^\\n]*i32 [^\\n]*\\)");
      EXPECT_EQ(
          std::distance(std::sregex_iterator(Text.begin(), Text.end(), Call),
                        std::sregex_iterator()),
          2)
          << Text;
      EXPECT_EQ(Text.find(std::string("@llvm.x86.") + Name + "("),
                std::string::npos)
          << Text;
    }
    EXPECT_EQ(Text.find(" asm "), std::string::npos) << Text;
  }
}

TEST_F(X86RepFrontendTest, LegacyStatePreservesWordAndFlagOutputs) {
  const auto Source = tmpFile("rep-state32.c");
  const auto IR = tmpFile("rep-state32.ll");
  writeFile(Source, R"C(
#if !defined(__NEVERC_X86_REP_STATE32__) || __NEVERC_X86_REP_STATE32__ != 1
#error missing legacy REP capability
#endif
void probe(unsigned a, unsigned b, unsigned c, unsigned char df,
           unsigned short flags, unsigned *out, unsigned short *cc) {
  __builtin_ia32_rep_movs_state32(a,b,c,df,1,16,0x64,out);
  __builtin_ia32_rep_stos_state32(a,b,c,df,4,32,out);
  __builtin_ia32_rep_lods_state32(a,b,c,df,2,16,0x65,out);
  __builtin_ia32_rep_cmps_state32(a,b,c,df,flags,4,32,0,0,out,cc);
  __builtin_ia32_rep_scas_state32(a,b,c,df,flags,2,16,1,out,cc);
  __builtin_ia32_rep_ins_state32(a,b,c,df,1,32,out);
  __builtin_ia32_rep_outs_state32(a,b,c,df,4,16,0x36,out);
}
)C");
  for (const char *Opt : {"-O0", "-O2"}) {
    const auto R =
        ncc({"--target=i386-unknown-linux-gnu", "-mno-sahf", Opt, "-Werror",
             "-ffreestanding", "-fno-lto", "-fno-builtin-mimalloc", "-S",
             "-emit-llvm", Source.string(), "-o", IR.string()});
    ASSERT_EQ(R.exitCode, 0) << R.err;
    const auto Text = readFile(IR);
    for (const char *Name :
         {"movs", "stos", "lods", "cmps", "scas", "ins", "outs"})
      EXPECT_NE(Text.find(std::string("@llvm.x86.rep.") + Name + ".state32("),
                std::string::npos)
          << Text;
    EXPECT_NE(Text.find("store i16"), std::string::npos) << Text;
    EXPECT_EQ(Text.find("store i64"), std::string::npos) << Text;
    EXPECT_EQ(Text.find("getelementptr i64"), std::string::npos) << Text;
    for (const char *Invalid : {" asm ", "memory(none)", "unreachable"})
      EXPECT_EQ(Text.find(Invalid), std::string::npos) << Text;
  }
}

TEST_F(X86RepFrontendTest, LegacyStateRejectsInvalidControlsAndCodeModes) {
  const auto Source = tmpFile("rep-invalid.c");
  for (const auto &[Triple, Call, Diagnostic] :
       std::vector<std::tuple<const char *, const char *, const char *>>{
           {"i386-unknown-linux-gnu", "__movsq(d,s,n)", "64-bit x86"},
           {"i386-unknown-linux-gnu", "__stosq(d,n,n)", "64-bit x86"},
           {"x86_64-unknown-linux-gnu", "__builtin_ia32_movsb32(d,s,n)",
            "32-bit x86"},
           {"x86_64-unknown-linux-gnu",
            "__builtin_ia32_rep_movs_state32(a,b,c,df,1,32,0,out)",
            "32-bit x86"},
           {"i386-unknown-linux-gnu",
            "__builtin_ia32_rep_movs_state32(a,b,c,df,8,32,0,out)", "REP"},
           {"i386-unknown-linux-gnu",
            "__builtin_ia32_rep_lods_state32(a,b,c,df,1,64,0,out)", "REP"},
           {"i386-unknown-linux-gnu",
            "__builtin_ia32_rep_cmps_state32(a,b,c,df,0,1,32,0,2,out,cc)",
            "REP"},
           {"i386-unknown-linux-gnu",
            "__builtin_ia32_rep_outs_state32(a,b,c,df,1,16,1,out)", "REP"},
           {"i386-unknown-linux-gnu",
            "__builtin_ia32_rep_stos_state32(a,b,c,df,n,32,out)",
            "constant integer"}}) {
    SCOPED_TRACE(Call);
    writeFile(
        Source,
        "void probe(void *d,const void *s,unsigned n,unsigned a,unsigned "
        "b,unsigned c,unsigned char df,unsigned *out,unsigned short *cc) { " +
            std::string(Call) + "; }\n");
    const auto R = ncc({std::string("--target=") + Triple, "-fms-extensions",
                        "-fsyntax-only", Source.string()});
    EXPECT_NE(R.exitCode, 0) << R.err;
    EXPECT_NE(R.err.find(Diagnostic), std::string::npos) << R.err;
  }
}
