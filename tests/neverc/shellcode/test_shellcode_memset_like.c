// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* Large stack buffer filled with a byte-wise loop, then summed.  This
 * exercises the edge case where Clang/LLVM might lower the loop into an
 * `llvm.memset` intrinsic.  The AArch64 backend should open-code the
 * memset when the size is known at compile time, keeping the shellcode
 * free of any external `memset` call.
 *
 * Fill a 64-byte buffer with the byte value 3, then sum the first 10
 * bytes -> 3 * 10 = 30.  Exit code: 30.
 */
int main(int a, int b) {
    (void)a; (void)b;
    unsigned char buf[64];
    for (int i = 0; i < 64; i++) buf[i] = 3;

    int sum = 0;
    for (int i = 0; i < 10; i++) sum += buf[i];
    return sum;
}
