**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Sistema de ejecución integrado de NeverC](../README.es.md)

# Cifrado de cadenas en tiempo de compilación (`xorstr`)

## Descripción general

NeverC proporciona cifrado de cadenas de dos capas en tiempo de compilación para código C, diseñado para escenarios de seguridad donde las cadenas en texto plano (nombres de API, rutas del registro) no deben ser visibles en el binario compilado.

- **Capa 1 — Macro explícita**: `NC_XORSTR("string")` / `NEVERC_XORSTR("string")` para control preciso por cadena
- **Capa 2 — Pase IR automático**: `-fencrypt-call-strings` para cifrar automáticamente todos los argumentos de cadena en llamadas a funciones

Ambas capas usan búferes asignados en la pila (sin asignación en el montón), un algoritmo de descifrado sin instrucción XOR (anti-firma), y limpieza con `memset` volátil antes del retorno de función.

---

## Inicio rápido

```c
#include <neverc/xorstr.h>
FARPROC addr = GetProcAddress(hModule, NC_XORSTR("NtQuerySystemInformation"));
```

```bash
neverc -fencrypt-call-strings main.c -o main
```

---

## Descifrado anti-firma

La operación de descifrado evita completamente la instrucción XOR, usando el equivalente matemático: `a + b − 2 × (a & b)`.

---

## Referencia de flags del compilador

| Flag | Descripción |
|------|-------------|
| `-fencrypt-call-strings` | Habilitar cifrado automático de cadenas |
| `-fno-encrypt-call-strings` | Deshabilitar cifrado automático |
| `-fencrypt-call-strings-max-len=N` | Longitud máxima en bytes (predeterminado: 1024) |
| `-fstring-encrypt-key=0xHEX` | Sobrescribir la semilla de clave XOR |
