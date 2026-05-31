**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Exemple DLL Windows Ring3

Une DLL Windows mode utilisateur compilée en croisé avec NeverC.

## Compilation

```bash
cd examples/windows-dll
make
```

## Compilation manuelle

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -fms-extensions -fms-compatibility -D_AMD64_ -shared -Xlinker --entry=DllMain -Xlinker --subsystem=windows -lkernel32 -luser32 -o example.dll dllmain.c
```

## Fonctionnalités

- Exporte des wrappers d'accès mémoire inter-processus
- Énumération processus/modules
- Helper chiffrement XOR

