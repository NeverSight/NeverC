// Test that __NEVERC_MIMALLOC__ is defined when -fbuiltin-mimalloc is used.
// RUN: %neverc -fbuiltin-mimalloc -c %s -o %t.o

#ifndef __NEVERC_MIMALLOC__
#error "__NEVERC_MIMALLOC__ should be defined with -fbuiltin-mimalloc"
#endif

int main(void) { return 0; }
