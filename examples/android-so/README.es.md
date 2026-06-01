**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Ejemplo biblioteca compartida Android

Una biblioteca compartida `.so` nativa ARM64 compilada cruzada para Android con NeverC. Compilable desde macOS, Windows o Linux.

## Compilación

```bash
cd examples/android-so
neverc make
```

## Compilación manual

```bash
neverc --target=aarch64-linux-android -Wall -shared -fPIC -ldl -o libneverc.so lib.c
```

## Funcionalidades

- Funciones auxiliares para investigación de seguridad en juegos: consulta PID, lectura `/proc/self/maps`, asignación memoria RWX, cifrado XOR
- Carga dinámica de `liblog.so` vía `dlopen`
- Demostración de asignación de memoria ejecutable con `mmap` + `PROT_EXEC`

