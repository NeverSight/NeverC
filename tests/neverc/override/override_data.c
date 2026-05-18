// Override of the global variable `g_config`. Exercises the variable code
// path (not just functions) of `__attribute__((override))`. The frontend
// records overrides for both functions and global variables, so a linker
// that only honored functions would fail this test.
__attribute__((override))
int g_config = 42;
