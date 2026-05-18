/*===---- stdlib.h - NeverC shellcode-oriented stdlib.h shim --------------===*\
|*
|* Forward to the system `<stdlib.h>` in normal builds.  In shellcode
|* mode (`__NEVERC_SHELLCODE__`) expose `exit` (lowered to the target's
|* native exit syscall or ExitProcess via PEB) plus the integer utility
|* subset that MemIntrinPass can lower to inline helpers.
|*
|* Allocation primitives (`malloc` / `free` / `calloc` / `realloc`)
|* are not provided; shellcode has no runtime heap.  Use `mmap` on
|* POSIX targets or `VirtualAlloc` on Windows targets instead.
|*
\*===----------------------------------------------------------------------===*/

#ifndef _NEVERC_STDLIB_SHIM_H_
#define _NEVERC_STDLIB_SHIM_H_

#if !defined(__NEVERC_SHELLCODE__)
#include_next <stdlib.h>
#else

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NULL
#define NULL ((void *)0)
#endif

#ifndef EXIT_SUCCESS
#define EXIT_SUCCESS 0
#endif
#ifndef EXIT_FAILURE
#define EXIT_FAILURE 1
#endif

void exit(int status);

/* ISO C integer helpers lowered to inline branchless selects. */
int abs(int x);
long labs(long x);
long long llabs(long long x);

#ifdef __cplusplus
}
#endif

#endif /* __NEVERC_SHELLCODE__ */
#endif /* _NEVERC_STDLIB_SHIM_H_ */
