#include "NativeBuiltinFixture.h"

#include <regex>

class X86SGXTransferContextTest : public X86NativeBuiltinTest {};

TEST_F(X86SGXTransferContextTest,
       RestoresInputStateAndAbandonsTheCompilerFrame) {
  const auto Source = tmpFile("sgx-transfer.c");
  writeFile(Source, R"C(
#include <sgxcontextintrin.h>
_Static_assert(sizeof(struct __sgx_transfer_context) == 144, "context size");
#if !__has_builtin(__builtin_ia32_enclu_transfer_context)
#error missing terminal ENCLU builtin
#endif
[[noreturn]] void transfer(const struct __sgx_transfer_context *context,
                          unsigned size, volatile unsigned *marker) {
  _Alignas(64) volatile unsigned char local[size + 32];
  local[0] = 17;
  *marker = local[0];
  __sgx_transfer(context);
  *marker = 23;
}
)C");
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(std::string(Target) + " " + Opt);
      for (bool IR : {false, true}) {
        const auto Output = tmpFile(IR ? "sgx.ll" : "sgx.s");
        std::vector<std::string> Args = {Target,
                                         Opt,
                                         "-std=c23",
                                         "-fno-lto",
                                         "-ffreestanding",
                                         "-fno-builtin-mimalloc",
                                         "-fno-stack-protector",
                                         "-Werror",
                                         "-masm=att",
                                         "-fno-omit-frame-pointer",
                                         "-mxsave",
                                         "-msgx",
                                         "-mllvm",
                                         "-verify-machineinstrs",
                                         "-S",
                                         Source.string(),
                                         "-o",
                                         Output.string()};
        if (IR)
          Args.push_back("-emit-llvm");
        const auto Result = ncc(Args);
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Text = readFile(Output);
        if (IR) {
          EXPECT_NE(Text.find("call void @llvm.x86.enclu.transfer.context(ptr"),
                    std::string::npos)
              << Text;
          EXPECT_NE(Text.find("convergent noduplicate noreturn nounwind"),
                    std::string::npos)
              << Text;
          EXPECT_EQ(Text.find("asm sideeffect"), std::string::npos) << Text;
        } else {
          EXPECT_TRUE(std::regex_search(
              Text,
              std::regex(
                  "movq[ \\t]+32\\(%rax\\), %rsp\\n[ \\t]*"
                  "movq[ \\t]+\\(%rax\\), %rax\\n[ \\t]*enclu\\n[ \\t]*ud2")))
              << Text;
          EXPECT_NE(Text.find("xrstor64"), std::string::npos) << Text;
          EXPECT_NE(Text.find("pushq\t128(%rax)"), std::string::npos) << Text;
          EXPECT_NE(Text.find("jbe"), std::string::npos) << Text;
          EXPECT_EQ(Text.find("#APP"), std::string::npos) << Text;
          EXPECT_EQ(Text.find("\tret"), std::string::npos) << Text;
        }
      }
    }
}

TEST_F(X86SGXTransferContextTest, RejectsMissingFeaturesAndInvalidSignatures) {
  for (unsigned Case = 0; Case != 4; ++Case) {
    const char *Call = Case < 2    ? "__builtin_ia32_enclu_transfer_context(p)"
                       : Case == 2 ? "__builtin_ia32_enclu_transfer_context()"
                                   : "__builtin_ia32_enclu_transfer_context(1)";
    const auto Source = tmpFile("bad-sgx.c");
    writeFile(Source, "void f(void *p) { " + std::string(Call) + "; }\n");
    const auto Result = ncc(
        {"--target=x86_64-unknown-linux-gnu", "-std=c23", "-ffreestanding",
         Case == 0 ? "-mno-xsave" : "-mxsave", Case == 1 ? "-mno-sgx" : "-msgx",
         "-Werror", "-S", Source.string(), "-o", tmpFile("bad.s").string()});
    EXPECT_NE(Result.exitCode, 0) << Result.err;
  }
}

TEST_F(X86SGXTransferContextTest,
       OriginalSiteUsesNoInputRegisterAndCannotBeDynamic) {
  const auto Source = tmpFile("sgx-original.c");
  writeFile(Source, R"C(
#include <sgxcontextintrin.h>
[[noreturn]] void transfer(const struct __sgx_transfer_context *context,
                          unsigned size, volatile unsigned *marker) {
  _Alignas(64) volatile unsigned char local[size+32];
  local[0]=17;*marker=local[0];
  __sgx_transfer_at(context,4096ULL);
}
)C");
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(std::string(Target) + " " + Opt);
      for (unsigned Kind = 0; Kind != 3; ++Kind) {
        const auto Output = tmpFile(Kind == 0   ? "original.s"
                                    : Kind == 1 ? "original.ll"
                                                : "original.o");
        std::vector<std::string> Args = {Target,
                                         Opt,
                                         "-std=c23",
                                         "-fno-lto",
                                         "-ffreestanding",
                                         "-fno-builtin-mimalloc",
                                         "-fno-stack-protector",
                                         "-fno-omit-frame-pointer",
                                         "-Werror",
                                         "-masm=att",
                                         "-mxsave",
                                         "-msgx",
                                         "-mllvm",
                                         "-verify-machineinstrs",
                                         Kind == 2 ? "-c" : "-S",
                                         Source.string(),
                                         "-o",
                                         Output.string()};
        if (Kind == 1)
          Args.push_back("-emit-llvm");
        const auto Result = ncc(Args);
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        if (Kind == 2)
          continue;
        const auto Text = readFile(Output);
        if (Kind == 1) {
          EXPECT_NE(Text.find("@llvm.x86.enclu.transfer.at.context(ptr"),
                    std::string::npos)
              << Text;
          EXPECT_NE(Text.find("i64 4096"), std::string::npos) << Text;
          EXPECT_EQ(Text.find("asm sideeffect"), std::string::npos) << Text;
        } else {
          EXPECT_TRUE(std::regex_search(
              Text, std::regex("movq[ \\t]+32\\(%rax\\), %rsp\\n[ \\t]*movq[ "
                               "\\t]+\\(%rax\\), %rax\\n[ \\t]*jmpq[ "
                               "\\t]+\\*[^\\n]+\\(%rip\\)")))
              << Text;
          EXPECT_TRUE(std::regex_search(Text, std::regex("\\.quad[ \\t]+4096")))
              << Text;
          EXPECT_NE(Text.find("xrstor64"), std::string::npos) << Text;
          EXPECT_EQ(Text.find("\tenclu"), std::string::npos) << Text;
          EXPECT_EQ(Text.find("\tret"), std::string::npos) << Text;
        }
      }
    }
  writeFile(Source, "void f(void *p,unsigned long long pc) { "
                    "__builtin_ia32_enclu_transfer_at_context(p,pc); }\n");
  const auto Result =
      ncc({"--target=x86_64-unknown-linux-gnu", "-std=c23", "-ffreestanding",
           "-mxsave", "-msgx", "-Werror", "-S", Source.string(), "-o",
           tmpFile("bad-original.s").string()});
  EXPECT_NE(Result.exitCode, 0) << Result.err;
}
