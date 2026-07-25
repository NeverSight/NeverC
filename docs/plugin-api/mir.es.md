**Idiomas**: [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

[← ABI de complementos de NeverC](README.es.md)

# API MIR de los complementos NeverC

`PluginMIR.h` expone la Machine IR: funciones máquina, bloques,
instrucciones, operandos, registros virtuales y físicos, el marco de pila, el
depósito de constantes, las tablas de saltos y los operandos de memoria. Un
complemento engancha pases en nueve puntos estables de la generación de
código, o sustituye por completo el rebajado de IR a MIR.

Aquí se encuentran dos esquemas. El **esquema genérico** es independiente del
destino y siempre está disponible. Todo lo específico del destino —un opcode
real, un número de registro, una clase de registros— exige un **esquema de
destino** negociado, y cada valor que lo necesita lo indica mediante una
bandera `RequiresTargetSchema`.

## Interfaces

```c
#include "neverc/Plugin/PluginMIR.h"
```

| Interfaz | Tabla | Ranuras | Propósito |
|---|---|--:|---|
| `NEVERC_INTERFACE_MIR_{HIGH,LOW}` | `NevercMIRAPI` | 89 | Leer y modificar funciones máquina |
| `NEVERC_INTERFACE_MIR_ANALYSIS_{HIGH,LOW}` | `NevercMIRAnalysisAPI` | 11 | Viveza, dominadores, bucles, presión |
| `NEVERC_INTERFACE_MIR_PASS_{HIGH,LOW}` | `NevercMIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_MIR_PROVIDER_{HIGH,LOW}` | `NevercMIRProviderAPI` | 3 | Sustituir el rebajado IR → MIR |

Las cuatro son `NEVERC_INTERFACE_STABLE` en la mayor 1. Compruebe el
`TableSize` devuelto contra el desplazamiento de la última ranura que use e
ignore cualquier cosa que un anfitrión más nuevo haya añadido más allá.

## Fases

Diez fases MIR, nueve de ellas puntos de enganche para pases:

| Fase | Cuándo |
|---|---|
| `neverc.mir.pass.post_isel` | Tras la selección de instrucciones |
| `neverc.mir.pass.post_legalize` | Tras la legalización |
| `neverc.mir.pass.pre_scheduler` | Antes de la planificación |
| `neverc.mir.pass.post_scheduler` | Tras la planificación |
| `neverc.mir.pass.pre_regalloc` | Antes de la asignación de registros |
| `neverc.mir.pass.post_regalloc` | Tras la asignación de registros |
| `neverc.mir.pass.post_prolog_epilog` | Tras insertar prólogo/epílogo |
| `neverc.mir.pass.preemit` | Justo antes de la emisión |
| `neverc.mir.pass.final` | La última ranura para complementos |
| `neverc.mir.final_verify` | `MachineVerifier` **sellado** del anfitrión |

Los nueve puntos de enganche son `OBSERVABLE | INTERCEPTABLE`. Qué análisis
existen depende de dónde se enganche: los intervalos de vida no están
disponibles antes de la asignación de registros, y los registros virtuales han
desaparecido después.

`neverc.mir.final_verify` ejecuta el `MachineVerifier` de LLVM tras la última
ranura de complemento. Ningún complemento puede desactivarlo, sustituirlo ni
omitirlo.

## El esquema

`Schema/PluginMIRSchema.inc` se genera y lo incluye `PluginMIR.h`:

```c
#define NEVERC_MIR_SCHEMA_DIGEST          "6b523b20…"
#define NEVERC_MIR_ENTITY_COUNT           UINT32_C(4)
#define NEVERC_MIR_OPERAND_COUNT          UINT32_C(21)
#define NEVERC_MIR_GENERIC_OPCODE_COUNT   UINT32_C(266)
#define NEVERC_MIR_PROPERTY_COUNT         UINT32_C(11)
```

Cuatro llamadas describen el esquema en tiempo de ejecución, y cada una
devuelve un `NevercMIRSchemaEntry` con el nombre canónico, el valor LLVM
subyacente y si hace falta un esquema de destino:

```c
NevercMIRSchemaEntry Entry = {0};
Entry.Header = /* … */;
MIR->GetGenericOpcodeInfo(MIR->Context, Opcode, &Entry);
/* Entry.StableID, .LLVMValue, .RequiresTargetSchema, .CanonicalName */
```

Las demás son `GetEntityInfo`, `GetOperandKindInfo` y
`GetMachinePropertyInfo`. `GetSchemaDigest` devuelve el resumen de la
correspondencia realmente en uso; compárelo con `NEVERC_MIR_SCHEMA_DIGEST`
antes de fiarse de cualquier valor específico del destino.

## Leer la MIR

El recorrido es por lista doblemente enlazada, no por cursor:

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

`CollectBasicBlocks` y `CollectInstructions` rellenan en cambio un arreglo
acotado, y `GetLastBasicBlock` / `GetPreviousInstruction` recorren hacia
atrás. Las consultas del grafo de flujo son `GetSuccessorCount` /
`GetSuccessor` (que produce una `NevercMIRCFGEdge` que lleva la probabilidad
de bifurcación como par numerador/denominador), `GetPredecessorCount` /
`GetPredecessor`, y `GetLiveInCount` / `GetLiveIn`.

Las banderas de instrucción son los 18 bits que van desde `FRAME_SETUP` y
`FRAME_DESTROY`, pasando por el grupo fast-math, hasta `NO_MERGE`,
`UNPREDICTABLE` y `NO_CONVERGENT`.

## Operandos

Los 21 tipos de operando vuelven por una única unión etiquetada:

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

Los tipos son `REGISTER`, `IMMEDIATE`, `C_IMMEDIATE`, `FP_IMMEDIATE`,
`MACHINE_BASIC_BLOCK`, `FRAME_INDEX`, `CONSTANT_POOL_INDEX`, `TARGET_INDEX`,
`JUMP_TABLE_INDEX`, `EXTERNAL_SYMBOL`, `GLOBAL_ADDRESS`, `BLOCK_ADDRESS`,
`REGISTER_MASK`, `REGISTER_LIVE_OUT`, `METADATA`, `MC_SYMBOL`, `CFI_INDEX`,
`INTRINSIC_ID`, `PREDICATE`, `SHUFFLE_MASK` y `DBG_INSTR_REF`.

Las banderas de operando de registro son `DEF`, `IMPLICIT`, `KILL`, `DEAD`,
`UNDEF`, `EARLY_CLOBBER`, `RENAMABLE`, `INTERNAL_READ` y `DEBUG`. Los
inmediatos de coma flotante llegan como `NevercMIRWordView` —palabras en
little-endian más una anchura en bits y una de las siete semánticas de coma
flotante, de `IEEE_HALF` a `PPC_DOUBLE_DOUBLE`—, de modo que no interviene
ningún tipo flotante del anfitrión.

## Registros

Un registro virtual se describe con un tipo de bajo nivel más una asignación:

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

Los tipos de asignación son `NONE`, `GENERIC`, `CLASS` y `BANK`; los tipos de
bajo nivel son `INVALID`, `SCALAR`, `POINTER`, `VECTOR` y `POINTER_VECTOR`,
con `IsScalable` para vectores escalables.

Las consultas def-uso son `GetRegisterDefCount` / `GetRegisterDef` y
`GetRegisterUseCount` / `GetRegisterUse`; `ReplaceRegister` reescribe cada
aparición en una sola operación preparada. Las entradas vivas a nivel de
función emparejan un registro físico con el registro virtual al que se copió
(`GetFunctionLiveIn`, `AddFunctionLiveIn`, `RemoveFunctionLiveIn`), mientras
que las de nivel de bloque llevan una máscara de carriles
(`AddBasicBlockLiveIn`, `RemoveBasicBlockLiveIn`).

## El marco de pila

```c
int32_t FrameIndex = 0;
MIR->CreateStackObject(MIR->Context, Task, Mutation, /*Size=*/16,
                       /*Alignment=*/8, /*IsSpillSlot=*/NEVERC_FALSE,
                       /*StackID=*/0, &FrameIndex);
```

`CreateFixedStackObject` coloca un objeto en un desplazamiento conocido (con
`IsImmutable` e `IsAliased`), y `CreateVariableSizedStackObject` se ocupa de
la reserva dinámica. `SetFrameObjectSize`, `SetFrameObjectAlignment` y
`SetFrameObjectOffset` ajustan uno después.

`NevercMIRFrameObjectInfo` informa de `Index`, `Flags`, `Size`, `Offset`,
`Alignment` y `StackID`; las banderas de marco son `FIXED`, `SPILL_SLOT`,
`VARIABLE_SIZED`, `IMMUTABLE`, `ALIASED`, `DEAD` y `PREALLOCATED`. El estado
de los registros guardados por el llamado se lee con `GetCalleeSaved` y se
reemplaza en bloque con `SetCalleeSaved`.

## Depósito de constantes, tablas de saltos, operandos de memoria

Las entradas del depósito de constantes llevan su valor como
`NevercMIRWordView`, así que una entrada entera y una de coma flotante tienen
la misma forma:

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

Las tablas de saltos se crean a partir de un arreglo de bloques de destino con
uno de siete tipos de entrada (`BLOCK_ADDRESS`, `GP_REL64_BLOCK_ADDRESS`,
`GP_REL32_BLOCK_ADDRESS`, `LABEL_DIFFERENCE32`, `LABEL_DIFFERENCE64`,
`INLINE`, `CUSTOM32`).

Los operandos de memoria son el descriptor más rico: banderas (`LOAD`,
`STORE`, `VOLATILE`, `NON_TEMPORAL`, `DEREFERENCEABLE`, `INVARIANT`, más tres
banderas de destino), tamaño y alineación, un puntero de uno de nueve tipos
(`IR_VALUE`, `FIXED_STACK`, `STACK`, `CONSTANT_POOL`, `JUMP_TABLE`, `GOT`,
`UNKNOWN_STACK`, `TARGET_CUSTOM`, `UNKNOWN`), los ordenamientos atómicos de
éxito y de fallo, un ámbito de sincronización y referencias TBAA,
alias-scope, no-alias y range. Se adjunta uno con
`AddInstructionMemoryOperand`.

## Mutación transaccional

Todo cambio se prepara dentro de una mutación ligada a una única función
máquina:

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

La confirmación ejecuta una comprobación estructural previa y luego el
verificador de Machine IR. Operandos inválidos, un grafo de flujo roto,
opcodes genéricos usados donde el esquema de destino exige uno real, o una
afirmación de propiedad no admitida, se revierten todos de forma atómica. El
aborto restaura el orden de los bloques, las instrucciones, los operandos, las
aristas del grafo de flujo y las propiedades máquina exactamente a como
estaban.

`EndMutation` libera el manejador y es independiente de la confirmación y del
aborto: llámelo en ambos caminos.

Las operaciones preparadas son `CreateBasicBlock`, `MoveBasicBlock`,
`EraseBasicBlock`, `CreateInstruction`, `MoveInstruction`,
`EraseInstruction`, `AppendOperand`, `SetOperandValue`,
`SetInstructionFlags`, `AddCFGEdge`, `RemoveCFGEdge`, las llamadas de
registros y marco de más arriba, las del depósito de constantes y las tablas
de saltos, las de operandos de memoria, y `SetMachinePropertyWithProof`.

## Las propiedades máquina necesitan una prueba

Las once propiedades máquina —`IS_SSA`, `NO_PH_IS`, `TRACKS_LIVENESS`,
`NO_V_REGS`, `FAILED_I_SEL`, `LEGALIZED`, `REG_BANK_SELECTED`, `SELECTED`,
`TIED_OPS_REWRITTEN`, `FAILS_VERIFICATION` y `TRACKS_DEBUG_USER_VALUES`— se
leen libremente pero nunca se establecen libremente:

```c
NevercMIRPropertyProof Proof = {0};
Proof.Header   = /* … */;
Proof.Property = NEVERC_MIR_PROPERTY_IS_SSA;
Proof.Kind     = NEVERC_MIR_PROPERTY_PROOF_INVALIDATION;
Proof.Value    = NEVERC_FALSE;
MIR->SetMachinePropertyWithProof(MIR->Context, Task, Mutation, &Proof);
```

Una prueba es de uno de dos tipos. `INVALIDATION` borra una propiedad cuyas
suposiciones rompió su cambio; eso siempre se acepta, porque renunciar a una
garantía es seguro. `STRUCTURAL_CHECK` pide al anfitrión que verifique la
propiedad antes de establecerla, así que afirmar `IS_SSA` cuesta una
comprobación real y no una promesa.

## Pases

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

Eso es `pluginsdk/examples/MachinePass.c` literalmente. Los niveles son
`MODULE`, `FUNCTION` y `BASIC_BLOCK`. `RequiredAnalyses` y
`PreservedAnalyses` son arreglos de `NevercMIRBuiltinAnalysis`, y
`RequiredTargetSchemaDigest` hace que el pase se niegue a ejecutarse frente a
un esquema para el que no fue construido.

La invocación lleva `Task`, `Phase`, `PassID`, `Level`, la `Function` y el
`BasicBlock` válidos para ese nivel, las tablas `Core` y `Analyses`, y el
`TargetSchemaDigest` activo.

Informe de la preservación mediante `OutPreserved`:
`NEVERC_MIR_PRESERVE_NONE`, `_CFG` o `_ALL`, más una lista explícita en
`Analyses`. Afirmar `PRESERVE_ALL` tras una mutación confirmada se rechaza.

Los pases de función pueden ejecutarse en particiones paralelas de generación
de código; los pases de nivel de módulo se ejecutan en barreras serializadas
de la tubería. Los modelos de concurrencia y reentrada declarados por el
complemento siguen gobernando su propio estado.

## Análisis

Seis integrados: `LIVE_INTERVALS`, `LIVE_VARIABLES`, `SLOT_INDEXES`,
`DOMINATOR_TREE`, `LOOP_INFO` y `REGISTER_PRESSURE`.

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

También disponibles: `DominatorTreeDominates`, `GetLoopCount` /
`GetLoopHeader` / `GetLoopForBlock`, `GetSlotIndex`,
`IsRegisterLiveInBlock`, y `GetRegisterPressureSetCount` /
`GetRegisterPressure`.

La disponibilidad depende del punto de enganche. Pedir intervalos de vida en
`post_isel` falla con `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` porque el
análisis subyacente de LLVM todavía no existe. Una mutación confirmada
invalida los manejadores de resultado a los que afecta.

## Sustituir el rebajado de IR a MIR

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

El descriptor de cobertura es lo que mantiene honesto a un proveedor parcial:
declare solo lo que de verdad rebajó, y el anfitrión se encargará del resto en
lugar de descartar en silencio globales, constructores, información de
depuración o tablas de desenrollado.

## Ejemplo

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

Use el sufijo de módulo que CMake haya producido para su plataforma.

## Reglas

- No conserve manejadores de tarea, manejadores MIR ni vistas prestadas
  después de que una retrollamada retorne, y nunca fabrique un valor de
  manejador ni un número de opcode de LLVM.
- Compare `GetSchemaDigest` con su resumen compilado antes de consumir
  cualquier valor cuya bandera `RequiresTargetSchema` esté puesta.
- Mute solo dentro de una mutación. Cada `BeginMutation` llega exactamente a
  un `EndMutation`, tras una confirmación o un aborto.
- No afirme una propiedad máquina sin una prueba, y prefiera `INVALIDATION` a
  `STRUCTURAL_CHECK` cuando su cambio haya renunciado a una.
- Nunca afirme `NEVERC_MIR_PRESERVE_ALL` tras una mutación confirmada.
- Compruebe que el análisis que necesita esté realmente disponible en el
  punto de enganche que eligió.
- Inicialice cada cabecera de tabla y cada campo reservado; devuelva estados a
  través de la frontera de C y nunca deje que una excepción de C++ la cruce.
- `neverc.mir.final_verify` está sellada. Se ejecuta pase lo que pase.

Vea `PluginMIR.h`, `Schema/PluginMIRSchema.inc`, `Schema/PhaseSchema.json` y
`coverage.json` para las declaraciones normativas, las constantes de esquema,
las políticas de fase y la evidencia de cobertura.
