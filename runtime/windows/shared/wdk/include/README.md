# Bundled WDK headers (kernel-mode drivers)

Windows Driver Kit **10.0.28000.0**, used when `-fms-kernel` is passed. Supports
**x64 and ARM64** WDM drivers.

## Layout

| Path | Contents |
|------|----------|
| `km/` | kernel interfaces — `ntddk.h`, `wdm.h`, `ntifs.h`, `fltKernel.h`, `ndis.h`, `storport.h`, … |
| `shared/` | the SDK headers `km/` is written against — `ntdef.h`, `sdkddkver.h`, `basetsd.h`, `guiddef.h`, pack pragmas, … |

`shared/` is vendored here rather than reused from
`runtime/windows/shared/msvc/sdk/include/shared`, because `km/` needs a newer
generation of it than the bundled user-mode SDK provides (`PUTF8_STRING`,
`CFORCEINLINE`, and similar). Keeping a kernel-side copy lets the two evolve
independently: user-mode compiles are unaffected by anything in this directory.

## Include order

`MSVC.cpp` injects these two directories **before** the bundled MSVC/SDK paths
when `-fms-kernel` is set, so `km/` resolves against the `shared/` next to it.
Kernel builds still fall through to the SDK afterwards for the C runtime
(`string.h`, `ctype.h`, …).

## What the driver does not have to declare

`-fms-kernel` derives the SDK-style architecture macro from the target triple
(`_AMD64_`, `_ARM64_`, `_X86_`) and already predefines `_KERNEL_MODE`, so a
driver only needs `--target=` plus `-fms-kernel`. `NTDDI_VERSION` defaults to
this WDK's own baseline (`NTDDI_WIN11_BR`); set it explicitly only to target an
older Windows.

## Import libraries

Per-architecture, alongside this tree:

- `runtime/windows/x64/wdk/lib/`
- `runtime/windows/arm64/wdk/lib/`

The two sets differ where the architecture does: `scsiport` and
`BufferOverflowK` are x64-only, `halextlib` is ARM64-only. Both carry
`ntoskrnl`, `hal`, `wdm`, `fltMgr`, `ndis`, `storport` and the rest of the
common set.

## Scope

WDM only — no KMDF/UMDF, so `wdf/` is not vendored. `getBundledWdkRoot`
resolves x86_64 and aarch64; other targets get no WDK paths.
