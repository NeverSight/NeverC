**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Examples](../../docs/examples/README.md)

# Windows Kernel Driver Example

A minimal WDM kernel driver built with NeverC. Targets **x64** by default, and
can also be built for ARM64. Cross-compiles from macOS / Linux.

NeverC is an all-in-one compiler — a single invocation handles preprocessing,
compilation, optimization (auto-LTO), and linking via the built-in linker.

## Build

From the repo:

```bash
cd examples/windows-driver
neverc make
```

That builds `ExampleDriver-x64.sys`. To build for ARM64 instead, or for both:

```bash
neverc make ARCH=arm64
neverc make all-arch
```

From a standalone NeverC release:

```bash
neverc make NEVERC=/path/to/neverc
```

The output is `ExampleDriver-<arch>.sys` (auto-LTO optimized).
Default build includes `-g` for debugging; **release builds should remove `-g`** to strip
debug symbols and reduce binary size (~38 KB → ~3 KB).

## Manual build (without Make)

```bash
neverc --target=x86_64-pc-windows-msvc \
  -g \
  -fms-kernel \
  -Wall -nostdlib -shared \
  -Xlinker --entry=DriverEntry \
  -Xlinker --subsystem=native \
  -Xlinker --nodefaultlib \
  -lntoskrnl -lhal \
  -o ExampleDriver-x64.sys driver.c
```

For ARM64, swap in `--target=aarch64-pc-windows-msvc`; nothing else changes.
`-fms-kernel` picks the WDK headers and import libraries matching the target
and defines the architecture macros the WDK expects, so they never have to be
passed by hand.

> `-g` emits DWARF debug info into the PE; inspect with `llvm-dwarfdump`.
> Omit for release builds to reduce binary size.

## What it does

- Creates a device object at `\Device\ExampleDriver`
- Creates a symbolic link at `\DosDevices\ExampleDriver`
- Handles `IRP_MJ_CREATE`, `IRP_MJ_CLOSE`, `IRP_MJ_DEVICE_CONTROL`
- Prints load/unload messages via `DbgPrint`

## Loading (on a Windows test machine)

```cmd
sc create ExampleDriver type= kernel binPath= C:\path\to\ExampleDriver-x64.sys
sc start ExampleDriver
sc stop ExampleDriver
sc delete ExampleDriver
```

Enable test signing or use a code signing certificate for production.
