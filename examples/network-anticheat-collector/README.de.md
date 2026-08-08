**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Beispiele](../../docs/examples/README.de.md)

# mTLS Anti-Cheat-Telemetrie-Sammler

Dieser ausführbare Sammler stellt das multiplexierte NRPC-Protokoll über
TLS 1.3 mit verpflichtenden Client-Zertifikaten bereit.
`anticheat.Telemetry/Collect` ist ein bidirektionaler Stream: Agenten senden
signierte Telemetrie-Datensätze und erhalten pro akzeptiertem Datensatz ein
Nonce-ACK.

Jede DATA-Nachricht besteht aus einem 64-Byte-Header gefolgt von einem
opaken Body (maximal 1 MiB). Der Header enthält Version `1`, drei Null-Bytes,
einen acht Byte großen Big-Endian-Unix-Millisekunden-Zeitstempel, ein
16-Byte-Nonce, eine vier Byte große Big-Endian-Body-Länge und ein
32-Byte-HMAC-SHA256. Der MAC deckt
`agent-id || first-32-header-bytes || body` ab. Der NRPC-Metadatenwert
`agent-id` ist erforderlich. Nonces werden nur einmal innerhalb eines
30-Sekunden-Zeitfensters akzeptiert.

Akzeptierte Datensätze gelangen in eine begrenzte Warteschlange. Ein
dedizierter Einzel-Schreiber hängt ein JSONL-Audit-Ereignis mit
Client-Zertifikat-Fingerabdruck, Nonce, Body-Digest, Zeitstempel und
Body-Größe an; der Sammler schreibt niemals nicht vertrauenswürdige
Body-Rohbytes direkt in das Audit-Log.

Erstellung für den Host oder jedes unterstützte Ziel:

```bash
neverc make          # debug: -g (Standard beim ersten Build)
neverc make release  # release: -O2 --strip
neverc make debug    # zurück zu debug
neverc make TARGET=aarch64-pc-windows-msvc OUTPUT=anticheat-collector.exe
```

Das Makefile speichert `PROFILE`, sodass spätere `neverc make`-Aufrufe dieselbe
debug/release-Auswahl behalten. Release nutzt NeverCs integriertes `--strip`.
Siehe [Release-Builds](../../docs/release-builds/README.de.md).


Ausführung mit Server-Zertifikat, Server-Schlüssel, vertrauenswürdiger
Client-CA, gemeinsam genutztem 32-Byte-Signierschlüssel und Audit-Pfad:

```bash
./anticheat-collector server.pem server-key.pem agent-ca.pem \
  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef \
  telemetry-audit.jsonl
```
