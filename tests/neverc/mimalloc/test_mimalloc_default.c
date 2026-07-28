// Test that mimalloc is injected by default on a hosted target, with no flag
// asking for it.
// RUN: %neverc -c %s -o %t.o

#ifndef __NEVERC_MIMALLOC__
#error "mimalloc should be on by default wherever there is a libc heap"
#endif

int main(void) { return 0; }
