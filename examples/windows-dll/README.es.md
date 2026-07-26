**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Ejemplos NeverC](../../docs/examples/README.es.md)

# Ejemplo DLL Windows Ring3

Una DLL Windows modo usuario compilada cruzada con NeverC.

## Compilación

```bash
cd examples/windows-dll
neverc make
```

## Compilación manual

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -shared -Xlinker --entry=DllMain -Xlinker --subsystem=windows -lkernel32 -luser32 -o example.dll dllmain.c
```

## Funcionalidades

- Exporta wrappers de acceso a memoria entre procesos
- Enumeración de procesos/módulos
- Helper de cifrado XOR

