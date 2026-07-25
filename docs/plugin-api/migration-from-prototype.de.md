**Sprachen**: [English](migration-from-prototype.md) | [简体中文](migration-from-prototype.zh-CN.md) | [繁體中文](migration-from-prototype.zh-TW.md) | [日本語](migration-from-prototype.ja.md) | [한국어](migration-from-prototype.ko.md) | [Français](migration-from-prototype.fr.md) | [Deutsch](migration-from-prototype.de.md) | [Español](migration-from-prototype.es.md) | [Italiano](migration-from-prototype.it.md) | [Русский](migration-from-prototype.ru.md) | [العربية](migration-from-prototype.ar.md)

# Migration von der Prototyp-Plugin-API

Die nie veröffentlichte Prototyp-Plugin-API — ihr Einstiegspunkt
`nevercGetPluginInfo`, die einzelne `NevercHostAPI`-vtable, die
`Register*Pass`-Aufrufe, die `NEVERC_INTERPOSE_*`-Hooks und der Lader
`-fplugin-pass=` — wurde vor der ersten öffentlichen Freigabe entfernt. Das
erste öffentliche ABI ist das phasenbasierte Deskriptor-ABI, das in
[README.md](README.md) dokumentiert ist: Plugins exportieren
`neverc_plugin_entry` und handeln unabhängig versionierte Capability-Tabellen
aus.

Es gibt weder eine Kompatibilitätsschicht noch eine Trennung in `v1`/`v2`.
Kompilieren Sie den *Quelltext* des Plugins gegen die öffentlichen Header neu;
diese Seite bildet jedes Prototyp-Konstrukt auf seinen Ersatz in der ersten
Version, auf eine semantische Änderung oder auf eine ausdrückliche
Nichtübernahme ab.

## Prototyp-Binärdateien werden abgewiesen

Das Laden eines Prototyp-Shared-Objects schlägt mit einer stabilen Diagnose
fehl:

```
plugin exports the removed 'nevercGetPluginInfo' prototype ABI; migrate it to
the first public descriptor ABI and export 'neverc_plugin_entry'
```

Eine Bibliothek, die keinen der beiden Einstiegspunkte exportiert, scheitert
mit `plugin has no 'neverc_plugin_entry' entry`. Solange der Quelltext nicht
portiert ist, wird nichts geladen.

## Einstiegspunkt

| Prototyp | Erstes öffentliches ABI |
|---|---|
| `NevercPluginInfo nevercGetPluginInfo(void)` | `NevercStatus NEVERC_CALL neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin)` |

Der Einstiegspunkt *liefert* keine Struktur mehr als Wert zurück. Er füllt
einen vom Aufrufer bereitgestellten `NevercPluginDescriptor` unter Beachtung
von `OutPlugin->Header.StructSize` und gibt einen `NevercStatus` zurück.
Fragen Sie die benötigten Capability-Tabellen bei `Bootstrap` ab, bevor Sie
deren Unterstützung ankündigen.

## Felder von `NevercPluginInfo`

| Prototyp-Feld | Abbildung in der ersten Version |
|---|---|
| `APIVersion` | `Descriptor.Header` (`NevercABITableHeader` mit `StructSize`, `NEVERC_PLUGIN_ABI_MAJOR`, `NEVERC_PLUGIN_ABI_MINOR`) |
| `PluginName` | `Descriptor.DisplayName` (`NevercStringView`) sowie eine stabile Reverse-DNS-`Descriptor.PluginID`, die als Schlüssel für den Zustand je Gültigkeitsbereich dient |
| `PluginVersion` | `Descriptor.Version` (`NevercSemanticVersion`) |
| `RegisterPasses(API, Reg)` | `Descriptor.Register(Core, Registrar, RegistrarContext, ProcessState)` sowie die Lebenszyklus-Callbacks `ProcessBegin`, `SessionBegin`/`SessionEnd`, `TaskBegin`/`TaskEnd` |
| `Destroy()` | `Descriptor.Destroy(Core, ProcessState)` |
| *(kein Prototyp-Äquivalent)* | `Descriptor.Concurrency` und `Descriptor.Reentrancy` müssen wahrheitsgemäß deklariert werden (etwa `NEVERC_CONCURRENCY_SESSION_SERIAL`, `NEVERC_REENTRANCY_ALLOWED`) |

## Host-Zugriff: eine vtable → Capability-Tabellen

Der Prototyp reichte jedem Callback eine einzelne `NevercHostAPI`-vtable mit
über 200 Einträgen und sicherte neue Felder mit `NEVERC_API_FN` ab. Die erste
Version ersetzt sie durch unabhängig versionierte Capability-Tabellen, die bei
Bedarf abgefragt werden:

```c
NevercInterfaceID Driver = { NEVERC_INTERFACE_DRIVER_HIGH,
                             NEVERC_INTERFACE_DRIVER_LOW };
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus S = Bootstrap->QueryInterface(
    Bootstrap->Context, Driver, NEVERC_DRIVER_API_MAJOR,
    NEVERC_DRIVER_API_MINOR, &Table, &Minor, &TableSize);
```

Fordern Sie die passende Major-Version an und prüfen Sie `TableSize` mit
`offsetof`, bevor Sie ein Feld lesen. Interfaces sind nach Domäne gegliedert:
Core, Driver, Source, Prep, AST, Sema, IR, MIR, Target, MC, Object, Link, LTO
und DynCode.

## Registrierung: `Register*Pass` + Hooks → Observer/Interceptor/Provider

Die Prototyp-Registrierung hängte einen Callback an einen Hook:

```c
API->RegisterModulePass(Reg, NEVERC_INTERPOSE_PRE_OPT, myPass, ud, "my-pass");
```

Die erste Version registriert innerhalb von `Register` einen typisierten
Handler an einer Phase, die durch eine 128-Bit-`NevercInterfaceID`
identifiziert wird:

| Prototyp-Aufruf | Registrar-Aufruf der ersten Version |
|---|---|
| schreibgeschützter Pass | `Registrar->RegisterObserver(NevercObserverDescriptor)` mit den Punkten `NEVERC_OBSERVER_BEFORE`/`NEVERC_OBSERVER_AFTER` |
| Pass, der eine Phase umschließt oder kurzschließt | `Registrar->RegisterInterceptor(NevercInterceptorDescriptor)`; rufen Sie `Continuation->InvokeNext` höchstens einmal auf und setzen Sie `OutResult->Action` |
| Pass, der eine eingebaute Transformation ersetzt | `Registrar->RegisterProvider(...)` an einer `REPLACEABLE`-Phase |
| Lesen von `-fplugin-pass-arg=` | `Registrar->RegisterOption(NevercOptionDescriptor)`, um eine echte Treiberoption zu deklarieren |

Aus einem Prototyp-„Modulpass bei `PRE_OPT`“ wird ein Observer, Interceptor
oder Provider an der IR-Phase `neverc.ir.pass.pre_opt`.

## Hook-zu-Phase-Zuordnung

| Prototyp-Hook | Phase der ersten Version (Name) |
|---|---|
| `NEVERC_INTERPOSE_PRE_OPT` | `neverc.ir.pass.pre_opt` |
| `NEVERC_INTERPOSE_POST_OPT` | `neverc.ir.pass.post_opt` |
| `NEVERC_INTERPOSE_PIPELINE_START` | `neverc.ir.pass.pipeline_start` |
| `NEVERC_INTERPOSE_PIPELINE_LAST` | `neverc.ir.pass.optimizer_last` |
| `NEVERC_INTERPOSE_BEFORE_CODEGEN_PREEMIT` | `neverc.mir.pass.preemit` |
| `NEVERC_INTERPOSE_AFTER_CODEGEN_FINAL_MIR` | `neverc.mir.pass.final` |
| `NEVERC_INTERPOSE_LTO_PRE_OPT` / `LTO_POST_OPT` | LTO-Phasen `neverc.link.lto_resolve` / `neverc.link.lto_generate` (siehe [mir.md](mir.md)) |
| `NEVERC_INTERPOSE_LINK_PRE_LAYOUT` / `LINK_POST_LAYOUT` | `neverc.link.layout`, beobachtet bei `BEFORE` / `AFTER` |
| `NEVERC_INTERPOSE_LINK_POST_EMIT` | `neverc.link.post_emit` |
| `NEVERC_INTERPOSE_SC_*` (dyncode) | die typisierten dyncode-Phasen in [dyncode.md](dyncode.md) |

Die normative Liste der Phasen-IDs, Richtlinien, Stabilitätsstufen und
Verifizierer-Gates ist
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`; der ausführbare
Abdeckungsvertrag ist [coverage.json](coverage.json). Ein Hook, der früher ein
einzelner Punkt war, kann auf mehr als eine Phasen-ID abbilden, jede mit
eigener Richtlinie und eigenem Nachweis.

## Pass-Callbacks, Handles und Byte-Änderungen

| Prototyp | Erste Version |
|---|---|
| `NevercModulePassFn(NevercModuleRef, API, ud)` und Verwandte | Callbacks erhalten einen `NevercPhaseFrame`; IR-/MIR-/AST-/Graph-Objekte sind typisierte, gültigkeitsbereichsgebundene, opake Handles aus der jeweiligen Capability-Tabelle (siehe [ir.md](ir.md), [mir.md](mir.md), [ast-sema.md](ast-sema.md), [target-mc-object.md](target-mc-object.md)) |
| generisches `NevercValueRef` | zugunsten typisierter IR-Handles entfernt |
| In-place-Änderung eines lebenden `Ref` | alle Änderungen laufen über die transaktionalen Host-APIs |
| `NevercBinaryPassFn(uint8_t **Data, uint64_t *Len, ...)` | entfernt; dyncode-Byte-Änderungen nutzen den geprüften Image-Builder (read/write/insert/append/resize), siehe [dyncode.md](dyncode.md) |

Handles und geliehene Views sind genau wie zuvor nur im Gültigkeitsbereich des
Callbacks gültig; speichern Sie sie nach dessen Rückkehr nicht zwischen.

## Entfernte Komfortschichten

Der Prototyp bündelte Allzweck-Hilfsmittel in der vtable. Diese sind **nicht**
Teil des ersten öffentlichen ABI:

| Prototyp | Erste Version |
|---|---|
| `ArenaCreate` / `StrMapCreate` / `IntMapCreate` / `StrBuilderCreate` / `ValueSetCreate` | nicht übernommen; verwenden Sie `Core->Allocate`/`Core->Deallocate` mit eigenen Containern oder die typisierten Domänen-APIs |
| Makros `NEVERC_FOR_EACH_*` / `NEVERC_COLLECT_*` | ersetzt durch die typisierte Iteration in der Capability-Tabelle jeder Domäne |
| `API->PluginGetArg` / `-fplugin-pass-arg=` | deklarieren Sie Optionen mit `RegisterOption` und lesen Sie sie über die Driver-API |
| `DiagNoteF` / `DiagWarningF` / `DiagErrorF` | `Core->EmitDiagnostic(NevercDiagnosticDescriptor)` |

## Laden und Kommandozeile

| Prototyp | Erste Version |
|---|---|
| `-fplugin-pass=<path>` | `-fplugin=<path>` |
| `-fplugin-pass-arg=key=value` | die Optionsschreibweise, die Sie in `RegisterOption` deklarieren (etwa `--driver-trace` oder `--my-opt=value`) |
| zwei Lader (`-fplugin` vs. `-fplugin-pass`) | ein Lader; ein Modul wird genau einem Lader übergeben |

## Versionierung

Der Prototyp verließ sich auf eine einzelne, monoton wachsende vtable samt
`NEVERC_API_FN`-Wächtern. In der ersten Version ist jede Capability-Tabelle für
sich versioniert: Fordern Sie die passende Major-Version an und prüfen Sie
`StructSize`/`TableSize`, bevor Sie ein angehängtes Feld lesen. Neue Funktionen
werden innerhalb der ersten ABI-Major an den stabilen Präfix einer Tabelle
angehängt, sodass ein gegen eine frühere Minor-Version gebautes Plugin mit
einem neueren Host weiterhin funktioniert.

## Durchgearbeitetes Beispiel

`pluginsdk/examples/DriverTracePlugin.c` zeigt die vollständige Gestalt der
ersten Version: den `neverc_plugin_entry`-Deskriptor, den Lebenszyklus
`ProcessBegin`/`Session`/`Task`, ein `RegisterOption` für ein echtes
CLI-Flag, ein `RegisterObserver` an `neverc.driver.raw_arguments` und ein
`RegisterInterceptor` an `neverc.driver.execute_job`, das `InvokeNext` genau
einmal aufruft. `pluginsdk/examples/ExamplePlugin.c` deckt die Phasen IR, MIR,
Object und Link ab.
