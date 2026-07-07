// RUN: %neverc -c %s -o %t.o
/*
 * DynCode loader for Windows (x86_64 and arm64).
 *
 * Maps the dyncode with proper W^X discipline: allocate RW, copy
 * the payload, then flip to RX before calling.  This avoids the RWX
 * artifact that anti-cheat / EDR memory scanners flag.
 *
 * Two calling conventions are exposed based on argc:
 *
 *   loader.exe dyncode.bin 3 4
 *     -> `int f(int, int)` and uses the return value as the exit code.
 *   loader.exe dyncode.bin
 *     -> `void f(void)`; dyncode is expected to exit via the OS
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
        fprintf(stderr, "usage: %s <dyncode.bin> [arg0] [arg1]\n", argv[0]);
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

    /* W^X: allocate RW, copy payload, then flip to RX.  The dyncode
     * .text blob is pure code after Data2TextPass — no runtime writes
     * to the code region are needed.  Data lives on the thread stack
     * (stackified globals) and in a separate RW arena (mmap). */
    void *mem = VirtualAlloc(NULL, bytes, MEM_COMMIT | MEM_RESERVE,
                             PAGE_READWRITE);
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

    DWORD old_prot;
    if (!VirtualProtect(mem, bytes, PAGE_EXECUTE_READ, &old_prot)) {
        fprintf(stderr, "VirtualProtect RX failed: %lu\n",
                (unsigned long)GetLastError());
        VirtualFree(mem, 0, MEM_RELEASE);
        return 1;
    }

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
    VirtualProtect(mem, bytes, PAGE_READWRITE, &old_prot);
    VirtualFree(mem, 0, MEM_RELEASE);
    return ret;
}
