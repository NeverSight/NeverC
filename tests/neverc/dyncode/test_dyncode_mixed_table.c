// RUN: %neverc %s -o %t && %t ; test $? -ne 139
// Mixed table: ConstantStruct with both an integer and a char pointer
// field, inside a ConstantArray.  Exercises the recursive writeInto
// path that must resolve the char-pointer element through
// getOrMaterialize (stackifying the string literal per function) while
// writing the integer field as a plain i32 store.
//
// Usage: loader passes argv[1]=i, argv[2] unused; entry returns
// tags[i].name[0] + tags[i].weight.
// For i=1: tags[1] = {"beta", 20}, 'b' + 20 = 98 + 20 = 118.

struct tag {
    const char *name;
    int weight;
};

static const struct tag tags[] = {
    {"alpha", 10},
    {"beta", 20},
    {"gamma", 30},
};

int main(int i, int _) {
    (void)_;
    return (int)tags[i].name[0] + tags[i].weight;
}
