**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Exemple EXE Windows Ring3

Un exécutable Windows mode utilisateur compilé en croisé avec NeverC. Démontre l'API Win32.

## Compilation

```bash
cd examples/windows-exe
neverc make
```

## Compilation manuelle

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -Xlinker --subsystem=console -lkernel32 -luser32 -lmsvcrt -o example.exe main.c
```

## Fonctionnalités

- Requête d'informations système via `GetSystemInfo`
- Énumération des processus avec `CreateToolhelp32Snapshot`
- Démo `VirtualAlloc`/`VirtualQuery`/`VirtualFree`

