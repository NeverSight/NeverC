// Same override semantics as override_compute.c but uses the Microsoft
// `__declspec(override)` spelling instead of the GCC `__attribute__` spelling.
// Both spellings must work because NeverC supports MSVC-style declarators.
__declspec(override)
int compute(void) { return 42; }
