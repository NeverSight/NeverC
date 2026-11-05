#include "i386_abi.h"

signed char scalar(signed char a, unsigned short b, _Bool c, int d) {
  return (signed char)((unsigned char)a ^ (b >> 8) ^ (unsigned)c ^ (unsigned)d);
}
U64 wide(U64 a, U64 b, unsigned c) { return (a ^ (b << 17)) + c; }
void *pointer(void *p, unsigned n) { return (char *)p + n; }
float floating(float a, double b) { return (float)(a + b); }
double doubling(double a, long double b) { return (double)(a + b); }
long double extended(long double a, long double b) { return a + b; }
struct Small small(struct Small a, int b) {
  a.y += b;
  return a;
}
struct Mixed mixed(struct Mixed a, char b, struct Small c) {
  a.x += b;
  a.y += c.y;
  a.z ^= 0x8765432112345678LL;
  return a;
}
struct Packed packed(struct Packed a, int b) {
  a.y += b;
  a.z ^= 0x1234;
  return a;
}
struct Aligned aligned(struct Aligned a, int b) {
  a.x += b;
  a.bytes[12] ^= 0x55;
  return a;
}
struct Nested nested(struct Nested a, struct Mixed b) {
  a.m.z ^= b.z;
  a.s.y += b.x;
  a.f += b.y;
  return a;
}
struct Bits bitfields(struct Bits a, unsigned n) {
  a.a ^= n;
  a.b = -a.b;
  a.c += n;
  return a;
}
union Union unions(union Union a, U64 mask) {
  a.bits ^= mask;
  return a;
}
CF complex_float(CF a, CF b) { return a + b; }
CD complex_double(CD a, CD b) { return a + b; }
CL complex_long_double(CL a, CL b) { return a + b; }
U97 bitint(U97 a, U97 b, unsigned n) { return (a ^ b) + n; }
V2 vector2(V2 a, V2 b) { return a + b; }
V4 vector4(V4 a, V4 b) { return a + b; }
long double variadic(unsigned n, ...) {
  __builtin_va_list ap, copy;
  __builtin_va_start(ap, n);
  __builtin_va_copy(copy, ap);
  long double sum = 0;
  for (unsigned i = 0; i < n; ++i) {
    int a = __builtin_va_arg(ap, int);
    U64 b = __builtin_va_arg(ap, U64);
    double c = __builtin_va_arg(ap, double);
    long double d = __builtin_va_arg(ap, long double);
    struct Mixed e = __builtin_va_arg(ap, struct Mixed);
    sum += a + (long double)b + c + d + e.x + e.y + e.z;
  }
  sum += __builtin_va_arg(copy, int);
  __builtin_va_end(copy);
  __builtin_va_end(ap);
  return sum;
}
struct Mixed variadic_record(int tag, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, tag);
  struct Mixed v = __builtin_va_arg(ap, struct Mixed);
  v.x += __builtin_va_arg(ap, int);
  v.z += __builtin_va_arg(ap, U64);
  __builtin_va_end(ap);
  return v;
}
int empty_argument(struct Empty a, int x, struct Empty b, int y) {
  return x + y;
}
int __attribute__((stdcall)) callee_cleanup(int a, int b, int c) {
  return a + b * 3 + c * 7;
}
int __attribute__((regparm(3))) registers(int a, int b, int c, int d) {
  return a + 3 * b + 7 * c + 11 * d;
}
U64 __attribute__((regparm(3))) register_wide(int a, U64 b, int c) {
  return b + (unsigned)a + (unsigned)c;
}
U97 __attribute__((regparm(3))) register_bitint(int a, U97 b, int c) {
  return b + (unsigned)a + (unsigned)c;
}
U64 __attribute__((regparm(3))) register_bitint65(U65 a, int b) {
  return (U64)(a >> 1) + (unsigned)b;
}
U64 __attribute__((regparm(3))) register_variadic(unsigned tag, ...) {
  __builtin_va_list ap;
  __builtin_va_start(ap, tag);
  U64 value = __builtin_va_arg(ap, U64);
  unsigned after = __builtin_va_arg(ap, unsigned);
  __builtin_va_end(ap);
  return value + tag + after;
}
struct Mixed __attribute__((regparm(3))) register_record(struct Small a, int b,
                                                         int c) {
  return (struct Mixed){a.x + b, a.y + c, 0x1234567887654321LL};
}
int __attribute__((regparm(3))) register_float(struct OneFloat a, int b, int c,
                                               int d) {
  return (int)a.x + b + 3 * c + 7 * d;
}
int callback(int (*fn)(int, int), int a, int b) { return fn(a, b) + 17; }

U97 variadic_bitint(unsigned tag, ...) {
  __builtin_va_list ap, copy;
  __builtin_va_start(ap, tag);
  int before = __builtin_va_arg(ap, int);
  __builtin_va_copy(copy, ap);
  U97 a = __builtin_va_arg(ap, U97);
  U65 b = __builtin_va_arg(ap, U65);
  int after = __builtin_va_arg(ap, int);
  U97 again = __builtin_va_arg(copy, U97);
  __builtin_va_end(copy);
  __builtin_va_end(ap);
  return (a == again ? (a ^ b) : 0) + tag + before + after;
}
