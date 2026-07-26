**Lingue**: [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

[← ABI dei plugin NeverC](README.it.md)

# API MIR dei plugin NeverC

[`PluginMIR.h`] espone la Machine IR: funzioni macchina, blocchi, istruzioni,
operandi, registri virtuali e fisici, lo stack frame, il pool di costanti, le
tabelle di salto e gli operandi di memoria. Un plugin aggancia pass a nove
hook stabili della generazione di codice, oppure sostituisce interamente
l'abbassamento da IR a MIR.

Qui si incontrano due schemi. Lo **schema generico** è indipendente dal target
ed è sempre disponibile. Tutto ciò che è specifico del target — un opcode
reale, un numero di registro, una classe di registri — richiede uno **schema di
target** negoziato, e ogni valore che ne ha bisogno lo dichiara tramite un
flag `RequiresTargetSchema`.

## Interfacce

```c
#include "neverc/Plugin/PluginMIR.h"
```

| Interfaccia | Tabella | Slot | Scopo |
|---|---|--:|---|
| `NEVERC_INTERFACE_MIR_{HIGH,LOW}` | `NevercMIRAPI` | 89 | Leggere e modificare funzioni macchina |
| `NEVERC_INTERFACE_MIR_ANALYSIS_{HIGH,LOW}` | `NevercMIRAnalysisAPI` | 11 | Vivacità, dominatori, cicli, pressione |
| `NEVERC_INTERFACE_MIR_PASS_{HIGH,LOW}` | `NevercMIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_MIR_PROVIDER_{HIGH,LOW}` | `NevercMIRProviderAPI` | 3 | Sostituire l'abbassamento IR → MIR |

Tutte e quattro sono `NEVERC_INTERFACE_STABLE` alla major 1. Confrontate il
`TableSize` restituito con l'offset dell'ultimo slot che usate e ignorate
qualsiasi cosa un host più recente abbia accodato oltre.

## Fasi

Dieci fasi MIR, nove delle quali hook per pass:

| Fase | Quando |
|---|---|
| `neverc.mir.pass.post_isel` | Dopo la selezione delle istruzioni |
| `neverc.mir.pass.post_legalize` | Dopo la legalizzazione |
| `neverc.mir.pass.pre_scheduler` | Prima dello scheduling |
| `neverc.mir.pass.post_scheduler` | Dopo lo scheduling |
| `neverc.mir.pass.pre_regalloc` | Prima dell'allocazione dei registri |
| `neverc.mir.pass.post_regalloc` | Dopo l'allocazione dei registri |
| `neverc.mir.pass.post_prolog_epilog` | Dopo l'inserimento di prologo/epilogo |
| `neverc.mir.pass.preemit` | Appena prima dell'emissione |
| `neverc.mir.pass.final` | L'ultimo slot per i plugin |
| `neverc.mir.final_verify` | `MachineVerifier` **sigillato** dell'host |

Tutti e nove gli hook sono `OBSERVABLE | INTERCEPTABLE`. Quali analisi esistano
dipende da dove vi agganciate: gli intervalli di vita non sono disponibili
prima dell'allocazione dei registri, e i registri virtuali non ci sono più
dopo.

`neverc.mir.final_verify` esegue il `MachineVerifier` di LLVM dopo l'ultimo
slot per plugin. Nessun plugin può disabilitarlo, sostituirlo o saltarlo.

## Lo schema

[`Schema/PluginMIRSchema.inc`] è generato e incluso da [`PluginMIR.h`]:

```c
#define NEVERC_MIR_SCHEMA_DIGEST          "6b523b20…"
#define NEVERC_MIR_ENTITY_COUNT           UINT32_C(4)
#define NEVERC_MIR_OPERAND_COUNT          UINT32_C(21)
#define NEVERC_MIR_GENERIC_OPCODE_COUNT   UINT32_C(266)
#define NEVERC_MIR_PROPERTY_COUNT         UINT32_C(11)
```

Quattro chiamate descrivono lo schema a runtime, ciascuna restituendo una
`NevercMIRSchemaEntry` con il nome canonico, il valore LLVM sottostante e
l'indicazione se serva uno schema di target:

```c
NevercMIRSchemaEntry Entry = {0};
Entry.Header = /* … */;
MIR->GetGenericOpcodeInfo(MIR->Context, Opcode, &Entry);
/* Entry.StableID, .LLVMValue, .RequiresTargetSchema, .CanonicalName */
```

Le altre sono `GetEntityInfo`, `GetOperandKindInfo` e
`GetMachinePropertyInfo`. `GetSchemaDigest` restituisce il digest della
corrispondenza effettivamente in uso: confrontatelo con
`NEVERC_MIR_SCHEMA_DIGEST` prima di fidarvi di qualsiasi valore specifico del
target.

## Leggere la MIR

L'attraversamento è a lista doppiamente concatenata anziché a cursore:

```c
NevercMachineBasicBlockHandle Block;
MIR->GetFirstBasicBlock(MIR->Context, Task, Function, &Block);

while (!neverc_handle_is_null(Block)) {
  NevercMachineInstrHandle Instruction;
  MIR->GetFirstInstruction(MIR->Context, Task, Block, &Instruction);

  while (!neverc_handle_is_null(Instruction)) {
    NevercMIRInstructionInfo Info = {0};
    Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_MIR_API_MAJOR,
                                         NEVERC_MIR_API_MINOR, 0};
    MIR->GetInstructionInfo(MIR->Context, Task, Instruction, &Info);
    /* Info.StableOpcode, .TargetOpcode, .RequiresTargetSchema,
       .IsBranch, .IsCall, .IsReturn, .IsTerminator, .IsBarrier,
       .IsInlineAssembly, .IsDebugInstruction, .IsPseudo, .IsBundle,
       .Flags, .OperandCount, .MemoryOperandCount                    */
    MIR->GetNextInstruction(MIR->Context, Task, Instruction, &Instruction);
  }
  MIR->GetNextBasicBlock(MIR->Context, Task, Block, &Block);
}
```

`CollectBasicBlocks` e `CollectInstructions` riempiono invece un array
limitato, mentre `GetLastBasicBlock` / `GetPreviousInstruction` camminano
all'indietro. Le interrogazioni sul CFG sono `GetSuccessorCount` /
`GetSuccessor` (che produce un `NevercMIRCFGEdge` con la probabilità di
diramazione come coppia numeratore/denominatore), `GetPredecessorCount` /
`GetPredecessor`, e `GetLiveInCount` / `GetLiveIn`.

I flag di istruzione sono i 18 bit che vanno da `FRAME_SETUP` e
`FRAME_DESTROY`, attraverso il gruppo fast-math, fino a `NO_MERGE`,
`UNPREDICTABLE` e `NO_CONVERGENT`.

## Operandi

Tutti i 21 generi di operando tornano attraverso un'unica unione etichettata:

```c
NevercMIROperandValue Value = {0};
Value.Header = /* … */;
MIR->GetOperandValue(MIR->Context, Task, Operand, &Value);

switch (Value.Kind) {
case NEVERC_MIR_OPERAND_REGISTER:
  /* Value.Payload.Register.Number, .SubRegister, .Flags, .IsPhysical */
  break;
case NEVERC_MIR_OPERAND_IMMEDIATE:
  /* Value.Payload.Immediate */
  break;
case NEVERC_MIR_OPERAND_MACHINE_BASIC_BLOCK:
  /* Value.Payload.BasicBlock */
  break;
case NEVERC_MIR_OPERAND_GLOBAL_ADDRESS:
  /* Value.Payload.SymbolOffset.Symbol, .Offset */
  break;
}
```

I generi sono `REGISTER`, `IMMEDIATE`, `C_IMMEDIATE`, `FP_IMMEDIATE`,
`MACHINE_BASIC_BLOCK`, `FRAME_INDEX`, `CONSTANT_POOL_INDEX`, `TARGET_INDEX`,
`JUMP_TABLE_INDEX`, `EXTERNAL_SYMBOL`, `GLOBAL_ADDRESS`, `BLOCK_ADDRESS`,
`REGISTER_MASK`, `REGISTER_LIVE_OUT`, `METADATA`, `MC_SYMBOL`, `CFI_INDEX`,
`INTRINSIC_ID`, `PREDICATE`, `SHUFFLE_MASK` e `DBG_INSTR_REF`.

I flag degli operandi registro sono `DEF`, `IMPLICIT`, `KILL`, `DEAD`,
`UNDEF`, `EARLY_CLOBBER`, `RENAMABLE`, `INTERNAL_READ` e `DEBUG`. Gli
immediati in virgola mobile arrivano come `NevercMIRWordView` — parole
little-endian più un'ampiezza in bit e una delle sette semantiche in virgola
mobile, da `IEEE_HALF` a `PPC_DOUBLE_DOUBLE` — così nessun tipo float dell'host
è coinvolto.

## Registri

Un registro virtuale è descritto da un tipo di basso livello più
un'assegnazione:

```c
NevercMIRVirtualRegisterDesc Desc = {0};
Desc.Header             = /* … */;
Desc.AssignmentKind     = NEVERC_MIR_REG_ASSIGNMENT_CLASS;
Desc.TargetID           = RegisterClassID;   /* needs the target schema */
Desc.Type.Kind          = NEVERC_MIR_LLT_SCALAR;
Desc.Type.ScalarSizeInBits = 32;

uint32_t Register = 0;
MIR->CreateVirtualRegister(MIR->Context, Task, Mutation, &Desc, &Register);
```

I generi di assegnazione sono `NONE`, `GENERIC`, `CLASS` e `BANK`; i generi di
tipo di basso livello sono `INVALID`, `SCALAR`, `POINTER`, `VECTOR` e
`POINTER_VECTOR`, con `IsScalable` per i vettori scalabili.

Le interrogazioni def-use sono `GetRegisterDefCount` / `GetRegisterDef` e
`GetRegisterUseCount` / `GetRegisterUse`; `ReplaceRegister` riscrive ogni
occorrenza in un'unica operazione preparata. I live-in a livello di funzione
accoppiano un registro fisico con il registro virtuale in cui è stato copiato
(`GetFunctionLiveIn`, `AddFunctionLiveIn`, `RemoveFunctionLiveIn`), mentre
quelli a livello di blocco portano una maschera di corsie
(`AddBasicBlockLiveIn`, `RemoveBasicBlockLiveIn`).

## Lo stack frame

```c
int32_t FrameIndex = 0;
MIR->CreateStackObject(MIR->Context, Task, Mutation, /*Size=*/16,
                       /*Alignment=*/8, /*IsSpillSlot=*/NEVERC_FALSE,
                       /*StackID=*/0, &FrameIndex);
```

`CreateFixedStackObject` colloca un oggetto a un offset noto (con
`IsImmutable` e `IsAliased`), e `CreateVariableSizedStackObject` gestisce
l'allocazione dinamica. `SetFrameObjectSize`, `SetFrameObjectAlignment` e
`SetFrameObjectOffset` ne regolano uno in seguito.

`NevercMIRFrameObjectInfo` riporta `Index`, `Flags`, `Size`, `Offset`,
`Alignment` e `StackID`; i flag di frame sono `FIXED`, `SPILL_SLOT`,
`VARIABLE_SIZED`, `IMMUTABLE`, `ALIASED`, `DEAD` e `PREALLOCATED`. Lo stato dei
registri salvati dal chiamato si legge con `GetCalleeSaved` e si sostituisce in
blocco con `SetCalleeSaved`.

## Pool di costanti, tabelle di salto, operandi di memoria

Le voci del pool di costanti portano il proprio valore come
`NevercMIRWordView`, così una voce intera e una in virgola mobile hanno la
stessa forma:

```c
NevercMIRConstantPoolEntryDesc Desc = {0};
Desc.Header       = /* … */;
Desc.Kind         = NEVERC_MIR_CONSTANT_INTEGER;
Desc.Alignment    = 8;
Desc.Value.Data   = Words;
Desc.Value.Count  = 1;
Desc.Value.BitWidth = 64;

uint32_t Index = 0;
MIR->CreateConstantPoolEntry(MIR->Context, Task, Mutation, &Desc, &Index);
```

Le tabelle di salto si creano da un array di blocchi di destinazione con uno di
sette generi di voce (`BLOCK_ADDRESS`, `GP_REL64_BLOCK_ADDRESS`,
`GP_REL32_BLOCK_ADDRESS`, `LABEL_DIFFERENCE32`, `LABEL_DIFFERENCE64`,
`INLINE`, `CUSTOM32`).

Gli operandi di memoria sono il descrittore più ricco: flag (`LOAD`, `STORE`,
`VOLATILE`, `NON_TEMPORAL`, `DEREFERENCEABLE`, `INVARIANT`, più tre flag di
target), dimensione e allineamento, un puntatore di uno dei nove generi
(`IR_VALUE`, `FIXED_STACK`, `STACK`, `CONSTANT_POOL`, `JUMP_TABLE`, `GOT`,
`UNKNOWN_STACK`, `TARGET_CUSTOM`, `UNKNOWN`), gli ordinamenti atomici di
successo e fallimento, un ambito di sincronizzazione e riferimenti TBAA,
alias-scope, no-alias e range. Se ne aggancia uno con
`AddInstructionMemoryOperand`.

## Mutazione transazionale

Ogni cambiamento viene preparato dentro una mutazione legata a una sola
funzione macchina:

```c
NevercMIRMutationHandle Mutation;
MIR->BeginMutation(MIR->Context, Task, Function, &Mutation);

NevercMIRInstructionOpcode Opcode = {0};
Opcode.StableOpcode = MyGenericOpcode;

NevercMachineInstrHandle New;
MIR->CreateInstruction(MIR->Context, Task, Mutation, Block,
                       /*InsertBefore=*/Terminator, Opcode, &New);

NevercMIROperandValue Op = {0};
Op.Header = /* … */;
Op.Kind   = NEVERC_MIR_OPERAND_IMMEDIATE;
Op.Payload.Immediate = 42;
MIR->AppendOperand(MIR->Context, Task, Mutation, New, &Op, &Operand);

Status = MIR->CommitMutation(MIR->Context, Task, Mutation);
if (Status.Code != NEVERC_STATUS_OK)
  MIR->AbortMutation(MIR->Context, Task, Mutation);
MIR->EndMutation(MIR->Context, Task, Mutation);
```

Il commit esegue un controllo strutturale preliminare e poi il verificatore
della Machine IR. Operandi non validi, un CFG rotto, opcode generici usati dove
lo schema di target ne pretende uno reale, o un'affermazione di proprietà non
supportata, vengono tutti annullati in modo atomico. L'abort ripristina
l'ordine dei blocchi, le istruzioni, gli operandi, gli archi del CFG e le
proprietà macchina esattamente com'erano.

`EndMutation` rilascia l'handle ed è separato da commit e abort: chiamatelo su
entrambi i percorsi.

Le operazioni preparate sono `CreateBasicBlock`, `MoveBasicBlock`,
`EraseBasicBlock`, `CreateInstruction`, `MoveInstruction`,
`EraseInstruction`, `AppendOperand`, `SetOperandValue`,
`SetInstructionFlags`, `AddCFGEdge`, `RemoveCFGEdge`, le chiamate su registri
e frame viste sopra, quelle sul pool di costanti e sulle tabelle di salto,
quelle sugli operandi di memoria, e `SetMachinePropertyWithProof`.

## Le proprietà macchina richiedono una prova

Le undici proprietà macchina — `IS_SSA`, `NO_PH_IS`, `TRACKS_LIVENESS`,
`NO_V_REGS`, `FAILED_I_SEL`, `LEGALIZED`, `REG_BANK_SELECTED`, `SELECTED`,
`TIED_OPS_REWRITTEN`, `FAILS_VERIFICATION` e `TRACKS_DEBUG_USER_VALUES` — si
leggono liberamente ma non si impostano mai liberamente:

```c
NevercMIRPropertyProof Proof = {0};
Proof.Header   = /* … */;
Proof.Property = NEVERC_MIR_PROPERTY_IS_SSA;
Proof.Kind     = NEVERC_MIR_PROPERTY_PROOF_INVALIDATION;
Proof.Value    = NEVERC_FALSE;
MIR->SetMachinePropertyWithProof(MIR->Context, Task, Mutation, &Proof);
```

Una prova è di uno di due generi. `INVALIDATION` cancella una proprietà le cui
ipotesi il vostro cambiamento ha infranto: è sempre accettata, perché
rinunciare a una garanzia è sicuro. `STRUCTURAL_CHECK` chiede all'host di
verificare la proprietà prima di stabilirla, così dichiarare `IS_SSA` costa una
verifica vera e non una promessa.

## Pass

```c
NevercMIRPassDescriptor Pass = {0};
Pass.Header = (NevercABITableHeader){sizeof(Pass), NEVERC_MIR_PASS_API_MAJOR,
                                     NEVERC_MIR_PASS_API_MINOR, 0};
Pass.PassID        = SV("example.machine-pass");
Pass.Phase         = (NevercInterfaceID){NEVERC_PHASE_MIR_PASS_PREEMIT_HIGH,
                                         NEVERC_PHASE_MIR_PASS_PREEMIT_LOW};
Pass.Level         = NEVERC_MIR_PASS_LEVEL_FUNCTION;
Pass.Deterministic = NEVERC_TRUE;
Pass.Run           = run_machine_function;
PassAPI->RegisterPass(PassAPI->Context, RegistrarContext, &Pass);
```

Questo è [`pluginsdk/examples/MachinePass.c`] alla lettera. I livelli sono
`MODULE`, `FUNCTION` e `BASIC_BLOCK`. `RequiredAnalyses` e
`PreservedAnalyses` sono array di `NevercMIRBuiltinAnalysis`, e
`RequiredTargetSchemaDigest` fa sì che il pass si rifiuti di girare contro uno
schema per cui non è stato costruito.

L'invocazione porta `Task`, `Phase`, `PassID`, `Level`, la `Function` e il
`BasicBlock` validi per quel livello, le tabelle `Core` e `Analyses`, e il
`TargetSchemaDigest` attivo.

Segnalate la preservazione tramite `OutPreserved`:
`NEVERC_MIR_PRESERVE_NONE`, `_CFG` o `_ALL`, più un elenco esplicito in
`Analyses`. Dichiarare `PRESERVE_ALL` dopo una mutazione confermata viene
respinto.

I pass di funzione possono girare in partizioni parallele di generazione del
codice; i pass a livello di modulo girano alle barriere serializzate della
pipeline. I modelli di concorrenza e rientranza dichiarati dal plugin
governano comunque il vostro stato.

## Analisi

Sei integrate: `LIVE_INTERVALS`, `LIVE_VARIABLES`, `SLOT_INDEXES`,
`DOMINATOR_TREE`, `LOOP_INFO` e `REGISTER_PRESSURE`.

```c
NevercMIRAnalysisResultHandle Intervals;
Analyses->QueryBuiltin(Analyses->Context, Task,
                       NEVERC_MIR_ANALYSIS_LIVE_INTERVALS, Function,
                       &Intervals);

uint64_t SegmentCount = 0;
Analyses->GetLiveIntervalSegmentCount(Analyses->Context, Task, Intervals,
                                      Register, &SegmentCount);
for (uint64_t I = 0; I != SegmentCount; ++I) {
  NevercMIRLiveRangeSegment Segment;
  Analyses->GetLiveIntervalSegment(Analyses->Context, Task, Intervals,
                                   Register, I, &Segment);
  /* Segment.Start, Segment.End */
}
```

Disponibili inoltre: `DominatorTreeDominates`, `GetLoopCount` /
`GetLoopHeader` / `GetLoopForBlock`, `GetSlotIndex`,
`IsRegisterLiveInBlock`, e `GetRegisterPressureSetCount` /
`GetRegisterPressure`.

La disponibilità dipende dall'hook. Chiedere gli intervalli di vita a
`post_isel` fallisce con `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` perché
l'analisi LLVM sottostante non esiste ancora. Una mutazione confermata invalida
gli handle di risultato che tocca.

## Sostituire l'abbassamento da IR a MIR

```c
NevercIRToMIRInputInfo In = {0};
In.Header = /* … */;
Provider->GetIRToMIRInput(Provider->Context, Frame, Frame->Input, &In);
/* In.Module, .IR, .TargetID, .CompatibilityKey, .TargetSchemaDigest,
   .DefinedFunctionCount */

const NevercMIRAPI *MIR;
NevercMachineFunctionHandle MF;
Provider->GetOrCreateMachineFunction(Provider->Context, Frame, IRFunction,
                                     &MIR, &MF);
/* … build the machine function … */

NevercMIRModuleCoverageDescriptor Coverage = {0};
Coverage.Header              = /* … */;
Coverage.HandlesGlobals      = NEVERC_TRUE;
Coverage.HandlesConstructors = NEVERC_TRUE;
Coverage.HandlesDebugInfo    = NEVERC_FALSE;
Coverage.HandlesUnwind       = NEVERC_FALSE;
Provider->PublishMIRModule(Provider->Context, Frame, &Coverage, &Output);
```

Il descrittore di copertura è ciò che tiene onesto un provider parziale:
dichiarate solo quello che avete davvero abbassato, e l'host penserà da sé al
resto invece di lasciar cadere in silenzio globali, costruttori, informazioni
di debug o tabelle di srotolamento.

## Esempio

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

Usate il suffisso di modulo che CMake ha prodotto per la vostra piattaforma.

## Regole

- Non trattenete handle di task, handle MIR o viste prese in prestito dopo il
  ritorno di una callback, e non fabbricate mai un valore di handle né un
  numero di opcode LLVM.
- Confrontate `GetSchemaDigest` con il digest compilato nel vostro codice
  prima di consumare qualsiasi valore il cui flag `RequiresTargetSchema` sia
  impostato.
- Modificate solo dentro una mutazione. Ogni `BeginMutation` arriva a
  esattamente un `EndMutation`, dopo un commit o un abort.
- Non dichiarate una proprietà macchina senza prova, e preferite
  `INVALIDATION` a `STRUCTURAL_CHECK` quando il vostro cambiamento vi ha
  rinunciato.
- Non dichiarate mai `NEVERC_MIR_PRESERVE_ALL` dopo una mutazione confermata.
- Verificate che l'analisi di cui avete bisogno sia davvero disponibile
  all'hook che avete scelto.
- Inizializzate ogni header di tabella e ogni campo riservato; restituite
  stati attraverso il confine C e non lasciate mai che un'eccezione C++ lo
  attraversi.
- `neverc.mir.final_verify` è sigillata. Gira in ogni caso.

Per le dichiarazioni normative, lo schema stesso, le sue costanti generate, le
policy delle fasi e le prove di copertura si vedano [`PluginMIR.h`],
[`Schema/MIRSchema.json`], [`Schema/PluginMIRSchema.inc`],
[`Schema/PhaseSchema.json`] e [`coverage.json`].

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginMIR.h`]: ../../neverc/include/neverc/Plugin/PluginMIR.h
[`pluginsdk/examples/MachinePass.c`]: ../../pluginsdk/examples/MachinePass.c
[`Schema/MIRSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/MIRSchema.json
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginMIRSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginMIRSchema.inc
