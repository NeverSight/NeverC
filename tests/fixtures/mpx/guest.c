// Freestanding CPL0 oracle for generated native MPX operations under QEMU TCG.
// This checks the processor model, not successful execution on real hardware.
typedef unsigned long long U64;
typedef unsigned U32;
typedef unsigned short U16;
typedef unsigned char U8;
typedef void (*Operation)(U64, U64, U64, U64);
struct Case { Operation fn; U32 operation, bound, bits, prefix; };
#include "cases.inc"

static volatile U32 current_case, expected_vector, fault_count;
static volatile U64 output[2], bounds[2], alternative[2], condition;

static void out8(U16 port, U8 value) {
  __asm__ volatile("outb %0,%1" : : "a"(value), "d"(port));
}
static void print_hex(U32 value) {
  for (int shift = 28; shift >= 0; shift -= 4)
    out8(0xe9, "0123456789abcdef"[(value >> shift) & 15]);
}
__attribute__((noreturn)) static void finish(U32 value) {
  __asm__ volatile("outl %0,%1" : : "a"(value), "d"((U16)0xf4));
  for (;;) __asm__ volatile("hlt");
}
__attribute__((noreturn)) static void fail(U32 reason) {
  out8(0xe9, 'F'); print_hex(current_case); out8(0xe9, ':');
  print_hex(reason); out8(0xe9, ':');
  print_hex(output[0] >> 32); print_hex(output[0]); out8(0xe9, ':');
  print_hex(output[1] >> 32); print_hex(output[1]);
  out8(0xe9, '\n'); finish(1);
}
static void check(int predicate, U32 reason) {
  if (!predicate) fail(reason);
}
static void msr(U32 index, U64 value) {
  __asm__ volatile("wrmsr" : : "c"(index), "a"((U32)value), "d"((U32)(value >> 32)) : "memory");
}
// A zero directory base works on both hardware and QEMU 10.2, whose 64-bit
// lookup extracts BNDCFG[63:20] instead of the specified BNDCFG[63:12].
static void config(U32 flags) { msr(0xd90, flags); }

void guest_trap(U64 *frame) {
  U32 vector = frame[0];
  if (vector != expected_vector) fail(0x100 + vector);
  ++fault_count;
  const U8 *code = (const U8 *)frame[2];
  unsigned length = 0;
  for (;;) {
    U8 b = code[length];
    if (b == 0x26 || b == 0x2e || b == 0x36 || b == 0x3e ||
        b == 0x64 || b == 0x65 || b == 0x66 || b == 0x67 ||
        b == 0xf0 || b == 0xf2 || b == 0xf3 || (b >= 0x40 && b <= 0x4f))
      ++length;
    else break;
  }
  check(code[length] == 0x0f && (code[length + 1] == 0x1a || code[length + 1] == 0x1b), 0x200);
  length += 2;
  U8 modrm = code[length++];
  if ((modrm >> 6) != 3) {
    U8 base = modrm & 7;
    if (base == 4) base = code[length++] & 7;
    if ((modrm >> 6) == 1) ++length;
    else if ((modrm >> 6) == 2 || base == 5) length += 4;
  }
  frame[2] += length;
}

struct __attribute__((packed)) Gate { U16 lo, selector; U8 ist, flags; U16 mid; U32 hi, zero; };
struct __attribute__((packed)) Table { U16 limit; U64 base; };
extern void fault5(void), fault6(void), fault13(void), fault14(void);
static void set_gate(U32 number, void (*handler)(void)) {
  volatile struct Gate *gate = (volatile struct Gate *)0x600000 + number;
  U64 address = (U64)handler;
  *gate = (struct Gate){address, 0x18, 0, 0x8e, address >> 16, address >> 32, 0};
}
static void expect_bounds(U64 lower, U64 upper, U32 reason) {
  check(output[0] == lower && output[1] == upper, reason);
}

static int model_limited(const struct Case *test) {
#ifdef QEMU_MPX_LIMITATIONS
  // QEMU 10.2 applies ordinary address truncation to these memory operations,
  // rejects LOCK before decoding BNDMOV, and reads decode->mem for register
  // checks even though decode_modrm initializes only decode->op there.
  // Keep strict mode as the default.
  return test->operation == STORE_LOCK ||
      (test->operation >= CHECK_LOWER_REG && test->operation <= CHECK_RAW_REG) ||
      (test->bits == 32 && (test->operation == LOAD || test->operation == STORE ||
                           test->operation == LOAD_TABLE || test->operation == STORE_TABLE));
#else
  return 0;
#endif
}

void guest_main(void) {
  set_gate(5, fault5); set_gate(6, fault6); set_gate(13, fault13); set_gate(14, fault14);
  struct Table idt = {4095, 0x600000};
  __asm__ volatile("lidt %0" : : "m"(idt));
  msr(0xc0000100, 0x2000); msr(0xc0000101, 0x2000);
  bounds[0] = 0x100000100ULL; bounds[1] = ~0x100000200ULL;
  alternative[0] = 0x8877665544332211ULL; alternative[1] = 0xabcdef0123456789ULL;
  U32 executed = 0, skipped = 0;
  for (U32 i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    current_case = i;
    const struct Case *test = &cases[i];
    if (model_limited(test)) { ++skipped; continue; }
    ++executed;
    U64 segment = test->prefix == 0x64 || test->prefix == 0x65 ? 0x2000 : 0;
    volatile U64 *source = (volatile U64 *)0x100010000ULL;
    volatile U64 *destination = (volatile U64 *)0x100010100ULL;
    source[0] = bounds[0]; source[1] = bounds[1];
    destination[0] = destination[1] = 0x7777777777777777ULL;
    U64 base = 0x100000180ULL, value = 0x100000200ULL;
    if (test->operation == MAKE) base = bounds[0];
    if (test->operation == LOAD) base = (U64)source - segment;
    if (test->operation == STORE || test->operation == STORE_LOCK) base = (U64)destination - segment;
    if (test->operation == CHECK_RAW_REG || test->operation == CHECK_RAW_ADDRESS) base = bounds[1];
    U64 translated = 0x100009000ULL;
    volatile U64 *directory = (volatile U64 *)((translated >> 20) << 3);
    volatile U64 *entry = (volatile U64 *)(0x800000 + (((translated >> 3) & 0x1ffff) << 5));
    *directory = 0x800001;
    entry[0] = alternative[0]; entry[1] = alternative[1]; entry[2] = 0x8877665544332211ULL;
    if (test->operation == LOAD_TABLE || test->operation == STORE_TABLE) {
      base = translated - segment;
      value = entry[2];
    }
    config(3);
    expected_vector = 0; fault_count = 0;
    output[0] = output[1] = 0x7777777777777777ULL;
    test->fn(base, value, (U64)bounds, (U64)output);
    check(fault_count == 0, 1);
    if (test->operation == LOAD_TABLE) expect_bounds(alternative[0], alternative[1], 2);
    else expect_bounds(bounds[0], bounds[1], 3);
    if (test->operation == STORE || test->operation == STORE_LOCK)
      check(destination[0] == bounds[0] && destination[1] == bounds[1], 4);
    if (test->operation == STORE_TABLE)
      check(entry[0] == bounds[0] && entry[1] == bounds[1] && entry[2] == value, 5);
    if (test->operation >= CHECK_LOWER_REG && test->operation <= CHECK_RAW_ADDRESS) {
      U64 invalid = (test->operation == CHECK_LOWER_REG || test->operation == CHECK_LOWER_ADDRESS)
                        ? bounds[0] - 1 : (test->operation == CHECK_RAW_REG || test->operation == CHECK_RAW_ADDRESS)
                        ? bounds[1] + 1 : ~bounds[1] + 1;
      expected_vector = 5;
      test->fn(invalid, value, (U64)bounds, (U64)output);
      check(fault_count == 1, 6);
      expect_bounds(bounds[0], bounds[1], 7);
    }
    if (test->operation == LOAD_TABLE) {
      expected_vector = 0; fault_count = 0;
      test->fn(base, value ^ 1, (U64)bounds, (U64)output);
      expect_bounds(0, 0, 8);
    }
    if (test->operation == LOAD_TABLE || test->operation == STORE_TABLE) {
      *directory = 0;
      expected_vector = 5; fault_count = 0;
      test->fn(base, value, (U64)bounds, (U64)output);
      check(fault_count == 1, 9);
    }
    config(0);
    expected_vector = 0; fault_count = 0;
    output[0] = output[1] = 0x5555555555555555ULL;
    destination[0] = destination[1] = 0x7777777777777777ULL;
    test->fn(0xffffffffffffffffULL, value, (U64)bounds, (U64)output);
    expect_bounds(0x5555555555555555ULL, 0x5555555555555555ULL, 10);
    check(fault_count == 0, 11);
    check(destination[0] == 0x7777777777777777ULL && destination[1] == 0x7777777777777777ULL, 12);
  }
  current_case = sizeof(cases) / sizeof(cases[0]);
  for (U32 take = 0; take < 2; ++take) {
    condition = take;
    config(1);
    branch_probe((U64)alternative, (U64)&condition, (U64)bounds, (U64)output);
    if (take) expect_bounds(bounds[0], bounds[1], 13);
    else expect_bounds(alternative[0], alternative[1], 14);
  }
  config(3);
  expected_vector = 5; fault_count = 0;
  repeated_probe(0, 0, (U64)bounds, (U64)output);
  check(fault_count == 2, 15);
  config(3);
  expected_vector = 0;
  fault_count = 0;
  U64 branch_input[8], branch_output[8];
  for (unsigned bank = 0; bank < 4; ++bank) {
    branch_input[2 * bank] = 0x100 + 0x111 * bank;
    branch_input[2 * bank + 1] = ~(0x1000ULL + 0x111 * bank);
  }
  extern void branch_control_call(void (*)(U64, U64, U64, U64), U64 *, U64 *,
                                  U64);
  unsigned branch_executed = 0, branch_skipped = 0;
  for (unsigned kind = 0; kind < 2; ++kind)
    for (unsigned control = 0; control < 4; ++control) {
      if (control == 2)
        continue;
#ifdef QEMU_MPX_LIMITATIONS
      // QEMU 10.2.2 gen_JMP omits gen_bnd_jmp for E9 (and direct CALL).
      // Strict mode still checks the actual effect; byte tests require E9.
      if (kind == 0 && control == 1) {
        ++branch_skipped;
        continue;
      }
#endif
      ++branch_executed;
      branch_control_call(kind ? preserve_probe : initialize_probe,
                          branch_input, branch_output, control);
      for (unsigned word = 0; word < 8; ++word) {
        current_case =
            sizeof(cases) / sizeof(cases[0]) + kind * 32 + control * 8 + word;
        output[0] = branch_output[word];
        output[1] = kind || control != 1 ? branch_input[word] : 0;
        check(output[0] == output[1], 16);
      }
    }
  check(fault_count == 0, 17);
  out8(0xe9, 'P'); out8(0xe9, 'A'); out8(0xe9, 'S'); out8(0xe9, 'S');
  print_hex(executed); out8(0xe9, ':'); print_hex(skipped);
  out8(0xe9, ':');
  print_hex(branch_executed);
  out8(0xe9, ':');
  print_hex(branch_skipped);
  out8(0xe9, '\n'); finish(0);
}
