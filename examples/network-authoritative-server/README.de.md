**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC Beispiele](../../docs/examples/README.de.md)

# Autoritativer Game-Server

Dieses ausführbare Beispiel nutzt NeverCs portable Netzwerk-Stack statt roher
Plattform-Sockets. Es bietet:

- eine TCP-Steuerungsebene, die CSPRNG-gestützte Sitzungs-Tokens ausgibt;
- UDP- und native QUIC-Echtzeit-Eingabeebenen;
- eine autoritative Simulations-Schleife mit 60 Hz und eine begrenzte
  Eingabe-Warteschlange;
- Timestamp/Nonce-Replay-Schutz für Joins und monotone Eingabe-Sequenznummern
  pro Sitzung.

Erstellung für den Host oder mit jedem unterstützten NeverC-Ziel-Triple:

```bash
neverc make          # debug: -g (Standard beim ersten Build)
neverc make release  # release: -O2 --strip
neverc make debug    # zurück zu debug
neverc make TARGET=aarch64-linux-gnu
neverc make TARGET=x86_64-pc-windows-msvc OUTPUT=authoritative-server.exe
```

Das Makefile speichert `PROFILE`, sodass spätere `neverc make`-Aufrufe dieselbe
debug/release-Auswahl behalten. Release nutzt NeverCs integriertes `--strip`.
Siehe [Release-Builds](../../docs/release-builds/README.de.md).


Ausführung mit P-256-TLS-Zertifikat und -Schlüssel für den QUIC-Endpunkt:

```bash
./authoritative-server cert.pem key.pem
```

Die optionalen Adressen lauten standardmäßig TCP `:7000`, UDP `:7001` und
QUIC `:7002`. Die TCP-Join-Zeile ist
`JOIN <client-id> <unix-ms> <32-hex-nonce>`. UDP-Eingabe ist
`INPUT <32-hex-session-token> <sequence> <dx> <dy>`. Ein QUIC-Client
handelt `neverc-game/1` aus, authentifiziert sich im ersten Stream mit
`AUTH <token>` und sendet dann Datagramme mit `<sequence> <dx> <dy>`.
