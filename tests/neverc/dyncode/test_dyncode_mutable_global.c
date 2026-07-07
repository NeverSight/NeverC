// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* Mutable globals are transparently stackified.
 *
 * The user writes plain C with file-scope mutable variables:
 *
 *     static int counter = 10;
 *     static void bump(int step) { counter += step; }
 *
 * ZeroRelocPass (phase 2) moves `counter` into an alloca in the entry
 * function after AlwaysInliner has folded every helper into main, so
 * the emitted __text never references a __DATA segment.
 *
 * Run: bump(1) x3, set multiplier=2, bump(2), bump(5).
 *   counter progression: 10 -> 11 -> 12 -> 13 -> 17 -> 27.
 * main returns 27.
 */
static int counter = 10;
static int multiplier = 1;

static void bump(int step) {
    counter += step * multiplier;
}

int main(int a, int b) {
    (void)a; (void)b;
    bump(1);
    bump(1);
    bump(1);
    multiplier = 2;   /* second mutable global: also gets stackified */
    bump(2);          /* counter += 4 */
    bump(5);          /* counter += 10 */
    return counter;   /* 27 */
}
