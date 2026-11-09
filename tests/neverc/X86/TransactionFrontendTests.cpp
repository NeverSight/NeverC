#include "NativeBuiltinFixture.h"

#include <cstdint>
#include <iterator>
#include <regex>
#include <vector>

class X86TransactionTest : public X86NativeBuiltinTest {};

TEST_F(X86TransactionTest, CpuidHeaderKeepsDeadOutputsAndSingleEvaluation) {
  const auto Source = tmpFile("cpuid-header.c"),
             IR = tmpFile("cpuid-header.ll");
  writeFile(Source, R"C(
#ifdef TEST_FALLBACK
#undef __has_builtin
#define __has_builtin(name) 0
#endif
#include <cpuid.h>
#define PROBE(name, result) \
__attribute__((noinline)) unsigned name(unsigned leaf, unsigned subleaf) { \
  unsigned a,b,c,d; __cpuid_count(leaf,subleaf,a,b,c,d); return result; \
}
PROBE(word_a,a)
PROBE(word_b,b)
PROBE(word_c,c)
PROBE(word_d,d)
__attribute__((noinline)) unsigned leaf_b(unsigned leaf) {
  unsigned a,b,c,d;__cpuid(leaf,a,b,c,d);return b;
}
int main(void) {
  const unsigned leaves[]={0,7,0x80000000};
  for(unsigned i=0;i<3;++i) {
    unsigned leaf=leaves[i]; int expected[4];
    __builtin_ia32_cpuidex(expected,leaf,0);
    if(word_a(leaf,0)!=(unsigned)expected[0] ||
       word_b(leaf,0)!=(unsigned)expected[1] ||
       word_c(leaf,0)!=(unsigned)expected[2] ||
       word_d(leaf,0)!=(unsigned)expected[3])return 1;
  }
  int expected[4];__builtin_ia32_cpuidex(expected,0,0);
  if(leaf_b(0)!=(unsigned)expected[1])return 2;
  unsigned leaf=0,subleaf=0,out[4],indices[4]={0,1,2,3};
  __cpuid_count(leaf++,subleaf++,out[indices[0]++],out[indices[1]++],
                out[indices[2]++],out[indices[3]++]);
  if(leaf!=1 || subleaf!=1)return 3;
  for(unsigned i=0;i<4;++i)
    if(indices[i]!=i+1 || out[i]!=(unsigned)expected[i])return 4;
  return 0;
}
)C");
  for (const char *Opt : {"-O0", "-O2"}) {
    auto Result = ncc({"--target=x86_64-unknown-linux-gnu", Opt,
                       "-ffreestanding", "-fno-lto", "-fno-builtin-mimalloc",
                       "-S", "-emit-llvm", Source.string(), "-o", IR.string()});
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    const auto Text = readFile(IR);
    EXPECT_NE(Text.find("@llvm.x86.cpuidex("), std::string::npos);
    EXPECT_EQ(Text.find("asm "), std::string::npos);
#if defined(__linux__) && defined(__x86_64__)
    for (const char *Mode : {"", " -DTEST_FALLBACK"})
      compileRunAndCheck(std::string("cpuid-header") + Opt + Mode,
                         Source.string(),
                         std::string("-std=c23 -fno-lto "
                                     "-fno-builtin-mimalloc ") +
                             Opt + Mode);
#endif
  }
}

TEST_F(X86TransactionTest, GenericCpuidEvaluatesEachArgumentOnce) {
  const auto Source = tmpFile("cpuid-once.c"), IR = tmpFile("cpuid-once.ll");
  writeFile(Source, R"C(
extern int *next_pointer(void);
extern unsigned next_leaf(void), next_subleaf(void);
void query(void) {
  __builtin_ia32_cpuidex(next_pointer(),next_leaf(),next_subleaf());
}
)C");
  for (const char *Triple :
       {"i386-unknown-linux-gnu", "x86_64-unknown-linux-gnu"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Triple);
      const auto Result =
          ncc({std::string("--target=") + Triple, Opt, "-ffreestanding",
               "-fno-lto", "-fno-builtin-mimalloc", "-S", "-emit-llvm",
               Source.string(), "-o", IR.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Text = readFile(IR);
      for (const char *Name : {"next_pointer", "next_leaf", "next_subleaf"}) {
        const std::regex Call(std::string("call [^\\n]*@") + Name + "\\(");
        EXPECT_EQ(
            std::distance(std::sregex_iterator(Text.begin(), Text.end(), Call),
                          std::sregex_iterator()),
            1)
            << Text;
      }
    }
}

TEST_F(X86TransactionTest, AbortPairAndCpuidUseNativeInstructions) {
  const auto Source = tmpFile("abort-pair.c");
  writeFile(Source, R"C(
unsigned pair(unsigned live, unsigned *memory) {
  *memory=71;
  unsigned status=__builtin_ia32_rtm_abort_pair(167);
  *memory=73;
  return status+live;
}
unsigned limits(void) {
  return __builtin_ia32_rtm_abort_pair(0) +
         __builtin_ia32_rtm_abort_pair(255);
}
void query(int *out, unsigned leaf, unsigned subleaf) {
  __builtin_ia32_cpuidex(out,leaf,subleaf);
}
)C");
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      for (const char *Kind : {"assembly", "ir", "object"}) {
        SCOPED_TRACE(Kind);
        const auto Output = tmpFile(Kind);
        std::vector<std::string> Args{Target,
                                      Opt,
                                      "-std=c23",
                                      "-ffreestanding",
                                      "-fno-lto",
                                      "-fno-builtin-mimalloc",
                                      "-mllvm",
                                      "-verify-machineinstrs",
                                      std::string(Kind) == "object" ? "-c"
                                                                    : "-S"};
        if (std::string(Kind) == "ir")
          Args.push_back("-emit-llvm");
        Args.insert(Args.end(), {Source.string(), "-o", Output.string()});
        const auto Result = ncc(Args);
        ASSERT_EQ(Result.exitCode, 0) << Result.err;
        const auto Text = readFile(Output);
        if (std::string(Kind) == "object") {
          for (unsigned Code : {0u, 167u, 255u}) {
            const unsigned char Bytes[] = {0xc7, 0xf8,         3, 0, 0, 0, 0xc6,
                                           0xf8, uint8_t(Code)};
            EXPECT_NE(
                Text.find(std::string(reinterpret_cast<const char *>(Bytes),
                                      sizeof(Bytes))),
                std::string::npos);
          }
        } else if (std::string(Kind) == "ir") {
          EXPECT_NE(Text.find("@llvm.x86.rtm.abort.pair(i32 167)"),
                    std::string::npos);
          EXPECT_NE(Text.find("@llvm.x86.cpuidex("), std::string::npos);
          EXPECT_NE(Text.find("convergent noduplicate nounwind"),
                    std::string::npos);
          EXPECT_EQ(Text.find("asm "), std::string::npos);
        } else {
          EXPECT_NE(Text.find("xbegin"), std::string::npos);
          EXPECT_NE(Text.find("xabort"), std::string::npos);
          EXPECT_NE(Text.find("cpuid"), std::string::npos);
          EXPECT_EQ(Text.find("#APP"), std::string::npos);
          EXPECT_EQ(Text.find("\tcall"), std::string::npos);
        }
      }
    }
}

TEST_F(X86TransactionTest, AbortPairValidatesConstantCodeAndHeader) {
  const auto Source = tmpFile("abort-pair-constant.c");
  for (const char *Code : {"256", "-1", "n"}) {
    writeFile(Source, std::string("unsigned f(unsigned n){return ") +
                          "__builtin_ia32_rtm_abort_pair(" + Code + ");}\n");
    const auto Result = ncc({"-std=c23", "-fsyntax-only", Source.string()});
    EXPECT_NE(Result.exitCode, 0) << Result.err;
  }
  writeFile(Source, "#include <immintrin.h>\n"
                    "unsigned f(void){return __rtm_abort_pair(255);}\n");
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"}) {
    const auto Result = ncc({Target, "-std=c23", "-mrtm", "-ffreestanding",
                             "-fsyntax-only", Source.string()});
    EXPECT_EQ(Result.exitCode, 0) << Result.err;
  }
}

TEST_F(X86TransactionTest,
       LegacyAbortPairRetainsNativeHeaderAndStatusContract) {
  const auto Source = tmpFile("abort-pair32.c");
  const auto IR = tmpFile("abort-pair32.ll");
  writeFile(Source, "#include <immintrin.h>\n"
                    "unsigned probe(unsigned live) {\n"
                    "return __rtm_abort_pair(167) + live; }\n");
  for (const char *Opt : {"-O0", "-O2"}) {
    const auto R =
        ncc({"--target=i386-unknown-linux-gnu", "-std=c23", "-mrtm", Opt,
             "-Werror", "-ffreestanding", "-fno-lto", "-fno-builtin-mimalloc",
             "-S", "-emit-llvm", Source.string(), "-o", IR.string()});
    ASSERT_EQ(R.exitCode, 0) << R.err;
    const auto Text = readFile(IR);
    EXPECT_NE(Text.find("@llvm.x86.rtm.abort.pair(i32 167)"),
              std::string::npos);
    EXPECT_NE(Text.find("convergent noduplicate nounwind"), std::string::npos);
    for (const char *Invalid :
         {" asm ", "willreturn", "nosync", "memory(none)"})
      EXPECT_EQ(Text.find(Invalid), std::string::npos) << Text;
  }
  for (const char *Code : {"256", "-1", "n"}) {
    writeFile(Source, "unsigned probe(unsigned n){return "
                      "__builtin_ia32_rtm_abort_pair(" +
                          std::string(Code) + ");}\n");
    const auto R = ncc(
        {"--target=i386-unknown-linux-gnu", "-fsyntax-only", Source.string()});
    EXPECT_NE(R.exitCode, 0) << R.err;
  }
}

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
