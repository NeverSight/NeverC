**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Esempi NeverC](../../docs/examples/README.it.md)

# Collettore di telemetria anti-cheat mTLS

Questo collettore eseguibile serve il protocollo NRPC multiplexato su TLS 1.3
con certificati client obbligatori. `anticheat.Telemetry/Collect` è uno stream
bidirezionale: gli agenti inviano record di telemetria firmati e ricevono un
ACK nonce per ogni record accettato.

Ogni messaggio DATA è un'intestazione di 64 byte seguita da un corpo opaco
(massimo 1 MiB). L'intestazione contiene la versione `1`, tre byte zero, un
timestamp Unix in millisecondi di otto byte in big-endian, un nonce di 16 byte,
una lunghezza del corpo di quattro byte in big-endian e un HMAC-SHA256 di
32 byte. Il MAC copre `agent-id || first-32-header-bytes || body`. Il valore
dei metadati NRPC `agent-id` è obbligatorio. I nonce vengono accettati una
sola volta entro una finestra temporale di 30 secondi.

I record accettati entrano in una coda limitata. Un singolo writer dedicato
accoda un evento di audit JSONL contenente l'impronta del certificato client,
il nonce, il digest del corpo, il timestamp e la dimensione del corpo; il
collettore non scrive mai direttamente i byte grezzi non attendibili del corpo
nel log di audit.

Target predefinito: `x86_64-linux-gnu`. Sovrascrivibile con qualsiasi target NeverC supportato:

```bash
neverc make          # debug: -g (predefinito alla prima build)
neverc make release  # release: -O2 --strip
neverc make debug    # torna a debug
neverc make TARGET=aarch64-linux-gnu
neverc make TARGET=x86_64-pc-windows-msvc OUTPUT=anticheat-collector.exe
```

Il Makefile conserva `TARGET` e `PROFILE`, quindi i successivi `neverc make`
mantengono la stessa scelta di artefatto. Release usa il `--strip` integrato di NeverC.
Vedi [Build di release](../../docs/release-builds/README.it.md).


Esecuzione con certificato server, chiave server, CA client attendibile,
chiave di firma condivisa da 32 byte e percorso di audit:

```bash
./anticheat-collector server.pem server-key.pem agent-ca.pem \
  0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef \
  telemetry-audit.jsonl
```
