/*===---- neverc/kernel.h - kernel-mode shellcode entry header ------------===*\
 *
 * Pulled in by user source that wants to write ring-0 shellcode with
 * `-mshellcode-context=kernel`.  This header intentionally does NOT
 * emulate any OS-specific kernel SDK (no `wdm.h`, no `linux/module.h`,
 * no `IOKit/IOLib.h`): kernel ABIs diverge too much to hide behind a
 * single portable surface, and the NeverC ring-0 pipeline is stricter
 * than any of those SDKs anyway.
 *
 * Resolver contract
 * =================
 *
 * When KernelImportPass has to rewrite ordinary extern helper calls, it
 * prepends two hidden parameters to the shellcode entry function:
 *
 *     void shellcode_entry(void *resolver, void *cookie, <user params>...)
 *
 * The loader (hosting driver / kext / LKM) must invoke that rewritten
 * entry with a resolver function pointer and an opaque cookie:
 *
 *     // Example: Linux LKM loader
 *     typedef void *(*resolver_t)(uint64_t name_hash, void *cookie);
 *     shellcode_entry((void *)my_resolver, (void *)my_cookie);
 *
 * The resolver's contract for that automatic rewrite path:
 *     void *resolver(uint64_t name_hash, void *cookie)
 *
 *   - `name_hash` is the FNV-1a 64-bit hash of the kernel symbol name
 *     (e.g. hash("printk"), hash("kmalloc")).  The hash matches the
 *     `neverc_kern_hash()` function below.
 *   - `cookie` is forwarded verbatim from the entry's second parameter.
 *     The loader can use it to pass context (e.g. a pointer to a
 *     pre-built symbol table, a module handle, etc.).
 *   - The returned pointer is cast to the correct function type and
 *     called directly.  NULL means "symbol not found" and the
 *     shellcode will crash (no error handling in the generated wrapper).
 *
 * Per-OS resolver examples:
 *
 *   Linux / Android:
 *     void *resolver(uint64_t hash, void *cookie) {
 *         // Walk a pre-built table or call kallsyms_lookup_name
 *         for (struct sym *s = cookie; s->hash; ++s)
 *             if (s->hash == hash) return s->addr;
 *         return NULL;
 *     }
 *
 *   Windows (ring-0):
 *     Walk PsLoadedModuleList from KPCR, find ntoskrnl.exe, parse
 *     its PE export directory.  Same technique as the user-mode PEB
 *     walk but starting from KPCR (x86_64: gs:[0x188] -> KPRCB ->
 *     CurrentThread -> ...; aarch64: TPIDR_EL1).
 *
 *   macOS / XNU:
 *     Hosting kext calls OSKextLookupKextWithIdentifier or reads
 *     the kernel symbol table directly.
 *
 * Using `#include <neverc/kernel.h>` outside of kernel mode is an
 * error; user-mode payloads keep using the regular user-mode shims
 * (`<windows.h>`, `<unistd.h>`, `<string.h>`, ...).
 *
\*===----------------------------------------------------------------------===*/

#ifndef _NEVERC_KERNEL_H_
#define _NEVERC_KERNEL_H_

#if !defined(__NEVERC_SHELLCODE__)
#error "<neverc/kernel.h> is only meaningful with -fshellcode."
#endif

#if !defined(__NEVERC_SHELLCODE_KERNEL__)
#error                                                                         \
    "<neverc/kernel.h> requires -mshellcode-context=kernel. For ring-3 payloads include <windows.h> / <unistd.h> / <string.h> instead."
#endif

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void *(*neverc_kern_resolve_t)(uint64_t name_hash, void *cookie);

/* FNV-1a 64-bit hash.  Matches the hash KernelImportPass embeds as
 * constant operands in the generated resolver calls, so user code
 * can pre-compute keys for a loader-side hash table. */
static inline uint64_t neverc_kern_hash(const char *s) {
  uint64_t h = 0xcbf29ce484222325ull;
  while (*s) {
    h ^= (unsigned char)*s++;
    h *= 0x100000001b3ull;
  }
  return h;
}

#ifndef NEVERC_KERNEL_ENTRY
#define NEVERC_KERNEL_ENTRY __attribute__((used))
#endif

#ifdef __cplusplus
}
#endif

#endif /* _NEVERC_KERNEL_H_ */
