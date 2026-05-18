// RUN: %neverc %s -o %t && %t ; test $? -ne 139
// Large sparse static table — 8 KB of zero-mostly data.  Real-world
// users write lookup tables like this for DFA/FSM implementations,
// ANSI code tables, Huffman decoders, etc.
//
// Data2TextPass's generic stackification flattens the entire 8192-byte
// initializer to a raw byte buffer, then emits a chunk-store cascade
// (i64 * 1024 for the aligned body, tails at i32 / i8).  The resulting
// `.bin` is dominated by the byte stream, not the control flow; our
// 4 KB budget here catches any regression that would bloat the
// per-byte encoding (e.g. reverting to 256-byte chunked stores that
// each take ~16 instructions).
//
// Args (i=500, _=ignored): table[500] == 11.

static const unsigned char table[8192] = {
    [100]  = 7,
    [500]  = 11,
    [5000] = 42,
    [8000] = 99,
};

int main(int i, int _) {
    (void)_;
    return table[i];
}
