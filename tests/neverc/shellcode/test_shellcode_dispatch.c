// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* Runtime dispatch via a switch — the idiomatic shellcode way to pick
 * one of several code paths.  Shows that even when the user wants
 * table-style polymorphism the pipeline produces a flat binary (no
 * __const pointer array, no late relocation):
 *
 *   op 0 -> inc(a)         = a + 1
 *   op 1 -> dbl(a)         = a * 2
 *   op 2 -> neg(a) & 0xff  = (-a) & 0xff
 *   op 3 -> xor1(a)        = a ^ 1
 *
 * Called with (2, 100) -> (-100) & 0xff = 0x9c = 156.
 */
static int inc(int x)  { return x + 1; }
static int dbl(int x)  { return x * 2; }
static int neg(int x)  { return (-x) & 0xff; }
static int xor1(int x) { return x ^ 1; }

int main(int op, int a) {
    switch (op) {
    case 0: return inc(a);
    case 1: return dbl(a);
    case 2: return neg(a);
    case 3: return xor1(a);
    default: return 0;
    }
}
