//===- FarTransferFrontendTests.cpp - Native far transfers
//-----------------===//
//
// NeverC Compiler
//
//===----------------------------------------------------------------------===//

#include "NativeBuiltinFixture.h"

#include <regex>

class X86FarTransferTest : public X86NativeBuiltinTest {};

TEST_F(X86FarTransferTest, CallsReturnAndJumpsAbandonTheCompilerFrame) {
  for (unsigned Bits : {16u, 32u, 64u}) {
    const std::string Width = std::to_string(Bits);
    const std::string Type = Bits == 16   ? "unsigned short"
                             : Bits == 32 ? "unsigned"
                                          : "unsigned long long";
    const auto Source = tmpFile("far.c");
    writeFile(Source,
              "#if !__has_builtin(__builtin_ia32_lcall" + Width +
                  ")\n"
                  "#error missing far call\n#endif\n"
                  "extern volatile unsigned observed;\n" +
                  Type +
                  " returning(const void *target, unsigned n) {\n"
                  "  volatile unsigned char local[n+32]; local[0] = 17;\n"
                  "  unsigned before = observed;\n  " +
                  Type + " value = __builtin_ia32_lcall" + Width +
                  "(target);\n"
                  "  observed = before + local[0]; return value;\n}\n"
                  "[[noreturn]] void terminal(const void *target, const void "
                  "*stack, unsigned n) {\n"
                  "  volatile unsigned char local[n+32]; local[0] = 17;\n"
                  "  __builtin_ia32_ljmp" +
                  Width + "(target, stack); observed = 23;\n}\n");
    for (const char *Target : {"x86_64-unknown-linux-gnu",
                               "x86_64-pc-windows-msvc", "x86_64-apple-darwin"})
      for (const char *Opt : {"-O0", "-O2"}) {
        SCOPED_TRACE(Width + " " + Target + " " + Opt);
        const auto Assembly = tmpFile("far.s"), IR = tmpFile("far.ll");
        std::vector<std::string> Args = {std::string("--target=") + Target,
                                         Opt,
                                         "-std=c23",
                                         "-ffreestanding",
                                         "-fno-lto",
                                         "-fno-builtin-mimalloc",
                                         "-fno-stack-protector",
                                         "-Werror",
                                         "-mllvm",
                                         "-verify-machineinstrs",
                                         "-masm=att",
                                         "-S",
                                         Source.string()};
        auto A = Args;
        A.insert(A.end(), {"-o", Assembly.string()});
        auto R = ncc(A);
        ASSERT_EQ(R.exitCode, 0) << R.err;
        const auto Text = readFile(Assembly);
        const std::string Suffix = Bits == 16 ? "w" : Bits == 32 ? "l" : "q";
        EXPECT_NE(Text.find("lcall" + Suffix), std::string::npos) << Text;
        EXPECT_TRUE(std::regex_search(
            Text, std::regex("movq[^\n]*, %rsp\n[ \t]*ljmp" + Suffix)))
            << Text;
        EXPECT_EQ(Text.find("#APP"), std::string::npos);
        EXPECT_EQ(Text.find("movl\t$23,"), std::string::npos);
        Args.insert(Args.end(), {"-emit-llvm", "-o", IR.string()});
        R = ncc(Args);
        ASSERT_EQ(R.exitCode, 0) << R.err;
        const auto IRText = readFile(IR);
        EXPECT_NE(IRText.find("call i" + Width + " @llvm.x86.lcall" + Width),
                  std::string::npos);
        EXPECT_NE(IRText.find("call void @llvm.x86.ljmp" + Width),
                  std::string::npos);
        EXPECT_NE(IRText.find("convergent noduplicate noreturn nounwind"),
                  std::string::npos);
        EXPECT_NE(IRText.find("convergent noduplicate nounwind"),
                  std::string::npos);
        EXPECT_EQ(IRText.find("asm sideeffect"), std::string::npos);
      }
  }
}

TEST_F(X86FarTransferTest,
       LegacyImmediateAndMemoryFormsKeepTheirNativeIdentity) {
  const auto Source = tmpFile("legacy.c"), IR = tmpFile("legacy.ll");
  writeFile(Source, R"(
unsigned short a(const void *p) { return __builtin_ia32_lcall16(p); }
unsigned b(const void *p) { return __builtin_ia32_lcall32(p); }
unsigned short c(void) { return __builtin_ia32_lcall16_imm(6144, 48); }
unsigned d(void) { return __builtin_ia32_lcall32_imm(6144, 48); }
void e(const void *p, void *sp) { __builtin_ia32_ljmp16(p, sp); }
void f(const void *p, void *sp) { __builtin_ia32_ljmp32(p, sp); }
void g(void *sp) { __builtin_ia32_ljmp16_imm(6144, 48, sp); }
void h(void *sp) { __builtin_ia32_ljmp32_imm(6144, 48, sp); }
)");
  for (const char *Opt : {"-O0", "-O2"}) {
    const auto R =
        ncc({"--target=i386-unknown-linux-gnu", Opt, "-ffreestanding",
             "-fno-lto", "-fno-builtin-mimalloc", "-Werror", "-S", "-emit-llvm",
             Source.string(), "-o", IR.string()});
    ASSERT_EQ(R.exitCode, 0) << R.err;
    const auto Text = readFile(IR);
    for (const char *Name : {"lcall16", "lcall32", "lcall16.imm", "lcall32.imm",
                             "ljmp16", "ljmp32", "ljmp16.imm", "ljmp32.imm"})
      EXPECT_NE(Text.find(std::string("@llvm.x86.") + Name + "("),
                std::string::npos);
    EXPECT_NE(Text.find("i16 immarg, i16 immarg"), std::string::npos);
    EXPECT_NE(Text.find("i32 immarg, i16 immarg"), std::string::npos);
  }
}

TEST_F(X86FarTransferTest, RejectsWrongModeTypesAndDynamicImmediateTargets) {
  for (const auto &Case : std::vector<std::pair<std::string, std::string>>{
           {"x86_64", "__builtin_ia32_lcall16_imm(1, 8)"},
           {"x86_64", "__builtin_ia32_ljmp32_imm(1, 8, p)"},
           {"i386", "__builtin_ia32_lcall64(p)"},
           {"i386", "__builtin_ia32_ljmp64(p, p)"},
           {"i386", "__builtin_ia32_lcall32_imm(value, 8)"},
           {"i386", "__builtin_ia32_ljmp16_imm(1, value, p)"},
           {"x86_64", "__builtin_ia32_lcall32()"},
           {"x86_64", "__builtin_ia32_lcall16(1)"},
           {"x86_64", "__builtin_ia32_ljmp64(p)"}}) {
    SCOPED_TRACE(Case.first + " " + Case.second);
    const auto Source = tmpFile("invalid.c");
    writeFile(Source,
              "void f(void *p, unsigned value) { " + Case.second + "; }\n");
    const auto R =
        ncc({"--target=" + Case.first + "-unknown-linux-gnu", "-std=c23",
             "-Werror", "-fsyntax-only", Source.string()});
    EXPECT_NE(R.exitCode, 0) << R.err;
  }
}
