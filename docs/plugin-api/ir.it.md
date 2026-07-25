**Lingue**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

[← ABI dei plugin NeverC](README.it.md)

# API IR dei plugin NeverC

`PluginIR.h` espone l'IR di LLVM attraverso sei tabelle di capacità e uno schema
generato. Un plugin legge e riscrive l'IR, registra passi in cinque punti stabili
della pipeline, definisce analisi proprie, oppure sostituisce del tutto la
generazione dell'IR e la pipeline di ottimizzazione — senza includere neppure un
header di LLVM.

Opcode, generi di tipo e proprietà delle istruzioni sono **ID di schema
stabili**, non valori di enumerazione LLVM. È questa indirezione a permettere che
un plugin compilato oggi continui a funzionare quando l'host passa a una nuova
release di LLVM.

## Interfacce

```c
#include "neverc/Plugin/PluginIR.h"
```

| Interfaccia | Tabella | Slot | Scopo |
|---|---|--:|---|
| `NEVERC_INTERFACE_IR_CORE_{HIGH,LOW}` | `NevercIRCoreAPI` | 99 | Leggere e modificare moduli, valori, tipi, costanti, metadati, attributi |
| `NEVERC_INTERFACE_IR_BUILDER_{HIGH,LOW}` | `NevercIRBuilderAPI` | 29 | Costruzione transazionale |
| `NEVERC_INTERFACE_IR_ANALYSIS_{HIGH,LOW}` | `NevercIRAnalysisAPI` | 13 | Analisi native e di plugin |
| `NEVERC_INTERFACE_IR_PASS_{HIGH,LOW}` | `NevercIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_IR_GEN_{HIGH,LOW}` | `NevercIRGenAPI` | 5 | Sostituire l'abbassamento SemanticUnit → IR |
| `NEVERC_INTERFACE_IR_OPTIMIZATION_{HIGH,LOW}` | `NevercIROptimizationAPI` | 7 | Sostituire l'intera pipeline di ottimizzazione |

Ciascuna è `NEVERC_INTERFACE_STABLE` al major 1. Negoziate con i corrispondenti
`NEVERC_IR_*_API_MAJOR` / `_MINOR` e verificate che `TableSize` arrivi fino
all'ultimo slot che chiamate, esattamente come fa
`pluginsdk/examples/FunctionPass.c`:

```c
Status = Bootstrap->QueryInterface(
    Bootstrap->Context,
    (NevercInterfaceID){NEVERC_INTERFACE_IR_PASS_HIGH,
                        NEVERC_INTERFACE_IR_PASS_LOW},
    NEVERC_IR_PASS_API_MAJOR, NEVERC_IR_PASS_API_MINOR, &Table, &Minor,
    &StructSize);
if (!Table ||
    StructSize < offsetof(NevercIRPassAPI, RegisterPass) +
                     sizeof(((NevercIRPassAPI *)0)->RegisterPass))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

## Fasi

Otto fasi IR:

| Fase | Politica |
|---|---|
| `neverc.ir.generate` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.optimize` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.pass.pre_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pipeline_start` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.optimizer_last` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.post_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pre_codegen` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.final_verify` | OBSERVABLE, **VARCO SIGILLATO DELL'HOST** |

Le cinque fasi `pass.*` sono quelle a cui punta `NevercIRPassDescriptor.Phase`.
`neverc.ir.final_verify` esegue il verificatore di LLVM e non può essere
intercettata, sostituita o saltata da nulla — provider di ottimizzazione
compreso.

## Lo schema

`Schema/PluginIRSchema.inc` è generato e incluso da `PluginIR.h`. Pubblica un
digest e questi insiemi di costanti:

```c
#define NEVERC_IR_SCHEMA_CAPABILITY_MAJOR   UINT16_C(1)
#define NEVERC_IR_SCHEMA_DIGEST             "4302919d…"
#define NEVERC_IR_TYPE_KIND_COUNT           UINT32_C(22)
#define NEVERC_IR_VALUE_KIND_COUNT          UINT32_C(29)
#define NEVERC_IR_OPCODE_COUNT              UINT32_C(67)
#define NEVERC_IR_PREDICATE_COUNT           UINT32_C(26)
#define NEVERC_IR_LINKAGE_COUNT             UINT32_C(11)
#define NEVERC_IR_CALLING_CONVENTION_COUNT  UINT32_C(21)
#define NEVERC_IR_PROPERTY_COUNT            UINT32_C(23)
```

Gli ID portano il dominio nel byte alto — `0x41……` per i tipi, `0x42……` per i
generi di valore, `0x43……` per gli opcode, `0x49……` per le proprietà — così un
valore usato nel posto sbagliato viene rifiutato invece che frainteso.

## Handle e proprietà

Gli handle IR sono coppie opache `{Owner, Value}` con ambito di un singolo task,
e tutto ciò che sta dietro appartiene all'host.

- Non trattenete mai un handle dopo la fine della sua callback o del suo task.
- Non usate mai un handle in un'altra sessione o in un altro task.
- Una sostituzione confermata invalida gli handle degli oggetti sostituiti.
- Una modifica abortita rende obsoleti gli handle che essa aveva creato.
- Gli errori sono `NEVERC_STATUS_STALE_HANDLE`, `WRONG_SCOPE` o `WRONG_TYPE` —
  mai un puntatore LLVM grezzo.

Le stringhe e le viste di byte restituite da un'interrogazione sono prestate per
la durata della callback. L'unica eccezione è `ExportModule`, che restituisce un
`NevercIRSerializedBufferHandle` da riconsegnare a
`ReleaseSerializedBuffer`.

## Percorrere un modulo

Le collezioni si leggono con un cursore che porta con sé la propria generazione,
così una modifica a metà percorso viene rilevata invece di saltare voci in
silenzio:

```c
NevercIRValueCursor Cursor = {0};
Cursor.Header = (NevercABITableHeader){sizeof(Cursor),
                                       NEVERC_IR_CORE_API_MAJOR,
                                       NEVERC_IR_CORE_API_MINOR, 0};
Core->BeginValueCursor(Core->Context, Task, Module,
                       NEVERC_IR_COLLECTION_MODULE_FUNCTIONS, &Cursor);

NevercIRValueHandle Batch[32];
uint64_t Count = 0;
for (;;) {
  Core->CollectValueCursor(Core->Context, Task, &Cursor, Batch, 32, &Count);
  if (Count == 0)
    break;
  for (uint64_t I = 0; I != Count; ++I) {
    NevercStringView Name;
    Core->GetValueName(Core->Context, Task, Batch[I], &Name);
  }
}
```

Ripetete finché `Count` non torna zero. Le sette collezioni sono
`MODULE_FUNCTIONS`, `MODULE_GLOBALS`, `MODULE_ALIASES`, `MODULE_I_FUNCS`,
`FUNCTION_ARGUMENTS`, `FUNCTION_BLOCKS` e `BLOCK_INSTRUCTIONS`.

Tutto il resto è un'interrogazione diretta: `GetValueKind`, `GetValueType`,
`GetOperandCount` / `GetOperand` / `SetOperand`, `GetValueUseCount` /
`GetValueUse`, `GetTerminator`, `GetPredecessor*`, `GetSuccessor*`,
`GetPHIIncoming*`, e a livello di modulo `GetModuleIdentifier`,
`GetModuleTargetTriple`, `GetModuleDataLayout`, `GetModuleInlineAssembly` con i
relativi setter.

## Tipi e costanti

I tipi sono internati, quindi chiedere due volte dà lo stesso handle:

```c
NevercIRTypeHandle I32, Ptr, Fn;
Core->GetIntegerType(Core->Context, Task, 32, &I32);
Core->GetPointerType(Core->Context, Task, /*AddressSpace=*/0, &Ptr);

NevercIRTypeHandle Params[] = {I32, Ptr};
Core->GetFunctionType(Core->Context, Task, I32, Params, 2,
                      /*Variadic=*/0, &Fn);
```

`GetPrimitiveType` prende un genere di schema come `NEVERC_IR_TYPE_VOID`,
`_FLOAT`, `_DOUBLE` o `_TOKEN`; `GetArrayType`, `GetVectorType` (con un flag
`Scalable`) e `GetStructType` (con nome o letterale, packed o no) coprono il
resto.

Le costanti intere e in virgola mobile si costruiscono da parole a 64 bit
little-endian, così un `i128` non richiede alcun percorso speciale:

```c
uint64_t Words[2] = {0xFFFFFFFFFFFFFFFFULL, 0x1ULL};
NevercIRValueHandle C;
Core->CreateIntegerConstant(Core->Context, Task, I128, Words, 2, &C);
```

`GetNullConstant`, `GetPoisonConstant`, `GetUndefConstant`,
`CreateAggregateConstant` e `GetGlobalAddressConstant` coprono i casi semplici;
`CreateConstantBinaryExpression`, `CreateConstantCastExpression`,
`CreateConstantCompareExpression` e `CreateConstantGEPExpression` costruiscono
espressioni costanti.

## Proprietà delle istruzioni

Anziché un accessore per ciascun flag, il dettaglio di un'istruzione passa da un
valore di proprietà etichettato, indicizzato per ID di schema:

```c
typedef struct NevercIRPropertyValue {
  NevercABITableHeader Header;
  NevercIRPropertyValueKind Kind;   /* BOOL, UINT, ENUM, FLAGS, STRING, TYPE */
  uint32_t Reserved;
  uint64_t UnsignedValue;
  NevercIRTypeHandle TypeValue;
  NevercStringView StringValue;
} NevercIRPropertyValue;

NevercIRPropertyValue Value = {0};
Value.Header = /* … */;
Core->GetInstructionProperty(Core->Context, Task, Instruction,
                             NEVERC_IR_PROPERTY_ALIGNMENT, &Value);
```

Le 23 proprietà sono `NAME`, `FAST_MATH_FLAGS`, `NUW`, `NSW`, `EXACT`,
`DISJOINT`, `VOLATILE`, `ALIGNMENT`, `ATOMIC_ORDERING`, `SYNC_SCOPE`,
`PREDICATE`, `CALLING_CONVENTION`, `TAIL_CALL_KIND`, `INDICES`, `WEAK`,
`SUCCESS_ORDERING`, `FAILURE_ORDERING`, `INBOUNDS`, `SOURCE_ELEMENT_TYPE`,
`ALLOCATED_TYPE`, `ATTRIBUTES`, `CLEANUP` e `NUSW`. Gli ordinamenti atomici
vanno da `NOT_ATOMIC` a `SEQUENTIALLY_CONSISTENT`; i generi di chiamata di coda
sono `NONE`, `TAIL`, `MUST_TAIL` e `NO_TAIL`; i flag fast-math sono i soliti
sette bit, da `ALLOW_REASSOC` a `APPROX_FUNC`.

## Attributi

Gli attributi sono valori che si creano e poi si agganciano, il che rende
uniformi i quattro generi (`ENUM`, `INTEGER`, `STRING`, `TYPE`):

```c
NevercIRAttributeHandle NoInline;
Core->CreateEnumAttribute(Core->Context, Task, SV("noinline"), &NoInline);
Core->AddFunctionAttribute(Core->Context, Task, Function,
                           NEVERC_IR_ATTRIBUTE_LOCATION_FUNCTION,
                           /*ParameterIndex=*/0, NoInline);

NevercBool Present = NEVERC_FALSE;
Core->HasFunctionAttribute(Core->Context, Task, Function, SV("noinline"),
                           &Present);
```

`pluginsdk/examples/CustomCallConvPlugin.c` usa questo insieme a
`GetFunctionStringAttribute` per pilotare una convenzione di chiamata definita
dai dati.

## Modifica transazionale

Ogni cambiamento strutturale passa da `NevercIRBuilderAPI`. La modifica è la
transazione; il builder è un cursore al suo interno.

```c
NevercIRMutationHandle Mutation;
NevercIRBuilderHandle Builder;

Builders->BeginMutation(Builders->Context, Task,
                        NEVERC_IR_MUTATION_SCOPE_FUNCTION, Function,
                        &Mutation);
Builders->CreateBuilder(Builders->Context, Task, Mutation, &Builder);
Builders->SetInsertBefore(Builders->Context, Task, Builder, Terminator);

NevercIRValueHandle Sum;
Builders->BuildBinary(Builders->Context, Task, Builder,
                      NEVERC_IR_OPCODE_ADD, Left, Right, SV("sum"), &Sum);

Status = Builders->CommitMutation(Builders->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  Builders->AbortMutation(Builders->Context, Task, Mutation);

Builders->DestroyBuilder(Builders->Context, Task, Builder);
Builders->DestroyMutation(Builders->Context, Task, Mutation);
```

Gli ambiti sono `NEVERC_IR_MUTATION_SCOPE_MODULE`, `_FUNCTION` e `_LOOP`;
`ScopeRoot` nomina la funzione o l'intestazione del ciclo. Il commit verifica il
candidato e pubblica atomicamente: se il verificatore fallisce, l'host torna
indietro e il modulo precedente sopravvive intatto.

Le chiamate di costruzione sono `BuildBinary`, `BuildUnary`, `BuildCompare`,
`BuildCast`, `BuildSelect`, `BuildAlloca`, `BuildLoad`, `BuildStore`,
`BuildGetElementPtr`, `BuildCall`, `BuildPhi`, `BuildBranch`,
`BuildConditionalBranch`, `BuildUnreachable`, `BuildReturn` e
`BuildReturnVoid`. `SetDebugLocation` e `SetFastMathFlags` si applicano a tutto
ciò che il builder emette in seguito.

Notate l'asimmetria: `AddPhiIncoming`, `CreateFunction` e `CreateBasicBlock`
prendono la **mutation**, non il builder, perché non sono legate a un punto di
inserimento.

`DestroyMutation` è distinto da commit e abort. Ogni `BeginMutation` richiede
esattamente un `DestroyMutation`, comunque sia finita la transazione.

## Passi

```c
NevercIRPassDescriptor Pass = {0};
Pass.Header = (NevercABITableHeader){sizeof(Pass), NEVERC_IR_PASS_API_MAJOR,
                                     NEVERC_IR_PASS_API_MINOR, 0};
Pass.PassID        = SV("example.function-pass");
Pass.Phase         = (NevercInterfaceID){
                         NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH,
                         NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW};
Pass.Level         = NEVERC_IR_PASS_LEVEL_FUNCTION;
Pass.Deterministic = NEVERC_TRUE;
Pass.Cacheable     = NEVERC_TRUE;
Pass.Run           = run_function;
PassAPI->RegisterPass(PassAPI->Context, RegistrarContext, &Pass);
```

I livelli sono `MODULE`, `CGSCC`, `FUNCTION` e `LOOP`. L'invocazione porta solo
gli handle validi per il proprio livello:

```c
typedef struct NevercIRPassInvocation {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercStringView PassID;
  NevercIRPassLevel Level;
  NevercIROptimizationLevel OptimizationLevel;  /* O0…O3, Os, Oz */
  NevercIRModuleHandle Module;
  NevercIRValueHandle Function;                 /* FUNCTION e LOOP       */
  NevercIRValueHandle LoopHeader;               /* solo LOOP             */
  const NevercIRValueHandle *SCCFunctions;      /* solo CGSCC            */
  uint64_t SCCFunctionCount;
  const NevercIRCoreAPI *Core;
  const NevercIRBuilderAPI *Builder;
  const NevercIRAnalysisAPI *Analyses;
  uint64_t Reserved[2];
} NevercIRPassInvocation;
```

I tre puntatori alle API arrivano con l'invocazione, quindi il corpo di un passo
non deve conservare alcuna tabella.

Segnalate che cosa è sopravvissuto tramite `OutPreserved`:

```c
OutPreserved->Flags = NEVERC_IR_PRESERVE_ALL;   /* oppure _NONE, oppure _CFG */
```

`NEVERC_IR_PRESERVE_CFG` significa che il grafo di flusso di controllo è intatto
anche se le istruzioni sono cambiate. Le analisi personalizzate si preservano
elencandole in `CustomAnalyses`. Non dichiarate `PRESERVE_ALL` dopo aver
cambiato l'IR: l'adattatore confronta la generazione del modulo e rifiuta una
dichiarazione falsa.

I passi di funzione e di ciclo possono girare in parallelo, quindi lo stato
mutabile del plugin deve corrispondere al `NevercConcurrencyModel` dichiarato.

## Analisi

Sette analisi native sono interrogabili per ID: `DOMINATOR_TREE`,
`POST_DOMINATOR_TREE`, `LOOP_INFO`, `SCALAR_EVOLUTION`, `MEMORY_SSA`,
`CALL_GRAPH` e `ALIAS`.

```c
NevercIRAnalysisResultHandle Loops;
Analyses->QueryBuiltin(Analyses->Context, Task,
                       NEVERC_IR_ANALYSIS_LOOP_INFO, Function, &Loops);

uint64_t LoopCount = 0;
Analyses->GetLoopCount(Analyses->Context, Task, Loops, &LoopCount);
for (uint64_t I = 0; I != LoopCount; ++I) {
  NevercIRValueHandle Header;
  Analyses->GetLoopHeader(Analyses->Context, Task, Loops, I, &Header);
}
```

Ognuna ha accessori tipizzati invece di un blocco opaco:
`DominatorTreeDominates`, `GetLoopCount` / `GetLoopHeader` /
`GetLoopForBlock`, `GetScalarEvolutionConstantTripCount`,
`GetMemoryAccessKind` (`NONE`, `USE`, `DEF`, `PHI`, `LIVE_ON_ENTRY`),
`GetDirectCalleeCount` / `GetDirectCallee`, e `Alias` (`NO`, `MAY`, `PARTIAL`,
`MUST`).

Un'analisi di plugin si registra con il proprio ciclo di vita:

```c
NevercIRAnalysisDescriptor Analysis = {0};
Analysis.Header          = /* … */;
Analysis.AnalysisID      = MyAnalysisID;
Analysis.Name            = SV("example.my-analysis");
Analysis.Level           = NEVERC_IR_PASS_LEVEL_FUNCTION;
Analysis.Dependencies    = Deps;
Analysis.DependencyCount = DepCount;
Analysis.Compute         = compute;
Analysis.Query           = query;
Analysis.Invalidate      = invalidate;
Analysis.Destroy         = destroy;
Analyses->RegisterAnalysis(Analyses->Context, RegistrarContext, &Analysis);
```

A `Invalidate` viene detto il perché — `INVALIDATED_BY_PASS` oppure
`INVALIDATED_BY_PLAN_DESTROY`. I risultati sono messi in cache per invocazione e
scartati in base a ciò che il passo in esecuzione ha preservato. I cicli di
dipendenza sono rifiutati alla registrazione, e modificare l'IR dall'interno di
una callback di analisi viene negato.

## Sostituire generazione e ottimizzazione

`NevercIRGenAPI` sostituisce `neverc.ir.generate`:

```c
NevercIRGeneratePhaseInput In = {0};
In.Header = /* … */;
Gen->GetGeneratePhaseInput(Gen->Context, Frame, Frame->Input, &In);
/* In.SemanticUnit, .TargetTriple, .DataLayout, .SourceIdentity,
   .SourceDigest */

const NevercIRCoreAPI *Core;
const NevercIRBuilderAPI *Builders;
Gen->CreateModule(Gen->Context, Frame, SV("my.module"), &Core, &Builders);
/* … costruire il modulo … */

NevercIRModuleArtifactDescriptor Descriptor = {0};
Descriptor.Header           = /* … */;
Descriptor.Product          = MyProductID;
Descriptor.DependencyDigest = Digest;
Gen->PublishModule(Gen->Context, Frame, &Descriptor, &Output);
```

`ImportModule` parte da bitcode o da IR testuale invece che da un modulo vuoto.
`NevercIROptimizationAPI` ha la stessa forma per `neverc.ir.optimize`, più
`GetInputModule` per raggiungere il modulo in ingresso e `RunBuiltinPipeline`
per delegare alla pipeline nativa e poi post-elaborarne il risultato.

Entrambe le vie pubblicano attraverso l'host invece di restituire un puntatore,
entrambe verificano la compatibilità col target, ed entrambe conservano
atomicamente il vecchio modulo se la pubblicazione fallisce. Dopodiché
`neverc.ir.final_verify` viene comunque eseguita.

## Esempi

| File | Mostra |
|---|---|
| `pluginsdk/examples/FunctionPass.c` | Un passo di funzione in sola lettura, negoziazione ABI inclusa |
| `pluginsdk/examples/ExamplePlugin.c` | Un passo di modulo che percorre le funzioni con un cursore di valori |
| `pluginsdk/examples/CustomCallConvPlugin.c` | Attributi e proprietà del punto di chiamata |

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

Usate il suffisso di modulo che CMake ha prodotto per la vostra piattaforma.

## Regole

- Restituite un `NevercStatus` da ogni callback. Il fallimento di un plugin
  diventa una diagnostica strutturata; non lasciate mai che un'eccezione superi
  il confine C.
- Azzerate ogni struttura di uscita e impostatene l'`Header` prima della chiamata
  che la riempie.
- Non scrivete a mano i valori numerici di opcode, tipo o proprietà. Usate i nomi
  di `PluginIRSchema.inc`, così una revisione dello schema diventa un errore di
  compilazione.
- Ogni `BeginMutation` arriva esattamente a un `DestroyMutation`, e ogni
  `CreateBuilder` esattamente a un `DestroyBuilder`, anche sui percorsi di
  errore.
- Rilasciate ciò che `ExportModule` vi consegna con
  `ReleaseSerializedBuffer`.
- Non dichiarate mai `NEVERC_IR_PRESERVE_ALL` dopo aver modificato l'IR.
- Date per scontato che i passi di funzione e di ciclo girino in parallelo, a
  meno che il plugin non abbia dichiarato
  `NEVERC_CONCURRENCY_SESSION_SERIAL`.
- `neverc.ir.final_verify` è sigillata. Nulla di ciò che un plugin fa può
  saltarla.

Vedere `PluginIR.h`, `Schema/PluginIRSchema.inc`, `Schema/PhaseSchema.json` e
`coverage.json` per le dichiarazioni normative, le costanti di schema, le
politiche di fase e le prove dei test.
