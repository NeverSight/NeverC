**Sprachen**: [English](target-mc-object.md) | [简体中文](target-mc-object.zh-CN.md) | [繁體中文](target-mc-object.zh-TW.md) | [日本語](target-mc-object.ja.md) | [한국어](target-mc-object.ko.md) | [Français](target-mc-object.fr.md) | [Deutsch](target-mc-object.de.md) | [Español](target-mc-object.es.md) | [Italiano](target-mc-object.it.md) | [Русский](target-mc-object.ru.md) | [العربية](target-mc-object.ar.md)

# Target-, MC-, Assembler- und Objekt-Plugins

Die Plugin-ABI der ersten NeverC-Version erlaubt es einem C-Plugin, ein Ziel zu
beschreiben, Codegenerierungsrouten zu ersetzen, die Maschinencode-Ausgabe zu
beobachten, Assembler zu parsen oder auszugeben und Objektdateien zu lesen oder
zu schreiben. Die öffentliche Grenze ist eine reine C-ABI: Plugins dürfen keine
C++-Objekte von LLVM, keine STL-Typen, keine Ausnahmen und keine host-eigenen
Zeiger austauschen, deren Lebensdauer nicht von einer API-Tabelle festgelegt ist.

## Kompatibilitätsstufen

Zielunabhängige Deskriptoren, Phasen-IDs, Artefakt-IDs, MC-Container,
ObjectGraph-Container, Ausgabetransaktionen und Callback-Verträge sind STABLE-ABI
der ersten Version. Zielspezifische Schemata für Opcodes, Register, Operanden,
Fixups, Relokationen und Aufrufkonventionen sind LOCKSTEP. Ein Plugin muss
Ziel-Schema-ID und -Digest vergleichen, bevor es LOCKSTEP-Werte verwendet. NeverC
weist nicht passende Schemata zurück, bevor der Provider aufgerufen wird.

## Ziel und Codegenerierungsroute registrieren

Fragen Sie während der Registrierung `NevercTargetAPI` ab, registrieren Sie einen
oder mehrere `NevercTargetDescriptor`-Einträge und hängen Sie
Zielmaschinen-Deskriptoren und Codegenerierungskanten an. Eine Route wird anhand
des kanonischen Zielschlüssels ausgewählt: Ziel-ID, Triple, CPU, Features, ABI,
Relokationsmodell, Codemodell, Objektformat und Schema-Digest.

Feingranulare Routen verwenden `IR -> MIR -> MC -> ObjectGraph -> ObjectImage`.
Eine grobe Kante kann die gesamte Route `IR -> ObjectImage` ersetzen. Auch grobe
Ausgaben durchlaufen den verpflichtenden Produktverifizierer des Hosts und den
transaktionalen Ausgabe-Commit; ein Provider kann kein Gate umgehen.

## MC erzeugen und beobachten

`NevercMCAPI` besitzt task-lokale `MCUnit`-Mutationen. Beginnen Sie eine Mutation,
erzeugen Sie Sections, Fragmente, Symbole, Ausdrücke, Instruktionen und Operanden
und committen oder verwerfen Sie sie dann. Handles sind task-begrenzt und
generationsgeprüft.

Der zielunabhängige Emissionsstrom stellt geordnete Ereignisse für
Section-Wechsel, Labels, Instruktionen, Ausrichtung, Symbolattribute, CFI,
Debug-Positionen und Daten bereit. `neverc.mc.emission.pre_instruction` ist
ersetzbar; die übrigen Ereignisphasen sind schreibgeschützte Beobachtungspunkte.
Siehe `pluginsdk/examples/MCObserverPlugin.c`.

Provider für Kodierung, Dekodierung und Layout arbeiten mit demselben
Zielschlüssel und Schema-Digest. Das Layout verantwortet die Relaxation und gibt
einen Beweis-Digest aus. Jede Mutation nach dem Layout entwertet diesen Beweis und
erzwingt vor dem Schreiben des Objekts ein erneutes Layout.

## Assembler-Syntax ersetzen

Ein Assembler-Parser-Provider konsumiert Quellbytes und veröffentlicht eine
`MCUnit`. Ein Assembler-Printer konsumiert eine `MCUnit` und schreibt
ausschließlich über die bereitgestellte Ausgabetransaktion. Vorverarbeiteter
Assembler (`.S`) durchläuft vor dem Parser-Provider den normalen
Frontend-Präprozessor; reiner Assembler (`.s`) gelangt direkt in den Parser.

Provider stellen die Ausgabe zunächst bereit. Parse-/Druckverifikation und das
Commit-Gate des Hosts laufen, bevor Bytes sichtbar werden, sodass ein Fehlschlag
keine partielle Ausgabe hinterlässt.

## Objekte lesen, umschreiben und schreiben

`NevercObjectAPI` stellt eine relozierbare Datei als normalisierten ObjectGraph
dar: Sections, Symbole, Relokationen, Gruppen/COMDATs, Importe/Exporte,
TLS-Metadaten, Unwind-Datensätze und Debug-Datensätze. Eingebaute Adapter decken
ELF, COFF und Mach-O ab; Plugins können weitere Formate registrieren.

Die Objekt-Pipeline lautet:

1. Bytes prüfen und in einen ObjectGraph einlesen;
2. `object.pre_write`-Graph-Interceptors ausführen;
3. Layout erstellen und `object.post_layout` ausführen (nach einer Mutation neu
   layouten);
4. ein begrenztes Kandidaten-Image schreiben;
5. `object.post_write`-Binär-Interceptors ausführen;
6. den versiegelten Endverifizierer und den atomaren Host-Commit ausführen.

Observer erhalten schreibgeschützte Brücken. Aus einem Observer versuchte
Mutationen werden mit `NEVERC_STATUS_POLICY_VIOLATION` abgelehnt. Writer und
Post-Write-Interceptors können nur auf den begrenzten transaktionalen Builder
zugreifen; Überlauf, ein fehlgeschlagener Callback oder eine fehlgeschlagene
Verifikation brechen die Bereitstellung ab. Siehe
`pluginsdk/examples/ObjectRewritePlugin.c`.

## Regeln für Nebenläufigkeit und Fehler

- Halten Sie veränderlichen Zustand im vom Host bereitgestellten
  Process-/Session-/Task-Zustand.
- Cachen Sie keine Task-Handles oder geliehenen Sichten über die Rückkehr eines
  Callbacks hinaus.
- Rufen Sie eine Interceptor-Continuation höchstens einmal und im Callback-Thread
  auf.
- Geben Sie den ursprünglichen `NevercStatus` zurück; veröffentlichen Sie keine
  Teilprodukte.
- Deklarieren Sie die engsten wahrheitsgemäßen Nebenläufigkeits- und
  Reentrancy-Modi.

Der ausführbare Abdeckungsvertrag ist `docs/plugin-api/coverage.json`. Er ordnet
jeder stabilen Phase positive, negative, Ersetzungs-, Nur-Lese-Observer- und
Sealed-Gate-Tests zu.
