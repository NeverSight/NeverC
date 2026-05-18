// RUN: %neverc %s -o %t && %t ; test $? -ne 139
// GCC's computed-goto extension: `goto *labels[i];` with a static
// const BlockAddress table.  Out of the box the AArch64 backend would
// lower each `ptr blockaddress(@main, %Li)` through a `__DATA,__const`
// literal pool entry with an `ARM64_RELOC_UNSIGNED` load-time
// relocation, which shellcode cannot carry.
//
// IndirectBrPass rewrites this in IR *before* codegen sees it:
//   goto *labels[i]  ==>  switch i32 %i { case 0: %L0; case 1: %L1; ... }
//
// The `switch` is lowered under the shellcode driver's injected
// `-fno-jump-tables` into a compare-branch chain that needs no data
// section and no relocations, so the user's GCC-extension code
// compiles straight to relocation-free shellcode without them having
// to rewrite it.
//
// Args (x=4, _=ignored): x % 3 = 1 -> label l1 -> return 200.

int main(int x, int _) {
    (void)_;
    static const void *labels[] = { &&l0, &&l1, &&l2 };
    goto *labels[x % 3];
l0: return 100;
l1: return 200;
l2: return 50;
}
