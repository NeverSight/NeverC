/* Compare generated NeverD V11 against independent raw BND instructions.
 * SPDX-License-Identifier: AGPL-3.0-only
 */
void *memcpy(void *to, const void *from, __SIZE_TYPE__ size) {
  volatile unsigned char *out = to;
  const volatile unsigned char *in = from;
  for (__SIZE_TYPE__ i = 0; i < size; ++i)
    out[i] = in[i];
  return to;
}
void *memset(void *to, int value, __SIZE_TYPE__ size) {
  volatile unsigned char *out = to;
  for (__SIZE_TYPE__ i = 0; i < size; ++i)
    out[i] = value;
  return to;
}
int memcmp(const void *a, const void *b, __SIZE_TYPE__ size) {
  const volatile unsigned char *x = a, *y = b;
  for (__SIZE_TYPE__ i = 0; i < size; ++i)
    if (x[i] != y[i])
      return x[i] < y[i] ? -1 : 1;
  return 0;
}
static volatile unsigned current_case, current_seed, native_fault;
static uint8_t xstate[4096] __attribute__((aligned(64)));
static uint64_t input[4][2], output[4][2];
static void out8(unsigned port, unsigned value) {
  __asm__ volatile("outb %b0,%w1" : : "a"(value), "d"(port));
}
static void hex(unsigned value) {
  for (int bit = 28; bit >= 0; bit -= 4)
    out8(0xe9, "0123456789abcdef"[(value >> bit) & 15]);
}
__attribute__((noreturn)) static void finish(unsigned value) {
  __asm__ volatile("outl %0,%w1" : : "a"(value), "d"(0xf4));
  for (;;)
    __asm__ volatile("hlt");
}
static void check(int condition, unsigned line) {
  if (condition)
    return;
  out8(0xe9, 'F');
  hex(current_case);
  out8(0xe9, ':');
  hex(current_seed);
  out8(0xe9, ':');
  hex(line);
  out8(0xe9, '\n');
  finish(1);
}
#define CHECK(x) check((x), __LINE__)
static void msr(unsigned index, uint64_t value) {
  __asm__ volatile("wrmsr"
                   :
                   : "c"(index), "a"((unsigned)value),
                     "d"((unsigned)(value >> 32))
                   : "memory");
}
struct __attribute__((packed)) Gate {
  uint16_t lo, selector;
  uint8_t ist, flags;
  uint16_t mid;
  uint32_t hi, zero;
};
struct __attribute__((packed)) Table {
  uint16_t limit;
  uint64_t base;
};
extern void fault5(void), fault6(void), fault13(void), fault14(void);
static void gate(unsigned n, void (*handler)(void)) {
  volatile struct Gate *g = (volatile struct Gate *)0x600000 + n;
  uint64_t p = (uint64_t)handler;
  *g = (struct Gate){p, 0x18, 0, 0x8e, p >> 16, p >> 32, 0};
}
void guest_trap(uint64_t *frame) {
  CHECK(frame[0] == 5);
  ++native_fault;
  frame[2] += cases[current_case].size;
}
void guest_main(void) {
  gate(5, fault5);
  gate(6, fault6);
  gate(13, fault13);
  gate(14, fault14);
  struct Table idt = {4095, 0x600000};
  __asm__ volatile("lidt %0" : : "m"(idt));
  unsigned a, b, c, d;
  __asm__ volatile("cpuid"
                   : "=a"(a), "=b"(b), "=c"(c), "=d"(d)
                   : "a"(0xd), "c"(4));
  CHECK(a == 64 && b + 64 <= sizeof(xstate));
  unsigned status_offset = b + 8;
  unsigned pairs = 0;
  for (unsigned ci = 0; ci < sizeof(cases) / sizeof(*cases); ++ci)
    for (unsigned seed = 0; seed < 12; ++seed)
      for (unsigned enabled = 0; enabled < 2; ++enabled) {
        current_case = ci;
        current_seed = seed * 2 + enabled;
        unsigned op = cases[ci].op, prefix = cases[ci].prefix;
        for (unsigned i = 0; i < 4; ++i) {
          input[i][0] = 0x100000100ULL + i;
          input[i][1] = ~(0x100000200ULL + i);
        }
        const uint64_t translated = 0x100009000ULL;
        volatile uint64_t *directory = (volatile uint64_t *)0x8000;
        volatile uint64_t *table = (volatile uint64_t *)0x824000;
        volatile uint64_t *ram = (volatile uint64_t *)0x100010000ULL;
        const uint64_t initial_status = 0xabcd002;
        uint64_t lower = 0x1234567800011000ULL + seed, upper = ~lower;
        uint64_t rax = 0x100000100ULL + seed, rcx = seed;
        msr(0xc0000100, prefix == 2 ? 0x2000 : 0);
        uint64_t fs = prefix == 2 ? 0x2000 : 0;
        if (op == 2 || op == 3)
          rax = (uint64_t)ram - 16 - fs;
        if (op == 8)
          rax = seed & 1 ? 0 : 0x100000180ULL;
        if (op >= 9) {
          rax = translated - 16 - fs;
          rcx = 0x8877665544332211ULL;
        }
        if (!enabled && op < 9)
          rax = UINT64_MAX;
        uint64_t pointer = op == 12 ? 0 : rcx;
        uint64_t stored = seed & 1 ? pointer : ~pointer;
        uint64_t bde = seed % 3 ? 0x800001 : 0;
        *directory = bde;
        table[0] = lower;
        table[1] = upper;
        table[2] = stored;
        table[3] = 0xfeedface;
        ram[0] = lower;
        ram[1] = upper;
        ram[2] = 0x11223344;
        memset(xstate, 0, sizeof(xstate));
        *(uint64_t *)(xstate + 512) = 16;
        *(uint64_t *)(xstate + status_offset) = initial_status;
        native_fault = 0;
        cases[ci].fn(rax, rcx, input, output, enabled ? 3 : 0, xstate);
        uint64_t native_status = *(uint64_t *)(xstate + status_offset);
        uint64_t native_ram[3], native_table[4];
        memcpy(native_ram, (const void *)ram, sizeof(native_ram));
        memcpy(native_table, (const void *)table, sizeof(native_table));
        *directory = bde;
        table[0] = lower;
        table[1] = upper;
        table[2] = stored;
        table[3] = 0xfeedface;
        ram[0] = lower;
        ram[1] = upper;
        ram[2] = 0x11223344;
        neverd_x64_state_v11 s = {0};
        s.PC = cases[ci].pc;
        s.RFlags = 2;
        s.MXCSR = 0x1f80;
        s.MPXSupported = 1;
        s.OSXSAVE = 1;
        s.XCR0 = 27;
        s.BNDCFGS = enabled ? 3 : 0;
        s.BNDSTATUS = initial_status;
        s.GPR[0] = rax;
        s.GPR[1] = rcx;
        s.FSBase = fs;
        memcpy(s.Bounds, input, sizeof(input));
        neverd_x64_state_v11 expected = s;
        memcpy(expected.Bounds, output, sizeof(output));
        expected.BNDSTATUS = native_status;
        if (!native_fault)
          expected.PC += cases[ci].size;
        uint64_t budget = 1;
        CHECK(neverd_x64_continue_v11(&s, &budget) == (native_fault ? 14 : 9));
        CHECK(budget == 0 && memcmp(&s, &expected, sizeof(s)) == 0);
        CHECK(memcmp((const void *)ram, native_ram, sizeof(native_ram)) == 0);
        CHECK(memcmp((const void *)table, native_table, sizeof(native_table)) ==
              0);
        CHECK(*directory == bde);
        ++pairs;
      }
  out8(0xe9, 'P');
  out8(0xe9, 'A');
  out8(0xe9, 'S');
  out8(0xe9, 'S');
  hex(pairs);
  out8(0xe9, '\n');
  finish(0);
}
