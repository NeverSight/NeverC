//===- MpxFrontendTests.cpp - Native legacy bounds interfaces ------------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//
/// \file
/// Check code-mode widths and shared MPX operand admission at the C boundary.
//===----------------------------------------------------------------------===//

#include "NativeBuiltinFixture.h"

#include <string>
#include <tuple>
#include <vector>

class X86MpxFrontendTest : public X86NativeBuiltinTest {};

TEST_F(X86MpxFrontendTest, LegacyStateUsesNativeWordInputsAndOrderedEffects) {
  const auto Source = tmpFile("mpx32.c");
  const auto IR = tmpFile("mpx32.ll");
  writeFile(Source, R"C(
void probe(unsigned a, unsigned b) {
  __builtin_ia32_bnd_branch(0);
  __builtin_ia32_bndmov_reg(2,3);
  __builtin_ia32_bndcl_reg32(2,a);
  __builtin_ia32_bndcu_reg32(2,a);
  __builtin_ia32_bndcn_reg32(2,a);
  __builtin_ia32_bndmov_load32(2,a,32,0x64);
  __builtin_ia32_bndmov_store32(2,a,32,0x65);
  __builtin_ia32_bndmov_store_lock32(2,a,32,0x36);
  __builtin_ia32_bndcl_address32(2,a,32,0);
  __builtin_ia32_bndcu_address32(2,a,32,0);
  __builtin_ia32_bndcn_address32(2,a,32,0);
  __builtin_ia32_bndmk32(2,a,b,32,0);
  __builtin_ia32_bndldx32(2,a,b,32,0);
  __builtin_ia32_bndstx32(2,a,b,32,0);
  __builtin_ia32_bnd_branch(1);
}
)C");
  for (const char *Opt : {"-O0", "-O2"}) {
    const auto R =
        ncc({"--target=i386-unknown-linux-gnu", Opt, "-Werror",
             "-ffreestanding", "-fno-lto", "-fno-builtin-mimalloc", "-S",
             "-emit-llvm", Source.string(), "-o", IR.string()});
    ASSERT_EQ(R.exitCode, 0) << R.err;
    const auto Text = readFile(IR);
    for (const char *Name :
         {"bndcl.reg", "bndcu.reg", "bndcn.reg", "bndmov.load", "bndmov.store",
          "bndmov.store.lock", "bndcl.address", "bndcu.address",
          "bndcn.address", "bndmk", "bndldx", "bndstx"}) {
      EXPECT_NE(Text.find(std::string("call void @llvm.x86.") + Name +
                          "32(i32 2, i32 "),
                std::string::npos)
          << Text;
      EXPECT_EQ(Text.find(std::string("@llvm.x86.") + Name + "("),
                std::string::npos)
          << Text;
    }
    EXPECT_NE(Text.find("@llvm.x86.bndmov.reg(i32 2, i32 3)"),
              std::string::npos);
    for (const char *Kind : {"0", "1"})
      EXPECT_NE(
          Text.find(std::string("@llvm.x86.bnd.branch(i32 ") + Kind + ")"),
          std::string::npos);
    for (const char *Invalid : {" asm ", "memory(none)", "unreachable"})
      EXPECT_EQ(Text.find(Invalid), std::string::npos) << Text;
  }
}

TEST_F(X86MpxFrontendTest, LegacyControlsRejectUnsupportedModesAndAddresses) {
  const auto Source = tmpFile("mpx-invalid.c");
  for (const auto &[Triple, Call] :
       std::vector<std::tuple<const char *, const char *>>{
           {"i386-unknown-linux-gnu", "__builtin_ia32_bnd_branch(2)"},
           {"i386-unknown-linux-gnu", "__builtin_ia32_bndmov_reg(4,0)"},
           {"i386-unknown-linux-gnu", "__builtin_ia32_bndmov_reg(0,4)"},
           {"i386-unknown-linux-gnu", "__builtin_ia32_bndcl_reg32(4,a)"},
           {"i386-unknown-linux-gnu", "__builtin_ia32_bndcl_reg32(a,a)"},
           {"i386-unknown-linux-gnu", "__builtin_ia32_bndmov_load32(0,a,16,0)"},
           {"i386-unknown-linux-gnu", "__builtin_ia32_bndmov_load32(0,a,64,0)"},
           {"i386-unknown-linux-gnu", "__builtin_ia32_bndmov_load32(0,a,32,1)"},
           {"i386-unknown-linux-gnu",
            "__builtin_ia32_bndmov_load32(0,a,32,256)"},
           {"i386-unknown-linux-gnu", "__builtin_ia32_bndmk32(0,a,b,32,a)"},
           {"i386-unknown-linux-gnu", "__builtin_ia32_bndstx32(0,a,b,32,256)"},
           {"i386-unknown-linux-gnu", "__builtin_ia32_bndcl_reg(0,a)"},
           {"x86_64-unknown-linux-gnu", "__builtin_ia32_bndcl_reg32(0,a)"},
           {"x86_64-unknown-linux-gnu",
            "__builtin_ia32_bndmov_load32(0,a,32,0)"}}) {
    SCOPED_TRACE(Call);
    writeFile(Source, "void probe(unsigned a,unsigned b){" + std::string(Call) +
                          ";}\n");
    const auto R = ncc(
        {std::string("--target=") + Triple, "-fsyntax-only", Source.string()});
    EXPECT_NE(R.exitCode, 0) << R.err;
  }
}
