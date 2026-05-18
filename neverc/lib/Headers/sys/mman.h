/*===---- sys/mman.h - NeverC shellcode-oriented mmap shim ---------------===*\
|*
|* Normal mode forwards to the system header.
|* Shellcode mode provides a compact declaration/constant subset for
|* mmap/mprotect/munmap/madvise workflows used by loaders and payloads.
|*
\*===----------------------------------------------------------------------===*/

#ifndef _NEVERC_SYS_MMAN_SHIM_H_
#define _NEVERC_SYS_MMAN_SHIM_H_

#if defined(__NEVERC_SHELLCODE_KERNEL__)
#error                                                                         \
    "<sys/mman.h> is a user-mode header. In -mshellcode-context=kernel mode use vmalloc / ExAllocatePool2 / IOMalloc via the kernel resolver shim instead."
#endif

#if !defined(__NEVERC_SHELLCODE__)
#include_next <sys/mman.h>
#else

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

/* Protection flags. */
#define PROT_NONE 0x0
#define PROT_READ 0x1
#define PROT_WRITE 0x2
#define PROT_EXEC 0x4

/* Mapping flags.  Values differ between Darwin and Linux/Android;
 * the shim must match the kernel ABI the syscall hits. */
#define MAP_SHARED 0x01
#define MAP_PRIVATE 0x02
#define MAP_FIXED 0x10
#if defined(__APPLE__)
#define MAP_ANONYMOUS 0x1000
#define MAP_JIT 0x0800
#else
#define MAP_ANONYMOUS 0x20
#endif
#define MAP_ANON MAP_ANONYMOUS

/* madvise hints commonly referenced by shellcode loaders.  Values 0-4
 * are shared across Darwin and Linux; platform-specific hints diverge. */
#define MADV_NORMAL 0
#define MADV_RANDOM 1
#define MADV_SEQUENTIAL 2
#define MADV_WILLNEED 3
#define MADV_DONTNEED 4
#if defined(__APPLE__)
#define MADV_FREE 5
#else /* Linux / Android */
#define MADV_FREE 8
#define MADV_DONTFORK 10
#define MADV_DOFORK 11
#define MADV_UNMERGEABLE 13
#endif

void *mmap(void *addr, size_t length, int prot, int flags, int fd, long offset);
int munmap(void *addr, size_t length);
int mprotect(void *addr, size_t len, int prot);
int madvise(void *addr, size_t length, int advice);

#ifdef __cplusplus
}
#endif

#endif /* __NEVERC_SHELLCODE__ */
#endif /* _NEVERC_SYS_MMAN_SHIM_H_ */
