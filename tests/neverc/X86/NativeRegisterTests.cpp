#include "NativeBuiltinFixture.h"

class X86NativeRegisterTest : public X86NativeBuiltinTest {};

TEST_F(X86NativeRegisterTest, CpuidPreservesRealignedFramesAndResults) {
#if !defined(__linux__) || !defined(__x86_64__)
  GTEST_SKIP() << "native CPUID comparison requires Linux x86-64";
#else
  const auto Source = tmpFile("native_register_frame.c");
  writeFile(Source, R"(
#include <cpuid.h>
#include <stdint.h>
__attribute__((noinline)) void touch(void *p) {
  *(volatile unsigned char *)p = 0xa5;
}
#define PROBE(name, abi) \
__attribute__((noinline, abi)) uint64_t name(unsigned n, int *out) { \
  volatile uint64_t fixed[16] __attribute__((aligned(64))); \
  fixed[0] = 0x12345678; fixed[15] = 0x76543210; \
  void *dynamic = __builtin_alloca(n); touch(dynamic); \
  __cpuidex(out, 0, 0); \
  touch(dynamic); \
  return fixed[0] ^ fixed[15]; \
}
PROBE(sysv_probe, sysv_abi)
PROBE(ms_probe, ms_abi)
int main(void) {
  unsigned expected[4];
  __cpuid_count(0, 0, expected[0], expected[1], expected[2], expected[3]);
  for (unsigned n = 65; n < 2048; n += 113) {
    int out[4];
    if (sysv_probe(n, out) != (0x12345678 ^ 0x76543210)) return 1;
    for (unsigned i = 0; i < 4; ++i)
      if ((unsigned)out[i] != expected[i]) return 2;
    if (ms_probe(n, out) != (0x12345678 ^ 0x76543210)) return 3;
    for (unsigned i = 0; i < 4; ++i)
      if ((unsigned)out[i] != expected[i]) return 4;
  }
  return 0;
}
)");
  for (const char *Opt : {"-O0", "-O2"})
    compileRunAndCheck(
        std::string("native_register_frame") + Opt, Source.string(),
        std::string(
            "-std=c23 -fms-extensions -fno-lto -fno-builtin-mimalloc ") +
            Opt);
#endif
}
