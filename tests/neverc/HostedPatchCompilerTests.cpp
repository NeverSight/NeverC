#include "NeverCTestFixture.h"

#include "llvm/ADT/StringExtras.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/SHA256.h"

namespace {
class HostedPatchCompilerTest : public NeverCTest {};

TEST_F(HostedPatchCompilerTest,
       HostedAuditHashesFinalRelocatedPayloadAndObject) {
  const auto Source = tmpFile("callback.c");
  writeFile(Source, R"(
typedef int (*Callback)(int);
__attribute__((noinline)) static int helper(Callback cb, int x) {
  volatile int value = cb(x); return value + 1;
}
int replacement(Callback cb, int x) {
  volatile int value = helper(cb, x); return value + 39;
})");
  auto Hash = [](const std::string &Bytes) {
    return llvm::toHex(
        llvm::SHA256::hash(llvm::ArrayRef<uint8_t>(
            reinterpret_cast<const uint8_t *>(Bytes.data()), Bytes.size())),
        true);
  };
  for (const std::string Triple :
       {"x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu",
        "x86_64-pc-windows-msvc", "aarch64-pc-windows-msvc",
        "x86_64-apple-macos14.0", "arm64-apple-macos14.0"}) {
    SCOPED_TRACE(Triple);
    const auto Output = tmpFile(Triple + ".bin");
    const auto Object = tmpFile(Triple + ".o");
    const auto Report = tmpFile(Triple + ".json");
    auto Result = ncc({"--no-default-config", "--target=" + Triple, "-fdyncode",
                       "-fdyncode-hosted", "-fdyncode-entry=replacement",
                       "-fdyncode-keep-obj=" + Object.string(),
                       "-fdyncode-report=" + Report.string(), "-o",
                       Output.string(), Source.string()});
    ASSERT_TRUE(Result.ok()) << Result.err;
    auto Parsed = llvm::json::parse(readFile(Report));
    ASSERT_TRUE(bool(Parsed));
    auto *Audit = Parsed->getAsObject();
    ASSERT_NE(Audit, nullptr);
    EXPECT_EQ(Audit->getString("input_digest"), Hash(readFile(Object)));
    EXPECT_EQ(Audit->getString("output_digest"), Hash(readFile(Output)));
    EXPECT_EQ(Audit->getString("execution_profile"), "hosted-v1");
    EXPECT_EQ(Audit->getString("target_triple"), Triple);
    EXPECT_TRUE(Audit->getBoolean("keep_unwind"));
  }
}

TEST_F(HostedPatchCompilerTest, CoreCppHasExplicitCrossTargetLayoutAndCExport) {
  const auto Source = tmpFile("replacement.cpp");
  writeFile(Source, R"(
struct Guard {
  int *p;
  Guard(int *q) : p(q) { *p += 3; }
  ~Guard() { *p += 5; }
};
extern "C" int replacement(int x) {
  int result = x;
  { Guard guard(&result); result *= 2; }
  return result + sizeof(long);
})");
  for (const std::string Triple :
       {"x86_64-unknown-linux-gnu", "aarch64-unknown-linux-gnu",
        "x86_64-pc-windows-msvc", "aarch64-pc-windows-msvc",
        "x86_64-apple-macos14.0", "arm64-apple-macos14.0"}) {
    SCOPED_TRACE(Triple);
    const auto Output = tmpFile(Triple + ".nc");
    auto Result = ncc({"translate", "--from", "cpp", Source.string(),
                       "--profile", "cpp-core-v2", "--target", Triple, "-o",
                       Output.string(), "--", "-std=c++17"});
    ASSERT_TRUE(Result.ok()) << Result.err;
    auto Manifest =
        llvm::json::parse(readFile(Output.string() + ".manifest.json"));
    ASSERT_TRUE(bool(Manifest));
    ASSERT_NE(Manifest->getAsObject(), nullptr);
    auto *Target = Manifest->getAsObject()->getObject("target");
    ASSERT_NE(Target, nullptr);
    EXPECT_EQ(Target->getString("triple"), Triple);
    int64_t PointerBits = 0;
    ASSERT_TRUE(Target->getInteger("pointer_bits", PointerBits));
    EXPECT_EQ(PointerBits, 64);
    EXPECT_EQ(Target->getBoolean("little_endian"), true);
    auto Compiled =
        ncc({"--no-default-config", "--target=" + Triple, "-std=c23", "-c",
             Output.string(), "-o", tmpFile("replacement.o").string()});
    ASSERT_TRUE(Compiled.ok()) << Compiled.err;
  }
}

TEST_F(HostedPatchCompilerTest,
       HostedDynCodeRejectsPersistentStateAndRuntimeCalls) {
  for (const std::string SourceText :
       {"static int value; int replacement(int x) { return ++value+x; }",
        "_Thread_local int value; int replacement(int x) { return value+x; }",
        "extern int write(int,const void*,unsigned long); int replacement(int "
        "x) { return write(1,&x,4); }",
        "extern void exit(int); int replacement(int x) { exit(x); return 0; }",
        "extern void *malloc(unsigned long); int replacement(int x) { return "
        "malloc(x)!=0; }"}) {
    SCOPED_TRACE(SourceText);
    auto Source = tmpFile("input.c"), Output = tmpFile("payload.bin");
    writeFile(Source, SourceText);
    auto Result =
        ncc({"--no-default-config", "--target=x86_64-unknown-linux-gnu",
             "-fdyncode", "-fdyncode-hosted", "-fdyncode-entry=replacement",
             "-fdyncode-keep-obj=" + tmpFile("payload.o").string(), "-o",
             Output.string(), Source.string()});
    EXPECT_FALSE(Result.ok()) << Result.err;
    EXPECT_FALSE(fs::exists(Output));
  }
}
} // namespace
