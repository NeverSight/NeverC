**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC-Plugin-ABI

Das erste öffentliche Plugin-ABI von NeverC ist eine reine C-Schnittstelle auf
Phasenbasis. Ein Plugin ist ein Shared Module, das genau eine Funktion
exportiert, versionierte Fähigkeitstabellen aushandelt und innerhalb expliziter
Process-, Session- und Task-Gültigkeitsbereiche läuft. Es bindet keinen
LLVM-Header ein, linkt den Compiler nicht und tauscht über die Grenze hinweg
keinen C++-Typ aus.

Die unveröffentlichte Prototyp-API und ihr Einstiegspunkt
`nevercGetPluginInfo` wurden **entfernt**. Prototyp-Binärdateien werden mit
einer Migrationsdiagnose abgewiesen; kompilieren Sie deren Quellen gegen die
öffentlichen Header neu. Die vollständige Zuordnung alt → neu finden Sie unter
[Migration von der Prototyp-API](migration-from-prototype.de.md).

## Hier beginnen

- [Source- und E/A-API](source.de.md)
- [Präprozessor-API](prep.de.md)
- [AST- und Semantik-API](ast-sema.de.md)
- [IR-API](ir.de.md)
- [MIR-API](mir.de.md)
- [Target-, MC-, Assembler- und Objekt-APIs](target-mc-object.de.md)
- [DynCode-API](dyncode.de.md)
- [Benutzerdefinierte Aufrufkonventionen](custom-callconv/README.de.md)
- [Migration von der Prototyp-API](migration-from-prototype.de.md)
- [Nachweis der Phasenabdeckung](coverage.json)

## Ausführungsmodell

Der Host steuert ein Plugin über drei verschachtelte Gültigkeitsbereiche. Jeder
Bereich übergibt dem Plugin einen opaken Zustandszeiger, den das Plugin selbst
alloziert und besitzt. Ein korrekt geschriebenes Plugin braucht daher keinen
globalen veränderlichen Zustand.

| Bereich | Rückrufe | Bedeutung |
|---|---|---|
| Process | `ProcessBegin`, `Register`, `Destroy` | Ein Compilerprozess. Hier werden Schnittstellen abgefragt und Fähigkeiten registriert. |
| Session | `SessionBegin`, `SessionEnd` | Ein Treiberaufruf. |
| Task | `TaskBegin`, `TaskEnd` | Eine Arbeitseinheit, gekennzeichnet durch `NevercTaskKind`. |

Die Task-Arten sind `INVOCATION`, `TRANSLATION_UNIT`, `LTO`, `LINK`, `CODEGEN`,
`OBJECT` und `DYNCODE`.

Der Host ruft zuerst `ProcessBegin` auf, danach genau einmal `Register`. Die
Registrierung ist die einzige Stelle, an der Optionen, Beobachter, Interceptoren
und Provider hinzugefügt werden dürfen; danach ist der Phasengraph eingefroren.

## Phasen

Eine Phase ist ein benannter, versionierter Übergang von einem Eingabe- zu einem
Ausgabeartefakt. NeverC liefert **130 eingebaute Phasen** über die Bereiche
Treiber, Source, Präprozessor, Syntax, Semantik, IR, Codegen, MIR, MC,
Assembler, Objekt, Linken und DynCode, dazu 8 Erweiterungs-ID-Familien, die für
plugindefinierte Phasen reserviert sind.

Jede Phase kündigt eine Richtlinie an, und ein Plugin darf sich nur so
anhängen, wie diese Richtlinie es erlaubt:

| Richtlinien-Flag | Was ein Plugin tun darf |
|---|---|
| `NEVERC_PHASE_OBSERVABLE` | Einen Beobachter für rein lesende Benachrichtigung registrieren. |
| `NEVERC_PHASE_INTERCEPTABLE` | Die Phase umschließen und entscheiden, ob der Rest der Kette aufgerufen wird. |
| `NEVERC_PHASE_REPLACEABLE` | Einen Provider registrieren, der die Ausgabe selbst liefert. |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | Den Übergang überspringen und dabei ein Proof-Handle liefern. |
| `NEVERC_PHASE_SEALED_HOST_GATE` | Nichts. Verifizierer und Commits gehören dem Host und lassen sich weder ersetzen noch abfangen noch überspringen. |

Beobachter werden an den von der Phase deklarierten Punkten zugestellt:
`NEVERC_OBSERVER_BEFORE`, `NEVERC_OBSERVER_AFTER` und
`NEVERC_OBSERVER_AFTER_COMMIT`.

Ein Interceptor erhält eine `NevercPhaseContinuation`. Er muss `InvokeNext`
**höchstens einmal** und im Rückruf-Thread aufrufen und danach in
`NevercPhaseResult.Action` eines von `NEVERC_PHASE_CONTINUE`,
`NEVERC_PHASE_REPLACE` oder `NEVERC_PHASE_SKIP` melden.

Normative Quelle für Phasen-IDs, Richtlinien, Stabilitätsstufen und
Verifizierer-Gates ist
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`. Die generierte Datei
`PluginPhaseSchema.inc` stellt sie als Übersetzungszeitkonstanten wie
`NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH` / `_LOW` bereit.

## Ein vollständiges Minimal-Plugin

Dies ist `pluginsdk/templates/minimal/Plugin.c`. Es lädt, handelt das ABI aus,
registriert nichts und entlädt sauber — kopieren Sie das Verzeichnis und bauen
Sie darauf auf.

```c
#include "neverc/Plugin/NevercPluginAPI.h"

#define MINIMAL_PLUGIN_ID "com.example.minimal"
#define STRING_VIEW_LITERAL(Text)                                              \
  { (Text), (uint64_t)(sizeof(Text) - 1) }

static NevercStatus status_code(NevercStatusCode Code) {
  NevercStatus Status = neverc_status_ok();
  Status.Code = Code;
  return Status;
}

static void copy_bytes(void *Destination, const void *Source, uint64_t Count) {
  uint64_t Index;
  unsigned char *Out = (unsigned char *)Destination;
  const unsigned char *In = (const unsigned char *)Source;
  for (Index = 0; Index != Count; ++Index)
    Out[Index] = In[Index];
}

static NevercStatus NEVERC_CALL
process_begin(const NevercCoreAPI *Core, void **OutProcessState) {
  if (Core == NULL || OutProcessState == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  *OutProcessState = NULL;
  return neverc_status_ok();
}

static NevercStatus NEVERC_CALL
register_plugin(const NevercCoreAPI *Core, const NevercRegistrarAPI *Registrar,
                void *RegistrarContext, void *ProcessState) {
  (void)Core;
  (void)RegistrarContext;
  (void)ProcessState;
  if (Registrar == NULL)
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  /* Hier Optionen, Beobachter, Interceptoren oder Provider registrieren. */
  return neverc_status_ok();
}

NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin) {
  NevercPluginDescriptor Descriptor = {0};
  uint32_t Capacity;
  uint64_t BytesToWrite;
  if (Bootstrap == NULL || OutPlugin == NULL ||
      OutPlugin->Header.StructSize < sizeof(uint32_t))
    return status_code(NEVERC_STATUS_INVALID_ARGUMENT);
  Capacity = OutPlugin->Header.StructSize;
  Descriptor.Header = (NevercABITableHeader){
      sizeof(Descriptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
  Descriptor.PluginID = (NevercStringView)STRING_VIEW_LITERAL(MINIMAL_PLUGIN_ID);
  Descriptor.DisplayName =
      (NevercStringView)STRING_VIEW_LITERAL("Minimal Plugin");
  Descriptor.Version = (NevercSemanticVersion){1, 0, 0, 0};
  Descriptor.Concurrency = NEVERC_CONCURRENCY_SESSION_SERIAL;
  Descriptor.Reentrancy = NEVERC_REENTRANCY_ALLOWED;
  Descriptor.ProcessBegin = process_begin;
  Descriptor.Register = register_plugin;
  BytesToWrite = Capacity < sizeof(Descriptor) ? Capacity : sizeof(Descriptor);
  copy_bytes(OutPlugin, &Descriptor, BytesToWrite);
  return neverc_status_ok();
}
```

`OutPlugin` ist ein Puffer im Besitz des Aufrufers. Beim Eintritt gibt
`Header.StructSize` die beschreibbare Kapazität an; das Plugin schreibt höchstens
so viele Bytes und meldet die tatsächlich erzeugte Größe.

## Aushandeln von Schnittstellen

Fähigkeitstabellen werden über eine 128-Bit-Schnittstellen-ID geholt, nicht über
Symbole. Fordern Sie die Hauptversion an, gegen die Sie übersetzt haben, sowie
die kleinste Nebenversion, mit der Sie arbeiten können:

```c
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus Status = Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_IR_PASS_HIGH,
                        NEVERC_INTERFACE_IR_PASS_LOW},
    NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
    &TableSize);
if (Status.Code != NEVERC_STATUS_OK)
  return Status;
if (!Table || TableSize < offsetof(NevercIRPassAPI, RegisterPass) +
                              sizeof(((NevercIRPassAPI *)0)->RegisterPass))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

`TableSize` gegen den Offset der zuletzt aufgerufenen Funktion zu prüfen ist die
Regel, die dieses ABI erweiterbar macht: Ein neuerer Host hängt Felder an, und
ein älteres Plugin funktioniert weiter, weil es nie über das von ihm geprüfte
Präfix hinaus liest. Das Makro
`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` wendet dieselbe Prüfung auf
eine empfangene Struktur an.

Die öffentlichen Schnittstellen und ihre Header:

| Schnittstelle | Tabelle | Header |
|---|---|---|
| `NEVERC_INTERFACE_CORE` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO`, `..._SOURCE_LOCATION` | `NevercIOAPI`, `NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST`, `..._PARSER` | `NevercASTAPI`, `NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE`, `..._BUILDER`, `..._ANALYSIS`, `..._PASS`, `..._GEN`, `..._OPTIMIZATION` | IR-Tabellen | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET`, `..._TARGET_ABI`, `..._CALLING_CONVENTION` | Target-Tabellen | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR`, `..._MIR_ANALYSIS`, `..._MIR_PASS`, `..._MIR_PROVIDER` | MIR-Tabellen | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC`, `..._MC_EMISSION`, `..._MC_PROVIDER`, `..._ASSEMBLY_PROVIDER` | MC-Tabellen | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT`, `..._OBJECT_FORMAT`, `..._OBJECT_PHASE` | Object-Tabellen | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK`, `..._LINK_REGISTRAR`, `..._LINK_PHASE` | Link-Tabellen | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO`, `..._LTO_REGISTRAR` | LTO-Tabellen | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE`, `..._DYNCODE_REGISTRAR`, `..._DYNCODE_PHASE` | DynCode-Tabellen | `PluginDynCode.h` |

Eine Schnittstelle ist entweder STABLE (ein neuerer Host darf nur anhängen) oder
LOCKSTEP (zielspezifische Schemata, die exakt übereinstimmen müssen).
Vergleichen Sie den Schema-Digest, bevor Sie LOCKSTEP-Werte verwenden.

## Bauen

Binden Sie den Sammelheader ein oder nur die Bereiche, die Sie nutzen:

```c
#include "neverc/Plugin/NevercPluginAPI.h"
```

Ein Shared Module mit NeverC selbst bauen:

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

Oder mit CMake gegen ein installiertes SDK:

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

Oder mit pkg-config:

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

Verwenden Sie je nach Host `.so`, `.dylib` oder `.dll`. Das SDK linkt weder LLVM
noch die NeverC-Laufzeit — `NevercPluginSDK::headers` ist ein reines
Header-Target.

## Laden und Konfigurieren

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| Option | Form | Zweck |
|---|---|---|
| `-fplugin=<path>` | wiederholbar | Ein Plugin-Shared-Module laden. |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | wiederholbar | Einen namensraumqualifizierten Wert an eine registrierte Plugin-Option übergeben. |
| `-fplugin-provider=<phase>:<plugin-id>` | wiederholbar | Auswählen, welches Plugin eine ersetzbare Phase bereitstellt. |

Der Qualifizierer `<plugin-id>:` darf nur entfallen, wenn genau ein Plugin aktiv
ist. Optionen, die ein Plugin mit `RegisterOption` registriert, werden auch
direkt unter ihrer deklarierten Schreibweise akzeptiert — als Flag, verbunden,
getrennt oder mit mehreren Argumenten. Plugin-Argumente oder Provider-Auswahlen
ohne `-fplugin=` sind ein harter Fehler und keine stille Wirkungslosigkeit.

## ABI-Regeln

- Fähigkeitstabellen über `QueryInterface` abfragen; die passende Hauptversion
  verlangen und `StructSize` prüfen, bevor ein Feld angefasst wird.
- `Header` und reservierten Speicher jeder öffentlichen Struktur initialisieren.
  Struktur nullen, dann `StructSize`, `Major`, `Minor` und `Flags` setzen.
- Handles und geliehene Sichten als bereichsgebundene, opake Werte behandeln.
  Ein Task-Handle nie über seinen Rückruf hinaus aufbewahren, nie in einer
  anderen Session oder Task verwenden und nie einen Handle-Wert selbst
  konstruieren.
- Aus jedem Rückruf ein `NevercStatus` zurückgeben. Weder eine C++-Ausnahme noch
  einen hosteigenen Zeiger über die C-Grenze lassen.
- Das engste wahrheitsgemäße `NevercConcurrencyModel` (`SESSION_SERIAL`,
  `THREAD_SAFE`, `PROCESS_SERIAL`) und `NevercReentrancyModel` (`NONE`,
  `ALLOWED`) deklarieren.
- Änderungen an IR, MIR, AST, Graphen und Artefakten über die transaktionalen
  Host-APIs vornehmen: Mutation beginnen, Änderungen vormerken, dann committen
  oder abbrechen. Der Commit verifiziert und veröffentlicht atomar; ein
  fehlgeschlagener Commit lässt den vorherigen Zustand unangetastet.
- Veränderlichen Zustand in den vom Host bereitgestellten Process-/Session-/
  Task-Zuständen halten. Globalen veränderlichen Zustand prüft
  `utils/plugin-api/check-global-state.py`.

Neue Funktionen werden an unabhängig versionierte Fähigkeitstabellen angehängt.
Das stabile Präfix einer Tabelle ändert sich innerhalb der ersten ABI-Hauptversion
(`NEVERC_PLUGIN_ABI_MAJOR` = 1) nicht.

## Status und Diagnosen

`NevercStatus` trägt einen `Code`, `Flags` und ein `Detail`-Wort. Häufige Codes:

| Code | Bedeutung |
|---|---|
| `NEVERC_STATUS_OK` | Erfolg. |
| `NEVERC_STATUS_INVALID_ARGUMENT` | Ein erforderlicher Zeiger oder Wert fehlte oder war fehlerhaft. |
| `NEVERC_STATUS_ABI_MISMATCH` | Die ausgehandelte Tabelle ist zu klein oder die Hauptversion weicht ab. |
| `NEVERC_STATUS_MISSING_INTERFACE` / `CAPABILITY_UNAVAILABLE` | Der Host bietet die angeforderte Fähigkeit nicht an. |
| `NEVERC_STATUS_STALE_HANDLE` / `WRONG_SESSION` / `WRONG_SCOPE` / `WRONG_TYPE` | Ein Handle wurde außerhalb seiner Gültigkeit verwendet. |
| `NEVERC_STATUS_POLICY_VIOLATION` | Die Phasenrichtlinie erlaubt die Operation nicht. |
| `NEVERC_STATUS_VERIFICATION_FAILED` | Ein versiegelter Host-Verifizierer hat das Produkt abgelehnt. |
| `NEVERC_STATUS_CANCELLED` / `BUSY` / `RESOURCE_EXHAUSTED` | Kooperativer Abbruch oder Ressourcengrenzen. |

Die Flag-Bits (`RECOVERABLE`, `OUTPUT_ALREADY_COMMITTED`,
`OUTPUT_MAY_BE_PARTIAL`, `OUTPUT_RECOVERY_REQUIRED`,
`DURABILITY_UNCONFIRMED`) beschreiben, was mit der Ausgabe geschehen ist — genau
das, was ein Build-System braucht, um zu entscheiden, ob ein erneuter Versuch
sicher ist.

Melden Sie Probleme mit `NevercCoreAPI.EmitDiagnostic` und einem
`NevercDiagnosticDescriptor` mit Schweregrad, Code, Plugin-ID, Phasen-ID,
Nachricht, Anmerkungen, Quellposition, Bereichen und Fix-its. Rufen Sie vor
aufwendiger Arbeit `CheckCancelled` auf.

## Beispiele

Alle bauen:

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

Jedes Beispiel wird zweimal übersetzt — einmal mit dem konfigurierten
Host-C-Compiler und einmal mit dem frisch gebauten NeverC — so wird das ABI von
beiden Seiten belegt. Die Module landen in
`build-neverc/neverc/pluginsdk/examples/host/`.

| Beispiel | CMake-Target | Zeigt |
|---|---|---|
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | Optionsregistrierung, Phasenbeobachtung, Job-Interception |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | Ein VFS-Provider, der einen Header aus dem Speicher liefert |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | Parser-Interception und atomare AST-Mutation |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | IR-Pass auf Modulebene, der die Funktionsliste mit einem Wert-Cursor durchläuft |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | Ein stabiler IR-Funktionspass |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | Ein stabiler MIR-Pass am Pre-Emit-Hook |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | Rein lesende MC-Emissionsereignisse |
| `ObjectRewritePlugin.c` | `neverc-plugin-example-object-rewrite` | Transaktionales Umschreiben des ObjectGraph |
| `CustomCallConvPlugin.c` | `neverc-plugin-example-custom-callconv` | Datengetriebene Aufrufkonventionen |
| `DynCodeTracePlugin.c` | `neverc-plugin-example-dyncode-trace` | Beobachtung der DynCode-Pipeline |
| `DynCodeEncoderPlugin.c` | `neverc-plugin-example-dyncode-encoder` | Interception der DynCode-Zeichensatzkodierung |
| `CrtShimPlugin.c` | `neverc-plugin-example-crt-shim` | Ein Plugin ganz ohne CRT-Abhängigkeit |
| `BenchPlugin.c` | `neverc-plugin-example-abi-bench` | Mikrobenchmark für ABI-Aufrufdurchsatz |

Eines laden:

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## Normative Quellen

| Datei | Garantien |
|---|---|
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | Phasen-IDs, Richtlinien, Stabilität, Verifizierer-Gates |
| `pluginsdk/manifest/plugin.json` | ABI-Version, Schnittstellen-IDs/-Versionen/-Stabilität, Schema-Digests, unterstützte Ziele |
| `pluginsdk/abi/plugin.json` | Gemessene Größe, Ausrichtung und Feld-Offsets jeder öffentlichen Struktur, je Host-ABI-Schlüssel |
| `docs/plugin-api/coverage.json` | Ordnet jeder stabilen Phase positive, negative, Ersetzungs-, Beobachter- und Sealed-Gate-Tests zu |

Ein SDK lässt sich damit maschinell gegen einen Host validieren, und ein
Plugin-Build kann sein Struktur-Layout gegen den ABI-Schlüssel behaupten, in den
es geladen wird.
