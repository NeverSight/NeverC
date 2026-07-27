# WDK `api/` headers (kernel driver subset)

Trimmed for **kernel-mode drivers** (`-fms-kernel`, see `examples/windows-driver/`).

The bundled WDK is Windows 7 vintage (its own `sdkddkver.h` stopped at
`NTDDI_WIN7`); the bundled SDK next to it is Windows 11 era. They are not two
independent trees — kernel mode compiles against **both**.

## Include order and who owns what

`MSVC.cpp` appends `include/ddk` then `include/api` *after* the SDK paths, so
for any name present in both, the SDK copy wins:

| Path | Role in kernel mode |
|------|---------------------|
| `msvc/crt/include` | `sal.h`, `excpt.h`, `vcruntime.h` |
| `msvc/sdk/include/ucrt` | libc (`string.h`, `ctype.h`, …) |
| `msvc/sdk/include/um` | `winioctl.h` |
| `msvc/sdk/include/shared` | version macros (`sdkddkver.h`), base types (`basetsd.h`, `guiddef.h`), SAL specs, `pshpack*.h` / `poppack.h` |
| `wdk/include/ddk` | the kernel interfaces: `ntddk.h`, `wdm.h`, `ntifs.h`, `fltKernel.h`, … |
| `wdk/include/api` | only what the SDK does *not* provide (below) |

The practical consequence: `NTDDI_VERSION` defaults to the SDK's value
(Windows 11), while the kernel declarations are Windows 7's. Build kernel code
with `-DNTDDI_VERSION=0x06010000 -D_WIN32_WINNT=0x0601` to keep the two
consistent, as `examples/windows-driver/Makefile` does.

## What `api/` keeps (13 headers)

`ntdef.h`, `ntstatus.h`, `bugcodes.h`, `ntiologc.h`, `basetyps.h`,
`devioctl.h`, `dpfilter.h`, `evntprov.h`, `evntrace.h`, `ia64reg.h`,
`sal_supp.h`, `SpecStrings_supp.h`.

Headers the SDK also ships were removed rather than left to be shadowed: they
were unreachable, and the uppercase `PSHPACK1.H` / `POPPACK.H` spellings only
resolved at all on case-insensitive hosts. Include spellings across `ddk/` are
now exact-case so the tree builds the same way on Linux.

## What was removed from `ddk/`

User-mode Win32 (COMMCTRL, GDI+, SETUPAPI, …), D3D, DirectMusic, HID
miniport-only stacks, and other headers not reachable from `ntddk.h`.

There is no KMDF/WDF here, so `-fms-kernel` builds WDM drivers only, and
`getBundledWdkRoot` resolves x86_64 alone.
