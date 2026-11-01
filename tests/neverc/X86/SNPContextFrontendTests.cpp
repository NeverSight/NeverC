#include "NativeBuiltinFixture.h"

class X86SNPContextTest : public X86NativeBuiltinTest {};

TEST_F(X86SNPContextTest, NativeCompletionAcrossFormats) {
  const auto Source = tmpFile("snp-context.c");
  writeFile(Source, R"(
#include <snpcontextintrin.h>
static_assert(sizeof(struct __snp_context) == 32);
void run(struct __snp_context *s) {
  __snp_psmash(s); __snp_pvalidate(s); __snp_pvalidate_addr32(s);
  __snp_rmpadjust(s); __snp_rmpupdate(s); __snp_rmpquery(s);
  __snp_rmpread(s); __snp_rmpopt(s); __snp_rmpchkd(s);
  __builtin_ia32_rmpread_context(&s->RAX, &s->RFLAGS, 0x6500);
  __sev_vmgexit(s->RAX);
}
)");
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      const auto Assembly = tmpFile("snp.s");
      auto Result =
          ncc({Target, Opt, "-std=c23", "-ffreestanding", "-fno-lto",
               "-fno-builtin-mimalloc", "-mllvm", "-verify-machineinstrs", "-S",
               Source.string(), "-o", Assembly.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Text = readFile(Assembly);
      EXPECT_EQ(Text.find("#APP"), std::string::npos) << Text;
      for (const char *Name :
           {"psmash", "pvalidate", "rmpadjust", "rmpupdate", "rmpquery",
            "rmpread", "rmpopt", "rmpchkd", "vmgexit", "lahf", "seto"})
        EXPECT_NE(Text.find(Name), std::string::npos) << Text;
      const auto IR = tmpFile("snp.ll");
      Result = ncc({Target, Opt, "-std=c23", "-ffreestanding", "-fno-lto",
                    "-fno-builtin-mimalloc", "-S", "-emit-llvm",
                    Source.string(), "-o", IR.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      EXPECT_NE(readFile(IR).find("@llvm.x86.rmpchkd.context("),
                std::string::npos);
      EXPECT_NE(readFile(IR).find("convergent noduplicate nounwind"),
                std::string::npos);
    }
}

TEST_F(X86SNPContextTest, RejectsNonconstantControlAndForeignArchitecture) {
  const auto Source = tmpFile("snp-invalid.c");
  writeFile(Source, "void f(void *p, unsigned n) { "
                    "__builtin_ia32_pvalidate_context(p,p,n); }\n");
  auto Result = ncc({"-std=c23", "-fsyntax-only", Source.string()});
  EXPECT_NE(Result.exitCode, 0);
  writeFile(Source, "#include <snpcontextintrin.h>\n");
  Result = ncc({"--target=aarch64-unknown-linux-gnu", "-std=c23",
                "-ffreestanding", "-fsyntax-only", Source.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("requires x86-64"), std::string::npos)
      << Result.err;
}

TEST_F(X86SNPContextTest, RejectsInvalidControlsBeforeCodeGeneration) {
  const auto Source = tmpFile("snp-control.c");
  for (const char *Name : {"psmash", "pvalidate", "rmpadjust", "rmpupdate",
                           "rmpquery", "rmpread", "rmpopt", "rmpchkd"}) {
    for (unsigned Control : {1u, 2u, 0x6600u, 0x10000u, 0xffffffffu}) {
      if (std::string(Name) == "pvalidate" && Control == 1)
        continue;
      SCOPED_TRACE(Name);
      SCOPED_TRACE(Control);
      writeFile(Source, std::string("void f(void *g,void *f){__builtin_ia32_") +
                            Name + "_context(g,f," + std::to_string(Control) +
                            "u);}\n");
      const auto Result = ncc({"-std=c23", "-fsyntax-only", Source.string()});
      EXPECT_EQ(Result.exitCode, 1) << Result.err;
      EXPECT_NE(Result.err.find("error:"), std::string::npos) << Result.err;
    }
  }
}
