/*===---- string.h - NeverC shellcode-oriented string.h shim -------------===*\
|*
|* Plain `string.h` shim.  In normal compilation mode we forward to the
|* host toolchain's `<string.h>` so user code sees the full libc surface.
|* In shellcode mode (`__NEVERC_SHELLCODE__`) we expose just the subset
|* that `MemIntrinPass` can lower to inline byte-loop helpers
|* (`__sc_mem*` / `__sc_str*`).  Users can keep writing ordinary
|* `#include <string.h>` code — no extern declarations, no custom length
|* macros — and the pipeline transparently routes every reference into
|* an `internal alwaysinline` IR helper.
|*
|* Design notes:
|*
|*   * `size_t` is taken from `<stddef.h>` so the signatures match what
|*     the host libc would expose, which keeps call-site compatibility
|*     intact across LLP64 (Windows) and LP64 (POSIX).  `MemIntrinPass`
|*     widens / narrows the size argument at the call site when the
|*     helper's `i64` slot disagrees with the user-declared width.
|*
|*   * No state-ful helpers (`strtok` / `strerror` / ...) appear here:
|*     they need runtime state the shellcode pipeline cannot materialise.
|*     User code that pulls those in still gets the original "unresolved
|*     external" diagnostic with a hint pointing at the inline
|*     alternative (see `ExtractorCommon::printExternHint`).
|*
\*===----------------------------------------------------------------------===*/

#ifndef _NEVERC_STRING_SHIM_H_
#define _NEVERC_STRING_SHIM_H_

#if !defined(__NEVERC_SHELLCODE__)
#include_next <string.h>
#else

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Memory helpers.  Every prototype matches the ISO C signature so the
 * extern declarations `MemIntrinPass` already intercepts line up with
 * user source. */
void *memcpy(void *dst, const void *src, size_t n);
void *memset(void *dst, int ch, size_t n);
void *memmove(void *dst, const void *src, size_t n);
int memcmp(const void *a, const void *b, size_t n);
void *memchr(const void *s, int c, size_t n);

/* BSD aliases that ship with every mainstream libc; routed through the
 * same helpers as the ISO names. */
int bcmp(const void *a, const void *b, size_t n);
void bzero(void *s, size_t n);

/* NUL-terminated byte-string helpers.  All are scan-until-NUL loops. */
size_t strlen(const char *s);
char *strcpy(char *dst, const char *src);
char *strncpy(char *dst, const char *src, size_t n);
int strcmp(const char *a, const char *b);
int strncmp(const char *a, const char *b, size_t n);
char *strcat(char *dst, const char *src);
char *strchr(const char *s, int c);
char *strrchr(const char *s, int c);

#ifdef __cplusplus
}
#endif

#endif /* __NEVERC_SHELLCODE__ */
#endif /* _NEVERC_STRING_SHIM_H_ */
