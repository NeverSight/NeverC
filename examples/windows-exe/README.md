**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Windows Ring3 EXE Example

A user-mode Windows executable cross-compiled using NeverC. Demonstrates Win32 API for system info, process enumeration, and virtual memory operations. Builds from macOS, Windows, or Linux — no MSVC or Visual Studio required.

## Build

```bash
cd examples/windows-exe
make
```

## Manual build (without Make)

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -fms-extensions -fms-compatibility -D_AMD64_ -Xlinker --subsystem=console -lkernel32 -luser32 -lmsvcrt -o example.exe main.c
```

## What it does

- Queries system info via `GetSystemInfo` and `GlobalMemoryStatusEx`
- Enumerates running processes using `CreateToolhelp32Snapshot`
- Demonstrates `VirtualAlloc`/`VirtualQuery`/`VirtualFree` for memory management

