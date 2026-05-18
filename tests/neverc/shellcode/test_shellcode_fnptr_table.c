// RUN: %neverc %s -o %t && %t ; test $? -ne 139
// Function pointer table: ordinary C code that would normally live in
// __DATA,__const as an array of pointers to other functions.
// Data2TextPass stackifies the outer table into a per-function alloca
// (one i64 slot per entry) and stores each callee address straight
// into the slot.  The AArch64 backend lowers each address with
// `adrp + add`, producing intra-section PAGE21 / PAGEOFF12 relocations
// that ShellcodeExtractor patches so the final flat binary has zero
// external references.
//
// Usage: loader passes argv[1]=i, argv[2]=x; entry returns table[i](x, x+1).
// For the test we call table[1](6, 7) -> mul2(6, 7) = 42.

static int add2(int a, int b) { return a + b; }
static int mul2(int a, int b) { return a * b; }
static int sub2(int a, int b) { return a - b; }

static int (*const table[])(int, int) = {add2, mul2, sub2};

int main(int i, int x) { return table[i](x, x + 1); }
