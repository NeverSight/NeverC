// Weak override: combines `__attribute__((override))` with `__attribute__((weak))`.
// Even though the binding is weak, the override attribute must still win over
// the strong library definition because override is orthogonal to weak/strong.
__attribute__((override, weak))
int compute(void) { return 42; }
