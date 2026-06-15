**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# macOS Dynamic Library Example

A native macOS `.dylib` cross-compiled using NeverC. Wraps Mach kernel interfaces for task introspection and virtual memory operations — designed for security research. Builds from macOS, Windows, or Linux — no Xcode required.

## Build

From the repo (default target: `arm64-apple-macos`):

```bash
cd examples/macos-dylib
neverc make
```

Build for Intel:

```bash
neverc make TARGET=x86_64-apple-macos
```

From a standalone NeverC release:

```bash
neverc make NEVERC=/path/to/neverc
```

## Manual build (without Make)

```bash
neverc --target=arm64-apple-macos -Wall -dynamiclib -o libneverc.dylib lib.c
```

## What it does

- Exports `nc_task_basic_info` wrapper for Mach `task_info` queries
- Provides `nc_vm_read`/`nc_vm_write` for Mach virtual memory read/write
- `nc_vm_alloc`/`nc_vm_dealloc` for Mach VM allocation and deallocation
- XOR buffer encryption helper and PID/task query functions
