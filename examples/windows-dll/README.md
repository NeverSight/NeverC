**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Windows Ring3 DLL Example

A user-mode Windows DLL cross-compiled using NeverC. Provides helper functions for process memory operations — designed for game security research. Builds from macOS, Windows, or Linux — no MSVC or Visual Studio required.

## Build

```bash
cd examples/windows-dll
make
```

## Manual build (without Make)

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -fms-extensions -fms-compatibility -D_AMD64_ -shared -Xlinker --entry=DllMain -Xlinker --subsystem=windows -lkernel32 -luser32 -o example.dll dllmain.c
```

## What it does

- Exports `ReadProcessMemory`/`VirtualAllocEx`/`VirtualFreeEx` wrappers for cross-process memory access
- Process/module enumeration via `OpenProcess` and `CreateToolhelp32Snapshot`
- XOR buffer encryption helper and PID/TID query functions

