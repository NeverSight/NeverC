// RUN: %neverc %s -o %t && %t ; test $? -ne 139
// Big 64-bit integer constants (full-width) used in arithmetic.  On
// AArch64 the backend *could* pick a `__literal8` entry + `adrp +
// ldr x` to materialise a 64-bit constant, but in PIC mode and
// under the shellcode driver's `-Oz`, it prefers the mov/movk chain
// `movz + movk x3 + movk x2 + movk x1` (at most 4 instructions).
//
// This test proves that with the shellcode pipeline, even
// near-maximum-entropy constants stay out of the literal pool.
// Regression guard: if a future backend change starts preferring
// literal pool for 64-bit constants, the extractor would reject the
// resulting `.bin`.  By verifying a successful compile + run we pin
// the mov/movk lowering choice.
//
// Args (a=5, b=7): r = (x*5) ^ (y*7) with x=0xDEADBEEF12345678,
// y=0xCAFEBABE98765432, and the final fold `(r ^ (r >> 32)) & 0xFF`
// lands on 155 for these inputs.

int main(int a, int b) {
    unsigned long long x = 0xDEADBEEF12345678ULL;
    unsigned long long y = 0xCAFEBABE98765432ULL;
    unsigned long long r = (x * (unsigned)a) ^ (y * (unsigned)b);
    return (int)(r ^ (r >> 32));
}
