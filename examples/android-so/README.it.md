**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Esempio libreria condivisa Android

Una libreria condivisa `.so` nativa ARM64 cross-compilata per Android con NeverC. Compilabile da macOS, Windows o Linux.

## Compilazione

```bash
cd examples/android-so
neverc make
```

## Compilazione manuale

```bash
neverc --target=aarch64-linux-android -Wall -shared -fPIC -ldl -o libneverc.so lib.c
```

## Funzionalità

- Funzioni helper per ricerca sicurezza nei giochi: query PID, lettura `/proc/self/maps`, allocazione memoria RWX, crittografia XOR
- Caricamento dinamico di `liblog.so` tramite `dlopen`
- Demo allocazione memoria eseguibile con `mmap` + `PROT_EXEC`

