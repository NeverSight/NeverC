**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Examples](../../docs/examples/README.md)

# Authoritative Game Server

This runnable example uses NeverC's portable networking stack rather than raw
platform sockets. It provides:

- a TCP control plane that issues CSPRNG-backed session tokens;
- UDP and native QUIC real-time input planes;
- a 60 Hz authoritative simulation loop and a bounded input queue;
- timestamp/nonce replay protection for joins and monotonic per-session input
  sequence numbers.

Build for the host, or set any supported NeverC target triple:

```bash
neverc make          # debug: -g (default on the first build)
neverc make release  # release: -O2 --strip
neverc make debug    # switch back to debug
neverc make TARGET=aarch64-linux-gnu
neverc make TARGET=x86_64-pc-windows-msvc OUTPUT=authoritative-server.exe
```

The Makefile persists `PROFILE`, so later `neverc make` keeps the same
debug/release selection. Release uses NeverC's integrated `--strip`.
See [Release builds](../../docs/release-builds/README.md).


Run with a P-256 TLS certificate and key for the QUIC endpoint:

```bash
./authoritative-server cert.pem key.pem
```

The optional addresses default to TCP `:7000`, UDP `:7001`, and QUIC `:7002`.
The TCP join line is `JOIN <client-id> <unix-ms> <32-hex-nonce>`. UDP input is
`INPUT <32-hex-session-token> <sequence> <dx> <dy>`. A QUIC client negotiates
`neverc-game/1`, authenticates its first stream with `AUTH <token>`, then sends
datagrams containing `<sequence> <dx> <dy>`.
