// Test: kernel-mode builds get no mimalloc, even by default -- its backend
// reaches for mmap/VirtualAlloc and thread-locals a kernel image does not have.
// Driven for -mkernel, -fms-kernel and -fandroid-kernel-driver-mode by
// MimallocTests.cpp.
// RUN: %neverc -mkernel -c %s -o %t.o

#ifdef __NEVERC_MIMALLOC__
#error "kernel mode must suppress mimalloc"
#endif

int main(void) { return 0; }
