**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Dokumentationsindex](../README.de.md) · [← NeverC-Projekt](../../README.md)

# Release-Binärdateien und `--strip`

Verwenden Sie `--strip` für zu verteilende Programme, Shared Libraries oder
fertige Android-Kernelmodule. Der kurze Alias ist `-s`; beide Schreibweisen
verhalten sich identisch.

## Schnellstart

```bash
neverc -O2 --strip app.c -o app
neverc -O2 -s app.c -o app

cd examples/android-kernel-hello
neverc make release
```

NeverC entfernt Metadaten direkt im integrierten Linker und startet kein
externes `llvm-strip`. Derselbe Befehl funktioniert daher beim Cross-Compiling
für ELF-, Mach-O- und PE/COFF-Ausgaben.

Verwechseln Sie diese CLI-Option nicht mit dem CMake-Paketierungsschalter
`NEVERC_STRIP_BINARY`: Dieser bearbeitet nach dem Build ausschließlich die
`neverc`-Compilerdatei und kann ein externes Strip-Werkzeug aufrufen. Von
NeverC erzeugte Programme werden dadurch nicht beeinflusst.

## Debug- und Symbolrichtlinie

| Aufruf | Debug-Informationen auf Quelltextebene | Gewöhnliche statische Symbolnamen | Darwin `.dSYM` |
|--------|----------------------------------------|-----------------------------------|----------------|
| Standard (ohne `-g`) | Nicht erzeugt | Können verbleiben; genaue Vorgabe ist formatabhängig | Nicht erzeugt |
| `-g` | Erzeugt | Bleiben erhalten | Bei normalem Darwin-Link erzeugt |
| `--strip` | Falls vorhanden entfernt | Nicht zur Laufzeit nötige Namen entfernt | Nicht erzeugt |
| `-g --strip` | Strip-Richtlinie gewinnt; im ausgelieferten Image nicht vorhanden | Nicht zur Laufzeit nötige Namen entfernt | Unterdrückt |

Ohne `-g` erzeugt das Frontend keine Debug-Informationen auf Quelltextebene.
Das bedeutet **nicht**, dass die Ausgabe vollständig gestrippt ist: ELF und
Mach-O können gewöhnliche Symbolnamen enthalten; PE hat normalerweise keine
statische COFF-Symboltabelle, sofern Debug-Einstellungen sie nicht anfordern.
Auto-LTO kann lokale Namen verwerfen, garantiert aber kein strip-all.

`-g` wechselt von keinen Quelldebuginformationen zu deren Erzeugung; es legt
nicht „mehr“ auf standardmäßig vorhandene Debugdaten. Unwind-Metadaten wie
`.eh_frame` bei ELF/Mach-O oder `.pdata`/`.xdata` bei PE sind Laufzeitdaten,
kein Quell-DWARF, und dürfen im gestrippten Image verbleiben.

## Implementierung und Formatverhalten

Der Treiber wandelt `--strip` in eine stark typisierte Linker-Richtlinie um und
übergibt sie an alle drei Backends. Jedes wendet sie mit Formatwissen an und
bewahrt Namen und Datensätze, die Loader oder dynamische ABI benötigen.

| Format | Entfernt | Bei Bedarf bewahrt |
|--------|----------|--------------------|
| ELF | `.debug*`-Daten und gewöhnliche statische Symbol-/Stringtabellen | Dynamische Importe/Exporte, Relokations- und Loader-Metadaten, Unwind-Informationen |
| Android-Kernel `.ko` (ELF ET_REL) | `.debug*`, `.comment` und für erhaltene Relokationen unnötige lokale/undefinierte Symbole | Eine mit `.strtab` verknüpfte `.symtab`, alle Relokationen und Ziele, definierte globale Symbole, Importe, `__versions`, `.codetag.alloc_tags`, Modul-ABI |
| Mach-O | Debug-Maps/STABS, nicht zur Laufzeit nötige lokale/globale Symbole und Erzeugung des begleitenden `.dSYM` | Binding-/Importdaten, exportierte ABI-Namen, Export-Trie-Einträge, zur Laufzeit referenzierte Symbole |
| PE/COFF | Eingebettete DWARF-Abschnitte und vorhandene statische COFF-Symbol-/Stringtabellen | PE-Importe/Exporte, Unwind-Tabellen, Load-Konfiguration und weitere Loader-Metadaten |

## Geltungsbereich und Vorrang

- `--strip` unterstützt fertig gelinkte Programme, Shared Libraries und die
  unten beschriebene enge Ausnahme für ein endgültiges Android-`.ko`.
- Mit `-c`, gewöhnlichem `-r`, einer Android-Zwischen-`.o`,
  `--emit-static-lib` oder `-fdyncode` meldet NeverC einen Fehler.
- Die Strip-Richtlinie hat Vorrang vor `-g` und Backend-Debugschaltern.
- Sowohl die standardmäßige Auto-LTO-Pipeline als auch `-fno-lto` sind abgedeckt.
- Für die dynamische ABI nötige Import- und Exportnamen bleiben erhalten.

## Android-Kernelmodule

Ein fertiges `.ko` bleibt ELF `ET_REL`. Der Linux-Modullader benötigt eine
Symboltabelle, ihre Stringtabelle, undefinierte Importe und Relokationen und
weist daher strip-all zurück. NeverC erlaubt `-r --strip` nur für ein
Android-Ziel mit `-fandroid-kernel-driver-mode`, `-r` und einem Ausgabenamen,
der auf `.ko` endet. Gewöhnliches `-r` und Zwischen-`.o` bleiben abgelehnt.

Dieser Pfad entspricht der sicheren Grenze von
`llvm-strip --strip-unneeded`, nicht `--strip-all`: Debugdaten, `.comment` und
für Relokationen unnötige lokale/undefinierte Symbole werden entfernt und
`.strtab` wird neu aufgebaut. Erhalten bleiben `.symtab`, alle Relokationen und
notwendigen Ziele, definierte nicht-lokale Symbole, Importe, `__versions`,
`.codetag.alloc_tags` und `.gnu.linkonce.this_module`. Verwenden Sie kein
`llvm-strip --strip-all` für `.ko` und entfernen Sie codetag-Abschnitte nicht
blind. Strippen Sie vor dem Signieren der endgültigen Bytes; `clean` darf nur
Dateien löschen.

## Sicherheitsgrenze

Stripping entfernt wertvolle Namen und Debug-Metadaten und verteuert die
Analyse. Es ist jedoch **keine** Verschleierung und verhindert Reverse
Engineering von Maschinencode nicht. Ein korrekt gestripptes Binary kann enthalten:

- dynamische Import- und Exportnamen, die der Loader benötigt;
- Symbolnamen, die erhaltene `.ko`-Relokationen benötigen;
- Stringliterale, Reflection-Tabellen oder anwendungseigene Metadaten;
- Unwind-, Relokations-, Signatur- und Load-Konfigurationsdatensätze;
- Maschinencode und seinen beobachtbaren Kontrollfluss.

`--strip` steuert nur das endgültige Image. Separat angeforderte Artefakte wie
Link-Maps, Optimierungsberichte oder `-save-temps`-Ausgaben werden nicht
gelöscht; prüfen Sie das Release-Verzeichnis und verteilen Sie diese
Begleitdateien nicht.

Nutzen Sie Stringverschlüsselung, Verschleierung und Manipulationsschutz bei
Bedarf als getrennte Schichten und betten Sie keine geheimen Werte ein.

## Artefakt prüfen

Prüfen Sie Release-Artefakte in CI mit den LLVM-Objektwerkzeugen. Passen Sie
die Befehle an das Format an und erlauben Sie benötigte ABI-Namen ausdrücklich.

```bash
llvm-readobj --sections --symbols --dyn-symbols app
llvm-dwarfdump app
strings app | grep neverc_private_release_symbol
test ! -e app.dSYM

llvm-readelf -h -S -s -r examples/android-kernel-hello/nvk_hello.ko
llvm-dwarfdump examples/android-kernel-hello/nvk_hello.ko
```

Ein gestripptes Artefakt sollte keine Quelldebugabschnitte oder privaten
statischen Symbolnamen enthalten. Benötigte dynamische Namen und Laufzeitdaten
sind erwartet und kein Fehler.
