**Idiomas**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Ejemplos NeverC](../../docs/examples/README.es.md)

# Recolector de telemetría anti-trampas mTLS

Este recolector ejecutable sirve el protocolo NRPC multiplexado sobre TLS 1.3
con certificados de cliente obligatorios. `anticheat.Telemetry/Collect` es un
flujo bidireccional: los agentes envían registros de telemetría firmados y
reciben un ACK de nonce por cada registro aceptado.

Cada mensaje DATA es una cabecera de 64 bytes seguida de un cuerpo opaco
(máximo 1 MiB). La cabecera contiene la versión `1`, tres bytes cero, una
marca de tiempo Unix en milisegundos de ocho bytes en big-endian, un nonce de
16 bytes, una longitud de cuerpo de cuatro bytes en big-endian y un
HMAC-SHA256 de 32 bytes. El MAC cubre
`agent-id || first-32-header-bytes || body`. El valor de metadatos NRPC
`agent-id` es obligatorio. Los nonces solo se aceptan una vez dentro de una
ventana de reloj de 30 segundos.

Los registros aceptados entran en una cola acotada. Un escritor único dedicado
añade un evento de auditoría JSONL que contiene la huella del certificado de
cliente, el nonce, el digest del cuerpo, la marca de tiempo y el tamaño del
cuerpo; el recolector nunca escribe bytes brutos no confiables del cuerpo
directamente en el registro de auditoría.

Compilación para el host o cualquier objetivo compatible:

```bash
neverc make
neverc make TARGET=aarch64-pc-windows-msvc OUTPUT=anticheat-collector.exe
```

Ejecución con certificado de servidor, clave de servidor, CA de cliente de
confianza, clave de firma compartida de 32 bytes y ruta de auditoría:

```bash
./anticheat-collector server.pem server-key.pem agent-ca.pem \
  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef \
  telemetry-audit.jsonl
```
