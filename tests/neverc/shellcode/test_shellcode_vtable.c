// RUN: %neverc %s -o %t && %t ; test $? -ne 139
// C-style virtual dispatch: a const struct holding two function
// pointers, with the `current vtable` chosen at runtime.  This is
// exactly the shape compiled from a C++ polymorphic class's vtable,
// proving that object-oriented C / C++-without-runtime code can be
// turned into shellcode without any user-facing workaround.
//
// Data2TextPass handles three intertwined constructs here:
//   * A ConstantStruct of function pointers (vt_arith / vt_other).
//   * A per-function runtime branch picking which const struct GV to
//     alias — both branches stackify into the same entry block.
//   * Indirect calls through the freshly-stored slot (`vt->f0(...)`)
//     which naturally become `blr xN`.
//
// For args (type=0, v=5): add_impl(5,10) + mul_impl(5,2) = 15 + 10 = 25.

typedef int (*op_t)(int, int);
struct vtable { op_t f0; op_t f1; };

static int add_impl(int a, int b) { return a + b; }
static int mul_impl(int a, int b) { return a * b; }
static int sub_impl(int a, int b) { return a - b; }
static int div_impl(int a, int b) { return a / b; }

static const struct vtable vt_arith = { add_impl, mul_impl };
static const struct vtable vt_other = { sub_impl, div_impl };

int main(int type, int v) {
    const struct vtable *vt = (type == 0) ? &vt_arith : &vt_other;
    return vt->f0(v, 10) + vt->f1(v, 2);
}
