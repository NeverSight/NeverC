#include "NativeBuiltinFixture.h"

class X86HyperVContextTest : public X86NativeBuiltinTest {};

TEST_F(X86HyperVContextTest, PageAndFullOutputsAcrossFormats) {
  const auto Source = tmpFile("hyperv-context.c");
  writeFile(Source, R"(
#include <hypervcontextintrin.h>
static_assert(sizeof(struct __hyperv_context)==224);
unsigned long long run(void *entry, struct __hyperv_context *s) {
  s->RAX = 7;
  __hyperv_call(entry, s);
  return s->RAX + s->RCX + s->RDX + s->R8 + s->XMM[5][15];
}
)");
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      const auto Assembly = tmpFile("hyperv.s");
      auto Result =
          ncc({Target, Opt, "-std=c23", "-ffreestanding", "-fno-lto",
               "-fno-builtin-mimalloc", "-mllvm", "-verify-machineinstrs",
               "-masm=att", "-S", Source.string(), "-o", Assembly.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Text = readFile(Assembly);
      EXPECT_EQ(Text.find("#APP"), std::string::npos) << Text;
      EXPECT_NE(Text.find("callq\t*%r9"), std::string::npos) << Text;
      EXPECT_NE(Text.find("%xmm5, 80(%r11)"), std::string::npos) << Text;
      const auto IR = tmpFile("hyperv.ll");
      Result = ncc({Target, Opt, "-std=c23", "-ffreestanding", "-fno-lto",
                    "-fno-builtin-mimalloc", "-S", "-emit-llvm",
                    Source.string(), "-o", IR.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      EXPECT_NE(readFile(IR).find("@llvm.x86.hyperv.call("), std::string::npos);
      EXPECT_NE(readFile(IR).find("convergent noduplicate nounwind"),
                std::string::npos);
    }
}

TEST_F(X86HyperVContextTest, RequiresImmediateStrideAndX64) {
  const auto Source = tmpFile("bad-hyperv.c");
  writeFile(
      Source,
      "void run(void *p,unsigned s) {__builtin_ia32_hyperv_call(p,p,p,s);}\n");
  auto Result = ncc({"-std=c23", "-fsyntax-only", Source.string()});
  EXPECT_NE(Result.exitCode, 0);
  writeFile(Source, "#include <hypervcontextintrin.h>\n");
  Result = ncc({"--target=aarch64-unknown-linux-gnu", "-std=c23",
                "-ffreestanding", "-fsyntax-only", Source.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("requires x86-64"), std::string::npos)
      << Result.err;
}
