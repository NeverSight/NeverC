**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC 外掛 ABI

NeverC 外掛是這樣一個共享模組：它只匯出一個函式，依 128 位元介面 ID 協商帶版本
的能力表，並把自己掛到一張凍結的具名編譯器階段圖上。整套介面都是純 C11。外掛永
遠不會引入 LLVM 標頭檔，不會連結編譯器，也不會讓任何 C++ 型別越過邊界。

```c
NEVERC_EXPORT NevercStatus NEVERC_CALL
neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap,
                    NevercPluginDescriptor *OutPlugin);
```

這個宣告位於 `PluginCore.h`，它就是全部的連結契約。其餘的一切——讀 IR、改寫目的
檔圖、替換最佳化流水線——都要透過你按 ID 向宿主索取的表來完成。

## 文件入口

| 指南 | 涵蓋內容 |
|---|---|
| [驅動程式 API](driver.zh-TW.md) | 命令列、工具鏈選擇、action 圖、job 圖 |
| [Source 與 I/O API](source.zh-TW.md) | VFS Provider、原始碼位置、緩衝區、輸出 sink、相依性 |
| [前置處理器 API](prep.zh-TW.md) | token、巨集、pragma、include、特性查詢、39 種事件 |
| [AST 與語意 API](ast-sema.zh-TW.md) | 剖析器擴充、AST 變更、名稱查找、型別、常數 |
| [IR API](ir.zh-TW.md) | LLVM IR 讀取、交易式建構、分析、pass、Provider |
| [MIR API](mir.zh-TW.md) | 機器函式、暫存器、堆疊框、MIR pass 與分析 |
| [Target、MC、組合語言、目的檔](target-mc-object.zh-TW.md) | 目標註冊、呼叫慣例、MC 編碼、目的檔圖 |
| [連結與 LTO API](link-lto.zh-TW.md) | 連結圖、符號解析、GC/ICF、連結器與 LTO Provider |
| [DynCode API](dyncode.zh-TW.md) | 扁平位置無關映像、匯入降級、字元集編碼 |
| [自訂呼叫慣例](custom-callconv/README.zh-TW.md) | 資料驅動的呼叫慣例外掛 |
| [階段涵蓋範圍證據](coverage.json) | 每個穩定階段的測試對應 |

## 執行模型

宿主透過三層巢狀範圍驅動外掛。每層範圍都會交給外掛一個不透明的狀態指標，由外掛
自行配置並擁有，因此撰寫正確的外掛不需要任何全域可變狀態。

| 範圍 | 回呼 | 意義 |
|---|---|---|
| Process | `ProcessBegin`、`Register`、`Destroy` | 一個編譯器行程。在此查詢介面、註冊能力。 |
| Session | `SessionBegin`、`SessionEnd` | 一次驅動程式呼叫。 |
| Task | `TaskBegin`、`TaskEnd` | 一個工作單元，由 `NevercTaskKind` 識別。 |

```c
typedef struct NevercPluginDescriptor {
  NevercABITableHeader Header;
  NevercStringView PluginID;
  NevercStringView DisplayName;
  NevercSemanticVersion Version;
  NevercConcurrencyModel Concurrency;
  NevercReentrancyModel Reentrancy;
  NevercStructArrayView RequiredInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView OptionalInterfaces;   /* NevercInterfaceRequirement[] */
  NevercStructArrayView Dependencies;         /* NevercPluginDependency[]     */
  NevercProcessBeginFn ProcessBegin;
  NevercRegisterPluginFn Register;
  NevercSessionBeginFn SessionBegin;
  NevercSessionEndFn SessionEnd;
  NevercTaskBeginFn TaskBegin;
  NevercTaskEndFn TaskEnd;
  NevercPluginDestroyFn Destroy;
} NevercPluginDescriptor;
```

實際上只有 `PluginID` 和 `Register` 是必需的；每個回呼槽位都可以留 `NULL`。Task
種類有 `NEVERC_TASK_INVOCATION`、`TRANSLATION_UNIT`、`LTO`、`LINK`、`CODEGEN`、
`OBJECT` 與 `DYNCODE`。

宿主先呼叫 `ProcessBegin`，接著恰好呼叫一次 `Register`。註冊是唯一可以加入選項、
觀察者、攔截器與 Provider 的地方；之後階段圖即被凍結。

狀態是在回呼內部取出來的，而不是事先捕捉的：

```c
Core->GetSessionState(Core->Context, Frame->Session, PluginID, &SessionState);
Core->GetTaskState(Core->Context, Frame->Task, PluginID, &TaskState);
```

## 階段（Phase）

階段是一個具名、帶版本的轉換：從輸入產物到輸出產物。NeverC 內建
**130 個階段**，另有 8 個保留給外掛自訂階段的擴充 ID 家族：

| 領域 | 階段數 | 領域 | 階段數 |
|---|--:|---|--:|
| `driver` | 6 | `mir` | 10 |
| `source` | 3 | `codegen` | 4 |
| `prep` | 6 | `mc` | 13 |
| `syntax` | 7 | `assembly` | 4 |
| `sema` | 7 | `object` | 8 |
| `ir` | 8 | `link` | 20 |
| | | `dyncode` | 34 |

這 130 個在 ABI major 1 中的穩定性層級全都是 `stable`。每個階段宣告一條 policy，
外掛只能以該 policy 允許的方式掛載：

| Policy 旗標 | 階段數 | 外掛可以做什麼 |
|---|--:|---|
| `NEVERC_PHASE_OBSERVABLE` | 130 | 註冊觀察者，以唯讀方式接收通知。 |
| `NEVERC_PHASE_INTERCEPTABLE` | 105 | 包裹該階段，自行決定是否呼叫鏈上其餘部分。 |
| `NEVERC_PHASE_REPLACEABLE` | 86 | 註冊 Provider，由它自己產出輸出。 |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | 13 | 在提供 proof handle 的前提下跳過該轉換。 |
| `NEVERC_PHASE_SEALED_HOST_GATE` | 14 | 什麼都不能做。驗證器與提交歸宿主所有。 |

那 14 個 sealed gate 是 `ir.final_verify`、`mir.final_verify`、
`codegen.product_verify`、`assembly.final_verify`、`assembly.commit`、
`object.final_verify`、`object.commit`、`link.image_verify`、
`link.side_outputs_verify`、`link.commit`、`dyncode.ir.final_verify`、
`dyncode.mir.final_verify`、`dyncode.verify` 與 `dyncode.commit`。它們可以被觀
察，但永遠不能被攔截、替換或跳過。

觀察者會在階段宣告的時機被送達：`NEVERC_OBSERVER_BEFORE`、
`NEVERC_OBSERVER_AFTER` 與 `NEVERC_OBSERVER_AFTER_COMMIT`。攔截器會收到一個
`NevercPhaseContinuation`，必須在回呼執行緒上**最多呼叫一次** `InvokeNext`，然後
在 `NevercPhaseResult.Action` 中回報 `NEVERC_PHASE_CONTINUE`、
`NEVERC_PHASE_REPLACE` 或 `NEVERC_PHASE_SKIP`。

每個階段回呼都會收到同樣的框：

```c
typedef struct NevercPhaseFrame {
  NevercABITableHeader Header;
  NevercSessionHandle Session;
  NevercTaskHandle Task;
  NevercInterfaceID Phase;
  NevercPhaseRoute Route;        /* triple, CPU, features, object format */
  NevercArtifactHandle Input;
  NevercArtifactHandle CurrentOutput;
  NevercHandle Cancellation;
} NevercPhaseFrame;
```

`Schema/PhaseSchema.json` 是階段 ID、policy、穩定性層級與驗證器 gate 的規範事實
來源。產生出的 `Schema/PluginPhaseSchema.inc` 會把它們每一個都公開為編譯期常數
——以階段 `neverc.ir.pass.pipeline_start` 為例：

```c
NEVERC_PHASE_IR_PASS_PIPELINE_START_NAME       /* "neverc.ir.pass.pipeline_start" */
NEVERC_PHASE_IR_PASS_PIPELINE_START_HIGH       /* UINT64_C(0x4e43504849520001)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_LOW        /* UINT64_C(0x0000000000000004)     */
NEVERC_PHASE_IR_PASS_PIPELINE_START_POLICY     /* OBSERVABLE | INTERCEPTABLE       */
NEVERC_PHASE_IR_PASS_PIPELINE_START_STABILITY
NEVERC_PHASE_IR_PASS_PIPELINE_START_INPUT_HIGH /* and _INPUT_LOW, _OUTPUT_*        */
```

`NEVERC_BUILTIN_PHASE_COUNT` 以及各領域的
`NEVERC_BUILTIN_<DOMAIN>_PHASE_COUNT` 常數，可以讓外掛對自己編譯時所依據的那張圖
下斷言。

## 一個完整的最小外掛

以下就是 `pluginsdk/templates/minimal/Plugin.c` 的原文。它可以載入、協商 ABI、不
註冊任何東西、乾淨卸載——複製該目錄即可在此基礎上擴充。

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
  /* Register options, observers, interceptors, or providers here. */
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

`OutPlugin` 是由呼叫方擁有的緩衝區。進入時它的 `Header.StructSize` 是可寫入容量；
外掛最多只能寫入那麼多位元組，並回報它實際產生的大小。先寫入描述子自己的
`Header`、再截斷這次複製，就同時滿足了這條規則的兩半。

## 介面協商

能力表是按 128 位元介面 ID 取得的，而不是按符號。索取你編譯時所依據的 major
版本，以及你能接受的最低 minor 版本：

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

拿 `TableSize` 去比對你所呼叫的最後一個函式的位移，正是讓這套 ABI 可擴充的規則：
較新的宿主在後面追加欄位，而較舊的外掛依然能運作，因為它永遠不會讀取超出自己驗
證過的那段前綴。`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` 巨集會對你收到
的結構套用同一個檢查。`NevercCoreAPI` 上也有同樣簽章的 `QueryInterface`，所以你
可以延後協商，而不必在進入點就完成。

公開介面、它們的表，以及它們的 ID 巨集：

| 介面巨集配對 | 表 | 標頭檔 |
|---|---|---|
| `NEVERC_INTERFACE_CORE_{HIGH,LOW}` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER_*` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO_*`、`..._SOURCE_LOCATION_*` | `NevercIOAPI`、`NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP_*` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST_*`、`..._PARSER_*` | `NevercASTAPI`、`NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA_*` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE_*`、`..._IR_BUILDER_*`、`..._IR_ANALYSIS_*`、`..._IR_PASS_*`、`..._IR_GEN_*`、`..._IR_OPTIMIZATION_*` | 六張 IR 表 | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET_*`、`..._TARGET_ABI_*`、`..._CALLING_CONVENTION_*` | `NevercTargetAPI`、`NevercTargetABIAPI`、`NevercCallingConventionAPI` | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR_*`、`..._MIR_ANALYSIS_*`、`..._MIR_PASS_*`、`..._MIR_PROVIDER_*` | 四張 MIR 表 | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC_*`、`..._MC_EMISSION_*`、`..._MC_PROVIDER_*`、`..._ASSEMBLY_PROVIDER_*` | 四張 MC 表 | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT_*`、`..._OBJECT_FORMAT_*`、`..._OBJECT_PHASE_*` | 三張目的檔表 | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK_*`、`..._LINK_REGISTRAR_*`、`..._LINK_PHASE_*` | 三張連結表 | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO_*`、`..._LTO_REGISTRAR_*` | `NevercLTOAPI`、`NevercLTORegistrarAPI` | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE_*`、`..._DYNCODE_REGISTRAR_*`、`..._DYNCODE_PHASE_*` | 三張 dyncode 表 | `PluginDynCode.h` |

每個標頭檔也都定義了對應的 `NEVERC_<DOMAIN>_API_MAJOR` 與 `_MINOR`，供你傳給
`QueryInterface`。

介面要麼是 `NEVERC_INTERFACE_STABLE`（較新的宿主只能追加），要麼是
`NEVERC_INTERFACE_LOCKSTEP`（必須完全相符的目標相關 schema）。在取用 LOCKSTEP
值之前請先比對 schema 摘要。

## 註冊

`Register` 會收到一個 `NevercRegistrarAPI` 和一個不透明的 `RegistrarContext`：

```c
typedef struct NevercRegistrarAPI {
  NevercABITableHeader Header;
  NevercRegisterInterfaceFn RegisterInterface;
  NevercRegisterPhaseFn RegisterPhase;
  NevercRegisterObserverFn RegisterObserver;
  NevercRegisterInterceptorFn RegisterInterceptor;
  NevercRegisterProviderFn RegisterProvider;
  NevercRegisterOptionFn RegisterOption;
} NevercRegistrarAPI;
```

這些呼叫都以 `RegistrarContext` 為第一個參數、以一個清零的描述符為第二個參數。
你呼叫哪一個，決定了宿主在該階段如何對待你：

| 呼叫 | 描述符 | 回呼 | 階段必須宣告 |
|---|---|---|---|
| `RegisterObserver` | `NevercObserverDescriptor` | `NevercPhaseObserverFn` | `OBSERVABLE` |
| `RegisterInterceptor` | `NevercInterceptorDescriptor` | `NevercPhaseInterceptorFn` | `INTERCEPTABLE` |
| `RegisterProvider` | `NevercProviderDescriptor` | `NevercPhaseProviderFn` | `REPLACEABLE` |
| `RegisterPhase` | `NevercPhaseDescriptor` | — | 一個外掛自訂 ID |
| `RegisterOption` | `NevercOptionDescriptor` | 選用的 `Validator` | — |
| `RegisterInterface` | 直接傳參 | — | — |

結構檢核不通過的描述符會當場以 `NEVERC_STATUS_INVALID_DESCRIPTOR` 被拒絕。策略檢
查則發生在宿主套用這次註冊的時候：未知的階段，或沒有宣告你這次呼叫所需策略的階
段，會在那時被拒。sealed gate 只接受觀察者這一種註冊。

各領域的註冊函式——`NevercIRPassAPI.RegisterPass`、
`NevercTargetAPI.RegisterTarget`、`NevercObjectFormatAPI.RegisterFormat` 等等
——都把同一個 `RegistrarContext` 當作第二個引數，宿主正是靠它把一次註冊歸屬到你
的外掛。

### 觀察者

```c
typedef struct NevercObserverDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercObserverPoint Points;
  uint32_t Reserved;
  NevercPhaseObserverFn Callback;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercObserverDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercPhaseObserverFn)(
    const NevercPhaseFrame *Frame, NevercObserverPoint Point, void *UserData);
```

`Points` 是 `NEVERC_OBSERVER_BEFORE`（1）、`NEVERC_OBSERVER_AFTER`（2）與
`NEVERC_OBSERVER_AFTER_COMMIT`（4）的位元遮罩，必須非零；回呼的 `Point` 參數會告訴
你觸發的是哪一個。取自 `pluginsdk/examples/DriverTracePlugin.c`：

```c
NevercObserverDescriptor Observer = {0};
Observer.Header = (NevercABITableHeader){
    sizeof(Observer), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
Observer.Phase = (NevercInterfaceID){NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_HIGH,
                                     NEVERC_PHASE_DRIVER_RAW_ARGUMENTS_LOW};
Observer.Points = NEVERC_OBSERVER_BEFORE | NEVERC_OBSERVER_AFTER;
Observer.Callback = observe_arguments;
Observer.UserData = Process;
Status = Registrar->RegisterObserver(RegistrarContext, &Observer);
```

`UserData` 會原樣回傳。設定 `DestroyUserData`——本節每個描述符都有這個欄位——會讓宿
主在該註冊消亡時釋放那塊記憶體，於是按註冊配置的記憶體不必再在 `Destroy` 裡追蹤。

### 攔截器

```c
typedef struct NevercInterceptorDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercPhaseInterceptorFn Callback;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercInterceptorDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercPhaseInterceptorFn)(
    const NevercPhaseFrame *Frame, NevercPhaseContinuation *Continuation,
    NevercPhaseResult *OutResult, void *UserData);
```

延續（continuation）就是鏈條的其餘部分，而結果是你用來回報自己做了什麼的：

```c
typedef struct NevercPhaseContinuation {
  NevercABITableHeader Header;
  NevercInvokeNextFn InvokeNext;
  void *Context;
  uint64_t Generation;
} NevercPhaseContinuation;

typedef struct NevercPhaseResult {
  NevercABITableHeader Header;
  NevercPhaseAction Action;
  uint32_t Reserved;
  NevercArtifactHandle Output;
  NevercProofHandle Proof;
} NevercPhaseResult;
```

這三個動作不可互換。宿主會拿結果與你的實際行為對照，一旦不符就以
`NEVERC_STATUS_POLICY_VIOLATION` 讓整條鏈失敗：

| `Action` | `InvokeNext` | `Output` | `Proof` | 另需 |
|---|---|---|---|---|
| `NEVERC_PHASE_CONTINUE` | 呼叫一次 | 空 | 空 | — |
| `NEVERC_PHASE_REPLACE` | 不呼叫 | 有值 | 空 | `REPLACEABLE` |
| `NEVERC_PHASE_SKIP` | 不呼叫 | 有值 | 有值 | `SKIPPABLE_WITH_PROOF` |

`InvokeNext` 最多呼叫一次，且只能在回呼執行緒上呼叫——呼叫第二次屬於策略違規，從別
的執行緒呼叫則回報 `NEVERC_STATUS_WRONG_SCOPE`。攔截器沒有呼叫它卻回傳 `CONTINUE`，
同樣是策略違規，因為那樣這個階段會悄無聲息地什麼都不產出。

```c
NevercInterceptorDescriptor Interceptor = {0};
Interceptor.Header = (NevercABITableHeader){
    sizeof(Interceptor), NEVERC_PLUGIN_ABI_MAJOR, NEVERC_PLUGIN_ABI_MINOR, 0};
Interceptor.Phase = (NevercInterfaceID){NEVERC_PHASE_DRIVER_EXECUTE_JOB_HIGH,
                                        NEVERC_PHASE_DRIVER_EXECUTE_JOB_LOW};
Interceptor.Callback = intercept_job;
Interceptor.UserData = Process;
Status = Registrar->RegisterInterceptor(RegistrarContext, &Interceptor);
```

### Provider

Provider 會徹底替換掉一個階段，因此它還要宣告建置快取所依賴的決定性契約：

```c
typedef struct NevercProviderDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercStringView ProviderID;
  NevercPhaseRoute Route;
  NevercBool Deterministic;
  NevercBool Cacheable;
  NevercBool FallbackSafe;
  uint32_t Reserved;
  NevercPhaseProviderFn Callback;
  void *UserData;
  NevercDestroyUserDataFn DestroyUserData;
} NevercProviderDescriptor;

typedef NevercStatus(NEVERC_CALL *NevercPhaseProviderFn)(
    const NevercPhaseFrame *Frame, NevercPhaseResult *OutResult,
    void *UserData);
```

```c
Provider.ProviderID    = SV("com.example.my-lowering");
Provider.Route         = /* triple / CPU / features / object format */;
Provider.Deterministic = NEVERC_TRUE;
Provider.Cacheable     = NEVERC_TRUE;
Provider.FallbackSafe  = NEVERC_FALSE;  /* built-in cannot silently take over */
```

`ProviderID` 必須是規範名稱：至多 255 位元組，只含小寫字母、數字、`.`、`_` 與 `-`，
不以點開頭或結尾，也不含 `..`。哪怕出現一個大寫字母，這次註冊也會被拒。
`Route.Header` 和其他表頭一樣必須初始化。

這裡沒有延續：回呼本身就是這個階段。它必須回報 `NEVERC_PHASE_REPLACE`，帶上
`Output` 且 `Proof` 為空——其他任何組合都是策略違規。

`FallbackSafe` 是這幾個旗標裡唯一在執行時有實際效果、而不只是記帳的。當它為
`NEVERC_TRUE` 且 Provider 以帶 `NEVERC_STATUS_FLAG_RECOVERABLE` 的狀態失敗時，宿主
可以丟棄已產生的部分效果，改跑內建實作。如果一次半途而廢的嘗試無法回復，就讓它保
持 `NEVERC_FALSE`。

### 外掛自訂階段

`RegisterPhase` 用來加入一個宿主並不知道的轉換，這正是那 8 個擴充 ID 族被保留下來的
用途：

```c
typedef struct NevercPhaseDescriptor {
  NevercABITableHeader Header;
  NevercInterfaceID Phase;
  NevercStringView CanonicalName;
  NevercInterfaceID InputArtifact;
  NevercInterfaceID OutputArtifact;
  NevercPhasePolicy Policy;
  NevercObserverPoint ObserverPoints;
  uint32_t Reserved;
} NevercPhaseDescriptor;
```

`Phase`、`InputArtifact` 與 `OutputArtifact` 都必須非零，`Policy` 必須非零且只能包含
已知旗標。宣告了 `ObserverPoints` 卻沒有 `NEVERC_PHASE_OBSERVABLE` 會被拒絕；把
`NEVERC_PHASE_SEALED_HOST_GATE` 與 `INTERCEPTABLE`、`REPLACEABLE` 或
`SKIPPABLE_WITH_PROOF` 中任何一個組合在一起同樣會被拒——這與內建階段圖所檢核的是同
一批不變式。請從你所在領域的族裡取 ID，這樣才不會與將來的內建階段相撞：

```c
#include "neverc/Plugin/Schema/PluginPhaseSchema.inc"

/* NEVERC_EXTENSION_FAMILY_COUNT is 8; family 1 is "neverc.ir.extension". */
NevercInterfaceID MyPhase = {NEVERC_EXTENSION_FAMILY_1_ID_HIGH,
                             NEVERC_EXTENSION_FAMILY_1_ID_LOW_MIN};
```

每個族都會給出 `_NAMESPACE`、`_ID_HIGH`、`_ID_LOW_MIN` 與 `_ID_LOW_MAX`，低 64 位元由
你在該區間內自行配置。

### 向其他外掛發布介面

`RegisterInterface` 是唯一不接受描述符的呼叫。它把你自己的一張表交給宿主，好讓另一
個外掛能透過查詢內建介面所用的同一個 `QueryInterface` 取得它：

```c
Registrar->RegisterInterface(RegistrarContext, MyInterfaceID,
                             NEVERC_INTERFACE_STABLE, &MyTable,
                             /* Compatibility = */ NULL);
```

當表裡攜帶的是經不起版本偏移的目標相關 schema 值時，改傳
`NEVERC_INTERFACE_LOCKSTEP`。lockstep 介面必須提供一個 `NevercCompatibilityKey`，
把消費者釘死在某一個生產者建置上：

```c
typedef struct NevercCompatibilityKey {
  NevercABITableHeader Header;
  NevercStringView ProducerBuildID;   /* compare against Bootstrap->HostBuildID */
  NevercStringView TargetABIKey;
  uint32_t LLVMMajor;                 /* compare against Bootstrap->LLVMMajor   */
  uint32_t Reserved;
} NevercCompatibilityKey;
```

lockstep 註冊要求這三個欄位全部填妥；建置 ID 為空、ABI key 為空，或者 LLVM 主版本號
為零，都會被當作無效描述符拒絕。

## 建置

可以引入彙總標頭檔，也可以只引入你會用到的領域：

```c
#include "neverc/Plugin/NevercPluginAPI.h"   /* everything */
#include "neverc/Plugin/PluginIR.h"          /* or one domain */
```

用 NeverC 自己建置一個共享模組：

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

或者用 CMake 依據已安裝的 SDK 建置：

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

或者用 pkg-config：

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

請依宿主平台選用 `.so`、`.dylib` 或 `.dll`。這套 SDK 不連結任何 LLVM、也不連結
任何 NeverC 執行期——`NevercPluginSDK::headers` 是純標頭檔的。

## 載入與設定

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| 選項 | 形式 | 用途 |
|---|---|---|
| `-fplugin=<path>` | 可重複 | 載入一個全工具鏈外掛共享模組。 |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | 可重複 | 把一個帶命名空間的值傳給已註冊的外掛選項。 |
| `-fplugin-provider=<phase>:<plugin-id>` | 可重複 | 選擇由哪個外掛提供某個可替換階段。 |
| `-fplugin-pass=<dsopath>` | 可重複 | 載入一個 C-ABI 的樹外 pass 外掛。 |
| `-fplugin-pass-arg=<key>=<value>` | 可重複 | 傳一個引數給 C-ABI pass 外掛。 |

只有在恰好只有一個外掛處於啟用狀態時，才可以省略 `<plugin-id>:` 限定詞。外掛用
`RegisterOption` 註冊的選項，也可以直接依它宣告的拼法使用，支援 flag、joined、
separate 與多引數等形式。若外掛引數或 Provider 選擇沒有對應的 `-fplugin=`，那是
硬性錯誤，而不是靜默的空操作。

已註冊的選項隨時都能透過 core 表讀回：

```c
uint64_t Count = 0;
Core->GetPluginOptionValueCount(Core->Context, Session, PluginID,
                                SV("--driver-trace"), &Count);
NevercStringView Value;
Core->GetPluginOptionValue(Core->Context, Session, PluginID,
                           SV("--driver-trace"), 0, &Value);
```

## ABI 規則

- 透過 `QueryInterface` 查詢能力表；要求相符的 major，並在碰任何欄位之前檢查
  `StructSize`。
- 初始化每個公開結構的 `Header` 與保留儲存空間。先把結構清零，再設定
  `StructSize`、`Major`、`Minor` 與 `Flags`。
- 把控制代碼與借用視圖當成有範圍的不透明值。絕不要把 Task 範圍的控制代碼留到回呼
  之後，絕不要在另一個 session 或 task 中使用它，也絕不要自己捏造控制代碼值。
- 每個回呼都要回傳 `NevercStatus`。不要讓 C++ 例外或宿主擁有的指標越過 C 邊界。
- 宣告最狹窄且屬實的 `NevercConcurrencyModel`（`SESSION_SERIAL`、`THREAD_SAFE`、
  `PROCESS_SERIAL`）與 `NevercReentrancyModel`（`NONE`、`ALLOWED`）。
- IR、MIR、AST、圖與產物的變更都要透過交易式宿主 API 進行：開啟一次變更、暫存修
  改，然後提交或放棄。提交會原子地驗證並發布；提交失敗則保持先前狀態不變。
- 當你希望宿主把記憶體計入帳時，請透過 `NevercCoreAPI.Allocate` / `Reallocate` /
  `Deallocate` 配置。
- 把可變狀態放在宿主提供的 process/session/task 狀態中。全域可變狀態由
  `utils/plugin-api/check-global-state.py` 檢查。

所有公開結構都在 `NEVERC_ABI_PACK_BEGIN`（8 位元組對齊封裝）之下佈局，且只使用定
寬型別。新函式一律追加到各自獨立版本化的能力表末尾；在第一個 ABI major
（`NEVERC_PLUGIN_ABI_MAJOR` = 1）之內，表的穩定前綴不會改變。

## 狀態與診斷

`NevercStatus` 帶有一個 `Code`、一組 `Flags` 和一個 `Detail` 字。完整的代碼集合：

| 代碼 | 意義 |
|---|---|
| `NEVERC_STATUS_OK` | 成功。 |
| `NEVERC_STATUS_INVALID_ARGUMENT` | 必需的指標或值缺失或格式錯誤。 |
| `NEVERC_STATUS_ABI_MISMATCH` | 協商到的表太小，或 major 不一致。 |
| `NEVERC_STATUS_MISSING_INTERFACE` | 宿主未發布所請求的介面。 |
| `NEVERC_STATUS_VERSION_MISMATCH` | 無法滿足所請求的 major/minor。 |
| `NEVERC_STATUS_INVALID_DESCRIPTOR` | 描述子未通過結構驗證。 |
| `NEVERC_STATUS_DUPLICATE_ID` | 該 ID 已被註冊。 |
| `NEVERC_STATUS_DEPENDENCY_MISSING` | 所宣告的相依項不存在。 |
| `NEVERC_STATUS_DEPENDENCY_CYCLE` | 註冊順序無法滿足。 |
| `NEVERC_STATUS_BUSY` | 某項資源正被他處持有。 |
| `NEVERC_STATUS_CANCELLED` | 已請求協作式取消。 |
| `NEVERC_STATUS_RESOURCE_EXHAUSTED` | 觸及某個預算或上限。 |
| `NEVERC_STATUS_STALE_HANDLE` | 控制代碼的壽命超過了它所指的物件。 |
| `NEVERC_STATUS_WRONG_SESSION` | 控制代碼被用在了另一個 session。 |
| `NEVERC_STATUS_WRONG_SCOPE` | 控制代碼被用在了它的範圍之外。 |
| `NEVERC_STATUS_WRONG_TYPE` | 控制代碼指的是另一種實體。 |
| `NEVERC_STATUS_INVALID_STATE` | 在目前狀態下該操作不合法。 |
| `NEVERC_STATUS_POLICY_VIOLATION` | 階段 policy 禁止該操作。 |
| `NEVERC_STATUS_VERIFICATION_FAILED` | 密封的宿主驗證器拒絕了該產物。 |
| `NEVERC_STATUS_CAPABILITY_UNAVAILABLE` | 宿主在此處無法提供該能力。 |
| `NEVERC_STATUS_PLUGIN_FAILURE` | 外掛回報了一般性失敗。 |
| `NEVERC_STATUS_PLUGIN_EXCEPTION` | 有例外逸出了外掛回呼。 |
| `NEVERC_STATUS_OUTPUT_PARTIAL` | 輸出只寫出了一部分。 |
| `NEVERC_STATUS_REENTRANCY_DENIED` | 重入呼叫被拒絕。 |
| `NEVERC_STATUS_NOT_FOUND` | 具名實體不存在。 |

旗標位元描述輸出發生了什麼事，這正是建置系統判斷重試是否安全所需要的資訊：
`NEVERC_STATUS_FLAG_RECOVERABLE`、`_OUTPUT_ALREADY_COMMITTED`、
`_OUTPUT_MAY_BE_PARTIAL`、`_OUTPUT_RECOVERY_REQUIRED` 與
`_DURABILITY_UNCONFIRMED`。

回報問題請用 `NevercCoreAPI.EmitDiagnostic` 搭配一個
`NevercDiagnosticDescriptor`，它帶有嚴重度（`NOTE`、`REMARK`、`WARNING`、
`ERROR`、`FATAL`）、代碼、外掛 ID、階段 ID、訊息、註記、原始碼位置、範圍與
fix-it。在昂貴的工作之前請呼叫 `CheckCancelled`。

## 範例

一次建置全部：

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

每個範例都會被編譯兩次——一次用設定好的宿主 C 編譯器，一次用剛建置出來的
NeverC——因此 ABI 從兩側都得到了驗證。模組會落在
`build-neverc/neverc/pluginsdk/examples/host/`。

| 範例 | CMake 目標 | 展示內容 |
|---|---|---|
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | 選項註冊、階段觀察、job 攔截 |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | 提供記憶體內標頭檔的 VFS Provider |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | 剖析器攔截與原子式 AST 變更 |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | 用 value cursor 走訪函式清單的模組層級 IR pass |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | 一個穩定的 IR 函式 pass |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | 掛在 pre-emit hook 的穩定 MIR pass |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | 唯讀的 MC 發射事件 |
| `ObjectRewritePlugin.c` | `neverc-plugin-example-object-rewrite` | 交易式 ObjectGraph 改寫 |
| `CustomCallConvPlugin.c` | `neverc-plugin-example-custom-callconv` | 資料驅動的呼叫慣例 |
| `DynCodeTracePlugin.c` | `neverc-plugin-example-dyncode-trace` | 觀察 dyncode 流水線 |
| `DynCodeEncoderPlugin.c` | `neverc-plugin-example-dyncode-encoder` | 攔截 dyncode 字元集編碼 |
| `CrtShimPlugin.c` | `neverc-plugin-example-crt-shim` | 零 CRT 相依的外掛 |
| `BenchPlugin.c` | `neverc-plugin-example-abi-bench` | ABI 呼叫吞吐量微基準 |

載入其中一個：

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## 規範事實來源

| 檔案 | 保證內容 |
|---|---|
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | 階段 ID、policy、穩定性、驗證器 gate |
| `pluginsdk/manifest/plugin.json` | ABI 版本、介面 ID/版本/穩定性、schema 摘要、支援的目標 |
| `pluginsdk/abi/plugin.json` | 每個公開結構在各宿主 ABI key 下實測的大小、對齊與欄位位移 |
| `docs/plugin-api/coverage.json` | 把每個穩定階段對應到正向、負向、替換、觀察者與密封 gate 的測試 |

因此，一套 SDK 可以機械地對宿主做驗證，而一次外掛建置也可以對它將載入的那個 ABI
key 斷言自己的結構佈局。
