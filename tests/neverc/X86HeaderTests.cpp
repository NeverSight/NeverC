#include "NeverCTestFixture.h"

#include "llvm/ADT/StringMap.h"
#include "llvm/TargetParser/Host.h"

class X86HeaderTest : public NeverCTest {};

namespace {
constexpr const char *ExtractSource = R"(
  #include <immintrin.h>
  __attribute__((noinline))
  __m128 observe(__m128 value, volatile unsigned *calls) {
    *calls = *calls + 1;
    return value;
  }
  #define EXTRACT(N) \
    __attribute__((noinline)) \
    unsigned lane##N(const unsigned *input, volatile unsigned *calls) { \
      __m128 value; \
      __builtin_memcpy(&value, input, sizeof(value)); \
      return (unsigned)_mm_extract_ps(observe(value, calls), N); \
    }
  EXTRACT(0)
  EXTRACT(1)
  EXTRACT(2)
  EXTRACT(3)
  int main(void) {
    const unsigned patterns[] = {
      0, 0x80000000, 0x7f800000, 0xff800000, 0x7fc12345, 0xffc54321,
      0x7f812345, 0xff854321, 1, 0x80000001, 0x3f800000, 0xbf800000
    };
    volatile unsigned calls = 0;
    unsigned state = 0x12345678;
    for (unsigned i = 0; i != 1036; ++i) {
      unsigned input[4];
      for (unsigned lane = 0; lane != 4; ++lane) {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        input[lane] = i < 12 ? patterns[(i + lane) % 12] : state;
      }
      if (lane0(input, &calls) != input[0] ||
          lane1(input, &calls) != input[1] ||
          lane2(input, &calls) != input[2] ||
          lane3(input, &calls) != input[3] || calls != 4 * (i + 1))
        return 1;
    }
    return 0;
  }
)";
} // namespace

TEST_F(X86HeaderTest, ExtractPsCompilesWithoutCxxBitCast) {
  const auto Source = tmpFile("extract.c");
  const auto IR = tmpFile("extract.ll");
  const auto Object = tmpFile("extract.o");
  writeFile(Source, ExtractSource);
  for (const char *Triple :
       {"x86_64-pc-windows-msvc", "x86_64-unknown-linux-gnu",
        "x86_64-apple-darwin"}) {
    for (const char *Opt : {"-O0", "-O2"}) {
      SCOPED_TRACE(Triple);
      SCOPED_TRACE(Opt);
      std::vector<std::string> Args = {std::string("--target=") + Triple,
                                       "-std=c23",
                                       "-msse4.1",
                                       Opt,
                                       "-ffreestanding",
                                       "-fno-builtin-mimalloc",
                                       "-fno-lto",
                                       "-Werror",
                                       "-mllvm",
                                       "-verify-machineinstrs",
                                       Source.string()};
      auto EmitIR = Args;
      EmitIR.insert(EmitIR.end(), {"-S", "-emit-llvm", "-o", IR.string()});
      auto Result = ncc(EmitIR);
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      const auto Text = readFile(IR);
      EXPECT_EQ(Text.find("fptosi"), std::string::npos) << Text;
      EXPECT_EQ(Text.find("fptoui"), std::string::npos) << Text;
      EXPECT_EQ(Text.find(" asm "), std::string::npos) << Text;
      Args.insert(Args.end(), {"-c", "-o", Object.string()});
      Result = ncc(Args);
      ASSERT_EQ(Result.exitCode, 0) << Result.err;
      EXPECT_GT(fileSize(Object), 0u);
    }
  }
}

TEST_F(X86HeaderTest, ExtractPsKeepsBitsAndEvaluatesInputOnce) {
  llvm::StringMap<bool, llvm::MallocAllocator> Features;
  if (!llvm::sys::getHostCPUFeatures(Features) || !Features.lookup("sse4.1"))
    GTEST_SKIP() << "native execution requires SSE4.1";
  const auto Source = tmpFile("extract.c");
  writeFile(Source, ExtractSource);
  for (const char *Opt : {"-O0", "-O2"})
    compileRunAndCheck(std::string("extract") + Opt, Source.string(),
                       std::string("-std=c23 -msse4.1 -fno-lto ") +
                           "-fno-builtin-mimalloc " + Opt);
}

TEST_F(X86HeaderTest, ExtractPsRequiresAnImmediateLaneInRange) {
  const auto Source = tmpFile("extract-invalid.c");
  for (const char *Lane : {"lane", "-1", "4"}) {
    SCOPED_TRACE(Lane);
    writeFile(Source, std::string("#include <immintrin.h>\n") +
                          "int f(__m128 value, int lane) { return " +
                          "_mm_extract_ps(value, " + Lane + "); }\n");
    const auto Result = ncc({"--target=x86_64-unknown-linux-gnu", "-std=c23",
                             "-msse4.1", "-fsyntax-only", Source.string()});
    EXPECT_NE(Result.exitCode, 0) << Result.err;
    EXPECT_NE(Result.err.find("__builtin_ia32_vec_ext_v4si"), std::string::npos)
        << Result.err;
  }
}
