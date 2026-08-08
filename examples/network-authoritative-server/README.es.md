**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Ejemplos NeverC](../../docs/examples/README.es.md)

# Servidor de juego autoritativo

Este ejemplo ejecutable usa la pila de red portable de NeverC en lugar de
sockets de plataforma en bruto. Proporciona:

- un plano de control TCP que emite tokens de sesión respaldados por CSPRNG;
- planos de entrada en tiempo real UDP y QUIC nativo;
- un bucle de simulación autoritativo a 60 Hz y una cola de entrada acotada;
- protección contra reproducción de timestamp/nonce para uniones y números de
  secuencia de entrada monótonos por sesión.

Destino predeterminado: `x86_64-linux-gnu`. Sustitúyalo por cualquier objetivo NeverC compatible:

```bash
neverc make          # debug: -g (predeterminado en la primera compilación)
neverc make release  # release: -O2 --strip
neverc make debug    # volver a debug
neverc make TARGET=aarch64-linux-gnu
neverc make TARGET=x86_64-pc-windows-msvc OUTPUT=authoritative-server.exe
```

El Makefile guarda `TARGET` y `PROFILE`, así que los siguientes `neverc make`
mantienen la misma selección de artefacto. Release usa el `--strip` integrado de NeverC.
Véase [Builds de release](../../docs/release-builds/README.es.md).


Ejecución con certificado y clave TLS P-256 para el endpoint QUIC:

```bash
./authoritative-server cert.pem key.pem
```

Las direcciones opcionales tienen por defecto TCP `:7000`, UDP `:7001` y
QUIC `:7002`. La línea de unión TCP es
`JOIN <client-id> <unix-ms> <32-hex-nonce>`. La entrada UDP es
`INPUT <32-hex-session-token> <sequence> <dx> <dy>`. Un cliente QUIC negocia
`neverc-game/1`, se autentica en su primer flujo con `AUTH <token>` y luego
envía datagramas que contienen `<sequence> <dx> <dy>`.
