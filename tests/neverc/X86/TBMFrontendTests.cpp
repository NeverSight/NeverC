#include "NativeBuiltinFixture.h"

class X86TBMTest : public X86NativeBuiltinTest {};

TEST_F(X86TBMTest, HeadersSelectEveryTbmOperationWithoutAssembly) {
  const auto Source = tmpFile("tbm_headers.c");
  const char *Operations[] = {"blcfill", "blci",  "blcic",  "blcmsk", "blcs",
                              "blsfill", "blsic", "t1mskc", "tzmsk",  "bextri"};
  std::string Code = "#include <x86intrin.h>\n";
  for (const char *Operation : Operations)
    for (unsigned Width : {32u, 64u}) {
      const std::string Name = Operation;
      const std::string Type = Width == 32 ? "unsigned" : "unsigned long long";
      const std::string Suffix = "_u" + std::to_string(Width);
      Code += "__attribute__((target(\"tbm\"))) " + Type + " test_" + Name +
              Suffix + "(" + Type + " x) { return __" + Name + Suffix + "(x" +
              (Name == "bextri" ? ", 0x804" : "") + "); }\n";
    }
  writeFile(Source, Code);
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"}) {
    SCOPED_TRACE(Target);
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Opt);
      const auto AssemblyFile = tmpFile("tbm_headers.s");
      auto Result =
          ncc({Target, Opt, "-fno-lto", "-fno-builtin-mimalloc",
               "-ffreestanding", "-Werror=implicit-function-declaration",
               "-mllvm", "-verify-machineinstrs", "-masm=att", "-S",
               Source.string(), "-o", AssemblyFile.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Assembly = readFile(AssemblyFile);
      EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
      EXPECT_EQ(Assembly.find("\tcall"), std::string::npos) << Assembly;
      // The nine arithmetic wrappers use LLVM's existing TBM patterns. At
      // O0 their ordinary arithmetic is valid; O2 must select native TBM.
      if (std::string(Opt) == "-O2")
        for (const char *Operation : Operations)
          for (const char *Suffix : {"l", "q"}) {
            const std::string Mnemonic =
                std::string(Operation) == "bextri" ? "bextr" : Operation;
            EXPECT_NE(Assembly.find("\t" + Mnemonic + Suffix + "\t"),
                      std::string::npos)
                << Mnemonic << Suffix << '\n'
                << Assembly;
          }
      const auto IRFile = tmpFile("tbm_headers.ll");
      Result = ncc({Target, Opt, "-fno-lto", "-fno-builtin-mimalloc",
                    "-ffreestanding", "-S", "-emit-llvm", Source.string(), "-o",
                    IRFile.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto IR = readFile(IRFile);
      EXPECT_EQ(IR.find("asm "), std::string::npos) << IR;
      EXPECT_NE(IR.find("@llvm.x86.tbm.bextri.u32"), std::string::npos) << IR;
      EXPECT_NE(IR.find("@llvm.x86.tbm.bextri.u64"), std::string::npos) << IR;
    }
  }
}

TEST_F(X86TBMTest, BitExtractImmediateCannotBeDynamic) {
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (unsigned Width : {32u, 64u}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Width);
      const auto Source = tmpFile("tbm_nonconstant.c");
      const std::string Type = Width == 32 ? "unsigned" : "unsigned long long";
      writeFile(Source, "#include <x86intrin.h>\n"
                        "__attribute__((target(\"tbm\"))) " +
                            Type + " f(" + Type + " x, " + Type +
                            " control) { return "
                            "__bextri_u" +
                            std::to_string(Width) + "(x, control); }\n");
      const auto Result =
          ncc({Target, "-fno-builtin-mimalloc", "-ffreestanding",
               "-fsyntax-only", Source.string()});
      EXPECT_GT(Result.exitCode, 0) << Result.err;
      EXPECT_NE(Result.err.find("must be a constant integer"),
                std::string::npos)
          << Result.err;
    }
}
