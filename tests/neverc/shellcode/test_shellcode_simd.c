// RUN: %neverc %s -o %t && %t ; test $? -ne 139
// SIMD vector types via GCC's `vector_size` attribute: 4-lane int32
// multiply + horizontal sum.  Out-of-the-box the AArch64 backend
// lowers `mul <4 x i32> %x, <i32 1, i32 2, i32 3, i32 4>` through a
// `__literal16` constant pool load + `adrp + ldr q1`, which shellcode
// cannot carry (the constant pool lives outside __text).
//
// Data2TextPass's `inlineVectorConstants` walks every vector-typed
// Constant operand and replaces it with an `insertelement` chain
// built from per-lane volatile-loaded integer bit patterns.  The
// backend is forced to materialise each lane via `mov + movk + mov
// v.s[i], wN`, producing zero literal-pool references.
//
// Args (a=3, b=4): x = {3,4,4,5}, y = {1,2,3,4}, z = {3,8,12,20},
// sum = 43.

typedef int v4i __attribute__((vector_size(16)));

int main(int a, int b) {
    v4i x = { a, b, a + 1, b + 1 };
    v4i y = { 1, 2, 3, 4 };
    v4i z = x * y;
    return z[0] + z[1] + z[2] + z[3];
}
