#include "NativeBuiltinFixture.h"

#include <regex>

class X86SystemReturnTest : public X86NativeBuiltinTest {};

TEST_F(X86SystemReturnTest, ReturnsAbandonTheCompilerFrameAcrossFormats) {
  for (const char *Name : {"sysret32", "sysret64", "sysexit32", "sysexit64"}) {
    const std::string Operation(Name);
    const bool Sysret = Operation.find("sysret") == 0;
    const bool Wide = Operation.find("64") != std::string::npos;
    const std::string Type = Wide ? "unsigned long long" : "unsigned";
    const auto Source = tmpFile(Operation + ".c");
    writeFile(Source, "#if !__has_builtin(__builtin_ia32_" + Operation +
                          ")\n"
                          "#error missing native return builtin\n#endif\n"
                          "extern volatile unsigned observed;\n"
                          "[[noreturn]] void transfer(" +
                          Type + " pc, " + Type +
                          " flags, unsigned long long sp, unsigned size) {\n"
                          "  volatile unsigned char frame[size + 32];\n"
                          "  frame[0] = 17; observed = frame[0];\n"
                          "  __builtin_ia32_" +
                          Operation + "(pc, " + (Sysret ? "flags, sp" : "sp") +
                          ");\n  observed = 2;\n}\n");
    for (const char *Target :
         {"--target=x86_64-unknown-linux-gnu",
          "--target=x86_64-pc-windows-msvc", "--target=x86_64-apple-darwin"})
      for (const char *Opt : {"-O0", "-O2"}) {
        SCOPED_TRACE(Operation + " " + Target + " " + Opt);
        const auto AssemblyFile = tmpFile(Operation + ".s");
        const auto Result = ncc(
            {Target, Opt, "-std=c23", "-fno-lto", "-fno-builtin-mimalloc",
             "-ffreestanding", "-Werror", "-fno-stack-protector",
             "-fno-omit-frame-pointer", "-mllvm", "-verify-machineinstrs",
             "-masm=att", "-S", Source.string(), "-o", AssemblyFile.string()});
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Assembly = readFile(AssemblyFile);
        const std::string Mnemonic =
            (Sysret ? "sysret" : "sysexit") + std::string(Wide ? "q" : "l");
        EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
        EXPECT_EQ(Assembly.find("\tret"), std::string::npos) << Assembly;
        EXPECT_NE(Assembly.find("\t" + Mnemonic), std::string::npos)
            << Assembly;
        EXPECT_EQ(Assembly.find("movl\t$2,"), std::string::npos) << Assembly;
        if (Sysret)
          EXPECT_TRUE(std::regex_search(
              Assembly, std::regex("movq[^\n]*, %rsp\n[ \t]*" + Mnemonic)))
              << Assembly;
        const auto IR = tmpFile(Operation + ".ll");
        const auto Emitted =
            ncc({Target, Opt, "-std=c23", "-fno-lto", "-fno-builtin-mimalloc",
                 "-ffreestanding", "-Werror", "-S", "-emit-llvm",
                 Source.string(), "-o", IR.string()});
        ASSERT_EQ(Emitted.exitCode, 0) << Emitted.err;
        const auto Text = readFile(IR);
        EXPECT_NE(Text.find("call void @llvm.x86." + Operation + "("),
                  std::string::npos)
            << Text;
        EXPECT_NE(Text.find("convergent noduplicate noreturn nounwind"),
                  std::string::npos)
            << Text;
        EXPECT_NE(Text.find("unreachable"), std::string::npos) << Text;
        EXPECT_EQ(Text.find("asm sideeffect"), std::string::npos) << Text;
      }
  }
}

TEST_F(X86SystemReturnTest, MalformedArgumentsAreRejectedByTheFrontend) {
  for (const char *Call :
       {"__builtin_ia32_sysret32(1, 2)", "__builtin_ia32_sysret64(1, 2, 3, 4)",
        "__builtin_ia32_sysexit32(1)", "__builtin_ia32_sysexit64(1, 2, 3)",
        "__builtin_ia32_sysret32((void *)0, 2, 3)",
        "__builtin_ia32_sysexit64(1, (void *)0)"}) {
    SCOPED_TRACE(Call);
    const auto Source = tmpFile("invalid_return.c");
    writeFile(Source, "void probe(void) { " + std::string(Call) + "; }\n");
    const auto Result =
        ncc({"--target=x86_64-pc-windows-msvc", "-std=c23", "-ffreestanding",
             "-Werror", "-fsyntax-only", Source.string()});
    EXPECT_NE(Result.exitCode, 0) << Result.err;
  }
}
