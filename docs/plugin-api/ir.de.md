**Sprachen**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

# IR-API für NeverC-Plugins

Die erste öffentliche Plugin-ABI stellt LLVM IR über stabile C-Tabellen bereit.
Plugins binden keine LLVM-Header ein und dürfen NeverC-Handles nicht in
LLVM-Objekte umwandeln.

## Schnittstellen

Fragen Sie Schnittstellen in `neverc_plugin_entry` mit
`NevercBootstrapAPI.QueryInterface` ab:

- `NEVERC_INTERFACE_IR_CORE` — Abfragen zu Modulen, Typen, Werten, CFG,
  Metadaten, Attributen, Konstanten und Serialisierung.
- `NEVERC_INTERFACE_IR_BUILDER` — transaktionale IR-Konstruktion und -Mutation.
- `NEVERC_INTERFACE_IR_ANALYSIS` — eingebaute und plugin-definierte Analysen.
- `NEVERC_INTERFACE_IR_PASS` — Module-, CGSCC-, Function- und Loop-Passes.
- `NEVERC_INTERFACE_IR_GEN` — Ersatz der Absenkung von SemanticUnit nach IR.
- `NEVERC_INTERFACE_IR_OPTIMIZATION` — vollständiger Ersatz der
  Optimierungspipeline.

Fordern Sie stets das Major/Minor-Paar des Headers an und prüfen Sie, dass die
zurückgegebene `StructSize` bis zum letzten vom Plugin genutzten Funktionszeiger
reicht. Ein neuerer Host kann Felder anhängen; ein Plugin muss unbekannte Enden
ignorieren.

## Handles und Eigentümerschaft

IR-Handles sind opake `{Owner, Value}`-Paare mit Task-Gültigkeit. Alle von ihnen
referenzierten Objekte gehören dem Host.

- Behalten Sie ein task-begrenztes Handle niemals über das Ende seines Callbacks
  oder seiner Task hinaus.
- Verwenden Sie ein Handle niemals in einer anderen Session oder Task.
- Ein committeter Ersatz invalidiert die Handles der ersetzten Objekte.
- Eine abgebrochene Mutation macht die von ihr erzeugten Handles ungültig.
- APIs melden `NEVERC_STATUS_STALE_HANDLE`, `WRONG_OWNER` oder `WRONG_TYPE`,
  statt einen LLVM-Zeiger offenzulegen.

Von Abfragen zurückgegebene Zeichenketten und Byte-Sichten sind geliehen, sofern
eine API nicht ausdrücklich einen freigebbaren Puffer zurückgibt.

## IR lesen

`NevercIRCoreAPI` bietet:

- Modulbezeichner, Triple, Data Layout und Inline-Assembler;
- stabile Wert-Cursor für Funktionen, Globals, Blöcke, Instruktionen,
  Verwendungen und Operanden;
- stabile Typ- und Opcode-IDs;
- Eigenschaften von Funktionen, Globals, Instruktionen, Metadaten und Attributen;
- Ganzzahl-, Gleitkomma-, Aggregat-, Null-, Poison- und Undef-Konstanten;
- Bitcode-Export/-Import und verifizierte Modulartefakte.

Sammlungs-Cursor sind begrenzt: Übergeben Sie eine Ausgabekapazität und sammeln
Sie weiter, bis die zurückgegebene Anzahl null ist.

## Transaktionale Mutation

Jede strukturelle Mutation verwendet `NevercIRBuilderAPI`:

1. Eine Modul- oder Funktionsmutation beginnen.
2. Einen an diese Mutation gebundenen Builder erzeugen.
3. Den Einfügepunkt setzen und Instruktionen, Funktionen oder Blöcke bauen.
4. Die Mutation committen.
5. Builder und Mutations-Handle zerstören.

Der Commit verifiziert die Kandidaten-IR und veröffentlicht sie atomar. Schlägt
der Verifizierer fehl, rollt der Host die Mutation zurück und behält das
vorherige Modul. `AbortMutation` rollt vorgemerkte Änderungen stets zurück.

Beanspruchen Sie nach einer IR-Änderung nicht `NEVERC_IR_PRESERVE_ALL`. Der
Pass-Adapter prüft die Modulgeneration und lehnt eine widersprüchliche
Erhaltungserklärung ab.

## Pass-Ebenen und Phasen

`NevercIRPassDescriptor.Level` unterstützt:

- `NEVERC_IR_PASS_LEVEL_MODULE`
- `NEVERC_IR_PASS_LEVEL_CGSCC`
- `NEVERC_IR_PASS_LEVEL_FUNCTION`
- `NEVERC_IR_PASS_LEVEL_LOOP`

Die stabilen Einfügephasen sind `PRE_OPT`, `PIPELINE_START`, `OPTIMIZER_LAST`,
`POST_OPT` und `PRE_CODEGEN`. Ein Aufruf enthält nur die für seine Ebene gültigen
Handles. Funktions- und Schleifen-Passes können nebenläufig laufen; veränderlicher
Plugin-Zustand muss daher dem deklarierten Concurrency-Vertrag folgen.

Der Host führt stets den finalen versiegelten IR-Verifizierer aus. Ein Plugin kann
dieses Gate weder ersetzen noch abfangen oder überspringen.

## Analysen

Die IDs eingebauter Analysen decken Aufrufgraph, Dominatorbaum,
Postdominatorbaum, Schleifeninformationen, Scalar Evolution, MemorySSA und
Alias-Analyse ab.

Plugin-Analysen deklarieren Abhängigkeiten und Lebenszyklus-Callbacks. Ergebnisse
werden pro Aufruf zwischengespeichert und gemäß dem Erhaltungsergebnis des Passes
invalidiert. Rekursive Abhängigkeitszyklen und Mutationen aus einem
Analyse-Callback werden abgelehnt.

## Vollständige Provider

Ein IR-Generierungs-Provider kann die eingebaute Absenkung ersetzen und ein
verifiziertes Modulartefakt veröffentlichen. Ein Optimierungs-Provider kann die
gesamte eingebaute Optimierungspipeline ersetzen. Beide Wege:

- konsumieren explizite Phaseneingaben;
- veröffentlichen über eine Host-API, statt einen LLVM-Zeiger zurückzugeben;
- prüfen Zielkompatibilität und Modulgültigkeit;
- behalten bei fehlgeschlagener Veröffentlichung atomar das alte Modul.

Der finale Verifizierer bleibt auch nach einem Optimierungs-Provider Pflicht.

## Minimales Beispiel

`pluginsdk/examples/FunctionPass.c` ist ein schreibgeschützter Funktions-Pass.
`pluginsdk/examples/ExamplePlugin.c` zeigt die Modul-Aufzählung, und
`pluginsdk/examples/CustomCallConvPlugin.c` demonstriert Attribute und
Aufrufstellen-Eigenschaften.

Ein Beispiel bauen und laden:

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

Verwenden Sie das von CMake erzeugte plattformspezifische Modulsuffix.

## Fehlerregeln

Geben Sie aus jedem Callback einen `NevercStatus` zurück. Plugin-Fehler werden zu
strukturierten Diagnosen; werfen Sie keine Ausnahmen über die C-Grenze.
Initialisieren Sie jeden Ausgabetabellen-Header und jedes reservierte Feld und
geben Sie `INVALID_ARGUMENT` für einen fehlenden Pflichtzeiger zurück.

Normative ABI-Deklarationen, Phasenrichtlinien und Testnachweise finden Sie in
`PluginIR.h`, `PluginPhaseSchema.h` und `coverage.json`.
