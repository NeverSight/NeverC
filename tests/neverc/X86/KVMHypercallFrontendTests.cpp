#include "NativeBuiltinFixture.h"

class X86KVMHypercallTest : public X86NativeBuiltinTest {};

TEST_F(X86KVMHypercallTest, FiveInputsAndReturningMemoryEffectsAcrossFormats) {
  const auto Source = tmpFile("kvm-hypercall.c");
  writeFile(Source, R"(
#include <kvmhypercallintrin.h>
unsigned long long run(unsigned long long *p, unsigned long long n,
                       unsigned long long a, unsigned long long b,
                       unsigned long long c, unsigned long long d) {
  *p = 7;
  unsigned long long r = __kvm_vmcall(n, a, b, c, d);
  *p = 9;
  __kvm_vmmcall(n, a, b, c, d);
  return r + *p;
}
)");
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      const auto AssemblyFile = tmpFile("kvm-hypercall.s");
      const auto Result = ncc(
          {Target, Opt, "-std=c23", "-fno-lto", "-fno-builtin-mimalloc",
           "-ffreestanding", "-Werror", "-mllvm", "-verify-machineinstrs",
           "-masm=att", "-S", Source.string(), "-o", AssemblyFile.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Assembly = readFile(AssemblyFile);
      EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
      EXPECT_NE(Assembly.find("\tvmcall"), std::string::npos) << Assembly;
      EXPECT_NE(Assembly.find("\tvmmcall"), std::string::npos) << Assembly;
      const auto IR = tmpFile("kvm-hypercall.ll");
      const auto Emitted =
          ncc({Target, Opt, "-std=c23", "-fno-lto", "-fno-builtin-mimalloc",
               "-ffreestanding", "-Werror", "-S", "-emit-llvm", Source.string(),
               "-o", IR.string()});
      ASSERT_EQ(Emitted.exitCode, 0) << Emitted.err;
      const auto Text = readFile(IR);
      const auto First = Text.find("call i64 @llvm.x86.vmcall.kvm(");
      const auto Second = Text.find("call i64 @llvm.x86.vmmcall.kvm(", First);
      ASSERT_NE(First, std::string::npos) << Text;
      ASSERT_NE(Second, std::string::npos) << Text;
      EXPECT_LT(Text.find("store i64 7"), First) << Text;
      EXPECT_LT(Text.find("store i64 9", First), Second) << Text;
      EXPECT_NE(Text.find("load i64", Second), std::string::npos) << Text;
      EXPECT_NE(Text.find("convergent noduplicate nounwind"), std::string::npos)
          << Text;
      EXPECT_EQ(Text.find("asm sideeffect"), std::string::npos) << Text;
    }
}

TEST_F(X86KVMHypercallTest, RejectsWrongArgumentCountAndOtherArchitectures) {
  const auto Source = tmpFile("invalid-kvm-hypercall.c");
  writeFile(Source, "unsigned long long run(void) { return "
                    "__builtin_ia32_vmcall_kvm(1,2,3,4); }\n");
  auto Result =
      ncc({"--target=x86_64-unknown-linux-gnu", "-std=c23", "-ffreestanding",
           "-Werror", "-fsyntax-only", Source.string()});
  EXPECT_NE(Result.exitCode, 0);
  writeFile(Source, "#include <kvmhypercallintrin.h>\n");
  Result = ncc({"--target=aarch64-unknown-linux-gnu", "-std=c23",
                "-ffreestanding", "-fsyntax-only", Source.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("require x86-64"), std::string::npos)
      << Result.err;
}
