// Single TU that overrides three library functions at once. Exercises the
// `.neverc.overrides` section containing multiple null-terminated entries
// and the IR-marker scan that has to register more than one symbol.
__attribute__((override)) int alpha(void) { return 10; }
__attribute__((override)) int beta(void)  { return 20; }
__attribute__((override)) int gamma(void) { return 30; }
