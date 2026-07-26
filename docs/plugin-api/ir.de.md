**Sprachen**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

[← NeverC-Plugin-ABI](README.de.md)

# NeverC Plugin-API für die IR

[`PluginIR.h`] legt die LLVM-IR über sechs Fähigkeitstabellen und ein generiertes
Schema offen. Ein Plugin liest und schreibt IR, registriert Durchläufe an fünf
stabilen Punkten der Pipeline, definiert eigene Analysen oder ersetzt die
IR-Erzeugung und die Optimierungspipeline vollständig — ohne einen einzigen
LLVM-Header einzubinden.

Opcodes, Typarten und Instruktionseigenschaften sind **stabile Schema-IDs**,
keine LLVM-Enumwerte. Genau diese Indirektion sorgt dafür, dass ein heute
übersetztes Plugin weiterläuft, wenn der Host auf eine neue LLVM-Version wechselt.

## Schnittstellen

```c
#include "neverc/Plugin/PluginIR.h"
```

| Schnittstelle | Tabelle | Plätze | Zweck |
|---|---|--:|---|
| `NEVERC_INTERFACE_IR_CORE_{HIGH,LOW}` | `NevercIRCoreAPI` | 99 | Module, Werte, Typen, Konstanten, Metadaten, Attribute lesen und ändern |
| `NEVERC_INTERFACE_IR_BUILDER_{HIGH,LOW}` | `NevercIRBuilderAPI` | 29 | Transaktionale Konstruktion |
| `NEVERC_INTERFACE_IR_ANALYSIS_{HIGH,LOW}` | `NevercIRAnalysisAPI` | 13 | Eingebaute und Plugin-Analysen |
| `NEVERC_INTERFACE_IR_PASS_{HIGH,LOW}` | `NevercIRPassAPI` | 1 | `RegisterPass` |
| `NEVERC_INTERFACE_IR_GEN_{HIGH,LOW}` | `NevercIRGenAPI` | 5 | Die Absenkung SemanticUnit → IR ersetzen |
| `NEVERC_INTERFACE_IR_OPTIMIZATION_{HIGH,LOW}` | `NevercIROptimizationAPI` | 7 | Die gesamte Optimierungspipeline ersetzen |

Jede ist bei Major 1 `NEVERC_INTERFACE_STABLE`. Verhandeln Sie mit den passenden
`NEVERC_IR_*_API_MAJOR` / `_MINOR` und prüfen Sie, dass `TableSize` bis zum
letzten von Ihnen aufgerufenen Platz reicht — genau wie es
[`pluginsdk/examples/FunctionPass.c`] tut:

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

## Phasen

Acht IR-Phasen:

| Phase | Richtlinie |
|---|---|
| `neverc.ir.generate` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.optimize` | OBSERVABLE, INTERCEPTABLE, REPLACEABLE |
| `neverc.ir.pass.pre_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pipeline_start` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.optimizer_last` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.post_opt` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.pass.pre_codegen` | OBSERVABLE, INTERCEPTABLE |
| `neverc.ir.final_verify` | OBSERVABLE, **VERSIEGELTES HOST-TOR** |

Auf die fünf `pass.*`-Phasen zeigt `NevercIRPassDescriptor.Phase`.
`neverc.ir.final_verify` führt den LLVM-Verifizierer aus und kann von nichts
abgefangen, ersetzt oder übersprungen werden — auch nicht von einem
Optimierungsanbieter.

## Das Schema

[`Schema/PluginIRSchema.inc`] wird generiert und von [`PluginIR.h`] eingebunden. Es
veröffentlicht einen Digest und diese Konstantenmengen:

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

IDs tragen ihre Domäne im höchstwertigen Byte — `0x41……` für Typen, `0x42……`
für Wertarten, `0x43……` für Opcodes, `0x49……` für Eigenschaften —, sodass ein
an falscher Stelle verwendeter Wert zurückgewiesen statt fehlgedeutet wird.

## Handles und Eigentum

IR-Handles sind opake `{Owner, Value}`-Paare mit der Gültigkeit einer Aufgabe,
und alles dahinter gehört dem Host.

- Behalten Sie ein Handle niemals über sein Rückruf- oder Aufgabenende hinaus.
- Verwenden Sie ein Handle niemals in einer anderen Sitzung oder Aufgabe.
- Ein festgeschriebener Ersatz macht die Handles der ersetzten Objekte ungültig.
- Eine abgebrochene Veränderung lässt die von ihr erzeugten Handles veralten.
- Fehler sind `NEVERC_STATUS_STALE_HANDLE`, `WRONG_SCOPE` oder `WRONG_TYPE` —
  niemals ein roher LLVM-Zeiger.

Zeichenketten und Bytesichten aus einer Abfrage sind für den Rückruf geliehen.
Die einzige Ausnahme ist `ExportModule`: Es liefert ein
`NevercIRSerializedBufferHandle`, das Sie an `ReleaseSerializedBuffer`
zurückgeben müssen.

## Ein Modul durchlaufen

Sammlungen werden über einen Cursor gelesen, der seine eigene Generation trägt;
eine Veränderung mitten im Durchlauf wird so erkannt, statt Einträge stillschweigend
zu überspringen:

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

Wiederholen Sie, bis `Count` null zurückgibt. Die sieben Sammlungen sind
`MODULE_FUNCTIONS`, `MODULE_GLOBALS`, `MODULE_ALIASES`, `MODULE_I_FUNCS`,
`FUNCTION_ARGUMENTS`, `FUNCTION_BLOCKS` und `BLOCK_INSTRUCTIONS`.

Alles Übrige ist eine direkte Abfrage: `GetValueKind`, `GetValueType`,
`GetOperandCount` / `GetOperand` / `SetOperand`, `GetValueUseCount` /
`GetValueUse`, `GetTerminator`, `GetPredecessor*`, `GetSuccessor*`,
`GetPHIIncoming*` sowie die modulweiten `GetModuleIdentifier`,
`GetModuleTargetTriple`, `GetModuleDataLayout`, `GetModuleInlineAssembly` mit
ihren Settern.

## Typen und Konstanten

Typen werden interniert; zweimal fragen liefert dasselbe Handle:

```c
NevercIRTypeHandle I32, Ptr, Fn;
Core->GetIntegerType(Core->Context, Task, 32, &I32);
Core->GetPointerType(Core->Context, Task, /*AddressSpace=*/0, &Ptr);

NevercIRTypeHandle Params[] = {I32, Ptr};
Core->GetFunctionType(Core->Context, Task, I32, Params, 2,
                      /*Variadic=*/0, &Fn);
```

`GetPrimitiveType` nimmt eine Schemaart wie `NEVERC_IR_TYPE_VOID`, `_FLOAT`,
`_DOUBLE` oder `_TOKEN`; `GetArrayType`, `GetVectorType` (mit einem
`Scalable`-Flag) und `GetStructType` (benannt oder literal, gepackt oder nicht)
decken den Rest ab.

Ganzzahl- und Gleitkommakonstanten entstehen aus 64-Bit-Wörtern in
Little-Endian-Reihenfolge, sodass ein `i128` keinen Sonderweg braucht:

```c
uint64_t Words[2] = {0xFFFFFFFFFFFFFFFFULL, 0x1ULL};
NevercIRValueHandle C;
Core->CreateIntegerConstant(Core->Context, Task, I128, Words, 2, &C);
```

`GetNullConstant`, `GetPoisonConstant`, `GetUndefConstant`,
`CreateAggregateConstant` und `GetGlobalAddressConstant` decken die einfachen
Fälle ab; `CreateConstantBinaryExpression`, `CreateConstantCastExpression`,
`CreateConstantCompareExpression` und `CreateConstantGEPExpression` bauen
konstante Ausdrücke.

## Instruktionseigenschaften

Statt eines Zugriffs je Flag läuft das Detail einer Instruktion über einen
getaggten Eigenschaftswert, der über eine Schema-ID adressiert wird:

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

Die 23 Eigenschaften sind `NAME`, `FAST_MATH_FLAGS`, `NUW`, `NSW`, `EXACT`,
`DISJOINT`, `VOLATILE`, `ALIGNMENT`, `ATOMIC_ORDERING`, `SYNC_SCOPE`,
`PREDICATE`, `CALLING_CONVENTION`, `TAIL_CALL_KIND`, `INDICES`, `WEAK`,
`SUCCESS_ORDERING`, `FAILURE_ORDERING`, `INBOUNDS`, `SOURCE_ELEMENT_TYPE`,
`ALLOCATED_TYPE`, `ATTRIBUTES`, `CLEANUP` und `NUSW`. Die atomaren Ordnungen
reichen von `NOT_ATOMIC` bis `SEQUENTIALLY_CONSISTENT`; die Tail-Call-Arten sind
`NONE`, `TAIL`, `MUST_TAIL` und `NO_TAIL`; die Fast-Math-Flags sind die
üblichen sieben Bits von `ALLOW_REASSOC` bis `APPROX_FUNC`.

## Attribute

Attribute sind Werte, die man erzeugt und dann anheftet; das hält die vier Arten
(`ENUM`, `INTEGER`, `STRING`, `TYPE`) einheitlich:

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

[`pluginsdk/examples/CustomCallConvPlugin.c`] nutzt dies zusammen mit
`GetFunctionStringAttribute`, um eine datengetriebene Aufrufkonvention zu
steuern.

## Transaktionale Veränderung

Strukturelle Änderungen laufen über `NevercIRBuilderAPI`. Die Veränderung ist die
Transaktion, der Erbauer ein Cursor darin.

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

Die Geltungsbereiche sind `NEVERC_IR_MUTATION_SCOPE_MODULE`, `_FUNCTION` und
`_LOOP`; `ScopeRoot` benennt die Funktion oder den Schleifenkopf. Das
Festschreiben prüft den Kandidaten und veröffentlicht atomar — scheitert der
Verifizierer, rollt der Host zurück, und das vorherige Modul überlebt unberührt.

Die Baumethoden sind `BuildBinary`, `BuildUnary`, `BuildCompare`, `BuildCast`,
`BuildSelect`, `BuildAlloca`, `BuildLoad`, `BuildStore`, `BuildGetElementPtr`,
`BuildCall`, `BuildPhi`, `BuildBranch`, `BuildConditionalBranch`,
`BuildUnreachable`, `BuildReturn` und `BuildReturnVoid`. `SetDebugLocation` und
`SetFastMathFlags` gelten für alles, was der Erbauer danach ausgibt.

Beachten Sie die Asymmetrie: `AddPhiIncoming`, `CreateFunction` und
`CreateBasicBlock` nehmen die **Veränderung**, nicht den Erbauer, weil sie nicht
an eine Einfügestelle gebunden sind.

`DestroyMutation` ist von Festschreiben und Abbruch getrennt. Zu jedem
`BeginMutation` gehört genau ein `DestroyMutation`, gleich wie die Transaktion
endete.

## Durchläufe

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

Die Ebenen sind `MODULE`, `CGSCC`, `FUNCTION` und `LOOP`. Der Aufruf trägt nur
die für seine Ebene gültigen Handles:

```c
typedef struct NevercIRPassInvocation {
  NevercABITableHeader Header;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercStringView PassID;
  NevercIRPassLevel Level;
  NevercIROptimizationLevel OptimizationLevel;  /* O0…O3, Os, Oz */
  NevercIRModuleHandle Module;
  NevercIRValueHandle Function;                 /* FUNCTION und LOOP     */
  NevercIRValueHandle LoopHeader;               /* nur LOOP              */
  const NevercIRValueHandle *SCCFunctions;      /* nur CGSCC             */
  uint64_t SCCFunctionCount;
  const NevercIRCoreAPI *Core;
  const NevercIRBuilderAPI *Builder;
  const NevercIRAnalysisAPI *Analyses;
  uint64_t Reserved[2];
} NevercIRPassInvocation;
```

Die drei API-Zeiger kommen mit dem Aufruf, ein Durchlaufrumpf braucht also keine
gespeicherte Tabelle.

Melden Sie über `OutPreserved`, was erhalten blieb:

```c
OutPreserved->Flags = NEVERC_IR_PRESERVE_ALL;   /* oder _NONE, oder _CFG */
```

`NEVERC_IR_PRESERVE_CFG` heißt, dass der Kontrollflussgraph unversehrt ist,
obwohl sich Instruktionen geändert haben. Eigene Analysen bleiben erhalten, wenn
Sie sie in `CustomAnalyses` aufführen. Behaupten Sie nach einer IR-Änderung
nicht `PRESERVE_ALL` — der Adapter vergleicht die Modulgeneration und weist eine
falsche Behauptung zurück.

Funktions- und Schleifendurchläufe können nebenläufig laufen, veränderlicher
Plugin-Zustand muss deshalb zum deklarierten `NevercConcurrencyModel` passen.

## Analysen

Sieben eingebaute Analysen sind per ID abfragbar: `DOMINATOR_TREE`,
`POST_DOMINATOR_TREE`, `LOOP_INFO`, `SCALAR_EVOLUTION`, `MEMORY_SSA`,
`CALL_GRAPH` und `ALIAS`.

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

Jede hat typisierte Zugriffsfunktionen statt eines opaken Klumpens:
`DominatorTreeDominates`, `GetLoopCount` / `GetLoopHeader` /
`GetLoopForBlock`, `GetScalarEvolutionConstantTripCount`,
`GetMemoryAccessKind` (`NONE`, `USE`, `DEF`, `PHI`, `LIVE_ON_ENTRY`),
`GetDirectCalleeCount` / `GetDirectCallee` und `Alias` (`NO`, `MAY`, `PARTIAL`,
`MUST`).

Eine Plugin-Analyse wird mit eigenem Lebenszyklus registriert:

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

`Invalidate` erfährt den Grund — `INVALIDATED_BY_PASS` oder
`INVALIDATED_BY_PLAN_DESTROY`. Ergebnisse werden je Aufruf zwischengespeichert
und je nachdem verworfen, was der laufende Durchlauf erhalten hat.
Abhängigkeitszyklen werden bei der Registrierung abgelehnt, und IR aus einem
Analyse-Rückruf heraus zu verändern wird verweigert.

## Erzeugung und Optimierung ersetzen

`NevercIRGenAPI` ersetzt `neverc.ir.generate`:

```c
NevercIRGeneratePhaseInput In = {0};
In.Header = /* … */;
Gen->GetGeneratePhaseInput(Gen->Context, Frame, Frame->Input, &In);
/* In.SemanticUnit, .TargetTriple, .DataLayout, .SourceIdentity,
   .SourceDigest */

const NevercIRCoreAPI *Core;
const NevercIRBuilderAPI *Builders;
Gen->CreateModule(Gen->Context, Frame, SV("my.module"), &Core, &Builders);
/* … das Modul bauen … */

NevercIRModuleArtifactDescriptor Descriptor = {0};
Descriptor.Header           = /* … */;
Descriptor.Product          = MyProductID;
Descriptor.DependencyDigest = Digest;
Gen->PublishModule(Gen->Context, Frame, &Descriptor, &Output);
```

`ImportModule` beginnt bei Bitcode oder textueller IR statt bei einem leeren
Modul. `NevercIROptimizationAPI` hat dieselbe Gestalt für
`neverc.ir.optimize`, dazu `GetInputModule`, um an das eingehende Modul zu
kommen, und `RunBuiltinPipeline`, um an die eingebaute Pipeline zu delegieren
und deren Ergebnis nachzubearbeiten.

Beide Wege veröffentlichen über den Host, statt einen Zeiger zurückzugeben,
beide prüfen die Zielkompatibilität, und beide behalten bei fehlgeschlagener
Veröffentlichung atomar das alte Modul. `neverc.ir.final_verify` läuft danach
trotzdem.

## Beispiele

| Datei | Zeigt |
|---|---|
| [`pluginsdk/examples/FunctionPass.c`] | Einen schreibgeschützten Funktionsdurchlauf samt ABI-Verhandlung |
| [`pluginsdk/examples/ExamplePlugin.c`] | Einen Moduldurchlauf, der Funktionen mit einem Wert-Cursor abläuft |
| [`pluginsdk/examples/CustomCallConvPlugin.c`] | Attribute und Aufrufstelleneigenschaften |

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

Verwenden Sie das Modulsuffix, das CMake für Ihre Plattform erzeugt hat.

## Regeln

- Geben Sie aus jedem Rückruf einen `NevercStatus` zurück. Ein Plugin-Fehler
  wird zu einer strukturierten Diagnose; lassen Sie niemals eine Ausnahme die
  C-Grenze überschreiten.
- Nullen Sie jede Ausgabestruktur und setzen Sie ihren `Header` vor dem Aufruf,
  der sie füllt.
- Schreiben Sie keine numerischen Opcode-, Typ- oder Eigenschaftswerte fest ein.
  Verwenden Sie die Namen aus [`PluginIRSchema.inc`], damit eine Schemarevision
  zum Übersetzungsfehler wird.
- Jedes `BeginMutation` erreicht genau ein `DestroyMutation`, jedes
  `CreateBuilder` genau ein `DestroyBuilder` — auch auf Fehlerpfaden.
- Geben Sie das, was `ExportModule` Ihnen übergibt, mit
  `ReleaseSerializedBuffer` frei.
- Behaupten Sie nach einer IR-Änderung niemals `NEVERC_IR_PRESERVE_ALL`.
- Nehmen Sie an, dass Funktions- und Schleifendurchläufe parallel laufen, sofern
  das Plugin nicht `NEVERC_CONCURRENCY_SESSION_SERIAL` deklariert hat.
- `neverc.ir.final_verify` ist versiegelt. Nichts, was ein Plugin tut, kann sie
  überspringen.

Die normativen Deklarationen, das Schema selbst, seine erzeugten Konstanten,
Phasenrichtlinien und Testnachweise stehen in [`PluginIR.h`],
[`Schema/IRSchema.json`], [`Schema/PluginIRSchema.inc`],
[`Schema/PhaseSchema.json`] und [`coverage.json`].

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginIR.h`]: ../../neverc/include/neverc/Plugin/PluginIR.h
[`PluginIRSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginIRSchema.inc
[`pluginsdk/examples/CustomCallConvPlugin.c`]: ../../pluginsdk/examples/CustomCallConvPlugin.c
[`pluginsdk/examples/ExamplePlugin.c`]: ../../pluginsdk/examples/ExamplePlugin.c
[`pluginsdk/examples/FunctionPass.c`]: ../../pluginsdk/examples/FunctionPass.c
[`Schema/IRSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/IRSchema.json
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
[`Schema/PluginIRSchema.inc`]: ../../neverc/include/neverc/Plugin/Schema/PluginIRSchema.inc
