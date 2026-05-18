// Override translation unit: replaces `compute` from `lib_compute.c`. The
// `__attribute__((override))` annotation tells the linker that this is an
// intentional replacement, so the duplicate-symbol diagnostic is suppressed
// and this definition is selected.
__attribute__((override))
int compute(void) { return 42; }
