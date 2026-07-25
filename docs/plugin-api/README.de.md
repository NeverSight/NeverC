**Sprachen**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC-Plugin-ABI

Ein NeverC-Plugin ist ein Shared Module, das genau eine Funktion exportiert,
versionierte Fähigkeitstabellen über eine 128-Bit-Schnittstellen-ID aushandelt
und sich an einen eingefrorenen Graphen benannter Compiler-Phasen anhängt. Die
gesamte Schnittstelle ist reines C11. Ein Plugin bindet niemals einen
LLVM-Header ein, linkt niemals den Compiler und reicht niemals einen C++-Typ
über die Grenze.

```c
NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin);
```

Diese in `PluginCore.h` deklarierte Signatur ist der gesamte Linkage-Vertrag.
Alles andere — IR lesen, einen Objektgraphen umschreiben, die
Optimierungspipeline ersetzen — erreicht man über Tabellen, die man beim Host
per ID anfordert.

## Leitfäden

| Leitfaden | Inhalt |
|---|---|
| [Driver-API](driver.de.md) | Kommandozeile, Toolchain-Auswahl, Aktionsgraph, Job-Graph |
| [Source- und E/A-API](source.de.md) | VFS-Provider, Quellpositionen, Puffer, Ausgabesenken, Abhängigkeiten |
| [Präprozessor-API](prep.de.md) | Token, Makros, Pragmas, Includes, Feature-Abfragen, 39 Ereignisarten |
| [AST- und Semantik-API](ast-sema.de.md) | Parser-Erweiterung, AST-Mutation, Namensauflösung, Typen, Konstanten |
| [IR-API](ir.de.md) | LLVM-IR lesen, transaktionales Bauen, Analysen, Passes, Provider |
| [MIR-API](mir.de.md) | Maschinenfunktionen, Register, Stackframes, MIR-Passes und -Analysen |
| [Target, MC, Assembly, Objekt](target-mc-object.de.md) | Target-Registrierung, Aufrufkonventionen, MC-Kodierung, Objektgraphen |
| [Link- und LTO-API](link-lto.de.md) | Link-Graph, Symbolauflösung, GC/ICF, Linker- und LTO-Provider |
| [DynCode-API](dyncode.de.md) | Flache positionsunabhängige Images, Import-Lowering, Zeichensatzkodierung |
| [Eigene Aufrufkonventionen](custom-callconv/README.de.md) | Datengetriebene Aufrufkonventions-Plugins |
| [Nachweis der Phasenabdeckung](coverage.json) | Testzuordnung für jede stabile Phase |

## Ausführungsmodell

Der Host steuert ein Plugin über drei verschachtelte Geltungsbereiche. Jeder
Bereich übergibt dem Plugin einen opaken Zustandszeiger, den das Plugin selbst
alloziert und besitzt — ein korrekt geschriebenes Plugin braucht daher keinen
globalen veränderlichen Zustand.

| Bereich | Callbacks | Bedeutung |
|---|---|---|
| Process | `ProcessBegin`, `Register`, `Destroy` | Ein Compilerprozess. Hier Schnittstellen abfragen und Fähigkeiten registrieren. |
| Session | `SessionBegin`, `SessionEnd` | Ein Driver-Aufruf. |
| Task | `TaskBegin`, `TaskEnd` | Eine Arbeitseinheit, identifiziert durch `NevercTaskKind`. |

```c
typedef struct NevercPluginDescriptor {
  NevercABITableHeader Header;
  NevercStringView PluginID;
  NevercStringView DisplayName;
  NevercSemanticVersion Version;
  NevercConcurrencyModel Concurrency;
  NevercReentrancyModel Reentrancy;
  NevercStructArrayView RequiredInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView OptionalInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView Dependencies;         /* NevercPluginDependency[]     */
  NevercProcessBeginFn ProcessBegin;
  NevercRegisterPluginFn Register;
  NevercSessionBeginFn SessionBegin;
  NevercSessionEndFn SessionEnd;
  NevercTaskBeginFn TaskBegin;
  NevercTaskEndFn TaskEnd;
  NevercPluginDestroyFn Destroy;
} NevercPluginDescriptor;
```

Praktisch zwingend sind nur `PluginID` und `Register`; jeder Callback-Slot darf
`NULL` bleiben. Die Task-Arten sind `NEVERC_TASK_INVOCATION`,
`TRANSLATION_UNIT`, `LTO`, `LINK`, `CODEGEN`, `OBJECT` und `DYNCODE`.

Der Host ruft zuerst `ProcessBegin` auf, dann genau einmal `Register`. Die
Registrierung ist die einzige Stelle, an der Optionen, Beobachter,
Interceptors und Provider hinzugefügt werden dürfen; danach ist der
Phasengraph eingefroren.

Zustand wird innerhalb eines Callbacks geholt, statt vorher festgehalten:

```c
Core->GetSessionState(Core->Context, Frame->Session, PluginID, &SessionState);
Core->GetTaskState(Core->Context, Frame->Task, PluginID, &TaskState);
```

## Phasen

Eine Phase ist ein benannter, versionierter Übergang von einem
Eingabeartefakt zu einem Ausgabeartefakt. NeverC liefert **130 eingebaute
Phasen** aus, dazu 8 Erweiterungs-ID-Familien, die für plugin-definierte
Phasen reserviert sind:

| Domäne | Phasen | Domäne | Phasen |
|---|--:|---|--:|
| `driver` | 6 | `mir` | 10 |
| `source` | 3 | `codegen` | 4 |
| `prep` | 6 | `mc` | 13 |
| `syntax` | 7 | `assembly` | 4 |
| `sema` | 7 | `object` | 8 |
| `ir` | 8 | `link` | 20 |
| | | `dyncode` | 34 |

Alle 130 haben in ABI-Major 1 die Stabilitätsstufe `stable`. Jede Phase gibt
eine Policy bekannt, und ein Plugin darf sich nur auf die von dieser Policy
erlaubten Arten anhängen:

| Policy-Flag | Phasen | Was ein Plugin darf |
|---|--:|---|
| `NEVERC_PHASE_OBSERVABLE` | 130 | Einen Beobachter für rein lesende Benachrichtigung registrieren. |
| `NEVERC_PHASE_INTERCEPTABLE` | 105 | Die Phase umhüllen und entscheiden, ob der Rest der Kette aufgerufen wird. |
| `NEVERC_PHASE_REPLACEABLE` | 86 | Einen Provider registrieren, der die Ausgabe selbst liefert. |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | 13 | Den Übergang überspringen und dabei ein Beweis-Handle liefern. |
| `NEVERC_PHASE_SEALED_HOST_GATE` | 14 | Nichts. Verifizierer und Commits gehören dem Host. |

Die 14 versiegelten Gates sind `ir.final_verify`, `mir.final_verify`,
`codegen.product_verify`, `assembly.final_verify`, `assembly.commit`,
`object.final_verify`, `object.commit`, `link.image_verify`,
`link.side_outputs_verify`, `link.commit`, `dyncode.ir.final_verify`,
`dyncode.mir.final_verify`, `dyncode.verify` und `dyncode.commit`. Sie lassen
sich beobachten, aber niemals abfangen, ersetzen oder überspringen.

Beobachter werden an den Punkten benachrichtigt, die eine Phase deklariert:
`NEVERC_OBSERVER_BEFORE`, `NEVERC_OBSERVER_AFTER` und
`NEVERC_OBSERVER_AFTER_COMMIT`. Ein Interceptor erhält eine
`NevercPhaseContinuation` und muss `InvokeNext` **höchstens einmal** auf dem
Callback-Thread aufrufen und dann in `NevercPhaseResult.Action`
`NEVERC_PHASE_CONTINUE`, `NEVERC_PHASE_REPLACE` oder `NEVERC_PHASE_SKIP`
melden.

Jeder Phasen-Callback erhält denselben Frame:

```c
typedef struct NevercPhaseFrame {
  NevercABITableHeader Header;
  NevercSessionHandle Session;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercPhaseRoute Route;        /* triple, CPU, features, object format */
  NevercArtifactHandle Input;
  NevercArtifactHandle CurrentOutput;
  NevercHandle Cancellation;
} NevercPhaseFrame;
```

`Schema/PhaseSchema.json` ist die normative Quelle für Phasen-IDs, Policies,
Stabilitätsstufen und Verifizierer-Gates. Das generierte
`Schema/PluginPhaseSchema.inc` legt jede davon als Compile-Zeit-Konstante
offen — für die Phase `neverc.ir.pass.pipeline_start`:

```c
NEVERC_PHASE_IR_PASS_PIPELINE_START_NAME       /* "neverc.ir.pass.pipeline_start" */
NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH       /* UINT64_C(0x4e43504849520001)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW        /* UINT64_C(0x0000000000000004)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_POLICY     /* OBSERVABLE | INTERCEPTABLE       */
NEVERC_PHASE_IR_PASS_PIPELINE_START_STABILITY
NEVERC_PHASE_IR_PASS_PIPELINE_START_INPUT_HIGH /* and _INPUT_LOW, _OUTPUT_*        */
```

Mit `NEVERC_BUILTIN_PHASE_COUNT` und den domänenweisen
`NEVERC_BUILTIN_<DOMAIN>_PHASE_COUNT`-Konstanten kann ein Plugin den Graphen
zusichern, gegen den es übersetzt wurde.

## Ein vollständiges Minimal-Plugin

Dies ist `pluginsdk/templates/minimal/Plugin.c` wortwörtlich. Es lädt,
handelt das ABI aus, registriert nichts und entlädt sich sauber — kopieren Sie
das Verzeichnis und bauen Sie von hier aus weiter.

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
  /* Register options, observers, interceptors, or providers here. */
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

`OutPlugin` ist ein Puffer, der dem Aufrufer gehört. Beim Eintritt gibt sein
`Header.StructSize` die beschreibbare Kapazität an; das Plugin schreibt
höchstens so viele Bytes und meldet die tatsächlich erzeugte Größe. Erst den
`Header` des Deskriptors selbst zu schreiben und dann die Kopie zu kürzen,
erfüllt beide Hälften dieser Regel.

## Aushandeln von Schnittstellen

Fähigkeitstabellen werden über eine 128-Bit-Schnittstellen-ID geholt, nicht
über Symbole. Fordern Sie die Major-Version an, gegen die Sie übersetzt haben,
und die niedrigste Minor-Version, mit der Sie arbeiten können:

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

`TableSize` gegen den Offset der letzten Funktion zu prüfen, die Sie aufrufen,
ist die Regel, die dieses ABI erweiterbar macht: Ein neuerer Host hängt Felder
an, und ein älteres Plugin funktioniert weiter, weil es nie über das von ihm
geprüfte Präfix hinaus liest. Das Makro
`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` wendet denselben Test auf
eine empfangene Struktur an. Dieselbe `QueryInterface`-Signatur gibt es auch
auf `NevercCoreAPI`, sodass Sie spät statt beim Eintritt aushandeln können.

Die öffentlichen Schnittstellen, ihre Tabellen und ihre ID-Makros:

| Schnittstellen-Makropaar | Tabelle | Header |
|---|---|---|
| `NEVERC_INTERFACE_CORE_{HIGH,LOW}` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER_*` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO_*`, `..._SOURCE_LOCATION_*` | `NevercIOAPI`, `NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP_*` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST_*`, `..._PARSER_*` | `NevercASTAPI`, `NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA_*` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE_*`, `..._IR_BUILDER_*`, `..._IR_ANALYSIS_*`, `..._IR_PASS_*`, `..._IR_GEN_*`, `..._IR_OPTIMIZATION_*` | sechs IR-Tabellen | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET_*`, `..._TARGET_ABI_*`, `..._CALLING_CONVENTION_*` | `NevercTargetAPI`, `NevercTargetABIAPI`, `NevercCallingConventionAPI` | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR_*`, `..._MIR_ANALYSIS_*`, `..._MIR_PASS_*`, `..._MIR_PROVIDER_*` | vier MIR-Tabellen | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC_*`, `..._MC_EMISSION_*`, `..._MC_PROVIDER_*`, `..._ASSEMBLY_PROVIDER_*` | vier MC-Tabellen | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT_*`, `..._OBJECT_FORMAT_*`, `..._OBJECT_PHASE_*` | drei Objekt-Tabellen | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK_*`, `..._LINK_REGISTRAR_*`, `..._LINK_PHASE_*` | drei Link-Tabellen | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO_*`, `..._LTO_REGISTRAR_*` | `NevercLTOAPI`, `NevercLTORegistrarAPI` | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE_*`, `..._DYNCODE_REGISTRAR_*`, `..._DYNCODE_PHASE_*` | drei dyncode-Tabellen | `PluginDynCode.h` |

Jeder Header definiert außerdem die passenden `NEVERC_<DOMAIN>_API_MAJOR` und
`_MINOR`, die Sie an `QueryInterface` übergeben sollten.

Eine Schnittstelle ist entweder `NEVERC_INTERFACE_STABLE` (ein neuerer Host
darf nur anhängen) oder `NEVERC_INTERFACE_LOCKSTEP` (target-spezifische
Schemata, die exakt übereinstimmen müssen). Vergleichen Sie den Schema-Digest,
bevor Sie LOCKSTEP-Werte verwenden.

## Registrierung

`Register` erhält eine `NevercRegistrarAPI` und einen opaken
`RegistrarContext`:

```c
typedef struct NevercRegistrarAPI {
  NevercABITableHeader Header;
  NevercRegisterInterfaceFn RegisterInterface;
  NevercRegisterPhaseFn RegisterPhase;
  NevercRegisterObserverFn RegisterObserver;
  NevercRegisterInterceptorFn RegisterInterceptor;
  NevercRegisterProviderFn RegisterProvider;
  NevercRegisterOptionFn RegisterOption;
} NevercRegistrarAPI;
```

Die domänenspezifischen Registrierungsfunktionen —
`NevercIRPassAPI.RegisterPass`, `NevercTargetAPI.RegisterTarget`,
`NevercObjectFormatAPI.RegisterFormat` und die übrigen — nehmen denselben
`RegistrarContext` als zweites Argument; so ordnet der Host eine Registrierung
Ihrem Plugin zu.

Ein Provider deklariert zusätzlich seinen Determinismus-Vertrag, auf den sich
der Build-Cache stützt:

```c
Provider.ProviderID    = SV("com.example.my-lowering");
Provider.Route         = /* triple / CPU / features / object format */;
Provider.Deterministic = NEVERC_TRUE;
Provider.Cacheable     = NEVERC_TRUE;
Provider.FallbackSafe  = NEVERC_FALSE;  /* built-in cannot silently take over */
```

## Bauen

Binden Sie den Sammel-Header ein oder nur die Domänen, die Sie nutzen:

```c
#include "neverc/Plugin/NevercPluginAPI.h"   /* everything */
#include "neverc/Plugin/PluginIR.h"          /* or one domain */
```

Ein Shared Module mit NeverC selbst bauen:

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

Oder gegen ein installiertes SDK mit CMake:

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

Oder mit pkg-config:

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

Verwenden Sie je nach Host `.so`, `.dylib` oder `.dll`. Das SDK linkt weder
LLVM noch eine NeverC-Laufzeit — `NevercPluginSDK::headers` ist reines
Header-Material.

## Laden und Konfigurieren

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| Option | Form | Zweck |
|---|---|---|
| `-fplugin=<path>` | wiederholbar | Ein Plugin-Shared-Module für die gesamte Toolchain laden. |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | wiederholbar | Einen namensraumqualifizierten Wert an eine registrierte Plugin-Option übergeben. |
| `-fplugin-provider=<phase>:<plugin-id>` | wiederholbar | Auswählen, welches Plugin eine ersetzbare Phase bereitstellt. |
| `-fplugin-pass=<dsopath>` | wiederholbar | Ein Out-of-Tree-Pass-Plugin mit C-ABI laden. |
| `-fplugin-pass-arg=<key>=<value>` | wiederholbar | Ein Argument an C-ABI-Pass-Plugins übergeben. |

Der Qualifizierer `<plugin-id>:` darf nur entfallen, wenn genau ein Plugin
aktiv ist. Optionen, die ein Plugin mit `RegisterOption` registriert, werden
auch direkt in ihrer deklarierten Schreibweise akzeptiert — als Flag, in
verbundener, getrennter oder mehrargumentiger Form. Plugin-Argumente und
Provider-Auswahlen ohne passendes `-fplugin=` sind ein harter Fehler und
werden nicht stillschweigend ignoriert.

Eine registrierte Option lässt sich jederzeit über die Core-Tabelle
zurücklesen:

```c
uint64_t Count = 0;
Core->GetPluginOptionValueCount(Core->Context, Session, PluginID,
                                SV("--driver-trace"), &Count);
NevercStringView Value;
Core->GetPluginOptionValue(Core->Context, Session, PluginID,
                           SV("--driver-trace"), 0, &Value);
```

## ABI-Regeln

- Fähigkeitstabellen über `QueryInterface` abfragen; die passende Major
  verlangen und `StructSize` prüfen, bevor ein Feld angefasst wird.
- Den `Header` und den reservierten Speicher jeder öffentlichen Struktur
  initialisieren. Die Struktur nullen, dann `StructSize`, `Major`, `Minor` und
  `Flags` setzen.
- Handles und geliehene Views als bereichsgebundene, opake Werte behandeln. Ein
  Task-gebundenes Handle nie über seinen Callback hinaus aufbewahren, nie in
  einer anderen Session oder Task verwenden und nie einen Handle-Wert
  erfinden.
- Aus jedem Callback ein `NevercStatus` zurückgeben. Weder eine C++-Ausnahme
  noch einen host-eigenen Zeiger über die C-Grenze lassen.
- Das engste zutreffende `NevercConcurrencyModel` (`SESSION_SERIAL`,
  `THREAD_SAFE`, `PROCESS_SERIAL`) und `NevercReentrancyModel` (`NONE`,
  `ALLOWED`) deklarieren.
- Änderungen an IR, MIR, AST, Graphen und Artefakten über die transaktionalen
  Host-APIs vornehmen: eine Mutation beginnen, Änderungen vormerken, dann
  committen oder abbrechen. Der Commit verifiziert und veröffentlicht atomar;
  ein fehlgeschlagener Commit lässt den vorherigen Zustand unangetastet.
- Über `NevercCoreAPI.Allocate` / `Reallocate` / `Deallocate` allozieren, wenn
  der Host den Speicher verbuchen soll.
- Veränderlichen Zustand im host-bereitgestellten Process-/Session-/Task-
  Zustand halten. Globaler veränderlicher Zustand wird von
  `utils/plugin-api/check-global-state.py` geprüft.

Alle öffentlichen Strukturen liegen unter `NEVERC_ABI_PACK_BEGIN`
(8-Byte-Packing) und verwenden ausschließlich Typen fester Breite. Neue
Funktionen werden an unabhängig versionierte Fähigkeitstabellen angehängt; das
stabile Präfix einer Tabelle ändert sich innerhalb der ersten ABI-Major
(`NEVERC_PLUGIN_ABI_MAJOR` = 1) nicht.

## Status und Diagnosen

`NevercStatus` trägt einen `Code`, `Flags` und ein `Detail`-Wort. Der
vollständige Codesatz:

| Code | Bedeutung |
|---|---|
| `NEVERC_STATUS_OK` | Erfolg. |
| `NEVERC_STATUS_INVALID_ARGUMENT` | Ein erforderlicher Zeiger oder Wert fehlte oder war fehlerhaft. |
| `NEVERC_STATUS_ABI_MISMATCH` | Die ausgehandelte Tabelle ist zu klein oder die Major weicht ab. |
| `NEVERC_STATUS_MISSING_INTERFACE` | Der Host veröffentlicht die angeforderte Schnittstelle nicht. |
| `NEVERC_STATUS_VERSION_MISMATCH` | Die angeforderte Major/Minor lässt sich nicht erfüllen. |
| `NEVERC_STATUS_INVALID_DESCRIPTOR` | Ein Deskriptor bestand die Strukturprüfung nicht. |
| `NEVERC_STATUS_DUPLICATE_ID` | Eine ID war bereits registriert. |
| `NEVERC_STATUS_DEPENDENCY_MISSING` | Eine deklarierte Abhängigkeit fehlt. |
| `NEVERC_STATUS_DEPENDENCY_CYCLE` | Die Registrierungsreihenfolge ist nicht erfüllbar. |
| `NEVERC_STATUS_BUSY` | Eine Ressource wird anderswo gehalten. |
| `NEVERC_STATUS_CANCELLED` | Kooperativer Abbruch wurde angefordert. |
| `NEVERC_STATUS_RESOURCE_EXHAUSTED` | Ein Budget oder Limit wurde erreicht. |
| `NEVERC_STATUS_STALE_HANDLE` | Ein Handle überlebte das benannte Objekt. |
| `NEVERC_STATUS_WRONG_SESSION` | Ein Handle wurde in einer anderen Session benutzt. |
| `NEVERC_STATUS_WRONG_SCOPE` | Ein Handle wurde außerhalb seines Bereichs benutzt. |
| `NEVERC_STATUS_WRONG_TYPE` | Ein Handle benannte eine andere Entitätsart. |
| `NEVERC_STATUS_INVALID_STATE` | Die Operation ist im aktuellen Zustand unzulässig. |
| `NEVERC_STATUS_POLICY_VIOLATION` | Die Phasen-Policy verbietet die Operation. |
| `NEVERC_STATUS_VERIFICATION_FAILED` | Ein versiegelter Host-Verifizierer hat das Produkt abgelehnt. |
| `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` | Der Host kann die Fähigkeit hier nicht anbieten. |
| `NEVERC_STATUS_PLUGIN_FAILURE` | Das Plugin meldete einen generischen Fehler. |
| `NEVERC_STATUS_PLUGIN_EXCEPTION` | Eine Ausnahme entkam einem Plugin-Callback. |
| `NEVERC_STATUS_OUTPUT_PARTIAL` | Die Ausgabe wurde nur teilweise geschrieben. |
| `NEVERC_STATUS_REENTRANCY_DENIED` | Ein reentranter Aufruf wurde abgelehnt. |
| `NEVERC_STATUS_NOT_FOUND` | Die benannte Entität existiert nicht. |

Die Flag-Bits beschreiben, was mit der Ausgabe geschehen ist — genau das, was
ein Build-System braucht, um zu entscheiden, ob ein erneuter Versuch sicher
ist: `NEVERC_STATUS_FLAG_RECOVERABLE`, `_OUTPUT_ALREADY_COMMITTED`,
`_OUTPUT_MAY_BE_PARTIAL`, `_OUTPUT_RECOVERY_REQUIRED` und
`_DURABILITY_UNCONFIRMED`.

Melden Sie Probleme mit `NevercCoreAPI.EmitDiagnostic` und einem
`NevercDiagnosticDescriptor` mit Schweregrad (`NOTE`, `REMARK`, `WARNING`,
`ERROR`, `FATAL`), Code, Plugin-ID, Phasen-ID, Meldung, Notizen,
Quellposition, Bereichen und Fix-its. Rufen Sie `CheckCancelled` vor teurer
Arbeit auf.

## Beispiele

Alle bauen:

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

Jedes Beispiel wird zweimal übersetzt — einmal mit dem konfigurierten
Host-C-Compiler und einmal mit dem frisch gebauten NeverC — sodass das ABI von
beiden Seiten bewiesen ist. Die Module landen in
`build-neverc/neverc/pluginsdk/examples/host/`.

| Beispiel | CMake-Target | Zeigt |
|---|---|---|
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | Optionsregistrierung, Phasenbeobachtung, Job-Interception |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | Ein VFS-Provider, der einen Header aus dem Speicher liefert |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | Parser-Interception und atomare AST-Mutation |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | Ein IR-Pass auf Modulebene, der die Funktionsliste mit einem Wert-Cursor durchläuft |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | Ein stabiler IR-Funktions-Pass |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | Ein stabiler MIR-Pass am Pre-Emit-Hook |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | Rein lesende MC-Emissionsereignisse |
| `ObjectRewritePlugin.c` | `neverc-plugin-example-object-rewrite` | Transaktionales Umschreiben eines ObjectGraph |
| `CustomCallConvPlugin.c` | `neverc-plugin-example-custom-callconv` | Datengetriebene Aufrufkonventionen |
| `DynCodeTracePlugin.c` | `neverc-plugin-example-dyncode-trace` | Beobachtung der dyncode-Pipeline |
| `DynCodeEncoderPlugin.c` | `neverc-plugin-example-dyncode-encoder` | Abfangen der dyncode-Zeichensatzkodierung |
| `CrtShimPlugin.c` | `neverc-plugin-example-crt-shim` | Ein Plugin ganz ohne CRT-Abhängigkeit |
| `BenchPlugin.c` | `neverc-plugin-example-abi-bench` | Mikrobenchmark des ABI-Aufrufdurchsatzes |

Eines laden:

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## Normative Quellen

| Datei | Garantien |
|---|---|
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | Phasen-IDs, Policies, Stabilität, Verifizierer-Gates |
| `pluginsdk/manifest/plugin.json` | ABI-Version, Schnittstellen-IDs/-Versionen/-Stabilität, Schema-Digests, unterstützte Targets |
| `pluginsdk/abi/plugin.json` | Gemessene Größe, Ausrichtung und Feld-Offsets jeder öffentlichen Struktur, je Host-ABI-Schlüssel |
| `docs/plugin-api/coverage.json` | Ordnet jeder stabilen Phase positive, negative, Ersetzungs-, Beobachter- und Sealed-Gate-Tests zu |

Ein SDK lässt sich damit maschinell gegen einen Host validieren, und ein
Plugin-Build kann sein Struktur-Layout gegen den ABI-Schlüssel zusichern, in
den es geladen wird.
