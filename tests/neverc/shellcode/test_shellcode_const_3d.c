// RUN: %neverc %s -o %t && %t ; test $? -ne 139
// Multi-dimensional const array (3-D `int[2][3][4]`) with runtime
// indices.  Common in DSP kernels, game physics LUTs, and block
// decoders.  Data2TextPass's `flattenConstantTo` / `writeInto`
// recursively flattens the nested ConstantArray-of-ConstantArray-of-
// ConstantArray structure into a single raw byte buffer, then emits
// chunk-stores (i64 / i32 / i8) into a per-function alloca of the
// same shape.  Multi-dim GEP continues to work through the alloca.
//
// Args (i=1, j=1): cube[1%2=1][1%3=1][(1+1)%4=2] = cube[1][1][2] = 19.

static const int cube[2][3][4] = {
    {{1,  2,  3,  4},  {5,  6,  7,  8},  {9,  10, 11, 12}},
    {{13, 14, 15, 16}, {17, 18, 19, 20}, {21, 22, 23, 24}},
};

int main(int i, int j) {
    return cube[i % 2][j % 3][(i + j) % 4];
}
