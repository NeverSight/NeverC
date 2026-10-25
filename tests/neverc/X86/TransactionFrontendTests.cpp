#include "NativeBuiltinFixture.h"

class X86TransactionTest : public X86NativeBuiltinTest {};

// ---------------------------------------------------------------------------
// TSX: xbegin, xabort, xend, xtest
// ---------------------------------------------------------------------------
TEST_F(X86TransactionTest, TsxOpsLowerCorrectly) {
  auto [rc, s] = compileToAsm(R"(
    void do_xbegin(void) { _xbegin(); }
    void do_xend(void)   { _xend(); }
    void do_xabort(void) { _xabort(0x42); }
    unsigned char do_xtest(void) { return _xtest(); }
  )",
                              "tsx");
  ASSERT_EQ(rc, 0) << s;
  EXPECT_NE(s.find("xbegin"), std::string::npos) << "expected xbegin\n" << s;
  EXPECT_NE(s.find("xend"), std::string::npos) << "expected xend\n" << s;
  EXPECT_NE(s.find("xabort"), std::string::npos) << "expected xabort\n" << s;
  EXPECT_NE(s.find("xtest"), std::string::npos) << "expected xtest\n" << s;
}

TEST_F(X86TransactionTest, TransactionAbortRequiresAConstantInEveryInterface) {
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"}) {
    SCOPED_TRACE(Target);
    for (unsigned Interface = 0; Interface != 3; ++Interface) {
      // The undeclared Microsoft builtin is active only in Microsoft mode.
      if (Interface == 2 &&
          std::string(Target).find("windows") == std::string::npos)
        continue;
      SCOPED_TRACE(Interface);
      const auto Source = tmpFile("transaction_nonconstant.c");
      const std::string Header =
          Interface == 0 ? "#include <immintrin.h>\n" : "";
      const std::string Name =
          Interface == 1 ? "__builtin_ia32_xabort" : "_xabort";
      writeFile(Source,
                Header +
                    "__attribute__((target(\"rtm\"))) void f(unsigned x) { " +
                    Name + "(x); }\n");
      // This must fail in the frontend, before LLVM can select an instruction
      // from an invalid nonconstant immediate or invent an encoded byte.
      const auto Result =
          ncc({Target, "-fno-builtin-mimalloc", "-ffreestanding",
               "-fsyntax-only", Source.string()});
      EXPECT_GT(Result.exitCode, 0) << Result.err;
      EXPECT_NE(Result.err.find("must be a constant integer"),
                std::string::npos)
          << Result.err;
    }
  }
}

TEST_F(X86TransactionTest, TransactionHeadersExposeNativeInterfaces) {
  for (const char *Header : {"immintrin.h", "x86intrin.h"}) {
    SCOPED_TRACE(Header);
    const auto Source = tmpFile("transaction_headers.c");
    writeFile(Source, std::string("#include <") + Header + R"(>
      #ifdef _MSC_VER
      #include <intrin.h>
      #endif
      _Static_assert(_XBEGIN_STARTED == ~0u, "transaction success status");
      _Static_assert(_XABORT_EXPLICIT == 1 && _XABORT_RETRY == 2 &&
                     _XABORT_CONFLICT == 4 && _XABORT_CAPACITY == 8 &&
                     _XABORT_DEBUG == 16 && _XABORT_NESTED == 32,
                     "abort status bits");
      _Static_assert(_XABORT_CODE(0xff000001u) == 255, "abort code");
      __attribute__((target("rtm")))
      unsigned begin(void) { return _xbegin(); }
      __attribute__((target("rtm")))
      void end(void) { _xend(); }
      __attribute__((target("rtm")))
      void abort_transaction(void) { _xabort(255); }
      __attribute__((target("rtm")))
      int query(void) { return _xtest(); }
    )");
    for (const char *Target :
         {"--target=x86_64-unknown-linux-gnu",
          "--target=x86_64-pc-windows-msvc", "--target=x86_64-apple-darwin"}) {
      SCOPED_TRACE(Target);
      for (const char *Opt : {"-O0", "-O2"}) {
        SCOPED_TRACE(Opt);
        const auto AssemblyFile = tmpFile("transaction_headers.s");
        auto Result =
            ncc({Target, Opt, "-fno-lto", "-fno-builtin-mimalloc",
                 "-ffreestanding", "-Werror=implicit-function-declaration",
                 "-mllvm", "-verify-machineinstrs", "-S", Source.string(), "-o",
                 AssemblyFile.string()});
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Assembly = readFile(AssemblyFile);
        for (const char *Mnemonic : {"xbegin", "xend", "xabort", "xtest"})
          EXPECT_NE(Assembly.find(std::string("\t") + Mnemonic),
                    std::string::npos)
              << Assembly;
        EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
        EXPECT_EQ(Assembly.find("\tcall"), std::string::npos) << Assembly;
        const auto IRFile = tmpFile("transaction_headers.ll");
        Result = ncc({Target, Opt, "-fno-lto", "-fno-builtin-mimalloc",
                      "-ffreestanding", "-S", "-emit-llvm", Source.string(),
                      "-o", IRFile.string()});
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto IR = readFile(IRFile);
        for (const char *Name : {"xbegin", "xend", "xabort", "xtest"})
          EXPECT_NE(IR.find(std::string("@llvm.x86.") + Name),
                    std::string::npos)
              << IR;
        EXPECT_EQ(IR.find("asm "), std::string::npos) << IR;
      }
    }
  }
}
