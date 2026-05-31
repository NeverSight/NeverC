**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Ejemplo Linux Hello World

Un programa C mínimo compilado cruzadamente a Linux ELF usando NeverC. Compilación desde macOS, Windows o Linux — sin necesidad de cadena de herramientas del sistema destino.

NeverC incluye un sysroot de Linux (Ubuntu 22.04, glibc 2.35) en `runtime/linux/`, de modo que una sola invocación gestiona el preprocesado, compilación, optimización (auto-LTO) y enlace mediante el enlazador integrado.

## Compilación

Desde el repositorio (destino predeterminado: `x86_64-linux-gnu`):

```bash
cd examples/linux-hello
make
```

Compilar para AArch64:

```bash
make TARGET=aarch64-linux-gnu
```

Con una versión independiente de NeverC:

```bash
make NEVERC=/path/to/neverc
```

## Compilación manual (sin Make)

```bash
neverc --target=x86_64-linux-gnu -O2 -Wall -o hello main.c
```

## Ejecución

Copie `hello` a una máquina Linux (o contenedor Docker) y ejecute:

```bash
chmod +x hello
./hello
```

## Funcionalidades

- Imprime un saludo con los argumentos de línea de comandos
- Demuestra `printf`, `strncpy`, `strlen`, `atoi` de la libc integrada
- Transformación XOR de una cadena para verificar operaciones básicas de entero/carácter
