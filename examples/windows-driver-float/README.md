**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Windows Kernel Driver with Floating-Point

A WDM kernel driver built with NeverC that demonstrates **safe use of
floating-point / SIMD in kernel mode**. Cross-compiles from macOS / Linux.

## Build

```bash
cd examples/windows-driver-float
make
```

From a standalone NeverC release:

```bash
make NEVERC=/path/to/neverc
```

The output is `FloatDriver.sys` (auto-LTO optimized).
Default build includes `-g` for debugging; remove `-g` for release builds.

---

## Two issues to handle

Kernel-mode floating-point has two distinct problems:

### Issue 1 — the `_fltused` ABI marker (compile/link time)

MSVC's compiler emits an undefined reference to the symbol `_fltused`
whenever a translation unit performs any floating-point operation. In
user-mode programs, `libcmt.lib` provides this symbol so the linker is
happy and a few FP-specific CRT bits get pulled in.

Kernel drivers do **not** link against `libcmt` (we pass `-nostdlib`
and `-Xlinker --nodefaultlib`), so an unresolved `_fltused` would cause
a link error.

**How NeverC solves it**: with `-fms-kernel`, LLVM's X86 backend defines
`_fltused` locally as 0. You can see this in the generated assembly:

```asm
# User-mode target:
    .globl  _fltused              # external reference -- needs libcmt
```

```asm
# -fms-kernel target:
    .globl  _fltused
    .set    _fltused, 0           # locally defined! no external symbol needed
```

So you **never have to manually `int _fltused = 0;`** in your driver.

### Issue 2 — kernel does NOT preserve FP/SIMD registers (runtime)

The Windows kernel does **not** save/restore the x87 / XMM / YMM / ZMM
registers across context switches by default. If a driver touches any of
these from arbitrary kernel code, it will silently corrupt the SIMD state
of whichever user-mode thread happens to be on the CPU.

**Solution**: bracket every floating-point / SIMD region with
[`KeSaveExtendedProcessorState`](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kesaveextendedprocessorstate)
and `KeRestoreExtendedProcessorState`:

```c
XSTATE_SAVE save;
NTSTATUS status = KeSaveExtendedProcessorState(XSTATE_MASK_LEGACY, &save);
if (!NT_SUCCESS(status))
    return status;

// ... your FP / SIMD code here ...

KeRestoreExtendedProcessorState(&save);
```

### XSTATE masks

| Mask | Covers |
|------|--------|
| `XSTATE_MASK_LEGACY_FLOATING_POINT` (bit 0) | x87 stack |
| `XSTATE_MASK_LEGACY_SSE` (bit 1) | XMM0–15 |
| `XSTATE_MASK_LEGACY` | bit 0 \| bit 1 (covers most plain `double` / SSE code) |
| `XSTATE_MASK_GSSE` / AVX (bit 2) | YMM0–15 upper halves |
| `XSTATE_MASK_AVX512` | AVX-512 ZMM registers |

Pass the OR-combined mask matching the widest registers your code uses.

---

## What this driver does

- Creates a device object at `\Device\FloatDriver` and a symbolic link at
  `\DosDevices\FloatDriver`
- In `DriverEntry`, calls `ComputeAreaSafe()` (which wraps `ComputeArea()`
  with FP state save/restore) twice with `radius=1.0` and `radius=5.0`
- Prints the resulting double's raw bits via `DbgPrint` (because
  `DbgPrint`'s `%f` is not supported — we use `RtlCopyMemory` to extract
  the 64-bit pattern)
- Defines `_fltused` implicitly via `-fms-kernel`

## Verifying `_fltused` emission

Compare what the compiler emits with and without `-fms-kernel`:

```bash
# User-mode (would need libcmt):
neverc --target=x86_64-pc-windows-msvc -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused

# Kernel (locally defined as 0):
neverc --target=x86_64-pc-windows-msvc -fms-kernel -S /tmp/foo.c -o - | grep fltused
#  .globl  _fltused
#  .set    _fltused, 0
```

## Loading (on a Windows test machine)

```cmd
sc create FloatDriver type= kernel binPath= C:\path\to\FloatDriver.sys
sc start FloatDriver
sc stop FloatDriver
sc delete FloatDriver
```

Enable test signing or use a code signing certificate for production.

## Caveats

- **`%f` doesn't work with `DbgPrint`** — the kernel debug print routine
  has no floating-point formatting. Convert your double to a fixed-point
  integer for display, or print the raw bits as this example does.
- **Don't use floating-point at IRQL ≥ DISPATCH_LEVEL** unless absolutely
  necessary. `KeSaveExtendedProcessorState` documents the IRQL constraints.
- **Performance**: state save/restore is not free; for hot paths
  consider batching FP work into a single bracketed region.
