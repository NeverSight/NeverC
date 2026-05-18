// User-supplied override of `foo`. The test that pairs this with
// `lib_indirect.c` verifies that the override is effective for *transitive*
// references: `bar` calls `foo`, and after linking that call must resolve
// to this definition (so bar() returns 42 + 1000 = 1042).
__attribute__((override))
int foo(void) { return 42; }
