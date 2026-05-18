// RUN: %neverc %s -o %t && %t ; test $? -ne 139
// Floating-point SIMD: 4-lane float multiply + horizontal sum.  Same
// path as the int32 SIMD test, but exercising the float-vector side
// of `inlineVectorConstants`: each FP lane's bit pattern goes through
// the volatile i64 slot, then trunc + bitcast back to float, then
// insertelement.  No `__literal16` / `__literal8` reference survives.
//
// Args (a=3, b=4): x = {3.0, 4.0, 4.0, 5.0}, y = {1.5, 2.5, 3.5, 4.5},
// z = {4.5, 10.0, 14.0, 22.5}, sum = 51.0 -> (int)51.

typedef float v4f __attribute__((vector_size(16)));

int main(int a, int b) {
    v4f x = { (float)a, (float)b, (float)(a + 1), (float)(b + 1) };
    v4f y = { 1.5f, 2.5f, 3.5f, 4.5f };
    v4f z = x * y;
    return (int)(z[0] + z[1] + z[2] + z[3]);
}
