#include "NeverCTestFixture.h"

class ShellcodeStressTest : public NeverCTest {};

TEST_F(ShellcodeStressTest, VolatileBarrier) {
  auto src = tmpFile("volatile_barrier.c");
  writeFile(src, R"(
static inline void compiler_barrier(void) {
    __asm__ volatile("" ::: "memory");
}
static volatile int g_flag;
int shellcode_entry(int a, int b) {
    volatile int local = a + b;
    compiler_barrier();
    g_flag = local;
    compiler_barrier();
    return g_flag + local;
}
int main(int a, int b) { return shellcode_entry(a, b); }
)");
  shellcodeCrossCompile("volatile_barrier", src.string());
}

TEST_F(ShellcodeStressTest, FnptrDispatch) {
  auto src = tmpFile("fnptr_dispatch.c");
  writeFile(src, R"(
static int op_add(int a, int b) { return a + b; }
static int op_sub(int a, int b) { return a - b; }
static int op_mul(int a, int b) { return a * b; }
static int op_xor(int a, int b) { return a ^ b; }
typedef int (*BinOp)(int, int);
static const BinOp kOps[] = { op_add, op_sub, op_mul, op_xor };
int shellcode_entry(int k, int x) {
    BinOp f = kOps[k & 3];
    return f(x, k);
}
int main(int a, int b) { return shellcode_entry(a, b); }
)");
  shellcodeCrossCompile("fnptr_dispatch", src.string());
}

TEST_F(ShellcodeStressTest, BitfieldsUnion) {
  auto src = tmpFile("bitfields_union.c");
  writeFile(src, R"(
typedef struct Packed { unsigned kind:4; unsigned flags:12; unsigned len:16; } Packed;
typedef union Mix { int as_int; unsigned char bytes[4]; Packed pk; } Mix;
int shellcode_entry(int a, int b) {
    Mix m = { .as_int = a ^ b };
    m.pk.kind = (unsigned)(a & 0xF);
    m.pk.flags = (unsigned)(b & 0xFFF);
    m.pk.len = (unsigned)((a + b) & 0xFFFF);
    int sum = 0;
    for (int i = 0; i < 4; ++i) sum += (int)m.bytes[i];
    return sum + (int)m.pk.kind + (int)m.pk.flags + (int)m.pk.len;
}
int main(int a, int b) { return shellcode_entry(a, b); }
)");
  shellcodeCrossCompile("bitfields_union", src.string());
}

TEST_F(ShellcodeStressTest, Int64Div) {
  auto src = tmpFile("int64_div.c");
  writeFile(src, R"(
int shellcode_entry(int a, int b) {
    unsigned long long au = (unsigned long long)(unsigned)a * 0x9E3779B97F4A7C15ULL;
    long long bi = (long long)a * (long long)b;
    if (b == 0) return 1;
    unsigned long long q = au / (unsigned long long)(unsigned)b;
    unsigned long long r = au % (unsigned long long)(unsigned)b;
    long long s = bi / (long long)b;
    long long t = bi % (long long)b;
    return (int)q ^ (int)r ^ (int)s ^ (int)t;
}
int main(int a, int b) { return shellcode_entry(a, b); }
)");
  shellcodeCrossCompile("int64_div", src.string());
}

TEST_F(ShellcodeStressTest, Builtins) {
  auto src = tmpFile("builtins.c");
  writeFile(src, R"(
int shellcode_entry(unsigned a, unsigned b) {
    unsigned s = 0;
    s += __builtin_popcount(a);
    s += (unsigned)__builtin_clz(a | 1);
    s += (unsigned)__builtin_ctz(a | 0x80000000u);
    s += __builtin_bswap32(a);
    s += (unsigned)__builtin_parity(b);
    unsigned long long d = __builtin_bswap64(((unsigned long long)a << 32) | b);
    if (__builtin_expect((int)(a + b) < 0, 0))
        return (int)(s ^ (unsigned)d ^ 0xDEADBEEFu);
    return (int)(s ^ (unsigned)d);
}
int main(int a, int b) { return shellcode_entry((unsigned)a, (unsigned)b); }
)");
  shellcodeCrossCompile("builtins", src.string());
}

TEST_F(ShellcodeStressTest, BigSwitch) {
  auto src = tmpFile("big_switch.c");
  std::string code = R"(
int shellcode_entry(int k, int v) {
    switch (k & 0x1F) {
)";
  for (int i = 0; i < 31; ++i)
    code += "        case " + std::to_string(i) + ": return v + " +
            std::to_string(i) + ";\n";
  code += R"(        default: return v ^ 0xCAFE;
    }
}
int main(int a, int b) { return shellcode_entry(a, b); }
)";
  writeFile(src, code);
  shellcodeCrossCompile("big_switch", src.string());
}

TEST_F(ShellcodeStressTest, AtomicOps) {
  auto src = tmpFile("atomic_ops.c");
  writeFile(src, R"(
static volatile int g_counter;
int shellcode_entry(int iters, int step) {
    int seen = 0;
    for (int i = 0; i < (iters & 0xFF); ++i)
        seen += __atomic_fetch_add(&g_counter, step, __ATOMIC_SEQ_CST);
    int expected = step;
    (void)__atomic_compare_exchange_n(&g_counter, &expected, 0, 0,
                                      __ATOMIC_SEQ_CST, __ATOMIC_SEQ_CST);
    return seen + expected + g_counter;
}
int main(int a, int b) { return shellcode_entry(a, b); }
)");
  shellcodeCrossCompile("atomic_ops", src.string());
}

TEST_F(ShellcodeStressTest, Varargs) {
  auto src = tmpFile("varargs.c");
  writeFile(src, R"(
#include <stdarg.h>
static int sum_n(int n, ...) {
    va_list ap;
    va_start(ap, n);
    int s = 0;
    for (int i = 0; i < n; ++i) s += va_arg(ap, int);
    va_end(ap);
    return s;
}
int shellcode_entry(int a, int b) {
    return sum_n(5, a, b, a + b, a - b, a * b);
}
int main(int a, int b) { return shellcode_entry(a, b); }
)");
  shellcodeCrossCompile("varargs", src.string());
}

TEST_F(ShellcodeStressTest, FloatMath) {
  auto src = tmpFile("float_math.c");
  writeFile(src, R"(
int shellcode_entry(int a, int b) {
    double x = (double)a * 3.1415926535 + (double)b;
    double y = (double)a / 2.0 - (double)b * 7.0;
    double z = x * x - y * y;
    z += (double)(a & 0xFF) * 1.5;
    return (int)z + (int)(z * 2.0);
}
int main(int a, int b) { return shellcode_entry(a, b); }
)");
  shellcodeCrossCompile("float_math", src.string());
}

TEST_F(ShellcodeStressTest, SretReturn) {
  auto src = tmpFile("sret_return.c");
  writeFile(src, R"(
typedef struct Big { int payload[16]; int checksum; } Big;
static Big make_big(int seed) {
    Big b; int c = 0;
    for (int i = 0; i < 16; ++i) { b.payload[i] = seed * (i + 1); c ^= b.payload[i]; }
    b.checksum = c;
    return b;
}
int shellcode_entry(int a, int b) {
    Big x = make_big(a); Big y = make_big(b);
    int sum = x.checksum + y.checksum;
    for (int i = 0; i < 16; ++i) sum += x.payload[i] - y.payload[i];
    return sum;
}
int main(int a, int b) { return shellcode_entry(a, b); }
)");
  shellcodeCrossCompile("sret_return", src.string());
}

TEST_F(ShellcodeStressTest, VLA) {
  auto src = tmpFile("vla.c");
  writeFile(src, R"(
int shellcode_entry(int n, int seed) {
    int count = (n & 0x1F) + 1;
    int buf[count];
    int sum = 0;
    for (int i = 0; i < count; ++i) buf[i] = seed + i * seed;
    for (int i = 0; i < count; ++i) sum ^= buf[i];
    return sum;
}
int main(int a, int b) { return shellcode_entry(a, b); }
)");
  shellcodeCrossCompile("vla", src.string());
}

TEST_F(ShellcodeStressTest, LongDouble) {
  auto src = tmpFile("long_double.c");
  writeFile(src, R"(
int shellcode_entry(int a, int b) {
    long double x = (long double)a + 1.5L;
    long double y = (long double)b * 2.25L;
    long double z = x * y + x - y;
    return (int)z;
}
int main(int a, int b) { return shellcode_entry(a, b); }
)");
  shellcodeCrossCompile("long_double", src.string());
}

TEST_F(ShellcodeStressTest, Unreachable) {
  auto src = tmpFile("unreachable.c");
  writeFile(src, R"(
int shellcode_entry(int a, int b) {
    switch (a & 3) {
        case 0: return a + b;
        case 1: return a - b;
        case 2: return a * b;
        case 3: return a ^ b;
        default: __builtin_unreachable();
    }
    if (b == 0xBAD) __builtin_trap();
    return 0;
}
int main(int a, int b) { return shellcode_entry(a, b); }
)");
  shellcodeCrossCompile("unreachable", src.string());
}
