#include "i386_abi.h"
extern int printf(const char *, ...);
#define CHECK(x)                                                               \
  do {                                                                         \
    ++checks;                                                                  \
    if (!(x)) {                                                                \
      printf("line=%u input=%u\n", (unsigned)__LINE__, i);                     \
      return 1;                                                                \
    }                                                                          \
  } while (0)
static int cb(int a, int b) { return a * 9 + b * 5; }
int main(void) {
  unsigned checks = 0;
  unsigned char bytes[128];
  for (unsigned i = 0; i < 10000; ++i) {
    int n = (int)(i % 1000) - 500;
    U64 a = ((U64)i << 45) ^ 0xfedcba9876543210ULL;
    U64 b = ((U64)i << 7) ^ 0x123456789abcdef0ULL;
    CHECK((unsigned char)scalar((signed char)i, (unsigned short)(i * 37), i & 1,
                                n) ==
          (unsigned char)((unsigned char)i ^ ((unsigned short)(i * 37) >> 8) ^
                          (i & 1) ^ (unsigned)n));
    CHECK(wide(a, b, i) == (a ^ (b << 17)) + i);
    CHECK(pointer(bytes, i % 128) == bytes + i % 128);
    CHECK(floating(n, 0.5) == n + 0.5f);
    CHECK(doubling(n, 0.125L) == n + 0.125);
    CHECK(extended(1.0L + 0x1p-63L, 0x1p-63L) == 1.0L + 0x1p-62L);
    struct Small s = {(char)(i % 60), (short)n};
    struct Small sr = small(s, 7);
    CHECK(sr.x == s.x && sr.y == n + 7);
    struct Mixed m = {n, n + 0.5, (long long)a};
    struct Mixed mr = mixed(m, 3, s);
    CHECK(mr.x == n + 3 && mr.y == n + 0.5 + n &&
          (U64)mr.z == (a ^ 0x8765432112345678ULL));
    struct Packed p = {7, n + 0.25, (short)n};
    struct Packed pr = packed(p, 9);
    CHECK(pr.x == 7 && pr.y == n + 9.25 && pr.z == (short)(n ^ 0x1234));
    struct Aligned al = {n, {0}};
    al.bytes[12] = (char)(i % 127);
    struct Aligned ar = aligned(al, 13);
    CHECK(ar.x == n + 13 && ar.bytes[12] == (char)(al.bytes[12] ^ 0x55));
    struct Nested nest = {s, m, 1.0L + 0x1p-63L};
    struct Nested nr = nested(nest, m);
    CHECK(nr.m.z == 0 && nr.s.y == 2 * n && nr.f == nest.f + m.y);
    struct Bits bits = {i & 31, n, i};
    struct Bits br = bitfields(bits, 3);
    CHECK(br.a == (bits.a ^ 3) && br.b == -n && br.c == ((i + 3) & 16383));
    union Union u = {.bits = a};
    CHECK(unions(u, b).bits == (a ^ b));
    CF cf = (float)n + 2.0fi;
    CF cfr = complex_float(cf, 3.0f + 4.0fi);
    CHECK(__real__ cfr == n + 3.0f && __imag__ cfr == 6.0f);
    CD cd = (double)n + 2.0i;
    CD cdr = complex_double(cd, 3.0 + 4.0i);
    CHECK(__real__ cdr == n + 3.0 && __imag__ cdr == 6.0);
    CL cl = (long double)n + 2.0Li;
    CL clr = complex_long_double(cl, 3.0L + 4.0Li);
    CHECK(__real__ clr == n + 3.0L && __imag__ clr == 6.0L);
    U97 w = ((U97)1 << 96) | a, v = ((U97)i << 64) | b;
    CHECK(bitint(w, v, i) == (w ^ v) + i);
    CHECK(variadic_bitint(i, 7, w, (U65)v, 11) == (w ^ (U65)v) + i + 18);
    V2 v2 = {i, i + 1};
    V2 r2 = vector2(v2, (V2){11, 17});
    CHECK(r2[0] == i + 11 && r2[1] == i + 18);
    V4 v4 = {i, i + 1, i + 2, i + 3};
    V4 r4 = vector4(v4, (V4){11, 17, 23, 31});
    CHECK(r4[0] == i + 11 && r4[1] == i + 18 && r4[2] == i + 25 &&
          r4[3] == i + 34);
    struct Mixed vm = {n, 0.5, 12345};
    CHECK(variadic(2, n, (U64)123, 0.25, 0.125L, vm, n, (U64)123, 0.25, 0.125L,
                   vm) == 5 * n + 24937.75L);
    struct Mixed vr = variadic_record(7, vm, 19, (U64)0x1234567887654321ULL);
    CHECK(vr.x == n + 19 && vr.y == 0.5 &&
          (U64)vr.z == 12345 + 0x1234567887654321ULL);
    CHECK(empty_argument((struct Empty){}, n, (struct Empty){}, 23) == n + 23);
    CHECK(callee_cleanup(n, 7, 11) == n + 98);
    CHECK(registers(n, 7, 11, 13) == n + 241);
    CHECK(register_wide(n, a, 29) == a + (unsigned)n + 29);
    CHECK(register_bitint(n, w, 29) == w + (unsigned)n + 29);
    CHECK(register_bitint65((U65)v, n) == (U64)((U65)v >> 1) + (unsigned)n);
    CHECK(register_variadic(i, a, 29U) == a + i + 29);
    struct Mixed rr = register_record(s, 31, 37);
    CHECK(rr.x == s.x + 31 && rr.y == n + 37 && rr.z == 0x1234567887654321LL);
    CHECK(register_float((struct OneFloat){17}, n, 19, 23) == n + 235);
    CHECK(callback(cb, n, 7) == n * 9 + 52);
  }
  printf("checks=%u\n", checks);
  return 0;
}
