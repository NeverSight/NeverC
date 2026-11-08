#include "NativeBuiltinFixture.h"

class X86SVMWidthTest : public X86NativeBuiltinTest {};

TEST_F(X86SVMWidthTest, I386UsesNativeWordAndAddressOverrideIntrinsics) {
  const auto Source = tmpFile("svm32.c");
  writeFile(Source, R"(
    #include <intrin.h>
    unsigned probe(unsigned address, unsigned asid, unsigned *ordinary) {
      *ordinary = address;
      __builtin_ia32_vmload32(address);
      __builtin_ia32_vmsave32(address);
      __builtin_ia32_invlpga32(address, asid);
      __builtin_ia32_vmload_addr16(address);
      __builtin_ia32_vmsave_addr16(address);
      __builtin_ia32_invlpga_addr16(address, asid);
      __svm_vmload(address);
      __svm_vmsave(address);
      __svm_invlpga((void *)address, (int)asid);
      __svm_stgi();
      __svm_clgi();
      __halt();
      return *ordinary ^ address ^ asid;
    }
  )");
  for (const char *Opt : {"-O0", "-O2"}) {
    SCOPED_TRACE(Opt);
    const auto Output = tmpFile("svm32.ll");
    const auto Result =
        ncc({"--target=i386-unknown-linux-gnu", "-std=c23", "-ffreestanding",
             "-fno-builtin-mimalloc", "-fms-extensions", "-D_MSC_VER=1930",
             "-Werror", Opt, "-S", "-emit-llvm", Source.string(), "-o",
             Output.string()});
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    const auto IR = readFile(Output);
    for (const char *Operation : {"vmload", "vmsave", "invlpga"}) {
      EXPECT_NE(IR.find("@llvm.x86." + std::string(Operation) + "32(i32 "),
                std::string::npos)
          << IR;
      EXPECT_NE(IR.find("@llvm.x86." + std::string(Operation) + ".addr16(i16 "),
                std::string::npos)
          << IR;
      EXPECT_EQ(IR.find("@llvm.x86." + std::string(Operation) + "(i64 "),
                std::string::npos)
          << IR;
    }
    for (const char *Operation : {"stgi", "clgi", "hlt"})
      EXPECT_NE(IR.find("@llvm.x86." + std::string(Operation) + "()"),
                std::string::npos)
          << IR;
    EXPECT_EQ(IR.find(" asm "), std::string::npos) << IR;
    EXPECT_EQ(IR.find("call void @__svm_"), std::string::npos) << IR;
    EXPECT_EQ(IR.find("call void @__halt"), std::string::npos) << IR;
  }
}

TEST_F(X86SVMWidthTest, ModeSpecificInterfacesRejectTheOtherCodeMode) {
  for (const char *Operation : {"vmload", "vmsave", "invlpga"})
    for (const char *Suffix : {"32", "_addr16", "", "_addr32"}) {
      const bool Legacy =
          std::string(Suffix) == "32" || std::string(Suffix) == "_addr16";
      SCOPED_TRACE(Operation);
      SCOPED_TRACE(Suffix);
      const auto Source = tmpFile("wrong_svm_mode.c");
      writeFile(Source, "void bad(void) { __builtin_ia32_" +
                            std::string(Operation) + Suffix + "(0" +
                            (std::string(Operation) == "invlpga" ? ", 0" : "") +
                            "); }\n");
      const auto Result = ncc({Legacy ? "--target=x86_64-unknown-linux-gnu"
                                      : "--target=i386-unknown-linux-gnu",
                               "-ffreestanding", "-fno-builtin-mimalloc",
                               "-Werror", "-S", "-emit-llvm", Source.string(),
                               "-o", tmpFile("wrong_svm_mode.ll").string()});
      EXPECT_NE(Result.exitCode, 0) << Result.err;
      EXPECT_NE(Result.err.find(Legacy ? "32-bit x86 target"
                                       : "undeclared identifier"),
                std::string::npos)
          << Result.err;
    }
}
