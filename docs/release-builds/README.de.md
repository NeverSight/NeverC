**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← Dokumentationsindex](../README.de.md) · [← NeverC-Projekt](../../README.md)

# Release-Binärdateien und `--strip`

Verwenden Sie `--strip` für zu verteilende Programme, Shared Libraries oder
fertige Android-Kernelmodule. Der kurze Schalter ist `-s`; beide Schreibweisen
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
| Android-Kernel `.ko` (ELF ET_REL) | `.debug*`, `.comment`, für Relokationen unnötige lokale/undefinierte Einträge und lesbare Namen gewöhnlicher erhaltener Definitionen | Eine mit `.strtab` verknüpfte `.symtab`, alle Relokationen und Ziele, exakte Loader-/CFI-Namen, exakte Importe, Namen in geschützten Abschnitten und Modul-ABI-Metadaten |
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

`neverc make release` bleibt der empfohlene Release-Befehl und wird zu
`-O2 --strip`. Diese Beispiele erfordern `neverc make`; ein externes `make`
wird abgewiesen, weil rekursive Weitergabe von Kommandozeilenvariablen,
Statusprüfung und sperrbewusste Bereinigung zum Build-Vertrag gehören. Ohne ein
geprüftes Paar aus `.nvk-build-flags` und `.nvk-build-integrity` verwendet
`neverc make` standardmäßig debug und wählt release nicht von selbst. Die Statusdateien
liegen neben `MODULE`, auch wenn es ein Unterverzeichnis bezeichnet. Die
Beispiel-Makefiles speichern eine
ausdrückliche Profilwahl, damit spätere `neverc make push`-, `neverc make run`-
und `neverc make`-Aufrufe dasselbe Artefakt verwenden. Beispiele mit `EXTRA` bewahren
dessen vollständigen Wert mit mehreren Wörtern über rekursive und spätere Builds
hinweg; rekursive Profilziele behalten auch andere Kommandozeilenüberschreibungen.
`neverc make debug`, `neverc make release` und `neverc make clean` müssen jeweils als einziges
Ziel aufgerufen werden; die Kombination mit einem weiteren Ziel wird zum Schutz
der gemeinsamen Ausgaben vor Wettläufen abgelehnt. `neverc make debug` oder ein
ausdrückliches `neverc make PROFILE=...` ersetzt die gespeicherte Wahl nur, wenn das
Modul/Map/Status-Bundle veröffentlicht wird; ein Fehler vor der Veröffentlichung lässt
das vorherige Artefakt und den vorherigen Status unverändert.
`.nvk-build-integrity` bindet die SHA-256-Werte des Moduls, der Build-Kennung
und des optionalen `EXTRA`-Status. Fehlender oder abweichender Status nach einer
unterbrochenen Veröffentlichung wird ignoriert und erzwingt einen Neuaufbau.
`neverc make clean` löscht den Status unter der Veröffentlichungssperre, sodass der
nächste Build wieder debug verwendet. Bei einem geprüften release-Profil baut ein
zielloses `neverc make` neu, wenn die Map fehlt. Ein ausdrückliches
`neverc make release` baut das
Modul/Map/Status-Bundle einmal bedingungslos neu; ein erneuter Aufruf repariert daher
auch ein Bundle mit abweichendem Digest. Auf diesem finalen Modulpfad entfernt
NeverC Debugabschnitte, `.comment` und für Relokationen unnötige
lokale/undefinierte Einträge und baut `.strtab` neu auf.

Nach einem erfolgreichen Release veröffentlicht NeverC das Modul und
`<module>.ko.symbols.json` daneben transaktional. Vorhandene Dateien bleiben
bis zu ihrer jeweiligen atomaren Ersetzung sichtbar. Parallele
Veröffentlichungen in dasselbe Ausgabeverzeichnis und `neverc make clean` werden über
`.neverc-output.lock` serialisiert. Die Bereinigung entfernt das Bundle
transaktional, bewahrt diese interne Sperrdatei aber absichtlich auf. Fehler vor der Veröffentlichung rollen das gesamte
Bundle zurück; späte Dauerhaftigkeitsfehler behalten ein
Wiederherstellungsjournal. Da zwei Verzeichniseinträge nicht mit einer einzigen
Dateisystemoperation ersetzt werden können, wird die Build-Statusintegrität
automatisch geprüft; prüfen Sie nach einem unsauberen Abbruch dennoch stets
`image_sha256` der Map. Die Zuordnung enthält für jedes erhaltene Symbol
mit geändertem Namen dessen `original`- und `release`-Namen:

```json
{
  "format": "neverc.android-kernel-symbol-map",
  "version": 2,
  "image_sha256": "…",
  "symbols": [
    {"original": "worker_dispatch", "release": "fn_C000"}
  ]
}
```

Die Einträge sind nach `release` sortiert. Entfernte Symbole sowie exakte
Loader-, Import- oder CFI-Namen fehlen, da sie keine Übersetzung benötigen.
Überschreibt ein Debug- oder anderer Nicht-Strip-Build denselben Ausgabepfad,
entfernt NeverC die veraltete Zuordnung. ELF erlaubt Nicht-UTF-8-Bytes in
Symbolnamen; solche seltenen Originalnamen werden in `original` als Base64
mit `"original_encoding": "base64"` gespeichert. Alle übrigen Originalnamen
bleiben lesbar. NeverC veröffentlicht die Sidecar-Datei unter POSIX mit Modus
`0600` und unter Windows mit einer geschützten, ausschließlich dem Eigentümer
gewährten `Windows ACL`; kann diese Einschränkung nicht gesetzt werden, schlägt
die Veröffentlichung fehl. Archivieren Sie die Zuordnung als privates
Debug-Artefakt und verteilen Sie sie weder mit dem `.ko` noch auf das Gerät.
Prüfen Sie vor der Übersetzung eines Release-Namens aus einem Absturzbericht
zunächst die Bindung:

```bash
actual="$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' \
  nvk_hello.ko)" &&
expected="$(jq -er '.image_sha256 | strings | select(test("^[0-9a-f]{64}$"))' \
  nvk_hello.ko.symbols.json)" &&
test "$actual" = "$expected" &&

python3 - nvk_hello.ko.symbols.json fn_C000 <<'PY'
import base64, json, sys
with open(sys.argv[1], encoding="utf-8") as stream:
    entry = next(item for item in json.load(stream)["symbols"]
                 if item["release"] == sys.argv[2])
original = entry["original"]
print(repr(base64.b64decode(original))
      if entry.get("original_encoding") == "base64" else original)
PY
```

Geeignete erhaltene Definitionen bekommen deterministische, von IDA inspirierte
Strukturnamen, ohne dessen reservierte Präfixe zu verwenden:

- `STT_FUNC` wird zu `fn_HEX`;
- `STT_OBJECT` wird zu `obj_HEX`;
- ausführbares `STT_NOTYPE` wird zu `code_HEX`;
- anderes alloziertes `STT_NOTYPE` wird zu `sym_HEX`;
- `SHN_ABS` wird zu `abs_HEX`;
- eine Definition außerhalb von `SHF_ALLOC` wird zu
  `sym_S<FINAL_SECTION_ORDINAL_HEX>_<OFFSET_HEX>`.

Alle `HEX`-Felder, einschließlich beider Felder der nicht allozierten Form,
werden als großgeschriebene Hexadezimalzahlen ohne überflüssige führende Nullen
ausgegeben. Benötigen mehrere Symbole dieselbe Schreibweise, folgen
deterministische dezimale Namensvarianten `_1`, `_2` usw.

Diese Schreibweisen sind von IDA inspiriert, belegen aber nicht dessen
Dummy-Namensraum. In einer neuen IDA-9.4-Datenbank erscheinen gespeicherte
ELF-Benutzersymbole `sub_0`, `sub_4` und `loc_8` als `_sub_0`, `_sub_4` und
`_loc_8`, während `fn_0`, `code_8` und `obj_10` unverändert bleiben. Die
Hex-Rays-Dokumentation zu
[`SN_NODUMMY`](https://python.docs.hex-rays.com/ida_name/index.html) bestätigt,
dass einem Benutzernamen mit einem Dummy-Präfix wie `sub_` ein Unterstrich
vorangestellt wird. NeverC leert den `st_name` einer gewöhnlichen Definition
nicht absichtlich, um IDA `sub_` erzeugen zu lassen: Kallsyms für
Android/Linux-Module ignoriert historisch Einträge ohne Namen, und ein leerer
Name würde den prüfbaren serialisierten Namensvertrag entfernen. Bereits
notwendige leere Einträge und Abschnittssymbole bleiben exakt.

ELF erlaubt mehreren Symbolen dieselbe canonical analysis EA. NeverC erhält
oder erzeugt den vollständigen Alias-Satz in `.symtab`; das
Adressnamensmodell von IDA 9.4 materialisiert jedoch möglicherweise nur einen
primären Namen für Symbole an derselben Adresse. Ein in IDA nicht angezeigter
Alias ist daher nicht zwangsläufig aus ELF verschwunden; der vollständige Satz
ist mit `llvm-readelf` oder `llvm-nm` zu prüfen.

Bei einem allozierten Symbol ist `HEX` NeverCs canonical analysis EA, also die
kanonische effektive Adresse ausschließlich für die statische Analyse. Beginnend
mit Cursor null besucht NeverC die endgültig erhaltenen `SHF_ALLOC`-Abschnitte
in finaler Abschnittstabellen-Reihenfolge, richtet den Cursor an
`max(sh_addralign, 1)` aus, merkt ihn als Abschnittsbasis und erhöht ihn um
`max(sh_size, 1)`; die EA ist diese Basis plus finalem `st_value`. `abs_HEX`
verwendet den absoluten finalen `st_value`. In der nicht allozierten Form ist
`FINAL_SECTION_ORDINAL_HEX` der finale Abschnittsindex und `OFFSET_HEX` der
finale `st_value` in diesem Abschnitt. Diese Koordinaten sind weder Hashwert
noch Verschlüsselung, Dateioffset, virtuelle ELF-Adresse oder Laufzeitadresse
des Kernels. Loader und KASLR können das Modul zur Laufzeit anders platzieren.

Exakt erhalten bleiben:

- jeder `SHN_UNDEF`-Import, da der Modullader ihn nach Namen auflöst;
- Symbole, die in `.modinfo`, `.text.ftrace_trampoline`,
  `.gnu.linkonce.this_module`, `__versions` oder `.codetag.alloc_tags` definiert sind;
- `init_module`, `cleanup_module`, `__cfi_check`, `__cfi_check_fail`,
  `__cfi_jt_init_module` und `__cfi_jt_cleanup_module`;
- Namen, die mit `__typeid__` oder `__kcfi_typeid_` beginnen.

Der von IDA angezeigte `extern`-Bereich ist eine synthetische Analyseansicht und
kein echter ELF-Abschnitt. In einem finalen `ET_REL`-`.ko` sind externe
Relokationsziele `SHN_UNDEF`-Einträge in `.symtab`, deren exakte Namen der Loader
benötigt. Die Richtlinie folgt daher der tatsächlichen ELF-Symbolklasse und dem
Definitionsabschnitt: Undefinierte Importe bleiben exakt, geeignete Definitionen
werden unabhängig von der Gruppierung des Analysewerkzeugs umbenannt.

Alle Namen werden vor der Änderung global geplant. Definitionen mit demselben
Basiskandidaten erhalten in deterministischer Reihenfolge die unnummerierte Form,
dann die Namensvarianten `_1`, `_2` usw.; dieser normale Fall ist kein Fehler.
Die Finalisierung bricht ab, wenn ein erzeugter Name mit dem reservierten
Namensraum der unverändert zu erhaltenden Namen kollidiert oder die Koordinaten-
bzw. Nummernberechnung den Zahlenbereich überschreitet.
Bei `SHN_COMMON`, `SHN_LIVEPATCH` oder einem unbekannten reservierten
ELF-Abschnittsindex bricht sie ebenfalls sicher ab, statt zu raten.
`SHN_COMMON` ist in einem ladbaren finalen Modul ungültig; kompilieren Sie mit
`-fno-common`. Livepatch-Module benötigen ursprüngliche Reihenfolge und Indizes
der Symboltabelle sowie zusätzliche Relokationsmetadaten, deren Erhalt diese
Richtlinie nicht verspricht.

Die Erkennung nutzt mehrere Signale: Jedes `SHN_LIVEPATCH`-Symbol, jeder
`.klp.*`-Abschnitt, jedes `SHF_RELA_LIVEPATCH`-Flag oder jedes NUL-getrennte
`.modinfo`-Feld, das mit `livepatch=` beginnt, kennzeichnet ein Livepatch-Modul
und führt zur sicheren Ablehnung. Der `.modinfo`-Marker allein genügt, auch
wenn weder ein `.klp.*`-Abschnitt noch ein Livepatch-Relokationsflag vorhanden ist.

Nur geeignete Namen in `.symtab` werden ersetzt. Ein ladbares `.ko` benötigt
weiterhin `.symtab`, die verknüpfte `.strtab` und Relokationen; allgemeine
Werkzeuge dürfen es daher zu Recht als `not stripped` bezeichnen. Unabhängige
Speicher und Schnittstellen wie BTF, Modulexporte, `.modinfo`, `__versions`,
Trace-Metadaten, `__ksymtab_strings`, `.rodata` und Stringliterale können
weiterhin Originalnamen oder identifizierenden Text verraten. Gewöhnliche
Kernelsymbolnamen ändern sich auch in kallsyms und Diagnosen. Dadurch werden
namensbasiertes ftrace, kprobe/BPF-Attachments und Absturzberichte weniger
nützlich. Verwenden Sie zur Diagnose einen ungestrippten Debug-Build und
verlassen Sie sich im Release-Modul nicht auf private Originalnamen.

### Plugin-Grenze eines finalisierten Android-Releases

Die Finalisierung errichtet um die Plugin-Ausgabephasen zwei unabhängige,
fail-closed Identitätsgrenzen:

- Vor jeder ersetzbaren `ObjectGraph`-Phase bindet das Graph-Siegel für jeden
  beibehaltenen logischen Abschnitt `section ID`, `final ordinal` und exakten
  Namen. Außerdem bindet es die `symbol ID` jedes Symbols mit exaktem Namen an
  Name, Klasse, Abschnitt, Wert, Größe, Binding, Typ und das vollständige
  `st_other`. Der Release-Verifizierer berechnet gewöhnliche Strukturnamen
  unabhängig neu.
- Nachdem der Host eine vertrauenswürdige Schreib-Baseline erstellt hat und vor
  `neverc.object.post_write` bindet das Image-Siegel Ordinal und Namen jedes
  beibehaltenen logischen Abschnitts, die Gesamtzahl der `.symtab`-Einträge und
  Namen sowie Attribute jedes Exact-Name-Symbols an seinen rohen `.symtab`
  `slot`.

Die Capability-Matrix ist daher absichtlich eng:

| Phasen-Binding | Verhalten im finalisierten Android-Release |
|----------------|---------------------------------------------|
| `neverc.object.write` `provider` / `interceptor` | `REJECTED`, bevor die vom Host errichtete vertrauenswürdige Schreib-Baseline ersetzt werden kann |
| `plugin-owned ObjectFormat graph writer` | `REJECTED`; das finalisierte Android-Release benötigt den host-eigenen Graph Writer, der die vertrauenswürdige Baseline errichtet |
| `observer` | `READ_ONLY`; Beobachtung bleibt erlaubt, Mutation des Artefakts nicht |
| `neverc.object.post_write` `interceptor` | `VALIDATED`; nur Payload-Bytes außerhalb der Identitätsfläche dürfen sich ändern, und das Ergebnis muss Release-Verifizierer, Eingabe-ABI-Vertrag und beide Identitätssiegel weiterhin erfüllen |

Auch die Eigentümerschaft des finalisierten Merges wird vom Host versiegelt.
Ein `MergedImage` oder unabhängige Bytes eines `third-party ObjectMergeProvider`
werden verworfen; der `host-owned graph writer` serialisiert dessen verifizierten,
finalisierten Graphen. Umgekehrt umgeht `built-in finalized input serialization`
die `external object phases` und übergibt dem Host-Merger exakt die
`audited native bytes`; dieser interne Eingabeschritt umgeht die obige
Ausgabegrenze nicht.

Finalisierung wird nur mit `Android module merge semantics` akzeptiert; sie
erfordert außerdem sowohl eine `relocatable output request` als auch eine
`relocatable driver configuration`, andernfalls schlägt sie `before routing`
fehl. Für ein finalisiertes Android-Relocatable-Release müssen
`frozen input format`,
`TargetKey.ObjectFormatID` und `frozen output format` dieselbe
`one format identity` teilen. Eine Abweichung wird `before provider dispatch`
abgelehnt, also auch vor Route Planning oder Sink-Erstellung; Capability
Preflight und tatsächlicher Graph-Writer-Dispatch können dadurch keine
unterschiedlichen Formate sehen.

Bei gewöhnlicher graph-repräsentierbarer Eingabe dürfen frühere
Graph-Interceptors nur laufen, solange sie Graph-Siegel und alle
Release-Semantiken bewahren. Benötigt die Eingabe Native-Image-Passthrough für
Fakten, die der `ObjectGraph` nicht darstellen kann, werden jeder ersetzbare
`route-matching provider` und alle Interceptors abgelehnt. Ein Provider mit
nicht passender target/CPU/features/object-format/execution-level Route wird
weder ausgeführt noch blockiert er das Release; nur Read-only-Observers sind
erlaubt. Nur eine
Ablehnung oder fehlgeschlagene Validierung `before sealed commit` bricht das
Staging ab und veröffentlicht keine Datei. Ein Fehler eines `AFTER_COMMIT`-
Observers wird nach der Veröffentlichung gemeldet und kann die veröffentlichte
Datei nicht zurückrollen.

Bearbeiten Sie ein `.ko` nicht nachträglich mit `llvm-strip --strip-all` oder
`objcopy`, und entfernen Sie codetag/BTF/ABI-Abschnitte nicht blind. Strippen Sie
vor dem Signieren der endgültigen Bytes; jede spätere Änderung macht die Signatur
ungültig. `clean` darf nur Dateien löschen, nie ein vorhandenes Modul strippen
oder signieren.

## Sicherheitsgrenze

Stripping entfernt wertvolle Namen und Debug-Metadaten und verteuert die
Analyse, verhindert Reverse Engineering von Maschinencode jedoch nicht. Ein
korrekt gestripptes Binary kann enthalten:

- dynamische Import- und Exportnamen, die der Loader benötigt;
- Loader-Namen und außerhalb von `.symtab` gespeicherte Namen eines `.ko`;
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
Die negierte `strings`-Prüfung unten darf keinen Treffer finden und ist nur dann
erfolgreich.

```bash
llvm-readobj --sections --symbols --dyn-symbols app
llvm-dwarfdump app
! strings app | grep -Fq -- neverc_private_release_symbol
test ! -e app.dSYM

file examples/android-kernel-hello/nvk_hello.ko
llvm-readelf -h -S -s -r examples/android-kernel-hello/nvk_hello.ko
llvm-dwarfdump examples/android-kernel-hello/nvk_hello.ko
```

Bei einem ladbaren ELF-`ET_REL`-`.ko` kann das allgemeine Werkzeug `file`
weiterhin `not stripped` melden, weil `.symtab` absichtlich erhalten bleibt.
Verwenden Sie diese Bezeichnung nicht als Release-Erfolgskriterium. Prüfen Sie
stattdessen, dass DWARF und `.comment` fehlen, geeignete Definitionen die
kanonischen Formen `fn_`/`obj_`/`code_`/`sym_`/`abs_` mit großgeschriebenen
Hexadezimalzahlen tragen, `SHN_UNDEF`-Importe und erforderliche Loader-/CFI-Namen
exakt bleiben und Relokationen gültig sind. Prüfen Sie BTF, Exporte, modinfo, versions,
Trace-Metadaten und Strings separat, wenn Namensoffenlegung relevant ist.

Ein gestripptes Artefakt sollte keine Quelldebugabschnitte oder privaten
statischen Symbolnamen enthalten. Benötigte dynamische Namen und Laufzeitdaten
sind erwartet und kein Fehler.
