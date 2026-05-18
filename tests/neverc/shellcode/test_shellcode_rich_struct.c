// RUN: %neverc %s -o %t && %t ; test $? -ne 139
// "Command dispatch table" pattern: const struct containing a string
// pointer, a function pointer, and an integer, all in one record.
// Typical for mini-interpreters, menu systems, opcode handlers.
//
// Exercises the triple-path of Data2TextPass in one shot:
//   * string literal (`"double"`, `"triple"`) — recursive stackify
//     into per-function sub-allocas
//   * function pointer (`double_it`, `triple_it`) — stored direct
//     into the slot; the backend emits `adrp + add + str`, the
//     PAGE21 / PAGEOFF12 relocs patched by ShellcodeExtractor
//   * plain i32 (`weight`) — ConstantInt byte-written directly
//
// Args (i=0, x=5): entries[0] = {"double", double_it, 10}.
//   double_it(5)    = 10
//   weight          = 10
//   name[0] ('d')   = 100
//   total           = 120.

typedef int (*op_t)(int);

static int double_it(int x) { return x * 2; }
static int triple_it(int x) { return x * 3; }

struct entry {
    const char *name;
    op_t op;
    int weight;
};

static const struct entry entries[] = {
    {"double", double_it, 10},
    {"triple", triple_it, 20},
};

int main(int i, int x) {
    return entries[i].op(x) + entries[i].weight + entries[i].name[0];
}
