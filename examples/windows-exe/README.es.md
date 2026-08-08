**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Ejemplos NeverC](../../docs/examples/README.es.md)

# Ejemplo EXE Windows Ring3

Un ejecutable Windows modo usuario compilado cruzado con NeverC. Demuestra API Win32.

## Compilación

```bash
cd examples/windows-exe
neverc make          # debug: -g (predeterminado en la primera compilación)
neverc make release  # release: -O2 --strip
neverc make debug    # volver a debug
```

El Makefile guarda `PROFILE`, así que los siguientes `neverc make`
conservan la misma selección debug/release. Release usa el `--strip`
integrado de NeverC: quita metadatos de depuración y nombres de símbolos
estáticos innecesarios, y conserva los nombres ABI dinámicos/del cargador
necesarios. Véase [Compilaciones de publicación](../../docs/release-builds/README.es.md).

## Compilación manual

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -Xlinker --subsystem=console -lkernel32 -luser32 -lmsvcrt -o example.exe main.c
```

## Funcionalidades

- Consulta información del sistema via `GetSystemInfo`
- Enumera procesos con `CreateToolhelp32Snapshot`
- Demo `VirtualAlloc`/`VirtualQuery`/`VirtualFree`

