#include "NeverCTestFixture.h"

class ShellcodeCrossTargetTest : public NeverCTest {};

TEST_F(ShellcodeCrossTargetTest, PureComputation) {
  auto src = tmpFile("cross_pure.c");
  writeFile(src, R"(
int shellcode_entry(int a, int b) {
    int acc = 0;
    for (int i = 0; i < 4; ++i) acc = acc * 31 + a + b * i;
    return acc;
}
)");
  shellcodeCrossCompile("pure", src.string());
}

TEST_F(ShellcodeCrossTargetTest, Echo) {
  auto src = tmpFile("cross_echo.c");
  writeFile(src, R"(
#include <unistd.h>
int main(void) { write(1, "OK\n", 3); _exit(0); return 0; }
)");
  shellcodeCrossCompile("echo", src.string());
}

TEST_F(ShellcodeCrossTargetTest, KRDeclarations) {
  auto src = tmpFile("cross_kr.c");
  writeFile(src, R"(
typedef __SIZE_TYPE__ size_t;
long write(int, const void *, size_t);
void _exit(int);
int main(void) { write(1, "KR\n", 3); _exit(0); return 0; }
)");
  shellcodeCrossCompile("kr", src.string());
}

TEST_F(ShellcodeCrossTargetTest, Heavy) {
  auto src = tmpFile("cross_heavy.c");
  writeFile(src, R"(
#include <string.h>
#include <unistd.h>
int main(void) {
    char dst[32];
    const char *src = "abcdefghijklmnop";
    memset(dst, 0, sizeof(dst));
    memcpy(dst, src, 16);
    if (memcmp(dst, src, 16) != 0) _exit(1);
    if ((int)strlen(dst) != 16) _exit(2);
    write(1, dst, 16);
    write(1, "\n", 1);
    _exit(0);
    return 0;
}
)");
  shellcodeCrossCompile("heavy", src.string());
}

TEST_F(ShellcodeCrossTargetTest, R0PureCompute) {
  auto src = tmpFile("cross_r0_pure.c");
  writeFile(src, R"(
int shellcode_entry(int seed) {
    int acc = seed;
    for (int i = 0; i < 5; ++i) acc = acc * 17 + 3;
    return acc;
}
)");
  shellcodeCrossCompileKernel("r0_pure", src.string());
}

TEST_F(ShellcodeCrossTargetTest, R0Extern) {
  auto src = tmpFile("cross_r0_extern.c");
  writeFile(src, R"(
#include <neverc/kernel.h>
extern void platform_log(const char *msg);
NEVERC_KERNEL_ENTRY
int shellcode_entry(int seed) {
    platform_log("hello from shellcode");
    return seed * 31;
}
)");
  shellcodeCrossCompileKernel("r0_extern", src.string());
}

TEST_F(ShellcodeCrossTargetTest, R3Atomics) {
  auto src = tmpFile("cross_r3_atomic.c");
  writeFile(src, R"(
int shellcode_entry(int seed) {
    int a = 0; long long b = 0;
    __atomic_store_n(&a, seed, __ATOMIC_SEQ_CST);
    int x = __atomic_load_n(&a, __ATOMIC_SEQ_CST);
    int y = __atomic_fetch_add(&a, 7, __ATOMIC_SEQ_CST);
    __atomic_store_n(&b, (long long)seed << 8, __ATOMIC_SEQ_CST);
    long long z = __atomic_fetch_or(&b, 0xff, __ATOMIC_SEQ_CST);
    return x + y + (int)(z >> 8);
}
)");
  shellcodeCrossCompile("r3_atomic", src.string());
  shellcodeCrossCompileKernel("r0_atomic", src.string());
}

TEST_F(ShellcodeCrossTargetTest, R3Float) {
  auto src = tmpFile("cross_r3_float.c");
  writeFile(src, R"(
int shellcode_entry(int a, int b) {
    double x = (double)a * 3.14;
    double y = (double)b / 2.0;
    float  z = (float)a * 1.5f + (float)b * 0.25f;
    return (int)(x + y + (double)z);
}
)");
  shellcodeCrossCompile("r3_float", src.string());
}

TEST_F(ShellcodeCrossTargetTest, R3Struct) {
  auto src = tmpFile("cross_struct.c");
  writeFile(src, R"(
struct point { int x, y; };
struct point make_pt(int x, int y) { struct point p = {x, y}; return p; }
int sum_pt(struct point p) { return p.x + p.y; }
int shellcode_entry(int a, int b) {
    struct point p = make_pt(a, b);
    return sum_pt(p) + p.x * p.y;
}
)");
  shellcodeCrossCompile("r3_struct", src.string());
  shellcodeCrossCompileKernel("r0_struct", src.string());
}

TEST_F(ShellcodeCrossTargetTest, R3Bitops) {
  auto src = tmpFile("cross_bitops.c");
  writeFile(src, R"(
int shellcode_entry(unsigned a, unsigned long long b) {
    int r = 0;
    r += __builtin_clz(a | 1);
    r += __builtin_ctz(a | 0x100);
    r += __builtin_popcount(a);
    r += __builtin_clzll(b | 1);
    r += __builtin_ctzll(b | 0x100ULL);
    r += __builtin_popcountll(b);
    return r;
}
)");
  shellcodeCrossCompile("r3_bitops", src.string());
  shellcodeCrossCompileKernel("r0_bitops", src.string());
}

TEST_F(ShellcodeCrossTargetTest, R3ThreadLocal) {
  auto src = tmpFile("cross_tls.c");
  writeFile(src, R"(
_Thread_local int counter = 0;
int shellcode_entry(int x) { counter += x; return counter * 3; }
)");
  shellcodeCrossCompile("r3_tls", src.string());
  shellcodeCrossCompileKernel("r0_tls", src.string());
}

TEST_F(ShellcodeCrossTargetTest, R3DesignatedInit) {
  auto src = tmpFile("cross_desig.c");
  writeFile(src, R"(
struct point { int x, y, z; };
int shellcode_entry(int a, int b) {
    struct point p = {.x = a, .y = b, .z = a + b};
    int sum = 0;
    int arr[5] = {[0] = a, [4] = b};
    for (int i = 0; i < 5; ++i) sum += arr[i];
    return p.x + p.y + p.z + sum;
}
)");
  shellcodeCrossCompile("r3_desig", src.string());
  shellcodeCrossCompileKernel("r0_desig", src.string());
}

TEST_F(ShellcodeCrossTargetTest, R3VLA) {
  auto src = tmpFile("cross_vla.c");
  writeFile(src, R"(
int shellcode_entry(int n) {
    int buf[n];
    for (int i = 0; i < n; ++i) buf[i] = i * 3 + 1;
    int s = 0;
    for (int i = 0; i < n; ++i) s += buf[i];
    return s;
}
)");
  shellcodeCrossCompile("r3_vla", src.string());
}

TEST_F(ShellcodeCrossTargetTest, OptMatrix) {
  auto src = tmpFile("cross_opt.c");
  writeFile(src, R"(
#include <unistd.h>
int shellcode_entry(void) {
    static const char m[] = "ok\n";
    write(1, m, 3);
    return 0;
}
)");
  static const char *triples[] = {
      "arm64-apple-macos",       "x86_64-apple-macos",
      "aarch64-linux-gnu",       "x86_64-linux-gnu",
      "aarch64-linux-android29", "x86_64-linux-android29",
      "aarch64-pc-windows-msvc", "x86_64-pc-windows-msvc",
  };
  for (auto *opt : {"-O0", "-O1", "-O2", "-O3", "-Os", "-Oz"}) {
    for (auto *triple : triples) {
      SCOPED_TRACE(std::string(opt) + " " + triple);
      auto bin = tmpFile(std::string("opt_") + opt + "_" + triple + ".bin");
      auto r = ncc({"-fshellcode", opt, "-target", triple, src.string(), "-o",
                    bin.string()});
      EXPECT_TRUE(r.exitCode == 0 && fs::exists(bin) && fileSize(bin) > 0)
          << opt << " " << triple << ": failed\n" << r.err;
    }
  }
}

// Negative diagnostics
TEST_F(ShellcodeCrossTargetTest, RejectLibm) {
  auto src = tmpFile("cross_libm.c");
  writeFile(src, R"(
double sqrt(double); double sin(double);
int shellcode_entry(int a) {
    return (int)(sqrt((double)a) + sin((double)a));
}
)");
  auto r = ncc({"-fshellcode", "-target", "aarch64-linux-gnu", src.string(),
                "-o", tmpFile("libm.bin").string()});
  EXPECT_NE(r.exitCode, 0);
  EXPECT_TRUE(r.stderrContains("libm transcendental") ||
              r.contains("libm transcendental"));
}

TEST_F(ShellcodeCrossTargetTest, RejectStdio) {
  auto src = tmpFile("cross_stdio.c");
  writeFile(src, R"(
int printf(const char *fmt, ...);
int shellcode_entry(int a) { printf("a=%d\n", a); return a; }
)");
  auto r = ncc({"-fshellcode", "-target", "aarch64-linux-gnu", src.string(),
                "-o", tmpFile("stdio.bin").string()});
  EXPECT_NE(r.exitCode, 0);
  EXPECT_TRUE(r.stderrContains("stdio call") || r.contains("stdio call"));
}

TEST_F(ShellcodeCrossTargetTest, RejectHeap) {
  auto src = tmpFile("cross_heap.c");
  writeFile(src, R"(
void *malloc(unsigned long); void free(void *);
int shellcode_entry(int n) {
    char *p = (char *)malloc((unsigned long)n);
    free(p);
    return 0;
}
)");
  auto r = ncc({"-fshellcode", "-fno-shellcode-heap-arena", "-target",
                "aarch64-linux-gnu", src.string(), "-o",
                tmpFile("heap.bin").string()});
  EXPECT_NE(r.exitCode, 0);
  EXPECT_TRUE(r.stderrContains("heap allocator") ||
              r.contains("heap allocator"));
}

TEST_F(ShellcodeCrossTargetTest, RejectConstructor) {
  auto src = tmpFile("cross_ctor.c");
  writeFile(src, R"(
__attribute__((constructor)) static void init(void) {}
int shellcode_entry(int x) { return x; }
)");
  auto r = ncc({"-fshellcode", "-target", "aarch64-linux-gnu", src.string(),
                "-o", tmpFile("ctor.bin").string()});
  EXPECT_NE(r.exitCode, 0);
  auto all = r.err + r.out;
  EXPECT_TRUE(all.find("global constructors are not allowed") !=
              std::string::npos);
}

TEST_F(ShellcodeCrossTargetTest, PrintOnlyModes) {
  auto src = tmpFile("cross_print_only.c");
  writeFile(src, "int shellcode_entry(int x) { return x + 1; }");
  for (auto *flag : {"-###", "-fdriver-only", "-ccc-print-phases",
                     "-ccc-print-bindings"}) {
    SCOPED_TRACE(flag);
    auto bin = tmpFile(std::string("print_only_") + flag + ".bin");
    auto r = ncc({"-fshellcode", flag, "-target", "aarch64-linux-gnu",
                  src.string(), "-o", bin.string()});
    EXPECT_FALSE(fs::exists(bin))
        << flag << " should not produce output file";
  }
}
