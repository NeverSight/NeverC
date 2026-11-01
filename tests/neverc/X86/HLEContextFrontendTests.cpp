#include "NativeBuiltinFixture.h"

class X86HLEContextTest : public X86NativeBuiltinTest {};

TEST_F(X86HLEContextTest, NativeMemoryAndPublicationAcrossFormats) {
  const auto Source = tmpFile("hle.c");
  writeFile(Source, R"(
#include <hlecontextintrin.h>
static_assert(sizeof(struct __hle_context) == 40);
void run(void *memory, struct __hle_context *s) {
  __hle_step(memory,s,__HLE_ADC,8,__HLE_ACQUIRE);
  __hle_step(memory,s,__HLE_XCHG,1,__HLE_ACQUIRE);
  __hle_step(memory,s,__HLE_CMPXCHG8B,8,__HLE_RELEASE);
  __hle_native(memory,s,__HLE_BTS,2,__HLE_ACQUIRE);
  __hle_native(memory,s,__HLE_MOV,8,__HLE_RELEASE);
}
)");
  for (const char *Target :
       {"--target=x86_64-unknown-linux-gnu", "--target=x86_64-pc-windows-msvc",
        "--target=x86_64-apple-darwin"})
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Target);
      SCOPED_TRACE(Opt);
      const auto Assembly = tmpFile("hle.s");
      auto Result =
          ncc({Target, Opt, "-std=c23", "-ffreestanding", "-fno-lto",
               "-fno-builtin-mimalloc", "-mllvm", "-verify-machineinstrs", "-S",
               Source.string(), "-o", Assembly.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Text = readFile(Assembly);
      EXPECT_EQ(Text.find("#APP"), std::string::npos) << Text;
      for (const char *Name : {"xacquire", "xrelease", "\tadc", "\txchg",
                               "cmpxchg8b", "\tbts", "pause", "lahf"})
        EXPECT_NE(Text.find(Name), std::string::npos) << Text;
      const auto IR = tmpFile("hle.ll");
      Result = ncc({Target, Opt, "-std=c23", "-ffreestanding", "-fno-lto",
                    "-fno-builtin-mimalloc", "-S", "-emit-llvm",
                    Source.string(), "-o", IR.string()});
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      EXPECT_NE(readFile(IR).find("@llvm.x86.hle.context("), std::string::npos);
      EXPECT_NE(readFile(IR).find("convergent noduplicate nounwind"),
                std::string::npos);
    }
}

TEST_F(X86HLEContextTest, RejectsInvalidWidthsHintsAndRuntimeControls) {
  const auto Source = tmpFile("hle-invalid.c");
  for (auto [Control, Bytes] : {std::pair{19u, 8u},
                                {0x400u, 8u},
                                {0x100u | 18u, 8u},
                                {3u, 1u},
                                {4u, 1u},
                                {5u, 1u},
                                {7u, 4u},
                                {0u, 0u},
                                {0u, 16u},
                                {0xffffffffu, 8u}}) {
    SCOPED_TRACE(Control);
    SCOPED_TRACE(Bytes);
    writeFile(Source, "void f(void *p){__builtin_ia32_hle_context(p,p,p," +
                          std::to_string(Control) + "u," +
                          std::to_string(Bytes) + "u);}\n");
    const auto Result = ncc({"-std=c23", "-fsyntax-only", Source.string()});
    EXPECT_EQ(Result.exitCode, 1) << Result.err;
    EXPECT_NE(Result.err.find("error:"), std::string::npos) << Result.err;
  }
  for (const char *Arguments : {"n,8", "0,n"}) {
    writeFile(
        Source,
        std::string(
            "void f(void*p,unsigned n){__builtin_ia32_hle_context(p,p,p,") +
            Arguments + ");}\n");
    EXPECT_NE(ncc({"-std=c23", "-fsyntax-only", Source.string()}).exitCode, 0);
  }
  writeFile(Source, "#include <hlecontextintrin.h>\n");
  const auto Result = ncc({"--target=aarch64-unknown-linux-gnu", "-std=c23",
                           "-ffreestanding", "-fsyntax-only", Source.string()});
  EXPECT_NE(Result.exitCode, 0);
  EXPECT_NE(Result.err.find("requires x86-64"), std::string::npos)
      << Result.err;
}

TEST_F(X86HLEContextTest, MicrosoftAliasesUseNativeCallsWithoutAssembly) {
  const char Source[] = R"C(
#include <intrin.h>
void f(long volatile *p, __int64 volatile *q, void *volatile *r) {
  (void)_InterlockedExchange_HLEAcquire(p,-17);
  (void)_InterlockedExchange_HLERelease(p,-17);
  (void)_InterlockedExchange64_HLEAcquire(q,-19);
  (void)_InterlockedExchange64_HLERelease(q,-19);
  (void)_InterlockedExchangeAdd_HLEAcquire(p,-17);
  (void)_InterlockedExchangeAdd_HLERelease(p,-17);
  (void)_InterlockedExchangeAdd64_HLEAcquire(q,-19);
  (void)_InterlockedExchangeAdd64_HLERelease(q,-19);
  (void)_InterlockedCompareExchange_HLEAcquire(p,-17,-17);
  (void)_InterlockedCompareExchange_HLERelease(p,-17,-17);
  (void)_InterlockedCompareExchange64_HLEAcquire(q,-19,-19);
  (void)_InterlockedCompareExchange64_HLERelease(q,-19,-19);
  (void)_InterlockedCompareExchangePointer_HLEAcquire(r,(void*)31,(void*)31);
  (void)_InterlockedCompareExchangePointer_HLERelease(r,(void*)31,(void*)31);
  _Store_HLERelease(p,11);
  _Store64_HLERelease(q,13);
  _StorePointer_HLERelease(r,(void*)17);
}
)C";
  for (const char *Opt : {"-O0", "-O2"}) {
    auto [Status, Assembly] = compileToAsm(Source, "hle-ms", Opt);
    ASSERT_EQ(Status, 0) << Assembly;
    EXPECT_EQ(Assembly.find("#APP"), std::string::npos) << Assembly;
    EXPECT_EQ(Assembly.find("callq\t_Interlocked"), std::string::npos)
        << Assembly;
    for (const char *Op : {"xacquire", "xrelease", "xadd", "xchg", "cmpxchg"})
      EXPECT_NE(Assembly.find(Op), std::string::npos) << Assembly;
    const auto Input = tmpFile("hle-ms.c"), IR = tmpFile("hle-ms.ll");
    writeFile(Input, Source);
    const auto Result =
        ncc({"--target=x86_64-pc-windows-msvc", Opt, "-ffreestanding",
             "-fno-lto", "-fno-builtin-mimalloc", "-S", "-emit-llvm",
             Input.string(), "-o", IR.string()});
    ASSERT_EQ(Result.exitCode, 0) << Result.err;
    EXPECT_EQ(readFile(IR).find("asm sideeffect"), std::string::npos);
    EXPECT_NE(readFile(IR).find("@llvm.x86.hle.context("), std::string::npos);
    for (const char *ControlWidth :
         {"i32 273, i32 4", "i32 273, i32 8", "i32 272, i32 4",
          "i32 272, i32 8", "i32 262, i32 4", "i32 262, i32 8", "i32 17, i32 4",
          "i32 17, i32 8", "i32 16, i32 4", "i32 16, i32 8", "i32 6, i32 4",
          "i32 6, i32 8", "i32 18, i32 4", "i32 18, i32 8"})
      EXPECT_NE(readFile(IR).find(ControlWidth), std::string::npos)
          << ControlWidth;
  }
}
