// RUN: %neverc -c %s -o %t.o
/*
 * Shellcode loader for macOS ARM64.
 *
 * Reads a flat .bin shellcode file, maps it as RWX, flushes the i-cache, and
 * calls the entry point.  The entry function signature is:
 *
 *   int entry(int arg0, int arg1)          -- for pure-computation tests
 *   void entry(void)                       -- for libSystem hello-world tests
 *
 * Usage:
 *   loader <shellcode.bin> [arg0] [arg1]
 *
 * Exit code is the shellcode's return value (or its exit() call for void
 * entries).  When called with two integer arguments, the entry is treated
 * as int(int,int); otherwise it's treated as void(void).
 *
 * Build:
 *   cc -o loader loader_arm64_macos.c
 *   codesign -s - --entitlements jit.entitlements -f loader
 */
#include <fcntl.h>
#include <libkern/OSCacheControl.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <shellcode.bin> [arg0] [arg1]\n", argv[0]);
        return 1;
    }

    const char *path = argv[1];
    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        perror("open");
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) < 0) {
        perror("fstat");
        close(fd);
        return 1;
    }
    size_t sz = (size_t)st.st_size;
    if (sz == 0) {
        fprintf(stderr, "error: %s is empty\n", path);
        close(fd);
        return 1;
    }

    /* Read the entire bin into a temporary buffer. */
    unsigned char *buf = malloc(sz);
    if (!buf) {
        perror("malloc");
        close(fd);
        return 1;
    }
    size_t nread = 0;
    while (nread < sz) {
        ssize_t r = read(fd, buf + nread, sz - nread);
        if (r <= 0) {
            perror("read");
            free(buf);
            close(fd);
            return 1;
        }
        nread += (size_t)r;
    }
    close(fd);

    /* Map RWX memory with MAP_JIT. */
    void *mem = mmap(NULL, sz, PROT_READ | PROT_WRITE | PROT_EXEC,
                     MAP_ANON | MAP_PRIVATE | MAP_JIT, -1, 0);
    if (mem == MAP_FAILED) {
        perror("mmap");
        free(buf);
        return 1;
    }

    /* On Apple Silicon, toggle the per-thread JIT write protection so we
       can write to the mapping. */
    pthread_jit_write_protect_np(0);

    __builtin_memcpy(mem, buf, sz);
    free(buf);

    /* Re-enable execute permission and flush the i-cache. */
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(mem, sz);

    int ret;
    if (argc >= 4) {
        /* int entry(int, int) */
        int a0 = atoi(argv[2]);
        int a1 = atoi(argv[3]);
        int (*fn)(int, int) = (int (*)(int, int))mem;
        ret = fn(a0, a1);
    } else {
        /* void entry(void) — use the process exit code set by the shellcode. */
        void (*fn)(void) = (void (*)(void))mem;
        fn();
        ret = 0;
    }

    munmap(mem, sz);
    return ret;
}
