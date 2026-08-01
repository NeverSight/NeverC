**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Examples](../../docs/examples/README.md)

# mTLS Anti-Cheat Telemetry Collector

This runnable collector serves the multiplexed NRPC protocol over TLS 1.3 with
mandatory client certificates. `anticheat.Telemetry/Collect` is a bidirectional
stream: agents send signed telemetry records and receive one nonce ACK per
accepted record.

Each DATA message is a 64-byte header followed by an opaque body (maximum 1
MiB). The header contains version `1`, three zero bytes, an eight-byte
big-endian Unix-millisecond timestamp, a 16-byte nonce, a four-byte big-endian
body length, and a 32-byte HMAC-SHA256. The MAC covers
`agent-id || first-32-header-bytes || body`. The `agent-id` NRPC metadata value
is required. Nonces are accepted only once inside a 30-second clock window.

Accepted records enter a bounded queue. A dedicated single writer appends a
JSONL audit event containing the client-certificate fingerprint, nonce, body
digest, timestamp, and body size; the collector never writes untrusted body
bytes directly to the audit log.

Build for the host or any supported target:

```bash
neverc make
neverc make TARGET=aarch64-pc-windows-msvc OUTPUT=anticheat-collector.exe
```

Run with a server certificate, server key, trusted client CA, shared 32-byte
signing key, and audit path:

```bash
./anticheat-collector server.pem server-key.pem agent-ca.pem \
  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef \
  telemetry-audit.jsonl
```
