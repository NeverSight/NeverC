// RUN: %neverc %s -o %t && %t ; test $? -ne 139
// Nested const table: an array of struct each holding a string
// pointer plus a pointer to another (const) array.  Exercises two
// levels of recursion inside Data2TextPass:
//   * sections[i].title points to a string literal (private ConstantDataArray).
//   * sections[i].tags points to tags_a / tags_b (another constant
//     GlobalVariable whose own initializer further contains string
//     pointers).
//
// getOrMaterialize visits each nested GV once per enclosing function
// via the per-(Function, GlobalValue) cache, so every string literal
// and every inner table ends up with exactly one alloca + initializer
// sequence per user function — no duplication even when referenced
// multiple times.
//
// For args (s=0, t=2): sections[0].tags[2].id (==3 for "blue") +
// sections[0].title[0] ('c' == 99) = 102.

struct tlv { int id; const char *name; };
struct section { const char *title; const struct tlv *tags; int count; };

static const struct tlv tags_a[] = {
    {1, "red"},
    {2, "green"},
    {3, "blue"},
};
static const struct tlv tags_b[] = {
    {10, "one"},
    {20, "two"},
};
static const struct section sections[] = {
    {"colors", tags_a, 3},
    {"numbers", tags_b, 2},
};

int main(int s, int t) {
    return sections[s].tags[t].id + sections[s].title[0];
}
