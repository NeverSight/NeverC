/*===---- neverc/xorstr.h - compile-time string encryption for C code -----===*\
 *
 * Wraps the __builtin_neverc_xorstr compiler builtin so that plain C
 * (and C++) source can encrypt string literals without relying on the
 * NeverC `.encrypt()` syntax extension.
 *
 * Usage:
 *     #include <neverc/xorstr.h>
 *     GetProcAddress(hModule, NC_XORSTR("NtQuerySystemInformation"));
 *
 * The macro expands to a `const char*` pointing to a stack-allocated
 * buffer that is XOR-decrypted at runtime and memset-zeroed before the
 * enclosing function returns.
 *
\*===----------------------------------------------------------------------===*/

#ifndef _NEVERC_XORSTR_H_
#define _NEVERC_XORSTR_H_

#include "xorstr_impl.inc"

#define NC_XORSTR(s) __builtin_neverc_xorstr(s)
#define NEVERC_XORSTR(s) __builtin_neverc_xorstr(s)

#endif /* _NEVERC_XORSTR_H_ */
