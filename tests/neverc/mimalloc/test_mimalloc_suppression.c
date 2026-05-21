// Test: -fno-builtin should suppress -fbuiltin-mimalloc.
// The __NEVERC_MIMALLOC__ macro should NOT be defined.
// RUN: %neverc -fbuiltin-mimalloc -fno-builtin -c %s -o %t.o

#ifdef __NEVERC_MIMALLOC__
#error "-fno-builtin should suppress -fbuiltin-mimalloc"
#endif

int main(void) { return 0; }
