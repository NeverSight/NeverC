**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Esempi NeverC](../../docs/examples/README.it.md)

# Esempio DLL Windows Ring3

Una DLL Windows user-mode cross-compilata con NeverC.

## Compilazione

```bash
cd examples/windows-dll
neverc make          # debug: -g (predefinito alla prima build)
neverc make release  # release: -O2 --strip
neverc make debug    # torna a debug
```

Il Makefile memorizza `PROFILE`, quindi i successivi `neverc make`
mantengono la stessa scelta debug/release. Release usa `--strip` integrato
in NeverC: rimuove metadati di debug e nomi di simboli statici non
necessari, preservando i nomi ABI dinamici/del loader richiesti.
Vedi [Build di rilascio](../../docs/release-builds/README.it.md).

## Compilazione manuale

```bash
neverc --target=x86_64-pc-windows-msvc -Wall -shared -Xlinker --entry=DllMain -Xlinker --subsystem=windows -lkernel32 -luser32 -o example.dll dllmain.c
```

## Funzionalità

- Esporta wrapper accesso memoria cross-processo
- Enumerazione processi/moduli
- Helper crittografia XOR

