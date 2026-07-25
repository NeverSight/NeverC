**Sprachen**: [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

[← NeverC-Plugin-ABI](README.de.md)

# NeverC-Plugin-MIR-API

[`PluginMIR.h`] legt die Machine IR offen: Maschinenfunktionen, Blöcke,
Instruktionen, Operanden, virtuelle und physische Register, den Stackframe,
den Konstantenpool, Sprungtabellen und Speicheroperanden. Ein Plugin hängt
Passes an neun stabile Hooks der Codeerzeugung an oder ersetzt das Lowering
von IR nach MIR vollständig.

Hier treffen zwei Schemata aufeinander. Das **generische Schema** ist
target-unabhängig und immer verfügbar. Alles Target-spezifische — ein echter
Opcode, eine Registernummer, eine Registerklasse — verlangt ein ausgehandeltes
**Target-Schema**, und jeder Wert, der eines braucht, sagt das über ein
`RequiresTargetSchema`-Flag.

## Schnittstellen

```c
#include "neverc/Plugin/PluginMIR.h"
```

| Schnittstelle | Tabelle | Slots | Zweck |
|---|---|--:|---|
| `NEVERC_INTERFACE_MIR_{HIGH,LOW}` | `NevercMIRAPI` | 89 | Maschinenfunktionen lesen und ändern |
| `NEVERC_INTERFACE_MIR_ANALYSIS_{HIGH,LOW}` | `NevercMIRAnalysisAPI` | 11 | Lebendigkeit, Dominatoren, Schleifen, Druck |
| `NEVERC_INTERFACE_MIR_PASS_{HIGH,LOW}` | `NevercMIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_MIR_PROVIDER_{HIGH,LOW}` | `NevercMIRProviderAPI` | 3 | Das Lowering IR → MIR ersetzen |

Alle vier sind `NEVERC_INTERFACE_STABLE` bei Major 1. Prüfen Sie die
zurückgegebene `TableSize` gegen den Offset des letzten Slots, den Sie nutzen,
und ignorieren Sie alles, was ein neuerer Host dahinter angehängt hat.

## Phasen

Zehn MIR-Phasen, neun davon Pass-Hooks:

| Phase | Wann |
|---|---|
| `neverc.mir.pass.post_isel` | Nach der Instruktionsauswahl |
| `neverc.mir.pass.post_legalize` | Nach der Legalisierung |
| `neverc.mir.pass.pre_scheduler` | Vor dem Scheduling |
| `neverc.mir.pass.post_scheduler` | Nach dem Scheduling |
| `neverc.mir.pass.pre_regalloc` | Vor der Registerzuteilung |
| `neverc.mir.pass.post_regalloc` | Nach der Registerzuteilung |
| `neverc.mir.pass.post_prolog_epilog` | Nach dem Einfügen von Prolog/Epilog |
| `neverc.mir.pass.preemit` | Kurz vor der Emission |
| `neverc.mir.pass.final` | Der letzte Plugin-Slot |
| `neverc.mir.final_verify` | **Versiegelter** `MachineVerifier` des Hosts |

Alle neun Hooks sind `OBSERVABLE | INTERCEPTABLE`. Welche Analysen existieren,
hängt davon ab, wo Sie sich anhängen: Live-Intervalle gibt es vor der
Registerzuteilung nicht, und virtuelle Register sind danach verschwunden.

`neverc.mir.final_verify` führt LLVMs `MachineVerifier` nach dem letzten
Plugin-Slot aus. Kein Plugin kann ihn abschalten, ersetzen oder überspringen.

## Das Schema

[`Schema/PluginMIRSchema.inc`] wird generiert und von [`PluginMIR.h`]
eingebunden:

```c
#define NEVERC_MIR_SCHEMA_DIGEST          "6b523b20…"
#define NEVERC_MIR_ENTITY_COUNT           UINT32_C(4)
#define NEVERC_MIR_OPERAND_COUNT          UINT32_C(21)
#define NEVERC_MIR_GENERIC_OPCODE_COUNT   UINT32_C(266)
#define NEVERC_MIR_PROPERTY_COUNT         UINT32_C(11)
```

Vier Aufrufe beschreiben das Schema zur Laufzeit; jeder liefert einen
`NevercMIRSchemaEntry` mit dem kanonischen Namen, dem zugrunde liegenden
LLVM-Wert und der Angabe, ob ein Target-Schema nötig ist:

```c
NevercMIRSchemaEntry Entry = {0};
Entry.Header = /* … */;
MIR->GetGenericOpcodeInfo(MIR->Context, Opcode, &Entry);
/* Entry.StableID, .LLVMValue, .RequiresTargetSchema, .CanonicalName */
```

Die übrigen sind `GetEntityInfo`, `GetOperandKindInfo` und
`GetMachinePropertyInfo`. `GetSchemaDigest` liefert den Digest der tatsächlich
verwendeten Zuordnung — vergleichen Sie ihn mit
`NEVERC_MIR_SCHEMA_DIGEST`, bevor Sie einem target-spezifischen Wert trauen.

## MIR lesen

Traversiert wird über eine doppelt verkettete Liste statt über einen Cursor:

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

`CollectBasicBlocks` und `CollectInstructions` füllen stattdessen ein
begrenztes Array, und `GetLastBasicBlock` / `GetPreviousInstruction` laufen
rückwärts. CFG-Abfragen sind `GetSuccessorCount` / `GetSuccessor` (das eine
`NevercMIRCFGEdge` liefert, die die Sprungwahrscheinlichkeit als Paar aus
Zähler und Nenner trägt), `GetPredecessorCount` / `GetPredecessor` sowie
`GetLiveInCount` / `GetLiveIn`.

Die Instruktions-Flags sind die 18 Bits von `FRAME_SETUP` und
`FRAME_DESTROY` über die Fast-Math-Gruppe bis hin zu `NO_MERGE`,
`UNPREDICTABLE` und `NO_CONVERGENT`.

## Operanden

Alle 21 Operandenarten kommen über eine einzige getaggte Union zurück:

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

Die Arten sind `REGISTER`, `IMMEDIATE`, `C_IMMEDIATE`, `FP_IMMEDIATE`,
`MACHINE_BASIC_BLOCK`, `FRAME_INDEX`, `CONSTANT_POOL_INDEX`, `TARGET_INDEX`,
`JUMP_TABLE_INDEX`, `EXTERNAL_SYMBOL`, `GLOBAL_ADDRESS`, `BLOCK_ADDRESS`,
`REGISTER_MASK`, `REGISTER_LIVE_OUT`, `METADATA`, `MC_SYMBOL`, `CFI_INDEX`,
`INTRINSIC_ID`, `PREDICATE`, `SHUFFLE_MASK` und `DBG_INSTR_REF`.

Register-Operanden-Flags sind `DEF`, `IMPLICIT`, `KILL`, `DEAD`, `UNDEF`,
`EARLY_CLOBBER`, `RENAMABLE`, `INTERNAL_READ` und `DEBUG`.
Fließkomma-Immediates kommen als `NevercMIRWordView` an — Little-Endian-Wörter
plus eine Bitbreite und eine von sieben Fließkomma-Semantiken von `IEEE_HALF`
bis `PPC_DOUBLE_DOUBLE` —, sodass kein Fließkommatyp des Hosts beteiligt ist.

## Register

Ein virtuelles Register wird durch einen Low-Level-Typ plus eine Zuordnung
beschrieben:

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

Zuordnungsarten sind `NONE`, `GENERIC`, `CLASS` und `BANK`;
Low-Level-Typarten sind `INVALID`, `SCALAR`, `POINTER`, `VECTOR` und
`POINTER_VECTOR`, mit `IsScalable` für skalierbare Vektoren.

Def-Use-Abfragen sind `GetRegisterDefCount` / `GetRegisterDef` und
`GetRegisterUseCount` / `GetRegisterUse`; `ReplaceRegister` schreibt jedes
Vorkommen in einer einzigen vorgemerkten Operation um. Live-Ins auf
Funktionsebene paaren ein physisches Register mit dem virtuellen Register, in
das es kopiert wurde (`GetFunctionLiveIn`, `AddFunctionLiveIn`,
`RemoveFunctionLiveIn`), während Live-Ins auf Blockebene eine Lane-Maske
tragen (`AddBasicBlockLiveIn`, `RemoveBasicBlockLiveIn`).

## Der Stackframe

```c
int32_t FrameIndex = 0;
MIR->CreateStackObject(MIR->Context, Task, Mutation, /*Size=*/16,
                       /*Alignment=*/8, /*IsSpillSlot=*/NEVERC_FALSE,
                       /*StackID=*/0, &FrameIndex);
```

`CreateFixedStackObject` platziert ein Objekt an einem bekannten Offset (mit
`IsImmutable` und `IsAliased`), und `CreateVariableSizedStackObject`
übernimmt dynamische Allokation. `SetFrameObjectSize`,
`SetFrameObjectAlignment` und `SetFrameObjectOffset` passen eines
nachträglich an.

`NevercMIRFrameObjectInfo` meldet `Index`, `Flags`, `Size`, `Offset`,
`Alignment` und `StackID`; Frame-Flags sind `FIXED`, `SPILL_SLOT`,
`VARIABLE_SIZED`, `IMMUTABLE`, `ALIASED`, `DEAD` und `PREALLOCATED`. Der
Callee-Saved-Zustand wird mit `GetCalleeSaved` gelesen und mit
`SetCalleeSaved` komplett ersetzt.

## Konstantenpool, Sprungtabellen, Speicheroperanden

Konstantenpool-Einträge tragen ihren Wert als `NevercMIRWordView`, sodass ein
Ganzzahl- und ein Fließkommaeintrag dieselbe Form haben:

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

Sprungtabellen entstehen aus einem Array von Zielblöcken mit einer von sieben
Eintragsarten (`BLOCK_ADDRESS`, `GP_REL64_BLOCK_ADDRESS`,
`GP_REL32_BLOCK_ADDRESS`, `LABEL_DIFFERENCE32`, `LABEL_DIFFERENCE64`,
`INLINE`, `CUSTOM32`).

Speicheroperanden sind der reichhaltigste Deskriptor: Flags (`LOAD`, `STORE`,
`VOLATILE`, `NON_TEMPORAL`, `DEREFERENCEABLE`, `INVARIANT` sowie drei
Target-Flags), Größe und Ausrichtung, ein Zeiger einer von neun Arten
(`IR_VALUE`, `FIXED_STACK`, `STACK`, `CONSTANT_POOL`, `JUMP_TABLE`, `GOT`,
`UNKNOWN_STACK`, `TARGET_CUSTOM`, `UNKNOWN`), atomare Ordnungen für Erfolg und
Misserfolg, ein Synchronisationsbereich sowie TBAA-, Alias-Scope-, No-Alias-
und Range-Referenzen. Angehängt wird einer mit
`AddInstructionMemoryOperand`.

## Transaktionale Mutation

Jede Änderung wird in einer Mutation vorgemerkt, die an genau eine
Maschinenfunktion gebunden ist:

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

Der Commit führt eine strukturelle Vorprüfung und dann den Machine-IR-Verifier
aus. Ungültige Operanden, ein kaputter CFG, generische Opcodes dort, wo das
Target-Schema einen echten verlangt, oder eine nicht unterstützte
Eigenschaftsbehauptung werden allesamt atomar zurückgerollt. Der Abbruch stellt
Blockreihenfolge, Instruktionen, Operanden, CFG-Kanten und
Maschineneigenschaften genau so wieder her, wie sie waren.

`EndMutation` gibt das Handle frei und ist von Commit und Abbruch getrennt —
rufen Sie es auf beiden Wegen auf.

Die vorgemerkten Operationen sind `CreateBasicBlock`, `MoveBasicBlock`,
`EraseBasicBlock`, `CreateInstruction`, `MoveInstruction`,
`EraseInstruction`, `AppendOperand`, `SetOperandValue`,
`SetInstructionFlags`, `AddCFGEdge`, `RemoveCFGEdge`, die oben genannten
Register- und Frame-Aufrufe, die Aufrufe für Konstantenpool und
Sprungtabellen, die für Speicheroperanden sowie
`SetMachinePropertyWithProof`.

## Maschineneigenschaften brauchen einen Beweis

Die elf Maschineneigenschaften — `IS_SSA`, `NO_PH_IS`, `TRACKS_LIVENESS`,
`NO_V_REGS`, `FAILED_I_SEL`, `LEGALIZED`, `REG_BANK_SELECTED`, `SELECTED`,
`TIED_OPS_REWRITTEN`, `FAILS_VERIFICATION` und `TRACKS_DEBUG_USER_VALUES` —
werden frei gelesen, aber nie frei gesetzt:

```c
NevercMIRPropertyProof Proof = {0};
Proof.Header   = /* … */;
Proof.Property = NEVERC_MIR_PROPERTY_IS_SSA;
Proof.Kind     = NEVERC_MIR_PROPERTY_PROOF_INVALIDATION;
Proof.Value    = NEVERC_FALSE;
MIR->SetMachinePropertyWithProof(MIR->Context, Task, Mutation, &Proof);
```

Ein Beweis ist von einer von zwei Arten. `INVALIDATION` löscht eine
Eigenschaft, deren Annahmen Ihre Änderung gebrochen hat — das wird immer
akzeptiert, denn eine Garantie aufzugeben ist sicher. `STRUCTURAL_CHECK`
bittet den Host, die Eigenschaft zu prüfen, bevor sie etabliert wird; `IS_SSA`
zu behaupten kostet also eine echte Prüfung statt eines Versprechens.

## Passes

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

Das ist [`pluginsdk/examples/MachinePass.c`] wortwörtlich. Die Ebenen sind
`MODULE`, `FUNCTION` und `BASIC_BLOCK`. `RequiredAnalyses` und
`PreservedAnalyses` sind Arrays von `NevercMIRBuiltinAnalysis`, und
`RequiredTargetSchemaDigest` sorgt dafür, dass der Pass die Ausführung gegen
ein Schema verweigert, für das er nicht gebaut wurde.

Der Aufruf trägt `Task`, `Phase`, `PassID`, `Level`, die für diese Ebene
gültige `Function` und den `BasicBlock`, die Tabellen `Core` und `Analyses`
sowie den aktiven `TargetSchemaDigest`.

Melden Sie die Erhaltung über `OutPreserved` — `NEVERC_MIR_PRESERVE_NONE`,
`_CFG` oder `_ALL`, plus eine explizite Liste in `Analyses`. Nach einer
committeten Mutation `PRESERVE_ALL` zu behaupten wird abgelehnt.

Funktions-Passes können in parallelen Codegen-Partitionen laufen;
Passes auf Modulebene laufen an serialisierten Pipeline-Barrieren. Die vom
Plugin deklarierten Modelle für Nebenläufigkeit und Reentranz gelten weiterhin
für Ihren eigenen Zustand.

## Analysen

Sechs eingebaute: `LIVE_INTERVALS`, `LIVE_VARIABLES`, `SLOT_INDEXES`,
`DOMINATOR_TREE`, `LOOP_INFO` und `REGISTER_PRESSURE`.

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

Außerdem verfügbar: `DominatorTreeDominates`, `GetLoopCount` /
`GetLoopHeader` / `GetLoopForBlock`, `GetSlotIndex`,
`IsRegisterLiveInBlock` sowie `GetRegisterPressureSetCount` /
`GetRegisterPressure`.

Die Verfügbarkeit hängt vom Hook ab. Live-Intervalle bei `post_isel`
anzufordern schlägt mit `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` fehl, weil die
zugrunde liegende LLVM-Analyse noch nicht existiert. Eine committete Mutation
entwertet die Ergebnis-Handles, die sie betrifft.

## Das Lowering von IR nach MIR ersetzen

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

Der Coverage-Deskriptor hält einen teilweisen Provider ehrlich: Deklarieren
Sie nur, was Sie tatsächlich gelowert haben, dann erledigt der Host den Rest
selbst, statt Globals, Konstruktoren, Debug-Informationen oder
Unwind-Tabellen stillschweigend fallen zu lassen.

## Beispiel

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

Verwenden Sie das Modul-Suffix, das CMake für Ihre Plattform erzeugt hat.

## Regeln

- Behalten Sie Task-Handles, MIR-Handles oder geliehene Views nicht über die
  Rückkehr eines Callbacks hinaus, und erfinden Sie niemals einen Handle-Wert
  oder eine LLVM-Opcode-Nummer.
- Vergleichen Sie `GetSchemaDigest` mit Ihrem einkompilierten Digest, bevor
  Sie einen Wert verwenden, dessen `RequiresTargetSchema`-Flag gesetzt ist.
- Ändern Sie nur innerhalb einer Mutation. Jedes `BeginMutation` erreicht
  genau ein `EndMutation`, nach einem Commit oder einem Abbruch.
- Behaupten Sie keine Maschineneigenschaft ohne Beweis, und bevorzugen Sie
  `INVALIDATION` gegenüber `STRUCTURAL_CHECK`, wenn Ihre Änderung eine
  aufgegeben hat.
- Behaupten Sie nach einer committeten Mutation niemals
  `NEVERC_MIR_PRESERVE_ALL`.
- Prüfen Sie, ob die benötigte Analyse an dem von Ihnen gewählten Hook
  überhaupt verfügbar ist.
- Initialisieren Sie jeden Tabellen-Header und jedes reservierte Feld; geben
  Sie Status über die C-Grenze zurück und lassen Sie niemals eine
  C++-Ausnahme hinüber.
- `neverc.mir.final_verify` ist versiegelt. Sie läuft in jedem Fall.

Siehe [`PluginMIR.h`], [`Schema/PluginMIRSchema.inc`], [`Schema/PhaseSchema.json`]
und [`coverage.json`] für die normativen Deklarationen, Schema-Konstanten,
Phasen-Policies und Abdeckungsnachweise.

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginMIR.h`]: ../../neverc/include/neverc/Plugin/PluginMIR.h
[`pluginsdk/examples/MachinePass.c`]: ../../pluginsdk/examples/MachinePass.c
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginMIRSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginMIRSchema.inc
