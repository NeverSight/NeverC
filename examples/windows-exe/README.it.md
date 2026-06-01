**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Esempio EXE Windows Ring3

Un eseguibile Windows user-mode cross-compilato con NeverC. Dimostra API Win32.

## Compilazione

```bash
cd examples/windows-exe
neverc make
```

## Compilazione manuale

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -Xlinker --subsystem=console -lkernel32 -luser32 -lmsvcrt -o example.exe main.c
```

## Funzionalità

- Query info sistema via `GetSystemInfo`
- Enumerazione processi con `CreateToolhelp32Snapshot`
- Demo `VirtualAlloc`/`VirtualQuery`/`VirtualFree`

