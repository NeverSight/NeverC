// Library default for `foo`. Paired with `override_foo.c` (returns 42) and
// `lib_indirect.c` (calls `foo` from inside `bar`).
int foo(void) { return 1; }
