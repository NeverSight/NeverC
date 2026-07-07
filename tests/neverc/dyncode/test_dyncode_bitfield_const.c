// RUN: %neverc %s -o %t && %t ; test $? -ne 139
// Bit-field constant table.  Protocol descriptors, instruction
// encoders, and similar domain code heavily use bit-field structs to
// compact multi-field data into a single machine word.  The Clang IR
// for `struct { a:3; b:5; c:8; d:16; }` packs all four fields into
// one i32 with shift/mask arithmetic at each read.
//
// Data2TextPass sees the ConstantStruct initializer as an i32
// ConstantInt for each entry; the per-field shift/mask needed to
// extract a/b/c/d happens in the loaded code, not in the table.
// The emitted `.bin` therefore contains the packed words as plain
// mov/movk chunks — no data section reference.
//
// Args (i=2, _=ignored): tbl[2] = {7, 31, 255, 65535}.
// 7 + 31 + 255 + 65535 = 65828, exit status (65828 mod 256) = 36.

struct flags {
    unsigned a : 3;
    unsigned b : 5;
    unsigned c : 8;
    unsigned d : 16;
};

static const struct flags tbl[] = {
    {1, 2, 3, 4},
    {5, 6, 7, 8},
    {7, 31, 255, 65535},
};

int main(int i, int _) {
    (void)_;
    return tbl[i].a + tbl[i].b + tbl[i].c + tbl[i].d;
}
