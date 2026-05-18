// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* Floating-point math: LLVM might want to park the constants (3.14, 2.0)
 * in a `__literal4` / `__literal8` section and read them back through
 * `adrp + ldr`.  With Data2TextPass running twice and the zero-data
 * audit in the extractor we expect the constants to end up inlined as
 * `mov + movk` sequences into FP registers.
 *
 * Computes  (int)(a * 3.14 + b / 2.0)  and returns it.  With a=20, b=40:
 *   20 * 3.14 + 40 / 2.0 = 62.8 + 20.0 = 82.8 -> (int) 82.
 */
int main(int a, int b) {
    double x = (double)a * 3.14;
    double y = (double)b / 2.0;
    return (int)(x + y);
}
