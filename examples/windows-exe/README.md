**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Examples](../../docs/examples/README.md)

# Windows Ring3 EXE Example

A user-mode Windows executable cross-compiled using NeverC. Demonstrates Win32 API for system info, process enumeration, and virtual memory operations. Builds from macOS, Windows, or Linux — no MSVC or Visual Studio required.

## Build

```bash
cd examples/windows-exe
neverc make          # debug: -g (default on the first build)
neverc make release  # release: -O2 --strip
neverc make debug    # switch back to debug
```

The Makefile persists `PROFILE`, so later `neverc make` keeps the same
debug/release selection. Release uses NeverC's integrated `--strip`:
debug metadata and unneeded static symbol names are removed while
loader/dynamic ABI names that the binary still needs are preserved.
See [Release builds](../../docs/release-builds/README.md).

## Manual build (without Make)

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -Xlinker --subsystem=console -lkernel32 -luser32 -lmsvcrt -o example.exe main.c
```

## What it does

- Queries system info via `GetSystemInfo` and `GlobalMemoryStatusEx`
- Enumerates running processes using `CreateToolhelp32Snapshot`
- Demonstrates `VirtualAlloc`/`VirtualQuery`/`VirtualFree` for memory management

