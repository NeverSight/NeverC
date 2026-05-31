**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Windows Ring3 EXE Beispiel

Eine Windows-Benutzermodus-Anwendung, cross-kompiliert mit NeverC. Demonstriert Win32-API.

## Build

```bash
cd examples/windows-exe
make
```

## Manueller Build

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -Xlinker --subsystem=console -lkernel32 -luser32 -lmsvcrt -o example.exe main.c
```

## Funktionen

- Systeminfo-Abfrage über `GetSystemInfo`
- Prozessauflistung mit `CreateToolhelp32Snapshot`
- `VirtualAlloc`/`VirtualQuery`/`VirtualFree`-Demo

