#ifndef NEVERC_TEST_I386_ABI_H
#define NEVERC_TEST_I386_ABI_H

_Static_assert(sizeof(void *) == 4 && sizeof(long) == 4, "ILP32");
_Static_assert(sizeof(long double) == 12 && _Alignof(long double) == 4, "x87");
_Static_assert(_Alignof(double) == 4 && _Alignof(long long) == 4,
               "stack alignment");
_Static_assert(__SIZEOF_POINTER__ == 4 && __SIZEOF_LONG_DOUBLE__ == 12,
               "macros");
#if !defined(__i386__) || defined(__x86_64__) || defined(__amd64__)
#error wrong architecture macros
#endif

typedef unsigned long long U64;
typedef _Complex float CF;
typedef _Complex double CD;
typedef _Complex long double CL;
typedef unsigned _BitInt(97) U97;
typedef unsigned _BitInt(65) U65;
typedef unsigned V2 __attribute__((vector_size(8)));
typedef unsigned V4 __attribute__((vector_size(16)));
struct Small {
  char x;
  short y;
};
struct Mixed {
  int x;
  double y;
  long long z;
};
struct Packed {
  char x;
  double y;
  short z;
} __attribute__((packed));
struct Aligned {
  int x;
  char bytes[13];
} __attribute__((aligned(32)));
struct Nested {
  struct Small s;
  struct Mixed m;
  long double f;
};
struct Bits {
  unsigned a : 5;
  signed b : 13;
  unsigned c : 14;
};
union Union {
  U64 bits;
  double number;
};
struct OneFloat {
  float x;
};
struct Empty {};

signed char scalar(signed char a, unsigned short b, _Bool c, int d);
U64 wide(U64 a, U64 b, unsigned c);
void *pointer(void *p, unsigned n);
float floating(float a, double b);
double doubling(double a, long double b);
long double extended(long double a, long double b);
struct Small small(struct Small a, int b);
struct Mixed mixed(struct Mixed a, char b, struct Small c);
struct Packed packed(struct Packed a, int b);
struct Aligned aligned(struct Aligned a, int b);
struct Nested nested(struct Nested a, struct Mixed b);
struct Bits bitfields(struct Bits a, unsigned n);
union Union unions(union Union a, U64 mask);
CF complex_float(CF a, CF b);
CD complex_double(CD a, CD b);
CL complex_long_double(CL a, CL b);
U97 bitint(U97 a, U97 b, unsigned n);
V2 vector2(V2 a, V2 b);
V4 vector4(V4 a, V4 b);
long double variadic(unsigned n, ...);
struct Mixed variadic_record(int tag, ...);
U97 variadic_bitint(unsigned tag, ...);
int empty_argument(struct Empty a, int x, struct Empty b, int y);
int __attribute__((stdcall)) callee_cleanup(int a, int b, int c);
int __attribute__((regparm(3))) registers(int a, int b, int c, int d);
U64 __attribute__((regparm(3))) register_wide(int a, U64 b, int c);
U97 __attribute__((regparm(3))) register_bitint(int a, U97 b, int c);
U64 __attribute__((regparm(3))) register_bitint65(U65 a, int b);
U64 __attribute__((regparm(3))) register_variadic(unsigned tag, ...);
struct Mixed __attribute__((regparm(3))) register_record(struct Small a, int b,
                                                         int c);
int __attribute__((regparm(3))) register_float(struct OneFloat a, int b, int c,
                                               int d);
int callback(int (*fn)(int, int), int a, int b);

#endif
