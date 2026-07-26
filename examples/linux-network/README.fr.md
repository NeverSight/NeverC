**Langues**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Exemples NeverC](../../docs/examples/README.fr.md)

# Exemple Linux réseau socket

Démo client/serveur TCP cross-compilée avec NeverC.

NeverC embarque un sysroot Linux (Ubuntu 22.04, glibc 2.35) dans `runtime/linux/`.

## Compilation

```bash
cd examples/linux-network
neverc make
```

AArch64:

```bash
neverc make TARGET=aarch64-linux-gnu
```

## Compilation manuelle

```bash
neverc --target=x86_64-linux-gnu -Wall -o network-demo main.c
```

## Exécution

```bash
chmod +x network-demo
./network-demo
```

## Fonctionnalités

- Serveur TCP (127.0.0.1)
- Connexion client
- Envoi de 3 messages
- Démo de `socket`, `bind`, `listen`, `accept`
