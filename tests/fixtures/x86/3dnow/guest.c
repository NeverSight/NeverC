// Compare compiled 3DNow values with original instruction bytes in QEMU TCG.
// This is a processor-model differential, not physical hardware qualification.
typedef unsigned long long U64;
typedef unsigned U32;
typedef unsigned short U16;
typedef unsigned char U8;
typedef U64 (*Operation)(U64, U64, const U64 *);
typedef U64 (*Reference)(U64, U64);
struct Case {
  Operation fn;
  Reference reference;
};
#include "cases.inc"

void *memcpy(void *destination, const void *source, __SIZE_TYPE__ count) {
  U8 *out = destination;
  const U8 *in = source;
  for (__SIZE_TYPE__ i = 0; i < count; ++i)
    out[i] = in[i];
  return destination;
}
void *memset(void *destination, int value, __SIZE_TYPE__ count) {
  U8 *out = destination;
  for (__SIZE_TYPE__ i = 0; i < count; ++i)
    out[i] = value;
  return destination;
}

static void put(U8 value) {
  __asm__ volatile("outb %0,%1" : : "a"(value), "d"((U16)0xe9));
}
static void hex(U32 value) {
  for (int shift = 28; shift >= 0; shift -= 4)
    put("0123456789abcdef"[(value >> shift) & 15]);
}
__attribute__((noreturn)) static void finish(U32 status) {
  __asm__ volatile("outl %0,%1" : : "a"(status), "d"((U16)0xf4));
  for (;;)
    __asm__ volatile("hlt");
}
__attribute__((noreturn)) static void fail(U32 index, U32 reason, U64 a,
                                           U64 b) {
  put('F');
  hex(index);
  put(':');
  hex(reason);
  put(':');
  hex(a >> 32);
  hex(a);
  put(':');
  hex(b >> 32);
  hex(b);
  put('\n');
  finish(1);
}

static void prepare(U16 control) {
  __asm__ volatile("fninit\n\tfldcw %0\n\tfld1" : : "m"(control) : "memory");
}
static void capture(U8 *state) {
#if defined(__x86_64__)
  __asm__ volatile("fxsave64 (%0)" : : "r"(state) : "memory");
#else
  __asm__ volatile("fxsave (%0)" : : "r"(state) : "memory");
#endif
}

void guest_main(void) {
  U32 a, b, c, d;
  __asm__ volatile("cpuid"
                   : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                   : "a"(0x80000001), "c"(0));
  if ((d & 0xc0000000U) != 0xc0000000U)
    fail(0, 1, d, 0xc0000000U);
  const U64 inputs[][2] = {{0, 0},
                           {0x8000000000000000ULL, 0x0000000080000000ULL},
                           {0x400000003f800000ULL, 0x4080000040400000ULL},
                           {0xc00000003fc00000ULL, 0x3f000000c0800000ULL},
                           {0x3f8000013f7fffffULL, 0x40000001407fffffULL},
                           {0x7f7fffff00800000ULL, 0x008000007f7fffffULL},
                           {0x0000000100800000ULL, 0x0080000000000001ULL},
                           {0x7fffffff80000000ULL, 0xffffffff00000001ULL}};
  U32 checked = 0;
  for (U32 rc = 0; rc < 4; ++rc)
    for (U32 i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i)
      for (U32 sample = 0; sample < sizeof(inputs) / sizeof(inputs[0]);
           ++sample) {
        U16 control = 0x37f | (rc << 10);
        U64 memory[] = {0x123456789abcdef0ULL, inputs[sample][1],
                        0xfedcba9876543210ULL};
        _Alignas(16) U8 expected_state[512], actual_state[512];
        prepare(control);
        U64 expected = cases[i].reference(inputs[sample][0], inputs[sample][1]);
        capture(expected_state);
        prepare(control);
        U64 actual =
            cases[i].fn(inputs[sample][0], inputs[sample][1], memory + 1);
        capture(actual_state);
        if (actual != expected)
          fail(i, 2 + sample * 8, expected, actual);
        for (U32 byte = 0; byte < 5; ++byte)
          if (actual_state[byte] != expected_state[byte])
            fail(i, 3 + sample * 8, byte, actual_state[byte]);
        if (actual_state[4])
          fail(i, 4 + sample * 8, 0, actual_state[4]);
        if (memory[0] != 0x123456789abcdef0ULL ||
            memory[1] != inputs[sample][1] ||
            memory[2] != 0xfedcba9876543210ULL)
          fail(i, 5 + sample * 8, memory[0], memory[2]);
        ++checked;
      }
  U32 contexts_checked = 0;
  for (U32 i = 0; i < sizeof(contexts) / sizeof(contexts[0]); ++i)
    for (U32 rc = 0; rc < 4; ++rc) {
      U64 memory[] = {0x123456789abcdef0ULL, 0x1122334455667788ULL,
                      0xa5a5a5a5a5a5a5a5ULL};
      _Alignas(16) U8 state[512];
      prepare(0x37f | (rc << 10));
      contexts[i](memory);
      capture(state);
      if (memory[0] != 0x123456789abcdef0ULL ||
          memory[1] != 0x3ff0000000000000ULL ||
          memory[2] != 0xa5a5a5a5a5a5a5a5ULL || state[4])
        fail(i, 0x100, memory[1], state[4]);
      ++contexts_checked;
    }
  put('P');
  put('A');
  put('S');
  put('S');
  hex(checked);
  put(':');
  hex(contexts_checked);
  put('\n');
  finish(0);
}
