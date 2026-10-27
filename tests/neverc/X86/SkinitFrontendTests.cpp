#include "NativeBuiltinFixture.h"

class X86SkinitTest : public X86NativeBuiltinTest {};

TEST_F(X86SkinitTest, LaunchHasNoOldABIContinuationAcrossFormats) {
  const auto Source = tmpFile("skinit.c");
  writeFile(Source, R"(
#if !__has_builtin(__builtin_ia32_skinit)
#error "missing native launch builtin"
#endif
extern volatile unsigned observed;
[[noreturn]] void launch(unsigned physical) {
  observed = 1;
  __builtin_ia32_skinit(physical);
  observed = 2;
}
[[noreturn]] void launch_constant(void) {
  __builtin_ia32_skinit(0x8765cdefU);
}
)");
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      const auto AssemblyFile = tmpFile("skinit.s");
      const auto Result = ncc(
          {Target, Opt, "-std=c23", "-fno-lto", "-fno-builtin-mimalloc",
           "-ffreestanding", "-Werror", "-mllvm", "-verify-machineinstrs",
           "-masm=att", "-S", Source.string(), "-o", AssemblyFile.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Assembly = readFile(AssemblyFile);
      EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
      EXPECT_EQ(Assembly.find("\tcall"), std::string::npos) << Assembly;
      EXPECT_EQ(Assembly.find("\tret"), std::string::npos) << Assembly;
      EXPECT_NE(Assembly.find("\tskinit"), std::string::npos) << Assembly;
      EXPECT_NE(Assembly.find("movl\t$1,"), std::string::npos) << Assembly;
      EXPECT_EQ(Assembly.find("movl\t$2,"), std::string::npos) << Assembly;
      EXPECT_TRUE(Assembly.find("$-2023371281, %eax") != std::string::npos ||
                  Assembly.find("$2271596015, %eax") != std::string::npos)
          << Assembly;
      const auto IR = tmpFile("skinit.ll");
      const auto Emitted =
          ncc({Target, Opt, "-std=c23", "-fno-lto", "-fno-builtin-mimalloc",
               "-ffreestanding", "-Werror", "-S", "-emit-llvm", Source.string(),
               "-o", IR.string()});
      ASSERT_EQ(Emitted.exitCode, 0) << Emitted.err;
      const auto Text = readFile(IR);
      EXPECT_NE(Text.find("call void @llvm.x86.skinit(i32"), std::string::npos)
          << Text;
      EXPECT_NE(Text.find("convergent noduplicate noreturn nounwind"),
                std::string::npos)
          << Text;
      EXPECT_NE(Text.find("unreachable"), std::string::npos) << Text;
      EXPECT_EQ(Text.find("asm sideeffect"), std::string::npos) << Text;
    }
}

TEST_F(X86SkinitTest, MicrosoftSpellingUsesTheSameNativeLaunch) {
  const auto Source = tmpFile("svm_skinit.c");
  writeFile(Source, R"(
#include <intrin.h>
extern volatile unsigned observed;
[[noreturn]] void launch(int physical) {
  __svm_skinit(physical);
  observed = 2;
}
)");
  for (const char *Opt : {"-O0", "-O2"}) {
    const auto AssemblyFile = tmpFile("svm_skinit.s");
    const auto Result =
        ncc({"--target=x86_64-pc-windows-msvc", Opt, "-std=c23", "-fno-lto",
             "-fno-builtin-mimalloc", "-ffreestanding", "-Werror", "-mllvm",
             "-verify-machineinstrs", "-masm=att", "-S", Source.string(), "-o",
             AssemblyFile.string()});
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    const auto Assembly = readFile(AssemblyFile);
    EXPECT_NE(Assembly.find("\tskinit"), std::string::npos) << Assembly;
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
    EXPECT_EQ(Assembly.find("\tcall"), std::string::npos) << Assembly;
    EXPECT_EQ(Assembly.find("\tret"), std::string::npos) << Assembly;
    EXPECT_EQ(Assembly.find("movl\t$2,"), std::string::npos) << Assembly;
  }
}

TEST_F(X86SkinitTest, MalformedArgumentsAreRejectedByTheFrontend) {
  for (const char *Call :
       {"__builtin_ia32_skinit()", "__builtin_ia32_skinit(1, 2)",
        "__builtin_ia32_skinit((void *)0)"}) {
    SCOPED_TRACE(Call);
    const auto Source = tmpFile("invalid_skinit.c");
    writeFile(Source, "void probe(void) { " + std::string(Call) + "; }\n");
    const auto Result =
        ncc({"--target=x86_64-pc-windows-msvc", "-std=c23", "-ffreestanding",
             "-Werror", "-fsyntax-only", Source.string()});
    EXPECT_NE(Result.exitCode, 0) << Result.err;
  }
}
