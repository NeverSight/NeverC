// Second override of `compute` — used to exercise the "two definitions both
// marked override" path. The linker should emit a warning and the definition
// seen later on the link line should win (matches --allow-multiple-definition
// for override symbols).
__attribute__((override))
int compute(void) { return 100; }
