// Test that __NEVERC_MIMALLOC__ is NOT defined without -fbuiltin-mimalloc.
// RUN: %neverc -c %s -o %t.o

#ifdef __NEVERC_MIMALLOC__
#error "__NEVERC_MIMALLOC__ should NOT be defined without -fbuiltin-mimalloc"
#endif

int main(void) { return 0; }
