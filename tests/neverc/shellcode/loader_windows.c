// RUN: %neverc -c %s -o %t.o
/*
 * Shellcode loader for Windows (x86_64 and arm64).
 *
 * Maps the shellcode RWX via VirtualAlloc, invalidates the i-cache on
 * arm64 (x86_64 is coherent), and calls the entry.  x86 has a unified
 * cache so no FlushInstructionCache is needed there, but we call it
 * unconditionally — cheap and future-proof.
 *
 * Two calling conventions are exposed based on argc:
 *
 *   loader.exe shellcode.bin 3 4
 *     -> `int f(int, int)` and uses the return value as the exit code.
 *   loader.exe shellcode.bin
 *     -> `void f(void)`; shellcode is expected to exit via the OS
 *        (ExitProcess / NtTerminateProcess).  Loader returns 0.
 *
 * Build with MSVC or clang-cl:
 *   cl /O2 loader_windows.c
 *   clang-cl /O2 loader_windows.c
 * Or with clang (MinGW/UCRT):
 *   clang --target=x86_64-pc-windows-msvc -O2 loader_windows.c -o loader.exe
 */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <shellcode.bin> [arg0] [arg1]\n", argv[0]);
        return 1;
    }
    const char *path = argv[1];
    HANDLE h = CreateFileA(path, GENERIC_READ, FILE_SHARE_READ, NULL,
                           OPEN_EXISTING, 0, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        fprintf(stderr, "open '%s' failed: %lu\n", path,
                (unsigned long)GetLastError());
        return 1;
    }
    LARGE_INTEGER sz;
    if (!GetFileSizeEx(h, &sz) || sz.QuadPart <= 0) {
        fprintf(stderr, "empty or unsized: %s\n", path);
        CloseHandle(h);
        return 1;
    }
    size_t bytes = (size_t)sz.QuadPart;

    /* VirtualAlloc with PAGE_EXECUTE_READWRITE is the classic
     * in-process shellcode mapping.  We could split RW + RX for
     * DEP-friendliness but the loader is a test harness, not a
     * production dropper. */
    void *mem = VirtualAlloc(NULL, bytes, MEM_COMMIT | MEM_RESERVE,
                             PAGE_EXECUTE_READWRITE);
    if (!mem) {
        fprintf(stderr, "VirtualAlloc failed: %lu\n",
                (unsigned long)GetLastError());
        CloseHandle(h);
        return 1;
    }
    DWORD read = 0;
    if (!ReadFile(h, mem, (DWORD)bytes, &read, NULL) || read != bytes) {
        fprintf(stderr, "ReadFile failed: %lu\n",
                (unsigned long)GetLastError());
        VirtualFree(mem, 0, MEM_RELEASE);
        CloseHandle(h);
        return 1;
    }
    CloseHandle(h);
    FlushInstructionCache(GetCurrentProcess(), mem, bytes);

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
    VirtualFree(mem, 0, MEM_RELEASE);
    return ret;
}
