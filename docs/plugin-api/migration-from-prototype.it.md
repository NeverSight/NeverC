**Lingue**: [English](migration-from-prototype.md) | [简体中文](migration-from-prototype.zh-CN.md) | [繁體中文](migration-from-prototype.zh-TW.md) | [日本語](migration-from-prototype.ja.md) | [한국어](migration-from-prototype.ko.md) | [Français](migration-from-prototype.fr.md) | [Deutsch](migration-from-prototype.de.md) | [Español](migration-from-prototype.es.md) | [Italiano](migration-from-prototype.it.md) | [Русский](migration-from-prototype.ru.md) | [العربية](migration-from-prototype.ar.md)

# Migrazione dall'API plugin prototipo

L'API plugin prototipo mai rilasciata — il suo punto di ingresso
`nevercGetPluginInfo`, l'unica vtable `NevercHostAPI`, le chiamate
`Register*Pass`, gli hook `NEVERC_INTERPOSE_*` e il caricatore
`-fplugin-pass=` — è stata rimossa prima della prima release pubblica. La
prima ABI pubblica è l'ABI a descrittori basata sulle fasi documentata in
[README.md](README.md): i plugin esportano `neverc_plugin_entry` e negoziano
tabelle di capacità versionate in modo indipendente.

Non esiste alcuno strato di compatibilità né una separazione `v1`/`v2`.
Ricompilate il *sorgente* del plugin contro gli header pubblici; questa pagina
mappa ogni costrutto del prototipo sul suo sostituto di prima versione, su un
cambiamento semantico o su una esplicita non continuità.

## I binari prototipo vengono rifiutati

Il caricamento di un oggetto condiviso prototipo fallisce con una diagnostica
stabile:

```
plugin exports the removed 'nevercGetPluginInfo' prototype ABI; migrate it to
the first public descriptor ABI and export 'neverc_plugin_entry'
```

Una libreria che non esporta nessuno dei due punti di ingresso fallisce con
`plugin has no 'neverc_plugin_entry' entry`. Nulla viene caricato finché il
sorgente non è stato portato.

## Punto di ingresso

| Prototipo | Prima ABI pubblica |
|---|---|
| `NevercPluginInfo nevercGetPluginInfo(void)` | `NevercStatus NEVERC_CALL neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin)` |

Il punto di ingresso non *restituisce* più una struttura per valore. Riempie un
`NevercPluginDescriptor` fornito dal chiamante, rispettando
`OutPlugin->Header.StructSize`, e restituisce un `NevercStatus`. Interrogate
`Bootstrap` per le tabelle di capacità che vi servono prima di dichiararne il
supporto.

## Campi di `NevercPluginInfo`

| Campo del prototipo | Corrispondenza nella prima versione |
|---|---|
| `APIVersion` | `Descriptor.Header` (`NevercABITableHeader` con `StructSize`, `NEVERC_PLUGIN_ABI_MAJOR`, `NEVERC_PLUGIN_ABI_MINOR`) |
| `PluginName` | `Descriptor.DisplayName` (`NevercStringView`), più un `Descriptor.PluginID` stabile in DNS inverso usato come chiave dello stato per ciascun ambito |
| `PluginVersion` | `Descriptor.Version` (`NevercSemanticVersion`) |
| `RegisterPasses(API, Reg)` | `Descriptor.Register(Core, Registrar, RegistrarContext, ProcessState)`, più i callback di ciclo di vita `ProcessBegin`, `SessionBegin`/`SessionEnd`, `TaskBegin`/`TaskEnd` |
| `Destroy()` | `Descriptor.Destroy(Core, ProcessState)` |
| *(nessun equivalente nel prototipo)* | `Descriptor.Concurrency` e `Descriptor.Reentrancy` devono essere dichiarati con verità (per esempio `NEVERC_CONCURRENCY_SESSION_SERIAL`, `NEVERC_REENTRANCY_ALLOWED`) |

## Accesso all'host: una vtable → tabelle di capacità

Il prototipo passava a ogni callback un'unica vtable `NevercHostAPI` con oltre
200 voci e proteggeva i nuovi campi con `NEVERC_API_FN`. La prima versione la
sostituisce con tabelle di capacità versionate in modo indipendente e
interrogate su richiesta:

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

Richiedete la major corrispondente e verificate `TableSize` con `offsetof`
prima di leggere un campo. Le interfacce sono suddivise per dominio: Core,
Driver, Source, Prep, AST, Sema, IR, MIR, Target, MC, Object, Link, LTO e
DynCode.

## Registrazione: `Register*Pass` + hook → observer/interceptor/provider

La registrazione del prototipo agganciava un callback a un hook:

```c
API->RegisterModulePass(Reg, NEVERC_INTERPOSE_PRE_OPT, myPass, ud, "my-pass");
```

La prima versione registra, all'interno di `Register`, un handler tipizzato su
una fase identificata da un `NevercInterfaceID` a 128 bit:

| Chiamata del prototipo | Chiamata al registrar nella prima versione |
|---|---|
| pass in sola lettura | `Registrar->RegisterObserver(NevercObserverDescriptor)` con i punti `NEVERC_OBSERVER_BEFORE`/`NEVERC_OBSERVER_AFTER` |
| pass che avvolge o cortocircuita una fase | `Registrar->RegisterInterceptor(NevercInterceptorDescriptor)`; chiamate `Continuation->InvokeNext` al più una volta e impostate `OutResult->Action` |
| pass che sostituisce una trasformazione integrata | `Registrar->RegisterProvider(...)` su una fase `REPLACEABLE` |
| lettura di `-fplugin-pass-arg=` | `Registrar->RegisterOption(NevercOptionDescriptor)` per dichiarare una vera opzione del driver |

Un «module pass a `PRE_OPT`» del prototipo diventa un observer, un interceptor
o un provider sulla fase IR `neverc.ir.pass.pre_opt`.

## Mappatura hook → fase

| Hook del prototipo | Fase della prima versione (nome) |
|---|---|
| `NEVERC_INTERPOSE_PRE_OPT` | `neverc.ir.pass.pre_opt` |
| `NEVERC_INTERPOSE_POST_OPT` | `neverc.ir.pass.post_opt` |
| `NEVERC_INTERPOSE_PIPELINE_START` | `neverc.ir.pass.pipeline_start` |
| `NEVERC_INTERPOSE_PIPELINE_LAST` | `neverc.ir.pass.optimizer_last` |
| `NEVERC_INTERPOSE_BEFORE_CODEGEN_PREEMIT` | `neverc.mir.pass.preemit` |
| `NEVERC_INTERPOSE_AFTER_CODEGEN_FINAL_MIR` | `neverc.mir.pass.final` |
| `NEVERC_INTERPOSE_LTO_PRE_OPT` / `LTO_POST_OPT` | fasi LTO `neverc.link.lto_resolve` / `neverc.link.lto_generate` (vedere [mir.md](mir.md)) |
| `NEVERC_INTERPOSE_LINK_PRE_LAYOUT` / `LINK_POST_LAYOUT` | `neverc.link.layout` osservata a `BEFORE` / `AFTER` |
| `NEVERC_INTERPOSE_LINK_POST_EMIT` | `neverc.link.post_emit` |
| `NEVERC_INTERPOSE_SC_*` (dyncode) | le fasi dyncode tipizzate in [dyncode.md](dyncode.md) |

L'elenco normativo degli ID di fase, delle politiche, dei livelli di stabilità
e dei gate di verifica è
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`; il contratto di
copertura eseguibile è [coverage.json](coverage.json). Un hook che un tempo era
un punto singolo può corrispondere a più ID di fase, ciascuno con la propria
politica e la propria prova.

## Callback di pass, handle e modifiche a byte

| Prototipo | Prima versione |
|---|---|
| `NevercModulePassFn(NevercModuleRef, API, ud)` e affini | i callback ricevono un `NevercPhaseFrame`; gli oggetti IR/MIR/AST/grafo sono handle tipizzati, delimitati e opachi ottenuti dalla tabella di capacità pertinente (vedere [ir.md](ir.md), [mir.md](mir.md), [ast-sema.md](ast-sema.md), [target-mc-object.md](target-mc-object.md)) |
| `NevercValueRef` generico | rimosso a favore di handle IR tipizzati |
| mutazione in loco di un `Ref` vivo | tutte le modifiche passano dalle API host transazionali |
| `NevercBinaryPassFn(uint8_t **Data, uint64_t *Len, ...)` | rimosso; le modifiche a byte di dyncode usano il costruttore di immagine verificato (read/write/insert/append/resize), vedere [dyncode.md](dyncode.md) |

Handle e viste prese in prestito sono validi solo nell'ambito del callback,
esattamente come prima; non memorizzateli dopo il ritorno del callback.

## Strati di comodità rimossi

Il prototipo includeva utilità generiche nella vtable. **Non** fanno parte
della prima ABI pubblica:

| Prototipo | Prima versione |
|---|---|
| `ArenaCreate` / `StrMapCreate` / `IntMapCreate` / `StrBuilderCreate` / `ValueSetCreate` | non riportati; usate `Core->Allocate`/`Core->Deallocate` con contenitori vostri, oppure le API di dominio tipizzate |
| macro `NEVERC_FOR_EACH_*` / `NEVERC_COLLECT_*` | sostituite dall'iterazione tipizzata nella tabella di capacità di ciascun dominio |
| `API->PluginGetArg` / `-fplugin-pass-arg=` | dichiarate le opzioni con `RegisterOption` e leggetele tramite l'API Driver |
| `DiagNoteF` / `DiagWarningF` / `DiagErrorF` | `Core->EmitDiagnostic(NevercDiagnosticDescriptor)` |

## Caricamento e riga di comando

| Prototipo | Prima versione |
|---|---|
| `-fplugin-pass=<path>` | `-fplugin=<path>` |
| `-fplugin-pass-arg=key=value` | la grafia dell'opzione che dichiarate in `RegisterOption` (per esempio `--driver-trace` o `--my-opt=value`) |
| due caricatori (`-fplugin` e `-fplugin-pass`) | un solo caricatore; un modulo viene affidato a un unico caricatore |

## Versionamento

Il prototipo si affidava a un'unica vtable in crescita monotona più le guardie
`NEVERC_API_FN`. Nella prima versione ogni tabella di capacità è versionata per
conto proprio: richiedete la major corrispondente e verificate
`StructSize`/`TableSize` prima di leggere un campo aggiunto. Le nuove funzioni
vengono accodate al prefisso stabile di una tabella all'interno della prima
major di ABI, così un plugin costruito contro una minor precedente continua a
funzionare con un host più recente.

## Esempio completo

`pluginsdk/examples/DriverTracePlugin.c` mostra la forma completa della prima
versione: il descrittore `neverc_plugin_entry`, il ciclo di vita
`ProcessBegin`/`Session`/`Task`, un `RegisterOption` per un vero flag da riga
di comando, un `RegisterObserver` su `neverc.driver.raw_arguments` e un
`RegisterInterceptor` su `neverc.driver.execute_job` che chiama `InvokeNext`
esattamente una volta. `pluginsdk/examples/ExamplePlugin.c` copre le fasi IR,
MIR, object e link.
