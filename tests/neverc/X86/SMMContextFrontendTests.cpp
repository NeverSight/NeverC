#include "NativeBuiltinFixture.h"

#include <regex>

class X86SMMContextTest : public X86NativeBuiltinTest {};

TEST_F(X86SMMContextTest, RestoresSoftwareBanksAtTheTerminalBoundary) {
  const auto Source = tmpFile("smm-context.c");
  writeFile(Source, R"C(
#include <smmcontextintrin.h>
_Static_assert(sizeof(struct __smm_rsm_context) == 40, "context size");
#if !__has_builtin(__builtin_ia32_rsm_context)
#error missing RSM context builtin
#endif
[[noreturn]] void transfer(const struct __smm_rsm_context *context,
                          unsigned size, volatile unsigned *marker) {
  volatile unsigned char local[size + 32];
  local[0] = 17;
  *marker = local[0];
  __smm_rsm_context64(context);
  *marker = 23;
}
)C");
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(std::string(Target) + " " + Opt);
      for (bool IR : {false, true}) {
        const auto Output = tmpFile(IR ? "smm.ll" : "smm.s");
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
          EXPECT_NE(Text.find("call void @llvm.x86.rsm.context(ptr"),
                    std::string::npos)
              << Text;
          EXPECT_NE(Text.find("convergent noduplicate noreturn nounwind"),
                    std::string::npos)
              << Text;
          EXPECT_EQ(Text.find("asm sideeffect"), std::string::npos) << Text;
        } else {
          EXPECT_TRUE(std::regex_search(
              Text, std::regex("xrstor64[^\\n]*\\n[ \\t]*rsm")))
              << Text;
          EXPECT_NE(Text.find("%cr2"), std::string::npos) << Text;
          EXPECT_NE(Text.find("%cr8"), std::string::npos) << Text;
          EXPECT_NE(Text.find("wrmsr"), std::string::npos) << Text;
          EXPECT_EQ(Text.find("#APP"), std::string::npos) << Text;
          EXPECT_EQ(Text.find("\tret"), std::string::npos) << Text;
        }
      }
    }
}

TEST_F(X86SMMContextTest, RejectsMissingFeatureAndInvalidSignature) {
  for (const char *Call :
       {"__builtin_ia32_rsm_context(p)", "__builtin_ia32_rsm_context()",
        "__builtin_ia32_rsm_context(1)"}) {
    const auto Source = tmpFile("bad-smm.c");
    writeFile(Source, "void f(void *p) { " + std::string(Call) + "; }\n");
    const bool ValidSignature =
        std::string(Call) == "__builtin_ia32_rsm_context(p)";
    const auto Result =
        ncc({"--target=x86_64-unknown-linux-gnu", "-std=c23", "-ffreestanding",
             ValidSignature ? "-mno-xsave" : "-mxsave", "-Werror", "-masm=att",
             "-S", Source.string(), "-o", tmpFile("bad.s").string()});
    EXPECT_NE(Result.exitCode, 0) << Result.err;
  }
}
