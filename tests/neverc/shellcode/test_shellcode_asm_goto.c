// RUN: %neverc %s -o %t && %t ; test $? -ne 139
// GCC's `asm goto` extension: an inline-asm block that can branch to
// one of the enumerated labels.  The backend emits the asm body
// verbatim and resolves the `%l[label]` placeholders to PC-relative
// branch instructions; no BlockAddress table, no relocation, no data
// section.  `asm goto` therefore compiles to shellcode without any
// pipeline assist — this test is a safety-net against accidental
// regressions in the surrounding pipeline that would break inline-asm
// handling.
//
// Args (x=0): takes the `b.eq %l[zero]` branch and returns 0.
// Args (x=7): falls through and returns 1.

int main(int x, int _) {
    (void)_;
    asm goto ("cmp %w0, #0\n\t"
              "b.eq %l[zero]"
              :: "r"(x) :: zero);
    return 1;
zero:
    return 0;
}
