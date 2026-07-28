// Test that -fno-builtin-mimalloc turns the default injection back off, and
// with it __NEVERC_MIMALLOC__.
// RUN: %neverc -fno-builtin-mimalloc -c %s -o %t.o

#ifdef __NEVERC_MIMALLOC__
#error "-fno-builtin-mimalloc should suppress the default mimalloc injection"
#endif

int main(void) { return 0; }
