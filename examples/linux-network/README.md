**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Linux Network Socket Example

A TCP client/server demo cross-compiled to Linux using NeverC. Both client and server run in the same process using loopback for simplicity.

NeverC bundles a Linux sysroot (Ubuntu 22.04, glibc 2.35) in `runtime/linux/`, so a single invocation handles preprocessing, compilation, optimization (auto-LTO), and linking via the built-in linker.

## Build

From the repo (default target: `x86_64-linux-gnu`):

```bash
cd examples/linux-network
neverc make
```

Build for AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## Manual build (without Make)

```bash
neverc --target=x86_64-linux-gnu -Wall -o network-demo main.c
```

## Run

Copy `network-demo` to a Linux machine (or Docker container) and execute:

```bash
chmod +x network-demo
./network-demo
```

## What it does

- Creates a TCP server on a random port (127.0.0.1)
- Connects a client to the server
- Sends 3 messages from client to server and prints the received data
- Demonstrates `socket`, `bind`, `listen`, `accept`, `connect`, `send`, `recv`
