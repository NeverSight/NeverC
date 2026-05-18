// RUN: %neverc -c %s -o %t.o
/*
 * Shellcode loader for Linux / Android (arm64 and x86_64).
 *
 * Reads a flat `.bin` shellcode file, maps it RWX via mmap, flushes
 * the i-cache on arm64 (x86 has coherent caches), and calls the entry.
 *
 * Two calling conventions are exposed based on argc:
 *
 *   ./loader <shellcode.bin> <int0> <int1>
 *       -> treats entry as `int f(int, int)` and uses its return
 *          value as the process exit code.  Useful for the
 *          computation-only shellcodes that do not self-exit.
 *
 *   ./loader <shellcode.bin>
 *       -> treats entry as `void f(void)`; the shellcode is expected
 *          to terminate via the kernel (exit / exit_group syscall).
 *          The loader itself returns 0 if control ever falls through.
 *
 * Build (native):
 *   cc -O2 -o loader_linux loader_linux.c
 *
 * Android cross-build:
 *   $NDK/toolchains/llvm/prebuilt/darwin-x86_64/bin/aarch64-linux-android29-clang \
 *       -O2 -o loader_android loader_linux.c
 *
 * Tested platforms:
 *   - Ubuntu 22.04 arm64
 *   - Ubuntu 22.04 x86_64
 *   - Android 14 arm64 (needs MADV_UNMERGEABLE; SELinux may require
 *     `setenforce 0` or a policy allowing execmem for the test user).
 */
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

static void icache_flush(void *addr, size_t sz) {
#if defined(__aarch64__) || defined(__arm__)
    __builtin___clear_cache((char *)addr, (char *)addr + sz);
#else
    (void)addr;
    (void)sz;
#endif
}

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <shellcode.bin> [arg0] [arg1]\n", argv[0]);
        return 1;
    }
    const char *path = argv[1];
    int fd = open(path, O_RDONLY);
    if (fd < 0) { perror("open"); return 1; }

    struct stat st;
    if (fstat(fd, &st) < 0) { perror("fstat"); close(fd); return 1; }
    size_t sz = (size_t)st.st_size;
    if (sz == 0) { fprintf(stderr, "empty: %s\n", path); close(fd); return 1; }

    /* Round up to a page so mmap gets a whole-page allocation. */
    long psize = sysconf(_SC_PAGESIZE);
    size_t alloc_sz = ((sz + (size_t)psize - 1) / (size_t)psize) * (size_t)psize;
    void *mem = mmap(NULL, alloc_sz, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (mem == MAP_FAILED) { perror("mmap"); close(fd); return 1; }

    ssize_t n = 0, total = 0;
    while ((size_t)total < sz) {
        n = read(fd, (char *)mem + total, sz - (size_t)total);
        if (n <= 0) { perror("read"); munmap(mem, alloc_sz); close(fd); return 1; }
        total += n;
    }
    close(fd);

    icache_flush(mem, sz);

    int ret;
    if (argc >= 4) {
        int a0 = atoi(argv[2]);
        int a1 = atoi(argv[3]);
        int (*fn)(int, int) = (int (*)(int, int))mem;
        ret = fn(a0, a1);
    } else {
        void (*fn)(void) = (void (*)(void))mem;
        fn();
        ret = 0;
    }
    munmap(mem, alloc_sz);
    return ret;
}
