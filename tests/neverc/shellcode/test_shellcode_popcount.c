// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* Pure computation: popcount via loop + bit ops.
 * Expected: popcount(0xFF) == 8  →  exit code 8
 */
static int popcount(unsigned int x) {
    int count = 0;
    while (x) {
        count += x & 1;
        x >>= 1;
    }
    return count;
}

int main(int x, int unused) {
    return popcount((unsigned int)x);
}
