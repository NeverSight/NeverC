// RUN: %neverc %s -o %t && %t ; test $? -ne 139
/* Constant array of aggregates (struct / nested struct / int fields).
 * Previously Data2TextPass only knew how to flatten
 * `ConstantDataSequential` (plain char / int arrays) and
 * `ConstantAggregateZero`; anything with nested `ConstantStruct` would
 * survive as a global and blow up the extractor.
 *
 * With the recursive `flattenConstantTo` helper in place, the pipeline
 * now stackifies arbitrary user aggregates byte-by-byte from the
 * initializer layout, keeping the emitted shellcode flat.
 *
 * Called with (idx, unused):
 *   idx=0 -> arr[0].x * arr[0].y = 1 * 2  = 2
 *   idx=2 -> arr[2].x * arr[2].y = 5 * 6  = 30
 *   idx=3 -> arr[3].x * arr[3].y = 7 * 8  = 56
 */
struct pt { int x; int y; };
static const struct pt arr[4] = {{1, 2}, {3, 4}, {5, 6}, {7, 8}};

int main(int idx, int unused) {
    (void)unused;
    return arr[idx].x * arr[idx].y;
}
