**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# NeverC 外掛 ABI

NeverC 的首個公開外掛 ABI 是純 C、以階段（phase）為核心的介面。外掛是一個共享
模組，只匯出一個函式，協商版本化的能力表，並在明確的 Process、Session、Task
範圍中執行。它不包含任何 LLVM 標頭檔，不連結編譯器，也不在邊界上傳遞任何 C++
型別。

未發布的原型 API 及其 `nevercGetPluginInfo` 進入點已被移除。原型二進位檔會收到
遷移診斷；請使用公開標頭檔重新編譯其原始碼。完整的舊→新對應請見
[從原型 API 遷移](migration-from-prototype.zh-TW.md)。

## 文件入口

- [Source 與 I/O API](source.zh-TW.md)
- [前置處理器 API](prep.zh-TW.md)
- [AST 與語意 API](ast-sema.zh-TW.md)
- [IR API](ir.zh-TW.md)
- [MIR API](mir.zh-TW.md)
- [Target、MC、組合語言與目的檔 API](target-mc-object.zh-TW.md)
- [DynCode API](dyncode.zh-TW.md)
- [自訂呼叫慣例](custom-callconv/README.zh-TW.md)
- [從原型 API 遷移](migration-from-prototype.zh-TW.md)
- [階段涵蓋範圍證據](coverage.json)

## 執行模型

宿主透過三層巢狀範圍驅動外掛。每層範圍都會交給外掛一個不透明的狀態指標，由外掛
自行配置並擁有——因此正確撰寫的外掛不需要任何全域可變狀態。

| 範圍 | 回呼 | 意義 |
|---|---|---|
| Process | `ProcessBegin`、`Register`、`Destroy` | 一個編譯器行程。在此查詢介面並註冊能力。 |
| Session | `SessionBegin`、`SessionEnd` | 一次驅動程式呼叫。 |
| Task | `TaskBegin`、`TaskEnd` | 一個工作單元，由 `NevercTaskKind` 識別。 |

Task 種類有 `INVOCATION`、`TRANSLATION_UNIT`、`LTO`、`LINK`、`CODEGEN`、
`OBJECT` 與 `DYNCODE`。

宿主先呼叫 `ProcessBegin`，接著恰好呼叫一次 `Register`。註冊是唯一可以加入選項、
觀察者、攔截器與 Provider 的地方；之後階段圖即被凍結。

## 階段（Phase）

階段是一個具名、帶版本的轉換：從輸入產物到輸出產物。NeverC 內建 **130 個階段**，
涵蓋 driver、source、前置處理器、語法、語意、IR、codegen、MIR、MC、組合語言、
目的檔、連結與 dyncode 各領域，另有 8 個保留給外掛自訂階段的擴充 ID 家族。

每個階段宣告自己的 policy，外掛只能以該 policy 允許的方式接入：

| Policy 旗標 | 外掛可以做什麼 |
|---|---|
| `NEVERC_PHASE_OBSERVABLE` | 註冊觀察者，以唯讀方式接收通知。 |
| `NEVERC_PHASE_INTERCEPTABLE` | 包裹該階段，自行決定是否呼叫鏈上其餘部分。 |
| `NEVERC_PHASE_REPLACEABLE` | 註冊 Provider，由外掛自己產出輸出。 |
| `NEVERC_PHASE_SKIPPABLE_WITH_PROOF` | 在提供 proof handle 的前提下跳過該轉換。 |
| `NEVERC_PHASE_SEALED_HOST_GATE` | 什麼都不能做。驗證器與提交由宿主獨佔，不可替換、攔截或跳過。 |

觀察者會在階段宣告的時機被送達：`NEVERC_OBSERVER_BEFORE`、
`NEVERC_OBSERVER_AFTER` 與 `NEVERC_OBSERVER_AFTER_COMMIT`。

攔截器會收到一個 `NevercPhaseContinuation`。它必須**最多呼叫一次**
`InvokeNext`，且必須在回呼執行緒上呼叫，然後在 `NevercPhaseResult.Action` 中
回報 `NEVERC_PHASE_CONTINUE`、`NEVERC_PHASE_REPLACE` 或 `NEVERC_PHASE_SKIP`
其中之一。

`neverc/include/neverc/Plugin/Schema/PhaseSchema.json` 是階段 ID、policy、
穩定性層級與驗證器 gate 的規範事實來源。產生出的 `PluginPhaseSchema.inc` 會將
它們公開為編譯期常數，例如 `NEVERC_PHASE_IR_PASS_PRE_OPT_HIGH` / `_LOW`。

## 一個完整的最小外掛

這是 `pluginsdk/templates/minimal/Plugin.c`。它可以載入、協商 ABI、不註冊任何
東西、乾淨卸載——複製該目錄即可在此基礎上擴充。

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
  /* 在這裡註冊選項、觀察者、攔截器或 Provider。 */
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

`OutPlugin` 是呼叫方擁有的緩衝區。進入時 `Header.StructSize` 表示可寫容量；
外掛最多寫入這麼多位元組，並回報自己實際產出的大小。

## 介面協商

能力表以 128 位元介面 ID 取得，而非以符號取得。請求你編譯時所用的 major，以及
你能接受的最低 minor：

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

將 `TableSize` 與你要呼叫的最後一個函式的位移做比較，正是讓這套 ABI 可擴充的
規則：新版宿主在尾端追加欄位，而舊外掛依然可用，因為它從不讀取自己驗證過的
前綴之外的內容。`NEVERC_ABI_FIELD_AVAILABLE(header, type, field)` 巨集對你收到
的結構施加同樣的檢查。

公開介面與其標頭檔：

| 介面 | 表 | 標頭檔 |
|---|---|---|
| `NEVERC_INTERFACE_CORE` | `NevercCoreAPI` | `PluginCore.h` |
| `NEVERC_INTERFACE_DRIVER` | `NevercDriverAPI` | `PluginDriver.h` |
| `NEVERC_INTERFACE_IO`、`..._SOURCE_LOCATION` | `NevercIOAPI`、`NevercSourceLocationAPI` | `PluginSource.h` |
| `NEVERC_INTERFACE_PREP` | `NevercPrepAPI` | `PluginPrep.h` |
| `NEVERC_INTERFACE_AST`、`..._PARSER` | `NevercASTAPI`、`NevercParserAPI` | `PluginAST.h` |
| `NEVERC_INTERFACE_SEMA` | `NevercSemaAPI` | `PluginSema.h` |
| `NEVERC_INTERFACE_IR_CORE`、`..._BUILDER`、`..._ANALYSIS`、`..._PASS`、`..._GEN`、`..._OPTIMIZATION` | IR 各表 | `PluginIR.h` |
| `NEVERC_INTERFACE_TARGET`、`..._TARGET_ABI`、`..._CALLING_CONVENTION` | Target 各表 | `PluginTarget.h` |
| `NEVERC_INTERFACE_MIR`、`..._MIR_ANALYSIS`、`..._MIR_PASS`、`..._MIR_PROVIDER` | MIR 各表 | `PluginMIR.h` |
| `NEVERC_INTERFACE_MC`、`..._MC_EMISSION`、`..._MC_PROVIDER`、`..._ASSEMBLY_PROVIDER` | MC 各表 | `PluginMC.h` |
| `NEVERC_INTERFACE_OBJECT`、`..._OBJECT_FORMAT`、`..._OBJECT_PHASE` | Object 各表 | `PluginObject.h` |
| `NEVERC_INTERFACE_LINK`、`..._LINK_REGISTRAR`、`..._LINK_PHASE` | Link 各表 | `PluginLink.h` |
| `NEVERC_INTERFACE_LTO`、`..._LTO_REGISTRAR` | LTO 各表 | `PluginLTO.h` |
| `NEVERC_INTERFACE_DYNCODE`、`..._DYNCODE_REGISTRAR`、`..._DYNCODE_PHASE` | DynCode 各表 | `PluginDynCode.h` |

介面不是 STABLE（新版宿主只能追加）就是 LOCKSTEP（目標相關的 schema，必須完全
相符）。在消費 LOCKSTEP 值之前必須比較 schema digest。

## 建置

包含彙總標頭檔，或只包含你用到的領域：

```c
#include "neverc/Plugin/NevercPluginAPI.h"
```

用 NeverC 本身建置共享模組：

```sh
neverc --target=arm64-apple-macosx -shared \
  -I/path/to/pluginsdk/include \
  -o MyPlugin.dylib MyPlugin.c
```

或以 CMake 對接已安裝的 SDK：

```cmake
find_package(NevercPluginSDK REQUIRED)
add_library(my_plugin MODULE my_plugin.c)
target_link_libraries(my_plugin PRIVATE NevercPluginSDK::headers)
```

或使用 pkg-config：

```sh
cc -shared $(pkg-config --cflags neverc-plugin) -o my_plugin.so my_plugin.c
```

依宿主平台選用 `.so`、`.dylib` 或 `.dll`。SDK 不連結 LLVM，也不連結 NeverC
執行期——`NevercPluginSDK::headers` 是純標頭檔目標。

## 載入與設定

```sh
neverc -fplugin=./MyPlugin.dylib -c input.c -o input.o
```

| 選項 | 形式 | 用途 |
|---|---|---|
| `-fplugin=<path>` | 可重複 | 載入外掛共享模組。 |
| `-fplugin-arg=<plugin-id>:<key>=<value>` | 可重複 | 向已註冊的外掛選項傳遞帶命名空間的值。 |
| `-fplugin-provider=<phase>:<plugin-id>` | 可重複 | 選擇由哪個外掛提供某個可替換階段。 |

只有在恰好啟用一個外掛時，才可以省略 `<plugin-id>:` 限定詞。外掛以
`RegisterOption` 註冊的選項也可以直接依其宣告的拼寫使用，支援 flag、joined、
separate 與多引數形式。沒有 `-fplugin=` 卻給出外掛引數或 Provider 選擇屬於硬
錯誤，而非靜默忽略。

## ABI 規則

- 透過 `QueryInterface` 查詢能力表；要求 major 相符，並在存取欄位前檢查
  `StructSize`。
- 初始化每個公開結構的 `Header` 與保留儲存空間。先將結構歸零，再設定
  `StructSize`、`Major`、`Minor` 與 `Flags`。
- 把 handle 與借用視圖當作有範圍的不透明值。絕不在回呼之外保留任務範圍的
  handle，絕不在另一個 session 或 task 中使用它，也絕不自行偽造 handle 值。
- 每個回呼都回傳 `NevercStatus`。不要讓 C++ 例外或宿主擁有的指標越過 C 邊界。
- 宣告**最窄且真實**的 `NevercConcurrencyModel`（`SESSION_SERIAL`、
  `THREAD_SAFE`、`PROCESS_SERIAL`）與 `NevercReentrancyModel`（`NONE`、
  `ALLOWED`）。
- IR、MIR、AST、圖與產物的修改一律走交易式宿主 API：開啟 mutation，暫存變更，
  然後 commit 或 abort。commit 會驗證並以不可分割的方式發布；失敗的 commit 會
  保持先前狀態不變。
- 把可變狀態放在宿主提供的 process/session/task 狀態中。全域可變狀態由
  `utils/plugin-api/check-global-state.py` 檢查。

新函式只會追加到各自獨立版本化的能力表尾端。在首個 ABI major
（`NEVERC_PLUGIN_ABI_MAJOR` = 1）內，表的穩定前綴不會改變。

## 狀態與診斷

`NevercStatus` 攜帶 `Code`、`Flags` 與一個 `Detail` 字組。常見狀態碼：

| 狀態碼 | 意義 |
|---|---|
| `NEVERC_STATUS_OK` | 成功。 |
| `NEVERC_STATUS_INVALID_ARGUMENT` | 缺少必要指標或值，或格式不合法。 |
| `NEVERC_STATUS_ABI_MISMATCH` | 協商到的表太小，或 major 不一致。 |
| `NEVERC_STATUS_MISSING_INTERFACE` / `CAPABILITY_UNAVAILABLE` | 宿主不提供所請求的能力。 |
| `NEVERC_STATUS_STALE_HANDLE` / `WRONG_SESSION` / `WRONG_SCOPE` / `WRONG_TYPE` | handle 在其有效範圍之外被使用。 |
| `NEVERC_STATUS_POLICY_VIOLATION` | 該階段的 policy 不允許此操作。 |
| `NEVERC_STATUS_VERIFICATION_FAILED` | 宿主的密封驗證器拒絕了產物。 |
| `NEVERC_STATUS_CANCELLED` / `BUSY` / `RESOURCE_EXHAUSTED` | 協作式取消或資源限制。 |

旗標位元（`RECOVERABLE`、`OUTPUT_ALREADY_COMMITTED`、`OUTPUT_MAY_BE_PARTIAL`、
`OUTPUT_RECOVERY_REQUIRED`、`DURABILITY_UNCONFIRMED`）描述輸出發生了什麼，
這正是建置系統判斷「重試是否安全」所需要的資訊。

以 `NevercCoreAPI.EmitDiagnostic` 搭配 `NevercDiagnosticDescriptor` 回報問題，
其中攜帶嚴重程度、代碼、外掛 ID、階段 ID、訊息、註記、原始碼位置、範圍與
fix-it。在執行昂貴工作前呼叫 `CheckCancelled`。

## 範例

建置全部範例：

```sh
cmake --build build-neverc --target neverc-pluginsdk-examples
```

每個範例都會被編譯兩次——一次用設定的宿主 C 編譯器，一次用剛建置出的 NeverC——
因而從兩側共同證明 ABI 的正確性。模組會產生在
`build-neverc/neverc/pluginsdk/examples/host/`。

| 範例 | CMake target | 展示內容 |
|---|---|---|
| `DriverTracePlugin.c` | `neverc-plugin-example-driver-trace` | 選項註冊、階段觀察、job 攔截 |
| `VirtualHeaderPlugin.c` | `neverc-plugin-example-virtual-header` | 提供記憶體內標頭檔的 VFS provider |
| `ASTRewritePlugin.c` | `neverc-plugin-example-ast-rewrite` | 剖析器攔截與不可分割的 AST 變更 |
| `ExamplePlugin.c` | `neverc-plugin-example-ir-overview` | 模組層級 IR pass，以值游標走訪函式清單 |
| `FunctionPass.c` | `neverc-plugin-example-function-pass` | 穩定的 IR function pass |
| `MachinePass.c` | `neverc-plugin-example-machine-pass` | pre-emit 掛鉤上的穩定 MIR pass |
| `MCObserverPlugin.c` | `neverc-plugin-example-mc-observer` | 唯讀的 MC 發射事件 |
| `ObjectRewritePlugin.c` | `neverc-plugin-example-object-rewrite` | 交易式 ObjectGraph 改寫 |
| `CustomCallConvPlugin.c` | `neverc-plugin-example-custom-callconv` | 資料驅動的呼叫慣例 |
| `DynCodeTracePlugin.c` | `neverc-plugin-example-dyncode-trace` | 觀察 dyncode 流水線 |
| `DynCodeEncoderPlugin.c` | `neverc-plugin-example-dyncode-encoder` | 攔截 dyncode 字元集編碼 |
| `CrtShimPlugin.c` | `neverc-plugin-example-crt-shim` | 零 CRT 相依的外掛 |
| `BenchPlugin.c` | `neverc-plugin-example-abi-bench` | ABI 呼叫吞吐量微基準 |

載入其中之一：

```sh
neverc -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

## 規範事實來源

| 檔案 | 保證的內容 |
|---|---|
| `neverc/include/neverc/Plugin/Schema/PhaseSchema.json` | 階段 ID、policy、穩定性、驗證器 gate |
| `pluginsdk/manifest/plugin.json` | ABI 版本、介面 ID/版本/穩定性、schema digest、支援的目標 |
| `pluginsdk/abi/plugin.json` | 每個公開結構在各宿主 ABI key 下實測的大小、對齊與欄位位移 |
| `docs/plugin-api/coverage.json` | 將每個穩定階段對應到正例、反例、替換、觀察者與密封 gate 測試 |

因此 SDK 可以對宿主進行機器化驗證，外掛的建置也可以針對它將要載入的 ABI key
斷言自己的結構佈局。
