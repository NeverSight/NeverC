// RUN: %neverc %s -o %t && %t ; test $? -ne 139
// Two `goto *labels[...]` dispatch sites sharing the same `labels`
// table.  At `-O0` Clang emits two separate `load ptr` + `gep`
// sequences that converge at a multi-incoming phi before the single
// `indirectbr`.  The earlier IndirectBrPass only handled the
// single-load case, so this pattern fell through to "leftover
// BlockAddress" rejection.
//
// Fix: IndirectBrPass's pattern matcher now synthesises a phi of
// *indices* (one per dispatch site) and uses it as the `switch`
// selector.  The resulting switch has exactly one path per original
// `goto *` and the whole BlockAddress table disappears.
//
// Args (x=1, y=1): y > 0 -> labels[x%3] = labels[1] = l2 -> 20.

int main(int x, int y) {
    static const void *labels[] = { &&l1, &&l2, &&l3 };
    if (y > 0)
        goto *labels[x % 3];
    else
        goto *labels[(x + 1) % 3];
l1:
    return 10;
l2:
    return 20;
l3:
    return 30;
}
