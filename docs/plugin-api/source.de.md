**Sprachen**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

# Source- und E/A-Plugin-API

Die erste öffentliche Plugin-ABI stellt Quelleingaben, virtuelle Dateien,
Abhängigkeiten und Compiler-Ausgaben über `PluginSource.h` bereit. Alle Pfade
sind normalisierte VFS-Pfade, und alle Handles sind auf die aktuelle
`TranslationUnit`-Task beschränkt.

## Source-Phasen

Die stabile Source-Pipeline lautet:

1. `neverc.source.resolve_input` prüft und normalisiert die angeforderte Eingabe.
2. `neverc.source.open` öffnet sie über das zusammengesetzte Host/Plugin-VFS.
3. `neverc.source.after_open` veröffentlicht ein Nur-Lese-Ereignis für die
   verifizierte `SourceUnit`.

`resolve_input` ist beobachtbar und abfangbar; `open` ist zusätzlich ersetzbar.
Der Host verifiziert jeden Ersatz, bevor er ihn als `SourceUnit`
veröffentlicht. Ein Plugin kann `after_open` nicht ersetzen.

## VFS-Provider

Fragen Sie `NevercIOAPI` während der Plugin-Registrierung ab und rufen Sie
`RegisterVFSProvider` auf. Ein Provider beantwortet zuerst `MatchesPath` und
implementiert dann die Operationen, für die er zuständig ist. Die Rückgabe von
`NEVERC_VFS_RESULT_NOT_HANDLED` delegiert an den nächsten Provider; die Rückgabe
von `HANDLED` macht einen fehlerhaften Status oder Inhalt zu einem harten Fehler
statt zu einem stillen Rückfall.

Von einem Provider zurückgegebene Puffer sind nur für den Callback geliehen.
NeverC kopiert akzeptierte Bytes in Task-eigenen Speicher. Provider müssen
erklären, ob ihr Ergebnis deterministisch und cachebar ist.

Das baubare Beispiel
[`VirtualHeaderPlugin.c`](../../pluginsdk/examples/VirtualHeaderPlugin.c)
liefert einen Header im Speicher, ohne das Host-VFS zu umgehen.

## Ausgabesenken und Abhängigkeiten

Datei- und Speicherausgaben verwenden dieselbe transaktionale Senke:

- in einen Kandidaten schreiben;
- finish aufrufen, um ihn für die Verifikation zuzulassen;
- das versiegelte Host-Gate ihn verifizieren lassen;
- bei Erfolg der Task atomar committen, bei jedem Fehler oder Abbruch verwerfen.

Ein Plugin veröffentlicht niemals durch direktes Schreiben in den Zielpfad.
Streaming-Ziele, die nicht zurückgerollt werden können, weisen Transformationen
zurück, die einen atomaren Kandidaten erfordern. Abhängigkeitsdatensätze
verwenden normalisierte VFS-Identitäten, sodass native und von Plugins
bereitgestellte Dateien dieselbe Herkunft und Cache-Semantik besitzen.

## Sicherheitsregeln

- Behalten Sie Source-, Datei-, Puffer-, Senken- oder Task-Handles nicht über den
  Callback hinaus.
- Behandeln Sie `NevercStringView` und `NevercByteView` als längenbegrenzte
  Sichten.
- Verwenden Sie den Host-Allokator, wenn Daten den Callback überleben müssen.
- Verwenden Sie hinter dem VFS-Vertrag keine Dateisystem-APIs des Hosts.
- Prüfen Sie auf Abbruch, bevor Sie teure Provider-Arbeit beginnen.
