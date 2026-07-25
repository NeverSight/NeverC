**Lingue**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# ABI dei plugin NeverC

La prima ABI pubblica per plugin di NeverC è un'interfaccia in C puro, basata su
fasi. Un plugin è un modulo condiviso che esporta una sola funzione, negozia
tabelle di capacità versionate ed è eseguito dentro ambiti espliciti di Process,
Session e Task. Non include mai un header LLVM, non collega mai il compilatore e
non scambia mai un tipo C++ attraverso il confine.

L'API prototipo mai rilasciata e il suo punto di ingresso `nevercGetPluginInfo`
sono stati **rimossi**. I binari prototipo vengono rifiutati con una diagnostica
di migrazione; ricompilate i loro sorgenti con gli header pubblici. Per la
corrispondenza completa vecchio → nuovo si veda
[Migrazione dall'API prototipo](migration-from-prototype.it.md).

## Da qui si comincia

- [API Source e I/O](source.it.md)
- [API del preprocessore](prep.it.md)
- [API AST e semantica](ast-sema.it.md)
- [API IR](ir.it.md)
- [API MIR](mir.it.md)
- [API Target, MC, assembly e object](target-mc-object.it.md)
- [API DynCode](dyncode.it.md)
- [Convenzioni di chiamata personalizzate](custom-callconv/README.it.md)
- [Migrazione dall'API prototipo](migration-from-prototype.it.md)
- [Prove di copertura delle fasi](coverage.json)

## Modello di esecuzione

L'host guida il plugin attraverso tre ambiti annidati. Ogni ambito consegna al
plugin un puntatore di stato opaco che il plugin stesso alloca e possiede: un
plugin scritto correttamente non ha quindi bisogno di alcuno stato globale
mutabile.

| Ambito | Callback | Significato |
|---|---|---|
| Process | `ProcessBegin`, `Register`, `Destroy` | Un processo del compilatore. Qui si interrogano le interfacce e si registrano le capacità. |
| Session | `SessionBegin`, `SessionEnd` | Una invocazione del driver. |
| Task | `TaskBegin`, `TaskEnd` | Una unità di lavoro, identificata da `NevercTaskKind`. |

I tipi di task sono `INVOCATION`, `TRANSLATION_UNIT`, `LTO`, `LINK`, `CODEGEN`,
`OBJECT` e `DYNCODE`.

L'host chiama prima `ProcessBegin`, poi `Register` esattamente una volta. La
registrazione è l'unico punto in cui si possono aggiungere opzioni, osservatori,
interceptor e provider; dopo di essa il grafo delle fasi è congelato.

## Fasi

Una fase è una transizione con nome e versione, da un artefatto di ingresso a
uno di uscita. NeverC fornisce **130 fasi integrate** nei domini driver, source,
preprocessore, sintassi, semantica, IR, codegen, MIR, MC, assembly, object,
link e dyncode, più 8 famiglie di ID di estensione riservate alle fasi definite
dai plugin.

Ogni fase dichiara una policy, e un plugin può agganciarsi soltanto nei modi che
quella policy consente:

| Flag di policy | Cosa può fare un plugin |
|---|---|
| `NEVERC_PHASE_OBSERVABLE` | Registrare un osservatore per una notifica in sola lettura. |
| `NEVERC_PHASE_INTERCEPTABLE` | Avvolgere la fase e decidere se chiamare il resto della catena. |
| `NEVERC_PHASE_REPLACEABLE` | Registrare un provider che fornisca esso stesso l'output. |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | Saltare la transizione fornendo un handle di prova. |
| `NEVERC_PHASE_SEALED_HOST_GATE` | Nulla. Verificatori e commit appartengono all'host e non si possono sostituire, intercettare o saltare. |

Gli osservatori sono consegnati nei punti dichiarati dalla fase:
`NEVERC_OBSERVER_BEFORE`, `NEVERC_OBSERVER_AFTER` e
`NEVERC_OBSERVER_AFTER_COMMIT`.

Un interceptor riceve una `NevercPhaseContinuation`. Deve chiamare `InvokeNext`
**al più una volta**, sul thread della callback, e poi riportare
`NEVERC_PHASE_CONTINUE`, `NEVERC_PHASE_REPLACE` o `NEVERC_PHASE_SKIP` in
`NevercPhaseResult.Action`.

La fonte normativa per ID di fase, policy, livelli di stabilità e gate di
verifica è `neverc/include/neverc/Plugin/Schema/PhaseSchema.json`. Il file
generato `PluginPhaseSchema.inc` li espone come costanti di compilazione quali
`NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH` / `_LOW`.

## Un plugin minimo completo

Questo è `pluginsdk/templates/minimal/Plugin.c`. Si carica, negozia l'ABI, non
registra nulla e si scarica in modo pulito: copiate la directory e fatela
crescere da qui.

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
  /* Registrate qui opzioni, osservatori, interceptor o provider. */
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

`OutPlugin` è un buffer di proprietà del chiamante. All'ingresso il suo
`Header.StructSize` è la capacità scrivibile; il plugin scrive al più quel
numero di byte e riporta la dimensione che ha effettivamente prodotto.

## Negoziazione delle interfacce

Le tabelle di capacità si ottengono tramite un ID di interfaccia a 128 bit, non
tramite simboli. Richiedete la versione major con cui avete compilato e la minor
più bassa con cui riuscite a funzionare:

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

Verificare `TableSize` rispetto all'offset dell'ultima funzione che chiamerete è
la regola che rende estensibile questa ABI: un host più recente aggiunge campi
in coda e un plugin più vecchio continua a funzionare perché non legge mai oltre
il prefisso che ha verificato. La macro
`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` applica lo stesso controllo a
una struttura ricevuta.

Le interfacce pubbliche e i relativi header:

| Interfaccia | Tabella | Header |
|---|---|---|
| `NEVERC_INTERFACE_CORE` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO`, `..._SOURCE_LOCATION` | `NevercIOAPI`, `NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST`, `..._PARSER` | `NevercASTAPI`, `NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE`, `..._BUILDER`, `..._ANALYSIS`, `..._PASS`, `..._GEN`, `..._OPTIMIZATION` | Tabelle IR | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET`, `..._TARGET_ABI`, `..._CALLING_CONVENTION` | Tabelle Target | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR`, `..._MIR_ANALYSIS`, `..._MIR_PASS`, `..._MIR_PROVIDER` | Tabelle MIR | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC`, `..._MC_EMISSION`, `..._MC_PROVIDER`, `..._ASSEMBLY_PROVIDER` | Tabelle MC | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT`, `..._OBJECT_FORMAT`, `..._OBJECT_PHASE` | Tabelle Object | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK`, `..._LINK_REGISTRAR`, `..._LINK_PHASE` | Tabelle Link | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO`, `..._LTO_REGISTRAR` | Tabelle LTO | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE`, `..._DYNCODE_REGISTRAR`, `..._DYNCODE_PHASE` | Tabelle DynCode | `PluginDynCode.h` |

Un'interfaccia è STABLE (un host più recente può solo aggiungere) oppure
LOCKSTEP (schemi specifici del target che devono corrispondere esattamente).
Confrontate il digest dello schema prima di consumare valori LOCKSTEP.

## Compilazione

Includete l'header aggregato oppure soltanto i domini che usate:

```c
#include "neverc/Plugin/NevercPluginAPI.h"
```

Costruire un modulo condiviso con NeverC stesso:

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

Oppure con CMake contro un SDK installato:

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

Oppure con pkg-config:

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

Usate `.so`, `.dylib` o `.dll` a seconda dell'host. L'SDK non collega né LLVM né
il runtime di NeverC: `NevercPluginSDK::headers` è un target di soli header.

## Caricamento e configurazione

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| Opzione | Forma | Scopo |
|---|---|---|
| `-fplugin=<path>` | ripetibile | Carica un modulo condiviso di plugin. |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | ripetibile | Passa un valore qualificato a un'opzione di plugin registrata. |
| `-fplugin-provider=<phase>:<plugin-id>` | ripetibile | Sceglie quale plugin fornisce una fase sostituibile. |

Il qualificatore `<plugin-id>:` può essere omesso soltanto quando è attivo
esattamente un plugin. Le opzioni che un plugin registra con `RegisterOption`
sono accettate anche direttamente con la grafia dichiarata, in forma flag,
unita, separata o a più argomenti. Fornire argomenti di plugin o selezioni di
provider senza `-fplugin=` è un errore netto, non un silenzioso nulla di fatto.

## Regole dell'ABI

- Interrogate le tabelle tramite `QueryInterface`; pretendete la stessa versione
  major e controllate `StructSize` prima di toccare un campo.
- Inizializzate lo `Header` e lo spazio riservato di ogni struttura pubblica.
  Azzerate la struttura, poi impostate `StructSize`, `Major`, `Minor` e `Flags`.
- Trattate handle e viste prese in prestito come valori opachi con ambito. Non
  conservate mai un handle di ambito task oltre la sua callback, non usatelo mai
  in un'altra sessione o task e non fabbricate mai un valore di handle.
- Restituite `NevercStatus` da ogni callback. Non lasciate che un'eccezione C++
  o un puntatore di proprietà dell'host attraversino il confine C.
- Dichiarate il `NevercConcurrencyModel` (`SESSION_SERIAL`, `THREAD_SAFE`,
  `PROCESS_SERIAL`) e il `NevercReentrancyModel` (`NONE`, `ALLOWED`) più
  restrittivi che siano **veritieri**.
- Eseguite le modifiche di IR, MIR, AST, grafi e artefatti tramite le API
  transazionali dell'host: aprire una mutation, preparare le modifiche, quindi
  fare commit o abort. Il commit verifica e pubblica atomicamente; un commit
  fallito lascia intatto lo stato precedente.
- Tenete lo stato mutabile negli stati process/session/task forniti dall'host.
  Lo stato globale mutabile è controllato da
  `utils/plugin-api/check-global-state.py`.

Le nuove funzioni vengono aggiunte in coda a tabelle di capacità versionate in
modo indipendente. Il prefisso stabile di una tabella non cambia entro la prima
major dell'ABI (`NEVERC_PLUGIN_ABI_MAJOR` = 1).

## Stato e diagnostica

`NevercStatus` porta un `Code`, dei `Flags` e una parola `Detail`. Codici
comuni:

| Codice | Significato |
|---|---|
| `NEVERC_STATUS_OK` | Successo. |
| `NEVERC_STATUS_INVALID_ARGUMENT` | Un puntatore o un valore richiesto mancava o era malformato. |
| `NEVERC_STATUS_ABI_MISMATCH` | La tabella negoziata è troppo piccola o la major è diversa. |
| `NEVERC_STATUS_MISSING_INTERFACE` / `CAPABILITY_UNAVAILABLE` | L'host non offre la capacità richiesta. |
| `NEVERC_STATUS_STALE_HANDLE` / `WRONG_SESSION` / `WRONG_SCOPE` / `WRONG_TYPE` | Un handle è stato usato fuori dalla sua validità. |
| `NEVERC_STATUS_POLICY_VIOLATION` | La policy della fase non permette l'operazione. |
| `NEVERC_STATUS_VERIFICATION_FAILED` | Un verificatore sigillato dell'host ha rifiutato il prodotto. |
| `NEVERC_STATUS_CANCELLED` / `BUSY` / `RESOURCE_EXHAUSTED` | Cancellazione cooperativa o limiti di risorse. |

I bit di flag (`RECOVERABLE`, `OUTPUT_ALREADY_COMMITTED`,
`OUTPUT_MAY_BE_PARTIAL`, `OUTPUT_RECOVERY_REQUIRED`, `DURABILITY_UNCONFIRMED`)
descrivono che cosa è successo all'output, che è esattamente ciò che serve a un
sistema di build per decidere se un nuovo tentativo è sicuro.

Segnalate i problemi con `NevercCoreAPI.EmitDiagnostic` e un
`NevercDiagnosticDescriptor` che porti gravità, codice, ID del plugin, ID della
fase, messaggio, note, posizione nel sorgente, intervalli e fix-it. Chiamate
`CheckCancelled` prima di lavori costosi.

## Esempi

Compilarli tutti:

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

Ogni esempio viene compilato due volte — una con il compilatore C host
configurato e una con il NeverC appena costruito — così l'ABI è dimostrata da
entrambi i lati. I moduli finiscono in
`build-neverc/neverc/pluginsdk/examples/host/`.

| Esempio | Target CMake | Mostra |
|---|---|---|
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | Registrazione di opzioni, osservazione di fasi, intercettazione di job |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | Un provider VFS che serve un header in memoria |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | Intercettazione del parser e mutazione atomica dell'AST |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | Pass IR a livello di modulo che percorre l'elenco delle funzioni con un cursore di valori |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | Un pass IR di funzione stabile |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | Un pass MIR stabile all'hook pre-emit |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | Eventi di emissione MC in sola lettura |
| `ObjectRewritePlugin.c` | `neverc-plugin-example-object-rewrite` | Riscrittura transazionale dell'ObjectGraph |
| `CustomCallConvPlugin.c` | `neverc-plugin-example-custom-callconv` | Convenzioni di chiamata guidate dai dati |
| `DynCodeTracePlugin.c` | `neverc-plugin-example-dyncode-trace` | Osservazione della pipeline dyncode |
| `DynCodeEncoderPlugin.c` | `neverc-plugin-example-dyncode-encoder` | Intercettazione della codifica del set di caratteri dyncode |
| `CrtShimPlugin.c` | `neverc-plugin-example-crt-shim` | Un plugin senza alcuna dipendenza dal CRT |
| `BenchPlugin.c` | `neverc-plugin-example-abi-bench` | Microbenchmark del throughput di chiamata dell'ABI |

Caricarne uno:

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## Fonti normative

| File | Garantisce |
|---|---|
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | ID di fase, policy, stabilità, gate di verifica |
| `pluginsdk/manifest/plugin.json` | Versione dell'ABI, ID/versioni/stabilità delle interfacce, digest degli schemi, target supportati |
| `pluginsdk/abi/plugin.json` | Dimensione, allineamento e offset dei campi misurati per ogni struttura pubblica, per chiave ABI dell'host |
| `docs/plugin-api/coverage.json` | Associa ogni fase stabile a test positivi, negativi, di sostituzione, di osservatore e di gate sigillato |

Un SDK può quindi essere validato meccanicamente contro un host, e la
compilazione di un plugin può asserire il layout delle proprie strutture rispetto
alla chiave ABI in cui verrà caricato.
