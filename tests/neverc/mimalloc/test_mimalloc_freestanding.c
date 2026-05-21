// Test: -ffreestanding should suppress -fbuiltin-mimalloc.
// RUN: %neverc -fbuiltin-mimalloc -ffreestanding -c %s -o %t.o

#ifdef __NEVERC_MIMALLOC__
#error "-ffreestanding should suppress -fbuiltin-mimalloc"
#endif

int main(void) { return 0; }
