**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Ejemplo Linux socket de red

Demo TCP cliente/servidor compilada cruzadamente con NeverC.

NeverC incluye un sysroot de Linux (Ubuntu 22.04, glibc 2.35) en `runtime/linux/`.

## Compilación

```bash
cd examples/linux-network
make
```

AArch64:

```bash
make TARGET=aarch64-linux-gnu
```

## Compilación manual

```bash
neverc --target=x86_64-linux-gnu -Wall -o network-demo main.c
```

## Ejecución

```bash
chmod +x network-demo
./network-demo
```

## Funcionalidades

- Servidor TCP (127.0.0.1)
- Conexión de cliente
- Envío de 3 mensajes
- Demo de `socket`, `bind`, `listen`, `accept`
