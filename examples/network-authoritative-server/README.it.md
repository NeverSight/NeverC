**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Esempi NeverC](../../docs/examples/README.it.md)

# Server di gioco autoritativo

Questo esempio eseguibile usa lo stack di rete portatile di NeverC anziché
socket di piattaforma grezzi. Fornisce:

- un piano di controllo TCP che emette token di sessione basati su CSPRNG;
- piani di input in tempo reale UDP e QUIC nativo;
- un loop di simulazione autoritativo a 60 Hz e una coda di input limitata;
- protezione da replay timestamp/nonce per le join e numeri di sequenza di
  input monotoni per sessione.

Target predefinito: `x86_64-linux-gnu`. Sovrascrivibile con qualsiasi target NeverC supportato:

```bash
neverc make          # debug: -g (predefinito alla prima build)
neverc make release  # release: -O2 --strip
neverc make debug    # torna a debug
neverc make TARGET=aarch64-linux-gnu
neverc make TARGET=x86_64-pc-windows-msvc OUTPUT=authoritative-server.exe
```

Il Makefile conserva `TARGET` e `PROFILE`, quindi i successivi `neverc make`
mantengono la stessa scelta di artefatto. Release usa il `--strip` integrato di NeverC.
Vedi [Build di release](../../docs/release-builds/README.it.md).


Esecuzione con certificato e chiave TLS P-256 per l'endpoint QUIC:

```bash
./authoritative-server cert.pem key.pem
```

Gli indirizzi opzionali hanno come predefiniti TCP `:7000`, UDP `:7001` e
QUIC `:7002`. La riga di join TCP è
`JOIN <client-id> <unix-ms> <32-hex-nonce>`. L'input UDP è
`INPUT <32-hex-session-token> <sequence> <dx> <dy>`. Un client QUIC negozia
`neverc-game/1`, si autentica sul primo stream con `AUTH <token>`, poi invia
datagrammi contenenti `<sequence> <dx> <dy>`.
