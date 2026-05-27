**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Shellcode Compiler](../README.md)

# Kernel-Mode (Ring-0) Shellcode Support

`-fshellcode` originally covered only ring-3 payloads (PEB walk, `svc`/`syscall` stubs, libSystem bridging). Ring-0 payloads (Windows drivers, Linux/Android kernel modules, macOS kexts) cannot simply reuse the ring-3 ABI: TEB/PEB does not exist, syscall instructions are user-to-kernel traps (drivers should not issue them), and x86_64 additionally requires a different code model and disabling the red zone.

## 1. Core Switch: `-mshellcode-context={user,kernel}`

- **User mode** (default): maintains existing PEB / syscall stub pipeline.
- **Kernel mode**:
  - Disables `SyscallStubPass` (`svc`/`syscall` are meaningless in ring-0).
  - Disables `WinPEBImportPass` (PEB lives in user TEB, unreachable from ring-0).
  - Windows kernel `TargetDesc` clears TCB/PEB read templates and syscall register descriptions.
  - Injects platform-specific driver flags (see [Section 3](#3-per-platform-driver-flag-differences)).
  - Enables `KernelImportPass` for automatic resolver-backed callsite rewriting ([Section 4](#4-kernelimportpass-automatic-resolver-injection)).
  - Injects `-D__NEVERC_SHELLCODE_KERNEL__=1` so user-mode shim headers (`<windows.h>` / `<unistd.h>` / etc.) issue `#error`, preventing accidental inclusion.

`-mshellcode-context=` is a strict enum: only `user` or `kernel`. Invalid values must fail at the driver stage to prevent typos from generating bytecode at the wrong privilege level.

## 2. `TargetDesc` New Fields

Execution level is an additional dimension of `describeTriple()`:

- `TargetDesc::Level`: `User` or `Kernel`.
- `TargetDesc::KernelImport`: `WindowsKernelResolverShim` / `LinuxKallsymsShim` / `DarwinXNUKextShim` / `None`. Only meaningful when `Level == Kernel`.
- `TargetDesc::KernelInjectFlags`: kernel-mode-only driver flag static array.

Adding kernel-mode support for a new OS/arch is still "add one table row".

## 3. Per-Platform Driver Flag Differences

| Dimension | x86_64 kernel | AArch64 kernel |
|-----------|---------------|----------------|
| Red zone | `-mno-red-zone` | Naturally absent |
| Code model | `-mcmodel=kernel` | Reuses existing `-mcmodel=small` |
| Implicit SIMD | `-mno-sse -mno-sse2 -mno-mmx` | `-mgeneral-regs-only` |
| Stack probe | Inherits existing `-mno-stack-arg-probe` | Same |

These flags stack as "user-mode baseline + kernel increment": cc1 processes later `-m...` flags as overrides.

## 4. `KernelImportPass`: Automatic Resolver Injection

Ring-0 symbol resolution varies significantly across platforms:

- Windows drivers: `PsLoadedModuleList` + `RtlFindExportedRoutineByName`, or `MmGetSystemRoutineAddress`.
- Linux/Android: `kallsyms_lookup_name` (5.7+ requires kprobe workaround), or ksymtab.
- macOS: `OSKextLookupKextWithIdentifier` + Mach-O symbol table.

`KernelImportPass` **automatically rewrites unresolved extern direct calls to resolver-backed indirect calls**. Users write normal C; the pass handles the rewriting.

### 4.1 Implicit Parameter Injection

When the module contains extern direct calls requiring a resolver, user code:
```c
void shellcode_entry(void) {
    printk("hello %d\n", 7);
}
```

is transformed to the equivalent of:
```c
void shellcode_entry(void *__resolver, void *__cookie) {
    void *fn = __resolver(hash("printk"), __cookie);
    ((int(*)(const char*, ...))fn)("hello %d\n", 7);
}
```

The user does not need to manually write resolver/cookie parameters — `KernelImportPass` injects them at the entry front. The loader passes them when calling the shellcode. Pure computation payloads or payloads that explicitly accept `neverc_kern_resolve_t` do not trigger this automatic prepend.

### 4.2 Callsite-Priority Rewriting

Each direct extern callsite is replaced in-place:
1. Load resolver and cookie from internal globals
2. Call `resolver(FNV1a_hash(bare_name), cookie)`
3. Cast returned `void*` to the correct function pointer type
4. Forward all original arguments; return the result

Callsite rewriting (rather than generic wrappers) is chosen to support variadic helpers like `printk("x=%d", v)`. Generic wrappers in LLVM IR cannot reliably forward anonymous variadic arguments.

Address-taken externs are not automatically wrapped; diagnostics direct the user to call helpers directly or have the loader pass pre-resolved function pointers.

### 4.3 Hash Algorithm

FNV-1a 64-bit, identical to `neverc_kern_hash()` in `<neverc/kernel.h>`:
```c
uint64_t h = 0xcbf29ce484222325ull;
while (*s) { h ^= (unsigned char)*s++; h *= 0x100000001b3ull; }
```

The pass strips leading underscores (Mach-O `_` prefix) before hashing for cross-platform consistency.

### 4.4 Loader Calling Convention

```c
typedef int (*Entry)(void *resolver, void *cookie /*, user params... */);
Entry e = (Entry)shellcode_memory;
e(my_resolver, my_cookie);
```

Under automatic resolver rewriting, the first two parameters are always `(resolver, cookie)`. If the user explicitly declares `shellcode_entry(neverc_kern_resolve_t resolver, void *cookie, ...)` and calls the resolver themselves, the entry signature is preserved as-is.

### 4.5 Three-Layer Defense

If `KernelImportPass` cannot complete rewriting (e.g., cannot find entry function, inline asm directly references symbols, third-party MIR pass re-inserts externs):
1. **IR layer**: pass outputs diagnostics or skips address-taken extern scenarios
2. **MIR layer**: `ShellcodeMIRPrepPass::auditExternalReferences` re-audits
3. **Extractor**: rejects all unresolved relocations

Users always see clear diagnostics; incorrect `.bin` files are never silently generated.

### 4.6 Kernel Helper Name Table-Driven Diagnostics

`Tables/KernelHelperNames.def` lists common ring-0 helpers for each OS (~120 Linux, ~120 Windows, ~60 Darwin). Both diagnostic paths query this table:

- **User mode**: when driver code is accidentally used in ring-3, the extractor emits `"'<name>' is a <os> kernel-only helper ... pass -mshellcode-context=kernel"`.
- **Kernel mode**: when inline asm bypasses `KernelImportPass`, the table identifies "you meant to resolve a kernel helper" and points to the appropriate `KernelImport` branch.

The table is user-extensible via CMake variable `NEVERC_EXTRA_KernelHelperNames`.

## 5. Android Kernel vs Android Ring-3

- Android **ring-3**: bionic + Linux syscall ABI (`svc #0`, x8=nr). Reuses Linux arm64 table entries.
- Android **ring-0**: pure Linux kernel (GKI/KMI). Reuses Linux kernel's `LinuxKallsymsShim` + `KernelInjectFlags`. The triple is kept separate for future GKI/KMI constraints.

## 6. Header File Division

| Mode | Allowed shim headers | Rejected shim headers |
|------|---------------------|----------------------|
| User-mode `-fshellcode` | `<windows.h>` / `<unistd.h>` / `<fcntl.h>` / `<sys/stat.h>` / `<sys/mman.h>` / `<string.h>` / `<stdlib.h>` | `<neverc/kernel.h>` |
| Kernel-mode `-mshellcode-context=kernel` | `<neverc/kernel.h>` / `<string.h>` / `<stdlib.h>` / `<stddef.h>` / `<stdint.h>` (pure type headers) | `<windows.h>` / `<unistd.h>` / `<fcntl.h>` / `<sys/stat.h>` / `<sys/mman.h>` |

`<neverc/kernel.h>` exposes:
- `neverc_kern_resolve_t` type alias
- `neverc_kern_hash()`: inline FNV-1a 64-bit hash, identical to the pass's internal algorithm
- `NEVERC_KERNEL_ENTRY`: kernel entry marker macro (equivalent to `__attribute__((used))`)

The shim layer intentionally does **not** emulate any OS kernel SDK — kernel ABI differences across platforms are too large for a pseudo-universal interface.

## 7. Writing Ring-0 Shellcode

### 7.1 Pure Computation Payload

```c
#include <neverc/kernel.h>

NEVERC_KERNEL_ENTRY
int shellcode_entry(int a, int b) {
    int s = a * 13 + b * 7;
    for (int i = 0; i < 4; ++i) s ^= (s << 3) + i;
    return s;
}
```

```bash
neverc -fshellcode -mshellcode-context=kernel \
       -target aarch64-linux-gnu shellcode.c -o sc.bin
```

### 7.2 Resolver-Based Payload (recommended for real drivers)

```c
#include <neverc/kernel.h>

typedef void (*PrintkFn)(const char *fmt, int a, int b);
typedef void *(*AllocFn)(unsigned int kind, unsigned long size);

NEVERC_KERNEL_ENTRY
int shellcode_entry(neverc_kern_resolve_t resolver, void *cookie,
                    int a, int b) {
    PrintkFn pf = (PrintkFn)resolver(neverc_kern_hash("printk"), cookie);
    AllocFn af = (AllocFn)resolver(neverc_kern_hash("kmalloc"), cookie);
    if (pf) pf("neverc: %d %d\n", a, b);
    void *blk = af ? af(0, 64) : (void *)0;
    (void)blk;
    return a * 13 + b * 7;
}
```

The first two parameters are `(neverc_kern_resolve_t resolver, void *cookie)`, prepared by the hosting driver / kext / LKM. The resolver implementation is hosting-side (Linux LKM wraps `kallsyms_lookup_name`; Windows driver wraps `MmGetSystemRoutineAddress`; XNU kext uses `OSKextLookup*`).

### 7.3 Hosting Driver Skeleton (Linux LKM example)

```c
struct sym_table { const char *name; void *addr; };
static struct sym_table st[] = {
    { "printk", (void*)printk },
    { "kmalloc", (void*)kmalloc },
    { NULL, NULL }
};

static uint64_t fnv1a(const char *s) {
    uint64_t h = 0xcbf29ce484222325ull;
    while (*s) { h ^= (unsigned char)*s++; h *= 0x100000001b3ull; }
    return h;
}
static void *my_resolver(uint64_t hash, void *cookie) {
    struct sym_table *tbl = cookie;
    for (int i = 0; tbl[i].name; ++i)
        if (fnv1a(tbl[i].name) == hash) return tbl[i].addr;
    return NULL;
}

typedef int (*Entry)(void *(*)(uint64_t, void*), void*, int, int);
Entry e = (Entry)kmem;
int result = e(my_resolver, st, 1, 2);
```

This is the **hosting driver's** code, compiled separately from the shellcode.

## 8. Roadmap

| Phase | Status | Content |
|-------|--------|---------|
| Kernel context switch + platform flags | Done | `-mshellcode-context=kernel`, `KernelInjectFlags`, pass gates |
| Resolver rewrite + diagnostic fallback | Done | `KernelImportPass` automatic callsite rewriting; MIR / extractor fallback |
| Ring-0 pure computation payload | Done | 8 triple coverage |
| Resolver-based payload | Done | `<neverc/kernel.h>` + stress test coverage |
| Automatic extern → resolver rewrite | Done | `KernelImportPass` with implicit parameter injection |
| Kernel SDK header subsets (`<linux/kernel.h>` / `<wdm.h>`) | Planned | To be added based on real driver payload needs |
