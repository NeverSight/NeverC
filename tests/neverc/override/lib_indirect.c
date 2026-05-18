// Library function `bar` whose body calls `foo`. Used by the indirect-call
// test: an override of `foo` in another TU should still affect the body of
// `bar` because at link time `bar`'s call target gets resolved to the
// overriding definition.
extern int foo(void);
int bar(void) { return foo() + 1000; }
