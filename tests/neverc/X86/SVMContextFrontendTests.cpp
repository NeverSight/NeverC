#include "NativeBuiltinFixture.h"

class X86SVMContextTest : public X86NativeBuiltinTest {};

TEST_F(X86SVMContextTest, PublicHeaderTransportsStateAcrossFormats) {
  const auto Source = tmpFile("svm-context.c");
  writeFile(Source, R"(
#include <svmcontextintrin.h>
static_assert(sizeof(struct __svm_vmrun_context) == 144);
static_assert(__builtin_offsetof(struct __svm_vmrun_context, RBX) == 32);
static_assert(__builtin_offsetof(struct __svm_vmrun_context, RCX) == 40);
static_assert(__builtin_offsetof(struct __svm_vmrun_context, R15) == 136);
unsigned long long run(struct __svm_vmrun_context *context) {
  context->RCX = 0x1234;
  __svm_vmrun_context64(context);
  __svm_vmrun_context32(context);
  return context->RCX;
}
)");
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      const auto AssemblyFile = tmpFile("svm-context.s");
      const auto Result = ncc(
          {Target, Opt, "-std=c23", "-fno-lto", "-fno-builtin-mimalloc",
           "-ffreestanding", "-Werror", "-mllvm", "-verify-machineinstrs",
           "-masm=att", "-S", Source.string(), "-o", AssemblyFile.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Assembly = readFile(AssemblyFile);
      EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
      EXPECT_NE(Assembly.find("\tvmrun"), std::string::npos) << Assembly;
      EXPECT_NE(Assembly.find("\taddr32"), std::string::npos) << Assembly;
      EXPECT_NE(Assembly.find("\txsave64"), std::string::npos) << Assembly;
      EXPECT_NE(Assembly.find("\txrstor64"), std::string::npos) << Assembly;
      EXPECT_NE(Assembly.find("\tret"), std::string::npos) << Assembly;
      const auto IR = tmpFile("svm-context.ll");
      const auto Emitted =
          ncc({Target, Opt, "-std=c23", "-fno-lto", "-fno-builtin-mimalloc",
               "-ffreestanding", "-Werror", "-S", "-emit-llvm", Source.string(),
               "-o", IR.string()});
      ASSERT_EQ(Emitted.exitCode, 0) << Emitted.err;
      const auto Text = readFile(IR);
      const auto FirstCall = Text.find("call void @llvm.x86.vmrun.context");
      const auto SecondCall =
          Text.find("call void @llvm.x86.vmrun.context", FirstCall + 1);
      ASSERT_NE(FirstCall, std::string::npos) << Text;
      ASSERT_NE(SecondCall, std::string::npos) << Text;
      EXPECT_NE(Text.find("load i64", SecondCall), std::string::npos) << Text;
      EXPECT_NE(Text.find("convergent noduplicate nounwind"), std::string::npos)
          << Text;
      EXPECT_EQ(Text.find("asm sideeffect"), std::string::npos) << Text;
    }
}

TEST_F(X86SVMContextTest, InvalidWidthOrShapeFailsBeforeCodeGeneration) {
  for (const char *Call : {"__builtin_ia32_vmrun_context(p, width)",
                           "__builtin_ia32_vmrun_context(p, 16)",
                           "__builtin_ia32_vmrun_context(p, 128)",
                           "__builtin_ia32_vmrun_context(p)",
                           "__builtin_ia32_vmrun_context(42, 64)"}) {
    SCOPED_TRACE(Call);
    const auto Source = tmpFile("invalid-svm-context.c");
    writeFile(Source, "void run(void *p, unsigned width) { " +
                          std::string(Call) + "; }\n");
    const auto Result =
        ncc({"--target=x86_64-unknown-linux-gnu", "-std=c23", "-ffreestanding",
             "-Werror", "-fsyntax-only", Source.string()});
    EXPECT_NE(Result.exitCode, 0) << Result.err;
  }
}

TEST_F(X86SVMContextTest, HeaderRejectsOtherArchitectures) {
  const auto Source = tmpFile("arm-svm-context.c");
  writeFile(Source, "#include <svmcontextintrin.h>\n");
  const auto Result = ncc({"--target=aarch64-unknown-linux-gnu", "-std=c23",
                           "-ffreestanding", "-fsyntax-only", Source.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("VMRUN context requires x86-64"), std::string::npos)
      << Result.err;
}
