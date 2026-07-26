**語言**: [English](target-mc-object.md) | [简体中文](target-mc-object.zh-CN.md) | [繁體中文](target-mc-object.zh-TW.md) | [日本語](target-mc-object.ja.md) | [한국어](target-mc-object.ko.md) | [Français](target-mc-object.fr.md) | [Deutsch](target-mc-object.de.md) | [Español](target-mc-object.es.md) | [Italiano](target-mc-object.it.md) | [Русский](target-mc-object.ru.md) | [العربية](target-mc-object.ar.md)

[← NeverC 外掛 ABI](README.zh-TW.md)

# NeverC 外掛 Target、MC、組合語言與目的檔 API

後端由四個標頭檔與二十九個階段構成。[`PluginTarget.h`] 描述一個目標以及穿過程式碼
產生的路線。[`PluginMC.h`] 建構並觀察機器碼。組合語言的剖析與列印也在同一個標頭檔
中。[`PluginObject.h`] 把可重定位檔案轉成正規化的圖，再轉回去。

它們合起來讓外掛能新增一個目標、替換其中一個降級步驟或全部步驟、在每條指令發射
時盯著它、定義一種組合語言方言，或改寫一個目的檔——全部透過純 C ABI，永遠不會暴
露 LLVM 的 `MCInst`、`MCSection` 或 `object::ObjectFile`。

## 介面

```c
#include "neverc/Plugin/PluginTarget.h"
#include "neverc/Plugin/PluginMC.h"
#include "neverc/Plugin/PluginObject.h"   /* includes both of the above */
```

| 介面 | 表 | 槽位 | 用途 |
|---|---|--:|---|
| `NEVERC_INTERFACE_TARGET_*` | `NevercTargetAPI` | 2 | `RegisterTarget`、`RegisterCodeGenEdge` |
| `NEVERC_INTERFACE_TARGET_ABI_*` | `NevercTargetABIAPI` | 1 | `RegisterABI` |
| `NEVERC_INTERFACE_CALLING_CONVENTION_*` | `NevercCallingConventionAPI` | 1 | `RegisterCallingConvention` |
| `NEVERC_INTERFACE_MC_*` | `NevercMCAPI` | 53 | 讀取與變更 `MCUnit`；註冊編碼器、解碼器、後端 |
| `NEVERC_INTERFACE_MC_EMISSION_*` | `NevercMCEmissionAPI` | 7 | 發射事件與版面配置快照 |
| `NEVERC_INTERFACE_MC_PROVIDER_*` | `NevercMCProviderAPI` | 4 | 替換 MIR → MC |
| `NEVERC_INTERFACE_ASSEMBLY_PROVIDER_*` | `NevercAssemblyProviderAPI` | 8 | 替換組合語言剖析器或列印器 |
| `NEVERC_INTERFACE_OBJECT_*` | `NevercObjectAPI` | 34 | 讀取與變更 ObjectGraph |
| `NEVERC_INTERFACE_OBJECT_FORMAT_*` | `NevercObjectFormatAPI` | 1 | `RegisterFormat` |
| `NEVERC_INTERFACE_OBJECT_PHASE_*` | `NevercObjectPhaseAPI` | 2 | `GetGraph`、`GetImage` |

## 兩個相容性層級

這是統管本文其餘一切的規則。

**STABLE**，可以放心寫死：與目標無關的描述子、階段 ID、產物 ID、MC 與 ObjectGraph
容器、輸出交易，以及每一條回呼契約。

**LOCKSTEP**，不檢查就不安全：與目標相關的 opcode、暫存器、運算元、fixup、重定位
與呼叫慣例 schema。它們的數值只有對著某一個確切的 schema 修訂版才有意義。

每個出現 LOCKSTEP 值的地方，旁邊都會有一個 schema 摘要。讀取該值之前先比對它：

```c
if (!string_equal(Target.SchemaDigest, MY_COMPILED_SCHEMA_DIGEST))
  return fail(NEVERC_STATUS_ABI_MISMATCH);
```

NeverC 也會在呼叫 Provider 之前拒絕不相符的 schema，所以這道檢查算是雙重保險
——但外掛若跳過它、照樣去讀原始 opcode，就會靜默地誤解指令。

## 各個階段

二十九個，分屬四個領域。

### `codegen` — 路由（4）

| 階段 | Policy |
|---|---|
| `neverc.codegen.ir_to_mir` | OBSERVABLE、INTERCEPTABLE、REPLACEABLE |
| `neverc.codegen.mir_to_mc` | OBSERVABLE、INTERCEPTABLE、REPLACEABLE |
| `neverc.codegen.coarse_lower` | OBSERVABLE、INTERCEPTABLE、REPLACEABLE |
| `neverc.codegen.product_verify` | OBSERVABLE、**SEALED** |

### `mc` — 機器碼（13）

`neverc.mc.encode`、`neverc.mc.decode` 與 `neverc.mc.layout` 是 OBSERVABLE、
INTERCEPTABLE、REPLACEABLE。

`neverc.mc.emission.pre_instruction` 是唯一同時可 REPLACEABLE 的發射事件——就是在
那裡替換指令。其餘九個（`unit_begin`、`unit_end`、`section_change`、
`post_instruction`、`post_encode`、`fixup`、`relaxation_round`、`pre_layout`、
`post_layout`）只能觀察。

### `assembly`（4）

`neverc.assembly.parse` 與 `neverc.assembly.print` 可 REPLACEABLE。
`neverc.assembly.final_verify` 與 `neverc.assembly.commit` 是 SEALED。

### `object`（8）

`neverc.object.probe`、`read`、`write`、`pre_write` 與 `post_layout` 可
REPLACEABLE；`neverc.object.post_write` 只能 INTERCEPTABLE；
`neverc.object.final_verify` 與 `neverc.object.commit` 是 SEALED。

## 註冊一個目標

`NevercTargetDescriptor` 是這套 ABI 中最大的描述子，因為它承載了前端與後端需要知
道的一切：

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

`TripleMatchers` 決定何時選中這個目標：每個匹配器指明一組架構、廠商、作業系統與
環境，外加一個 `Priority`，用來在與內建目標打平時分出勝負。

`Machine` 是一個 `NevercTargetMachineDescriptor`——資料佈局、預設與可調校的 CPU、
特性表、支援的 ABI、呼叫慣例與目的檔格式、位址空間、重定位與程式碼模型（同時給出
預設值與支援遮罩）、例外模型（`NONE`、`DWARF`、`SJLJ`、`SEH`、`WASM`）、展開模
型、位元組序、pointer/int/long/long long 的寬度、堆疊對齊、最大原子與向量寬度、
`va_list` 種類、執行層級（`USER`、`KERNEL`、`HYPERVISOR`、`FIRMWARE`）以及 TLS
支援。

目標內建函式各自帶著自己的降級回呼，該回呼會收到一個可用的 IR builder：

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

## ABI 與呼叫慣例

ABI 負責對函式簽章分類：

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

引數種類有 `DIRECT`、`EXTEND`、`INDIRECT`、`IGNORE`、`EXPAND`、
`INDIRECT_ALIASED` 與 `COERCE_AND_EXPAND`；旗標有 `BYVAL`、`REALIGN`、`INREG`、
`SRET_AFTER_THIS`、`CAN_BE_FLATTENED`、`SIGN_EXTEND` 與 `PADDING_INREG`。強制轉
換為 `NONE`、`INTEGER`、`FLOAT` 或 `POINTER`，而 `COERCE_AND_EXPAND` 會提供一個
`NevercABICoercionElement` 陣列。

呼叫慣例則更低一層，負責指派實際位置：

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

`Query->SchemaDigest` 是 LOCKSTEP 值——`RegisterNumber` 只有對著它所指名的那個
schema 才有意義。完整的實作範例請見
[自訂呼叫慣例](custom-callconv/README.zh-TW.md#具現化的-plan) 與
[`pluginsdk/examples/CustomCallConvPlugin.c`]。

## 程式碼產生路線

路線是從正規的 `NevercTargetKey` 中選出來的：目標 ID、triple 各部分、CPU、tune
CPU、特性、ABI、呼叫慣例、目的檔格式、重定位模型、程式碼模型、執行層級、指標寬度、
位元組序與 schema 摘要。註冊你能服務的邊：

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

產物種類有 `IR`、`MIR`、`MC`、`ASSEMBLY`、`OBJECT_GRAPH`、`OBJECT_IMAGE` 與
`CUSTOM`。細緻路線是 `IR → MIR → MC → ObjectGraph → ObjectImage`。

設定 `NEVERC_CODEGEN_EDGE_COARSE` 並提供 `CoarseLower`，就能一步替換掉整段
`IR → ObjectImage`：

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

粗略路線仍然要通過 `neverc.codegen.product_verify` 與交易式輸出提交。呼叫
`VerifyProduct` 時會帶上宿主要求你已經履行的義務——`VERIFY_FINAL_IR`、
`VERIFY_TARGET_KEY`、`VERIFY_PRODUCT_KIND`、`VERIFY_PRODUCT_ID`、
`VERIFY_STRUCTURE`——所以 Provider 無法靠抄近路悄悄跳過某道關卡。

## 建構 MC

一個 `MCUnit` 持有 section、符號、運算式、fragment、指令、運算元與 fixup。讀取採
first/next 迭代：

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

變更是交易式的，跟別處一樣：

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

控制代碼限定於任務範圍並會做世代檢查，因此來自已放棄變更的控制代碼會被拒絕，而不
是被重複使用。

Section 旗標有 `ALLOCATED`、`EXECUTABLE`、`WRITABLE`、`MERGEABLE` 與 `DEBUG`。符
號繫結有 `LOCAL`、`GLOBAL`、`WEAK`；型別有 `NONE`、`FUNCTION`、`OBJECT`、
`SECTION`、`TLS`；定義有 `UNDEFINED`、`SECTION`、`ABSOLUTE`、`COMMON`。運算式支
援一元的 `PLUS`、`MINUS`、`NOT`，以及二元的 `ADD`、`SUBTRACT`、`MULTIPLY`、
`DIVIDE`、`AND`、`OR`、`XOR`、`SHIFT_LEFT`、`SHIFT_RIGHT`。想讓宿主替你決定位置
時，就傳 `NEVERC_MC_AUTOMATIC_OFFSET`。

`RegisterSchema` 會發布一份目標 MC schema，而 `GetSchemaToken` /
`GetSchemaTokenInfo` 負責在名稱與 LOCKSTEP token 之間互相解析。

## 觀察發射過程

發射串流會依序回報十一種事件。以觀察者身分訂閱並讀取事件：

```c
NevercMCEmissionEventInfo Event = {0};
Event.Header = /* … */;
Emission->GetEvent(Emission->Context, Frame, &Event);
/* Event.Kind, Event.Flags */
```

`Flags` 說明事件的哪些部分已被填入：`HAS_SECTION`、`HAS_INSTRUCTION`、
`HAS_ENCODING`、`HAS_FIXUP`、`HAS_LAYOUT` 與 `CAN_REPLACE_INSTRUCTION`。讀取對應
欄位之前先檢查旗標——尚未有編碼的事件，不會因為你問了就憑空生出一份。

一旦設定了 `HAS_LAYOUT`，`GetLayoutSection`、`GetLayoutFragment`、
`GetLayoutSymbol` 與 `GetLayoutFixup` 就會給出位址與大小。

在 `pre_instruction`、且僅當設定了 `CAN_REPLACE_INSTRUCTION` 時，你可以替換：

```c
Emission->BeginInstructionReplacement(Emission->Context, Frame, &Builder);
/* build the replacement through the MC builder */
Emission->PublishInstructionReplacement(Emission->Context, Frame, NewInstr);
```

[`pluginsdk/examples/MCObserverPlugin.c`] 是它的唯讀版本。

## 編碼器、解碼器與版面配置

三種註冊可以擴充機器碼後端，全都以目標與 schema 摘要為鍵：

```c
MC->RegisterEncoder(MC->Context, RegistrarContext, &EncoderDescriptor);
MC->RegisterDecoder(MC->Context, RegistrarContext, &DecoderDescriptor);
MC->RegisterAsmBackend(MC->Context, RegistrarContext, &BackendDescriptor);
```

編碼器透過 sink 寫出，而不是回傳一個緩衝區，這樣所有權就留在宿主那一側：

```c
Sink->WriteBytes(Sink->Context, Bytes);
Sink->AddFixup(Sink->Context, &Fixup);
```

解碼器會回報 `NEVERC_MC_DECODE_SUCCESS`、`_SOFT_FAIL`、`_UNKNOWN` 或 `_FAIL` 其中
之一。Fixup 種類透過 `NevercMCFixupKindInfo` 以 `PC_RELATIVE`、`SIGNED`、
`RELAXABLE` 與 `TARGET` 旗標自我描述。

asm 後端負責 relaxation。版面配置會發出一份證明摘要，而**版面配置之後的任何變更都
會使該證明失效**，並在目的檔可以寫出之前強制重新配置版面——這與連結圖所用的世代檢
查模式如出一轍。

## 組合語言

剖析器 Provider 消費原始位元組並發布一個 `MCUnit`：

```c
NevercAssemblyParseInputInfo In = {0};
In.Header = /* … */;
Asm->GetParseInput(Asm->Context, Frame, &In);

NevercAssemblyTokenInfo Token = {0};
Asm->PeekSourceToken(Asm->Context, Frame, &Token);
Asm->AdvanceSourceToken(Asm->Context, Frame);

const NevercMCAPI *MC;
NevercMCUnitHandle Unit;
Asm->GetParseMCBuilder(Asm->Context, Frame, &MC, &Unit);
/* … build … */
Asm->PublishParsedMCUnit(Asm->Context, Frame, Unit, &Output);
```

來源不是 `NEVERC_ASSEMBLY_SOURCE_BUFFER` 就是
`NEVERC_ASSEMBLY_SOURCE_RENDERED_TOKENS`。經過前置處理的組合語言（`.S`）會先走一
遍正常的前端前置處理器，然後以已渲染的 token 形式抵達；純組合語言（`.s`）則以緩
衝區形式直接進入剖析器。

列印器走的是反方向——`GetPrintInput`，接著 `WritePrintOutput` 寫進所提供的輸出交
易，最後 `PublishAssemblyOutput`。不支援寫到其他任何地方：剖析／列印的驗證與宿主
提交關卡都在位元組可見之前執行，所以列印失敗不會留下任何殘缺檔案。

## 目的檔圖

`NevercObjectAPI` 把可重定位檔案正規化為 section、符號、重定位與 COMDAT。內建轉
接器涵蓋 ELF、COFF 與 Mach-O；`RegisterFormat` 可以再加一種。

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

四種實體的變更都遵循 create/replace/move/erase 模式，並暫存在
`BeginMutation` … `CommitMutation` / `AbandonMutation` 之間。

Section 旗標有 `ALLOCATED`、`EXECUTABLE`、`WRITABLE`、`MERGEABLE`、`STRINGS`、
`TLS`、`DEBUG`、`UNWIND`、`DISCARDABLE` 與 `RETAIN`。重定位目標為 `SYMBOL`、
`SECTION`、`ABSOLUTE` 或 `FORMAT_EXTENSION`。

每個描述子都有一組 `ExtensionOwner` / `ExtensionVersion` / `Extension`。格式正是
靠這個來保留正規化圖沒有欄位可放的資料——那些位元組會跟著實體走，並在寫出時回來，
而不是在來回轉換中被丟掉。

### 註冊一種格式

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

`Probe` 會回報一個介於 0 到 `NEVERC_OBJECT_PROBE_MAX_CONFIDENCE`（1000）之間的
`Confidence`、它辨識出的 `NevercObjectArtifactKind`（`RELOCATABLE`、`ARCHIVE`、
`EXECUTABLE_IMAGE`、`SHARED_IMAGE`、`UNIVERSAL_BINARY`），以及一個
`ConsumedMinimum`——也就是它為了確定所需要的位元組數，上限為
`NEVERC_OBJECT_PROBE_MAX_CONSUMED_MINIMUM`（65536）。信心最高者勝出。

`Reader` 會拿到一張圖與一次開啟的變更，由它把內容填進去。`Writer` 則拿到那張圖、
它的版面配置證明，以及有界的二進位建構器。

### 寫出流水線

1. 探測並把位元組讀入 ObjectGraph；
2. 執行 `object.pre_write` 圖攔截器；
3. 進行版面配置，然後執行 `object.post_layout`（任何變更之後都要重新配置版面）；
4. 寫出有界的候選映像；
5. 執行 `object.post_write` 二進位攔截器；
6. 執行密封的 `object.final_verify` 與不可分割的 `object.commit`。

映像狀態的走向是 `CANDIDATE` → `VERIFIED` → `COMMITTED`，或者 `ABORTED` /
`FAILED_PARTIAL`。

觀察者取得的是唯讀橋接；從觀察者發起變更會以 `NEVERC_STATUS_POLICY_VIOLATION`
遭拒。寫出器與 post-write 攔截器只能取得有界的 `NevercMutableBinaryAPI` 建構器
——`Reserve`、`Write`、`WriteAt`、`Tell`、`ReadAt`、`Insert`、`Append`、`Resize`。
溢位、回呼失敗或驗證失敗都會中止暫存，因此失敗絕不會在磁碟上留下半個檔案。

[`pluginsdk/examples/ObjectRewritePlugin.c`] 是一次完整的交易式改寫。

## 規則

- 在取用任何 LOCKSTEP 的 opcode、暫存器、運算元、fixup、重定位或呼叫慣例值之前，
  先比對 schema 摘要。
- 把可變狀態放在宿主提供的 process、session 與 task 狀態中。
- 回呼返回之後，不要快取任務控制代碼或借用視圖。
- 攔截器的延續最多呼叫一次，且必須在回呼執行緒上呼叫。
- 每個 `BeginMutation` 都恰好對應一次提交或放棄。
- 變更了已完成版面配置的 MCUnit 或 ObjectGraph 之後要重新配置版面；舊的版面配置證
  明已經過期，宿主會拒絕它。
- 讀取事件欄位之前先檢查 `NevercMCEmissionEventInfo.Flags`，且只有在設定了
  `CAN_REPLACE_INSTRUCTION` 時才替換指令。
- 只透過所提供的交易或位元組 sink 寫出輸出。
- 失敗時回傳原始的 `NevercStatus`，且不發布任何殘缺產物。
- 宣告最狹窄且屬實的並行與可重入模型。
- `codegen.product_verify`、`assembly.final_verify`、`assembly.commit`、
  `object.final_verify` 與 `object.commit` 是密封的。只能觀察。

規範性的宣告請見 [`PluginTarget.h`]、[`PluginMC.h`]、[`PluginObject.h`] 與
[`Schema/PhaseSchema.json`]；它們使用的實體、運算元、fixup 與區段種類來自
[`Schema/MCSchema.json`] 與 [`Schema/ObjectSchema.json`]，兩者分別產生
[`Schema/PluginMCSchema.inc`] 與 [`Schema/PluginObjectSchema.inc`]。
[`coverage.json`] 則把這些穩定階段各自對應到正向、負向、替換、唯讀觀察者與密封
gate 的測試。

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
