**Lingue**: [English](target-mc-object.md) | [简体中文](target-mc-object.zh-CN.md) | [繁體中文](target-mc-object.zh-TW.md) | [日本語](target-mc-object.ja.md) | [한국어](target-mc-object.ko.md) | [Français](target-mc-object.fr.md) | [Deutsch](target-mc-object.de.md) | [Español](target-mc-object.es.md) | [Italiano](target-mc-object.it.md) | [Русский](target-mc-object.ru.md) | [العربية](target-mc-object.ar.md)

[← ABI dei plugin NeverC](README.it.md)

# API target, MC, assembly e oggetti dei plugin NeverC

Il back-end sono quattro header e ventinove fasi. [`PluginTarget.h`] descrive un
target e le rotte attraverso la generazione di codice. [`PluginMC.h`] costruisce
e osserva il codice macchina. L'analisi e la stampa dell'assembly vivono nello
stesso header. [`PluginObject.h`] trasforma un file rilocabile in un grafo
normalizzato e viceversa.

Insieme permettono a un plugin di aggiungere un target, sostituire un passo di
abbassamento o tutti quanti, sorvegliare ogni istruzione mentre viene emessa,
definire un dialetto assembly o riscrivere un file oggetto — attraverso un ABI
C puro che non espone mai un `MCInst`, un `MCSection` o un
`object::ObjectFile` di LLVM.

## Interfacce

```c
#include "neverc/Plugin/PluginTarget.h"
#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginObject.h"   /* includes both of the above */
```

| Interfaccia | Tabella | Slot | Scopo |
|---|---|--:|---|
| `NEVERC_INTERFACE_TARGET_*` | `NevercTargetAPI` | 2 | `RegisterTarget`, `RegisterCodeGenEdge` |
| `NEVERC_INTERFACE_TARGET_ABI_*` | `NevercTargetABIAPI` | 1 | `RegisterABI` |
| `NEVERC_INTERFACE_CALLING_CONVENTION_*` | `NevercCallingConventionAPI` | 1 | `RegisterCallingConvention` |
| `NEVERC_INTERFACE_MC_*` | `NevercMCAPI` | 53 | Leggere e modificare un `MCUnit`; registrare encoder, decoder, back-end |
| `NEVERC_INTERFACE_MC_EMISSION_*` | `NevercMCEmissionAPI` | 7 | Eventi di emissione e istantanee del layout |
| `NEVERC_INTERFACE_MC_PROVIDER_*` | `NevercMCProviderAPI` | 4 | Sostituire MIR → MC |
| `NEVERC_INTERFACE_ASSEMBLY_PROVIDER_*` | `NevercAssemblyProviderAPI` | 8 | Sostituire il parser o lo stampatore assembly |
| `NEVERC_INTERFACE_OBJECT_*` | `NevercObjectAPI` | 34 | Leggere e modificare un ObjectGraph |
| `NEVERC_INTERFACE_OBJECT_FORMAT_*` | `NevercObjectFormatAPI` | 1 | `RegisterFormat` |
| `NEVERC_INTERFACE_OBJECT_PHASE_*` | `NevercObjectPhaseAPI` | 2 | `GetGraph`, `GetImage` |

## Due livelli di compatibilità

È la regola che governa tutto il resto di questo documento.

**STABLE**, e sicuro da fissare nel codice: i descrittori indipendenti dal
target, gli identificatori di fase, gli identificatori di artefatto, i
contenitori MC e ObjectGraph, le transazioni di output e ogni contratto di
callback.

**LOCKSTEP**, e pericoloso senza un controllo: gli schemi di opcode, registro,
operando, fixup, rilocazione e convenzione di chiamata specifici del target. I
loro valori numerici hanno senso solo rispetto a una precisa revisione dello
schema.

Ovunque compaia un valore LOCKSTEP, accanto compare un digest dello schema.
Confrontatelo prima di leggere il valore:

```c
if (!string_equal(Target.SchemaDigest, MY_COMPILED_SCHEMA_DIGEST))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

Anche NeverC rifiuta uno schema discordante prima di invocare un provider,
quindi il controllo è una doppia sicurezza — ma un plugin che lo salta e legge
comunque un opcode grezzo interpreterà le istruzioni in modo errato, in
silenzio.

## Le fasi

Ventinove, in quattro domini.

### `codegen` — instradamento (4)

| Fase | Policy |
|---|---|
| `neverc.codegen.ir_to_mir` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.mir_to_mc` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.coarse_lower` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.codegen.product_verify` | OBSERVABLE, **SEALED** |

### `mc` — codice macchina (13)

`neverc.mc.encode`, `neverc.mc.decode` e `neverc.mc.layout` sono OBSERVABLE,
INTERCEPTABLE, REPLACEABLE.

`neverc.mc.emission.pre_instruction` è l'unico evento di emissione che è anche
REPLACEABLE: è lì che si sostituisce un'istruzione. Gli altri nove
(`unit_begin`, `unit_end`, `section_change`, `post_instruction`,
`post_encode`, `fixup`, `relaxation_round`, `pre_layout`, `post_layout`) sono
di sola osservazione.

### `assembly` (4)

`neverc.assembly.parse` e `neverc.assembly.print` sono REPLACEABLE.
`neverc.assembly.final_verify` e `neverc.assembly.commit` sono SEALED.

### `object` (8)

`neverc.object.probe`, `read`, `write`, `pre_write` e `post_layout` sono
REPLACEABLE; `neverc.object.post_write` è solo INTERCEPTABLE;
`neverc.object.final_verify` e `neverc.object.commit` sono SEALED.

## Registrare un target

`NevercTargetDescriptor` è il descrittore più grande dell'ABI perché porta con
sé tutto ciò che front-end e back-end devono sapere:

```c
typedef struct NevercTargetDescriptor {
  NevercABITableHeader Header;
  NevercTargetID TargetID;
  NevercStringView CanonicalName;
  NevercStringArrayView Aliases;
  NevercStructArrayView TripleMatchers;    /* NevercTargetTripleMatcher[] */
  NevercTargetABIID DefaultABI;
  NevercCallingConventionID DefaultCallingConvention;
  NevercInterfaceID MCSchemaID;
  NevercInterfaceID DefaultObjectFormatID;
  NevercTargetMachineDescriptor Machine;
  NevercStructArrayView Macros;            /* predefined macros           */
  NevercStructArrayView Builtins;          /* target builtins + lowering  */
  NevercStructArrayView Registers;         /* inline-asm register names   */
  NevercStructArrayView Constraints;       /* inline-asm constraints      */
  NevercStringView Clobbers;
  uint64_t Flags;
  NevercTargetValidateCPUFn ValidateCPU;
  NevercTargetCanonicalizeCPUFn CanonicalizeCPU;
  NevercTargetListCPUsFn ListCPUs;
  NevercTargetResolveFeaturesFn ResolveFeatures;
  NevercCreateTargetMachineFn CreateTargetMachine;
  NevercDestroyTargetMachineFn DestroyTargetMachine;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercTargetDescriptor;
```

`TripleMatchers` decide quando il target viene selezionato: ogni matcher
indica un'architettura, un fornitore, un sistema operativo e un ambiente, più
una `Priority` che scioglie i pareggi rispetto ai target integrati.

`Machine` è un `NevercTargetMachineDescriptor` — layout dei dati, CPU
predefinita e di tuning, la tabella delle funzionalità, gli ABI, le convenzioni
di chiamata e i formati oggetto supportati, gli spazi di indirizzamento, i
modelli di rilocazione e di codice (sia come predefinito sia come maschera dei
supportati), il modello delle eccezioni (`NONE`, `DWARF`, `SJLJ`, `SEH`,
`WASM`), il modello di srotolamento, l'endianness, l'ampiezza di
pointer/int/long/long long, l'allineamento dello stack, le ampiezze massime
atomica e vettoriale, il tipo di `va_list`, i livelli di esecuzione (`USER`,
`KERNEL`, `HYPERVISOR`, `FIRMWARE`) e il supporto TLS.

Le funzioni integrate del target portano la propria callback di abbassamento,
che riceve un costruttore IR vivo:

```c
static NevercStatus NEVERC_CALL
lower_builtin(void *UserData,
              const NevercTargetBuiltinLoweringInvocation *In,
              NevercIRValueHandle *OutResult) {
  /* In->Core, In->Builder, In->Mutation, In->IRBuilder,
     In->ResultType, In->Arguments, In->ArgumentCount */
  return In->Builder->BuildCall(/* … */);
}
```

## ABI e convenzioni di chiamata

Un ABI classifica le firme delle funzioni:

```c
static NevercStatus NEVERC_CALL
classify(void *UserData, const NevercABIFunctionQuery *Query,
         NevercABIArgumentClassification *ReturnValue,
         NevercABIArgumentClassificationArray *Arguments) {
  ReturnValue->Kind = NEVERC_ABI_ARGUMENT_DIRECT;
  for (uint64_t I = 0; I != Arguments->Count; ++I) {
    NevercABIArgumentClassification *A = &Arguments->Data[I];
    A->Kind  = NEVERC_ABI_ARGUMENT_INDIRECT;
    A->Flags = NEVERC_ABI_ARGUMENT_BYVAL;
  }
  return neverc_status_ok();
}
```

I generi di argomento sono `DIRECT`, `EXTEND`, `INDIRECT`, `IGNORE`,
`EXPAND`, `INDIRECT_ALIASED` e `COERCE_AND_EXPAND`; i flag sono `BYVAL`,
`REALIGN`, `INREG`, `SRET_AFTER_THIS`, `CAN_BE_FLATTENED`, `SIGN_EXTEND` e
`PADDING_INREG`. La coercizione è `NONE`, `INTEGER`, `FLOAT` o `POINTER`, e
`COERCE_AND_EXPAND` fornisce un array di `NevercABICoercionElement`.

Una convenzione di chiamata scende di un livello e assegna le posizioni
effettive:

```c
static NevercStatus NEVERC_CALL
plan(void *UserData, const NevercCallingConventionQuery *Query,
     NevercCallingConventionPlan *Plan) {
  /* Query->TargetID, ->CallingConventionID, ->SchemaDigest, ->Function */
  /* Fill Plan->ReturnLocations and Plan->ArgumentLocations with
     NevercCallingConventionLocation records: REGISTER or STACK,
     ValueIndex, PieceOffset, Size, Alignment, RegisterNumber,
     StackOffset, and INDIRECT / BYVAL flags.                       */
  Plan->CalleeSavedRegisters = MySavedRegisters;
  Plan->StackAlignment       = 16;
  return neverc_status_ok();
}
```

`Query->SchemaDigest` è un valore LOCKSTEP: `RegisterNumber` significa
qualcosa solo rispetto allo schema che nomina. Per l'esempio completo si vedano
[Convenzioni di chiamata personalizzate](custom-callconv/README.it.md#piani-materializzati) e
[`pluginsdk/examples/CustomCallConvPlugin.c`].

## Rotte di generazione del codice

Una rotta viene scelta a partire dalla `NevercTargetKey` canonica:
identificatore del target, parti della tripla, CPU, CPU di tuning,
funzionalità, ABI, convenzione di chiamata, formato oggetto, modello di
rilocazione, modello di codice, livello di esecuzione, ampiezza dei puntatori,
endianness e digest dello schema. Registrate gli archi che sapete servire:

```c
NevercCodeGenEdgeDescriptor Edge = {0};
Edge.Header          = /* … */;
Edge.EdgeID          = MyEdgeID;
Edge.CanonicalName   = SV("com.example.mir-to-mc");
Edge.TargetID        = MyTargetID;
Edge.InputKind       = NEVERC_CODEGEN_PRODUCT_MIR;
Edge.OutputKind      = NEVERC_CODEGEN_PRODUCT_MC;
Edge.CompatibilityKey = SV("…");
Edge.ProviderID      = SV("com.example.backend");
Target->RegisterCodeGenEdge(Target->Context, RegistrarContext, &Edge);
```

I generi di prodotto sono `IR`, `MIR`, `MC`, `ASSEMBLY`, `OBJECT_GRAPH`,
`OBJECT_IMAGE` e `CUSTOM`. La rotta a grana fine è
`IR → MIR → MC → ObjectGraph → ObjectImage`.

Impostare `NEVERC_CODEGEN_EDGE_COARSE` e fornire `CoarseLower` sostituisce in
un colpo solo l'intera tratta `IR → ObjectImage`:

```c
static NevercStatus NEVERC_CALL
coarse_lower(void *UserData, NevercTaskHandle Task,
             const NevercCodeGenRequest *Request,
             NevercCodeGenProductCandidate *OutCandidate) {
  /* Request->Target, ->Input, ->InputKind, ->OutputKind,
     ->OptimizationLevel, ->HasFinalIRProof                */
  OutCandidate->Kind      = NEVERC_CODEGEN_PRODUCT_OBJECT_IMAGE;
  OutCandidate->Artifact  = MyImage;
  OutCandidate->ProductID = MyProductID;
  return neverc_status_ok();
}
```

Una rotta grossolana passa comunque per `neverc.codegen.product_verify` e per
il commit transazionale dell'output. `VerifyProduct` viene chiamata con gli
obblighi che l'host si aspetta siano stati assolti — `VERIFY_FINAL_IR`,
`VERIFY_TARGET_KEY`, `VERIFY_PRODUCT_KIND`, `VERIFY_PRODUCT_ID`,
`VERIFY_STRUCTURE` — così un provider non può saltare di soppiatto un gate
prendendo una scorciatoia.

## Costruire MC

Un `MCUnit` contiene sezioni, simboli, espressioni, frammenti, istruzioni,
operandi e fixup. La lettura è un'iterazione first/next:

```c
NevercMCUnitInfo Unit = {0};
Unit.Header = /* … */;
MC->GetUnitInfo(MC->Context, Task, UnitHandle, &Unit);

NevercMCSectionHandle Section;
MC->GetFirstSection(MC->Context, Task, UnitHandle, &Section);
while (!neverc_handle_is_null(Section)) {
  NevercMCFragmentHandle Fragment;
  MC->GetFirstFragment(MC->Context, Task, Section, &Fragment);
  /* … */
  MC->GetNextSection(MC->Context, Task, Section, &Section);
}
```

La mutazione è transazionale, come ovunque:

```c
NevercMCMutationHandle Mutation;
MC->BeginMutation(MC->Context, Task, Unit, &Mutation);
MC->CreateSection(MC->Context, Task, Mutation, &SectionDescriptor, &Section);
MC->CreateSymbol(MC->Context, Task, Mutation, &SymbolDescriptor, &Symbol);
MC->AppendInstruction(MC->Context, Task, Mutation, Section, &Instruction);
Status = MC->CommitMutation(MC->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  MC->AbandonMutation(MC->Context, Task, Mutation);
```

Gli handle hanno ambito di task e sono controllati per generazione, così un
handle proveniente da una mutazione abbandonata viene rifiutato anziché
riutilizzato.

I flag di sezione sono `ALLOCATED`, `EXECUTABLE`, `WRITABLE`, `MERGEABLE` e
`DEBUG`. I binding dei simboli sono `LOCAL`, `GLOBAL` e `WEAK`; i tipi sono
`NONE`, `FUNCTION`, `OBJECT`, `SECTION` e `TLS`; le definizioni sono
`UNDEFINED`, `SECTION`, `ABSOLUTE` e `COMMON`. Le espressioni supportano gli
unari `PLUS`, `MINUS`, `NOT` e i binari `ADD`, `SUBTRACT`, `MULTIPLY`,
`DIVIDE`, `AND`, `OR`, `XOR`, `SHIFT_LEFT`, `SHIFT_RIGHT`. Passate
`NEVERC_MC_AUTOMATIC_OFFSET` dove volete che sia l'host a collocare qualcosa
per voi.

`RegisterSchema` pubblica uno schema MC di target, e `GetSchemaToken` /
`GetSchemaTokenInfo` risolvono un nome in un token LOCKSTEP e viceversa.

## Osservare l'emissione

Il flusso di emissione riporta in ordine dieci generi di evento — uno per
ciascuna fase `neverc.mc.emission.*`. L'ABI riserva anche
`NEVERC_MC_EMISSION_PRE_OBJECT_WRITE`; la scrittura dell'oggetto in sé è la
fase separata `neverc.object.pre_write`.
Sottoscrivetevi come osservatori e leggete l'evento:

```c
NevercMCEmissionEventInfo Event = {0};
Event.Header = /* … */;
Emission->GetEvent(Emission->Context, Frame, Frame->Input, &Event);
/* Event.Kind, Event.Flags */
```

`Flags` dice quali parti dell'evento sono popolate: `HAS_SECTION`,
`HAS_INSTRUCTION`, `HAS_ENCODING`, `HAS_FIXUP`, `HAS_LAYOUT` e
`CAN_REPLACE_INSTRUCTION`. Controllate il flag prima di leggere il campo
corrispondente: un evento che non ha ancora una codifica non ne avrà una solo
perché l'avete chiesta.

`GetLayoutSection`, `GetLayoutFragment`, `GetLayoutSymbol` e
`GetLayoutFixup` forniscono indirizzi e dimensioni una volta impostato
`HAS_LAYOUT`.

A `pre_instruction`, e solo quando `CAN_REPLACE_INSTRUCTION` è impostato,
potete sostituire:

```c
const NevercMCAPI *MC;
NevercMCUnitHandle Unit;
NevercMCInstHandle Instruction;
Emission->BeginInstructionReplacement(Emission->Context, Frame, Continuation,
                                       &MC, &Unit, &Instruction);
/* mutate Instruction through MC->BeginMutation / … / CommitMutation */
Emission->PublishInstructionReplacement(Emission->Context, Frame, Continuation,
                                         &OutResult->Output);
```

[`pluginsdk/examples/MCObserverPlugin.c`] ne è la versione in sola lettura.

## Encoder, decoder e layout

Tre registrazioni estendono il back-end del codice macchina, tutte indicizzate
per target e digest dello schema:

```c
MC->RegisterEncoder(MC->Context, RegistrarContext, &EncoderDescriptor);
MC->RegisterDecoder(MC->Context, RegistrarContext, &DecoderDescriptor);
MC->RegisterAsmBackend(MC->Context, RegistrarContext, &BackendDescriptor);
```

Un encoder scrive attraverso un sink invece di restituire un buffer, il che
lascia la proprietà dalla parte dell'host:

```c
Sink->WriteBytes(Sink->Context, Bytes);
Sink->AddFixup(Sink->Context, &Fixup);
```

Un decoder riporta uno tra `NEVERC_MC_DECODE_SUCCESS`, `_SOFT_FAIL`,
`_UNKNOWN` e `_FAIL`. I generi di fixup si autodescrivono tramite
`NevercMCFixupKindInfo` con i flag `PC_RELATIVE`, `SIGNED`, `RELAXABLE` e
`TARGET`.

Il back-end asm possiede il rilassamento. Il layout emette un digest di prova,
e **qualsiasi mutazione successiva al layout invalida quella prova** e impone un
nuovo layout prima che l'oggetto possa essere scritto: lo stesso schema di
controllo per generazione usato dal grafo di collegamento.

## Assembly

Un provider di parsing consuma byte sorgente e pubblica un `MCUnit`:

```c
NevercAssemblyParseInputInfo In = {0};
In.Header = /* … */;
Asm->GetParseInput(Asm->Context, Frame, Frame->Input, &In);

NevercAssemblyTokenInfo Token = {0};
Asm->PeekSourceToken(Asm->Context, Frame, In.Source.Cursor, &Token);
Asm->AdvanceSourceToken(Asm->Context, Frame, In.Source.Cursor);

const NevercMCAPI *MC;
NevercMCUnitHandle Unit;
Asm->GetParseMCBuilder(Asm->Context, Frame, &MC, &Unit);
/* … build into Unit … */
Asm->PublishParsedMCUnit(Asm->Context, Frame, &Output);
```

Le sorgenti sono o `NEVERC_ASSEMBLY_SOURCE_BUFFER` o
`NEVERC_ASSEMBLY_SOURCE_RENDERED_TOKENS`. L'assembly preprocessato (`.S`)
attraversa prima il normale preprocessore del front-end e arriva come token
renderizzati; l'assembly puro (`.s`) entra direttamente nel parser come
buffer.

Uno stampatore fa il percorso inverso — `GetPrintInput`, poi
`WritePrintOutput` nella transazione di output fornita, poi
`PublishAssemblyOutput`. Scrivere altrove non è supportato: la verifica di
parsing/stampa e il gate di commit dell'host girano prima che i byte diventino
visibili, così una stampa fallita non lascia dietro di sé alcun file parziale.

## Grafi oggetto

`NevercObjectAPI` normalizza un file rilocabile in sezioni, simboli,
rilocazioni e COMDAT. Gli adattatori integrati coprono ELF, COFF e Mach-O;
`RegisterFormat` ne aggiunge un altro.

```c
NevercObjectGraphInfo Info = {0};
Info.Header = /* … */;
Object->GetGraphInfo(Object->Context, Task, Graph, &Info);
/* Info.Target, .ObjectSchemaDigest, .Generation, .SectionCount,
   .SymbolCount, .RelocationCount, .ComdatCount, .HasLayoutProof */

NevercObjectSymbolHandle Symbol;
Object->GetFirstSymbol(Object->Context, Task, Graph, &Symbol);
while (!neverc_handle_is_null(Symbol)) {
  NevercObjectSymbolInfo SymInfo = {0};
  SymInfo.Header = /* … */;
  Object->GetSymbolInfo(Object->Context, Task, Symbol, &SymInfo);
  Object->GetNextSymbol(Object->Context, Task, Symbol, &Symbol);
}
```

La mutazione segue il pattern crea/sostituisci/sposta/cancella per tutti e
quattro i generi di entità, preparata dentro `BeginMutation` …
`CommitMutation` / `AbandonMutation`.

I flag di sezione sono `ALLOCATED`, `EXECUTABLE`, `WRITABLE`, `MERGEABLE`,
`STRINGS`, `TLS`, `DEBUG`, `UNWIND`, `DISCARDABLE` e `RETAIN`. I bersagli
delle rilocazioni sono `SYMBOL`, `SECTION`, `ABSOLUTE` o `FORMAT_EXTENSION`.

Ogni descrittore ha una terna `ExtensionOwner` / `ExtensionVersion` /
`Extension`. È così che un formato conserva dati per i quali il grafo
normalizzato non ha alcun campo: i byte viaggiano con l'entità e ritornano in
scrittura, invece di essere persi nell'andata e ritorno.

### Registrare un formato

```c
NevercObjectFormatDescriptor Format = {0};
Format.Header           = /* … */;
Format.FormatID         = MyFormatID;
Format.CanonicalName    = SV("com.example.myfmt");
Format.SupportedTargets = MyTargets;
Format.DefaultExtension = SV(".mof");
Format.Flags            = NEVERC_OBJECT_FORMAT_CAN_PROBE |
                          NEVERC_OBJECT_FORMAT_CAN_READ  |
                          NEVERC_OBJECT_FORMAT_CAN_WRITE;
Format.Probe            = probe;
Format.Reader           = read;
Format.Writer           = write;
ObjectFormat->RegisterFormat(ObjectFormat->Context, RegistrarContext,
                             &Format);
```

`Probe` riporta una `Confidence` da 0 a
`NEVERC_OBJECT_PROBE_MAX_CONFIDENCE` (1000), il `NevercObjectArtifactKind`
riconosciuto (`RELOCATABLE`, `ARCHIVE`, `EXECUTABLE_IMAGE`, `SHARED_IMAGE`,
`UNIVERSAL_BINARY`) e un `ConsumedMinimum` — quanti byte gli sono serviti per
esserne certo, limitato a `NEVERC_OBJECT_PROBE_MAX_CONSUMED_MINIMUM` (65536).
Vince la confidenza più alta.

A `Reader` vengono consegnati un grafo e una mutazione aperta, che riempie. A
`Writer` vengono consegnati il grafo, la sua prova di layout e il costruttore
binario limitato.

### La pipeline di scrittura

1. sondare e leggere i byte in un ObjectGraph;
2. eseguire gli intercettori di grafo `object.pre_write`;
3. fare il layout, poi eseguire `object.post_layout` (rifare il layout dopo
   ogni mutazione);
4. scrivere un'immagine candidata limitata;
5. eseguire gli intercettori binari `object.post_write`;
6. eseguire il sigillato `object.final_verify` e l'atomico `object.commit`.

Lo stato dell'immagine passa per `CANDIDATE` → `VERIFIED` → `COMMITTED`,
oppure `ABORTED` / `FAILED_PARTIAL`.

Gli osservatori ricevono ponti in sola lettura; una mutazione tentata da un
osservatore viene respinta con `NEVERC_STATUS_POLICY_VIOLATION`. Gli scrittori
e gli intercettori post-scrittura ottengono solo il costruttore limitato
`NevercMutableBinaryAPI` — `Reserve`, `Write`, `WriteAt`, `Tell`, `ReadAt`,
`Insert`, `Append`, `Resize`. Un overflow, una callback fallita o una verifica
fallita interrompono la preparazione, così un fallimento non lascia mai mezzo
file sul disco.

[`pluginsdk/examples/ObjectRewritePlugin.c`] è una riscrittura transazionale
completa.

## Regole

- Confrontate il digest dello schema prima di consumare qualsiasi valore
  LOCKSTEP di opcode, registro, operando, fixup, rilocazione o convenzione di
  chiamata.
- Tenete lo stato mutabile nello stato process, session e task fornito
  dall'host.
- Non mettete in cache handle di task o viste prese in prestito dopo il
  ritorno di una callback.
- Invocate la continuazione di un intercettore al massimo una volta, sul
  thread della callback.
- Ogni `BeginMutation` arriva a esattamente un commit o un abbandono.
- Rifate il layout dopo aver mutato un MCUnit o un ObjectGraph già disposto:
  la vecchia prova di layout è scaduta e l'host la rifiuterà.
- Controllate `NevercMCEmissionEventInfo.Flags` prima di leggere un campo
  dell'evento, e sostituite un'istruzione solo quando
  `CAN_REPLACE_INSTRUCTION` è impostato.
- Scrivete l'output solo attraverso la transazione o il sink di byte forniti.
- In caso di fallimento restituite il `NevercStatus` originale e non
  pubblicate nulla di parziale.
- Dichiarate i modelli di concorrenza e rientranza più stretti che siano
  veritieri.
- `codegen.product_verify`, `assembly.final_verify`, `assembly.commit`,
  `object.final_verify` e `object.commit` sono sigillati. Solo osservazione.

Per le dichiarazioni normative si vedano [`PluginTarget.h`], [`PluginMC.h`],
[`PluginObject.h`] e [`Schema/PhaseSchema.json`]; i generi di entità, operando,
fixup e sezione che usano provengono da [`Schema/MCSchema.json`] e
[`Schema/ObjectSchema.json`], che generano [`Schema/PluginMCSchema.inc`] e
[`Schema/PluginObjectSchema.inc`]. Si veda anche [`coverage.json`], che mappa
ciascuna di queste fasi stabili sui suoi test positivi, negativi, di
sostituzione, di osservatore in sola lettura e di gate sigillato.

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginMC.h`]: ../../neverc/include/neverc/Plugin/PluginMC.h
[`PluginObject.h`]: ../../neverc/include/neverc/Plugin/PluginObject.h
[`pluginsdk/examples/CustomCallConvPlugin.c`]: ../../pluginsdk/examples/CustomCallConvPlugin.c
[`pluginsdk/examples/MCObserverPlugin.c`]: ../../pluginsdk/examples/MCObserverPlugin.c
[`pluginsdk/examples/ObjectRewritePlugin.c`]: ../../pluginsdk/examples/ObjectRewritePlugin.c
[`PluginTarget.h`]: ../../neverc/include/neverc/Plugin/PluginTarget.h
[`Schema/MCSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/MCSchema.json
[`Schema/ObjectSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/ObjectSchema.json
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginMCSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginMCSchema.inc
[`Schema/PluginObjectSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginObjectSchema.inc
