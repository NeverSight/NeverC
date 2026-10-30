#include "NativeBuiltinFixture.h"

#include <algorithm>
#include <regex>

class X86FrameReturnTest : public X86NativeBuiltinTest {};

TEST_F(X86FrameReturnTest, NativeTransfersAbandonFramesAcrossFormats) {
  struct Operation {
    const char *Name;
    const char *Mnemonic;
    const char *Arguments;
  };
  for (const auto &Op :
       {Operation{"iret16", "iretw", "frame"},
        Operation{"iret32", "iretl", "frame"},
        Operation{"iret64", "iretq", "frame"},
        Operation{"retf16", "lretw", "frame"},
        Operation{"retf32", "lretl", "frame"},
        Operation{"retf64", "lretq", "frame"},
        Operation{"retf16_imm", "lretw", "frame, 65535"},
        Operation{"retf32_imm", "lretl", "frame, 0"},
        Operation{"retf64_imm", "lretq", "frame, 32"},
        Operation{"erets", "erets", "frame"},
        Operation{"eretu", "eretu", "frame"},
        Operation{"uiret", "uiret", "frame"},
        Operation{"sysenter", "sysenter", ""}, Operation{"rsm", "rsm", ""}}) {
    const auto Source = tmpFile(std::string(Op.Name) + ".c");
    writeFile(
        Source,
        "#if !__has_builtin(__builtin_ia32_" + std::string(Op.Name) +
            ")\n#error missing transfer builtin\n#endif\n"
            "extern volatile unsigned observed;\n"
            "[[noreturn]] void transfer(const void *frame, unsigned size) {\n"
            "  volatile unsigned char local[size + 32];\n"
            "  local[0] = 17; observed = local[0];\n"
            "  __builtin_ia32_" +
            Op.Name + "(" + Op.Arguments + ");\n  observed = 2;\n}\n");
    for (const char *Target :
         {"--target=x86_64-unknown-linux-gnu",
          "--target=x86_64-pc-windows-msvc", "--target=x86_64-apple-darwin"})
      for (const char *Opt : {"-O0", "-O2"}) {
        SCOPED_TRACE(std::string(Op.Name) + " " + Target + " " + Opt);
        const auto AssemblyPath = tmpFile("frame-return.s");
        const auto Result =
            ncc({Target, Opt, "-std=c23", "-fno-lto", "-fno-builtin-mimalloc",
                 "-ffreestanding", "-Werror", "-fno-stack-protector",
                 "-fno-omit-frame-pointer", "-muintr", "-mllvm",
                 "-verify-machineinstrs", "-masm=att", "-S", Source.string(),
                 "-o", AssemblyPath.string()});
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Assembly = readFile(AssemblyPath);
        EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
        EXPECT_EQ(Assembly.find("\tret"), std::string::npos) << Assembly;
        EXPECT_NE(Assembly.find("\t" + std::string(Op.Mnemonic)),
                  std::string::npos)
            << Assembly;
        EXPECT_EQ(Assembly.find("movl\t$2,"), std::string::npos) << Assembly;
        if (*Op.Arguments)
          EXPECT_TRUE(std::regex_search(Assembly,
                                        std::regex("movq[^\n]*, %rsp\n[ \t]*" +
                                                   std::string(Op.Mnemonic))))
              << Assembly;
        const auto IRPath = tmpFile("frame-return.ll");
        const auto IRResult =
            ncc({Target, Opt, "-std=c23", "-fno-lto", "-fno-builtin-mimalloc",
                 "-ffreestanding", "-Werror", "-muintr", "-S", "-emit-llvm",
                 Source.string(), "-o", IRPath.string()});
        ASSERT_EQ(IRResult.exitCode, 0) << IRResult.err;
        auto Intrinsic = std::string(Op.Name);
        std::replace(Intrinsic.begin(), Intrinsic.end(), '_', '.');
        const auto IR = readFile(IRPath);
        EXPECT_NE(IR.find("call void @llvm.x86." + Intrinsic + "("),
                  std::string::npos)
            << IR;
        EXPECT_NE(IR.find("convergent noduplicate noreturn nounwind"),
                  std::string::npos)
            << IR;
        EXPECT_NE(IR.find("unreachable"), std::string::npos) << IR;
        EXPECT_EQ(IR.find("asm sideeffect"), std::string::npos) << IR;
        if (Intrinsic.find(".imm") != std::string::npos)
          EXPECT_NE(IR.find("i16 immarg"), std::string::npos) << IR;
      }
  }
}

TEST_F(X86FrameReturnTest,
       InvalidFrameSignaturesAndDynamicAdjustmentAreRejected) {
  for (const char *Call :
       {"__builtin_ia32_iret64()", "__builtin_ia32_iret32(1)",
        "__builtin_ia32_retf16((void *)0, 1)",
        "__builtin_ia32_retf64_imm((void *)0)",
        "__builtin_ia32_retf32_imm((void *)0, amount)",
        "__builtin_ia32_eretu(1)", "__builtin_ia32_sysenter(1)",
        "__builtin_ia32_rsm((void *)0)"}) {
    SCOPED_TRACE(Call);
    const auto Source = tmpFile("invalid-frame-return.c");
    writeFile(Source,
              "void f(unsigned short amount) { " + std::string(Call) + "; }\n");
    const auto Result =
        ncc({"--target=x86_64-unknown-linux-gnu", "-std=c23", "-ffreestanding",
             "-Werror", "-fsyntax-only", Source.string()});
    EXPECT_NE(Result.exitCode, 0) << Result.err;
  }
}

TEST_F(X86FrameReturnTest, UserInterruptReturnRequiresItsFeature) {
  const auto Source = tmpFile("uiret-feature.c");
  writeFile(Source, "void f(void *p) { __builtin_ia32_uiret(p); }\n");
  const auto Result =
      ncc({"--target=x86_64-unknown-linux-gnu", "-std=c23", "-ffreestanding",
           "-mno-uintr", "-S", Source.string(), "-o",
           tmpFile("invalid-uiret.s").string()});
  EXPECT_NE(Result.exitCode, 0) << Result.err;
  EXPECT_NE(Result.err.find("uintr"), std::string::npos) << Result.err;
}
