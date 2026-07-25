**語言**: [English](migration-from-prototype.md) | [简体中文](migration-from-prototype.zh-CN.md) | [繁體中文](migration-from-prototype.zh-TW.md) | [日本語](migration-from-prototype.ja.md) | [한국어](migration-from-prototype.ko.md) | [Français](migration-from-prototype.fr.md) | [Deutsch](migration-from-prototype.de.md) | [Español](migration-from-prototype.es.md) | [Italiano](migration-from-prototype.it.md) | [Русский](migration-from-prototype.ru.md) | [العربية](migration-from-prototype.ar.md)

# 從原型外掛 API 遷移

未發布的原型外掛 API —— 包含 `nevercGetPluginInfo` 進入點、單一的 `NevercHostAPI`
vtable、`Register*Pass` 呼叫、`NEVERC_INTERPOSE_*` 掛鉤，以及 `-fplugin-pass=`
載入器 —— 已於首次公開發行前全部移除。第一版公開 ABI 是
[README.md](README.md) 中記載的階段式描述符 ABI：外掛匯出 `neverc_plugin_entry`，
並協商各自獨立版本化的能力表。

不存在相容層，也沒有 `v1`/`v2` 之分。請針對公開標頭重新編譯外掛*原始碼*；本頁將每一項
原型構件對應到其第一版的替代物、語意變更，或明確不予沿用的說明。

## 原型二進位檔會被拒絕

載入原型共享物件會失敗，並輸出穩定的診斷訊息：

```
plugin exports the removed 'nevercGetPluginInfo' prototype ABI; migrate it to
the first public descriptor ABI and export 'neverc_plugin_entry'
```

兩個進入點都未匯出的程式庫則以 `plugin has no 'neverc_plugin_entry' entry` 失敗。
在原始碼完成移植之前，不會載入任何東西。

## 進入點

| 原型 | 第一版公開 ABI |
|---|---|
| `NevercPluginInfo nevercGetPluginInfo(void)` | `NevercStatus NEVERC_CALL neverc_plugin_entry(const NevercBootstrapAPI *Bootstrap, NevercPluginDescriptor *OutPlugin)` |

進入點不再以值*回傳*結構體。它填寫呼叫端提供的 `NevercPluginDescriptor`，並遵守
`OutPlugin->Header.StructSize`，然後回傳 `NevercStatus`。在宣告支援某項能力之前，
請先從 `Bootstrap` 查詢所需的能力表。

## `NevercPluginInfo` 欄位

| 原型欄位 | 第一版對應 |
|---|---|
| `APIVersion` | `Descriptor.Header`（帶有 `StructSize`、`NEVERC_PLUGIN_ABI_MAJOR`、`NEVERC_PLUGIN_ABI_MINOR` 的 `NevercABITableHeader`） |
| `PluginName` | `Descriptor.DisplayName`（`NevercStringView`），外加用於索引各作用域狀態的穩定反向 DNS 格式 `Descriptor.PluginID` |
| `PluginVersion` | `Descriptor.Version`（`NevercSemanticVersion`） |
| `RegisterPasses(API, Reg)` | `Descriptor.Register(Core, Registrar, RegistrarContext, ProcessState)`，以及生命週期回呼 `ProcessBegin`、`SessionBegin`/`SessionEnd`、`TaskBegin`/`TaskEnd` |
| `Destroy()` | `Descriptor.Destroy(Core, ProcessState)` |
| *（原型無對應）* | 必須據實宣告 `Descriptor.Concurrency` 與 `Descriptor.Reentrancy`（例如 `NEVERC_CONCURRENCY_SESSION_SERIAL`、`NEVERC_REENTRANCY_ALLOWED`） |

## 存取宿主：單一 vtable → 能力表

原型會把一份超過 200 個項目的 `NevercHostAPI` vtable 傳給每個回呼，並以
`NEVERC_API_FN` 保護新欄位。第一版改以各自獨立版本化、按需查詢的能力表取代：

```c
NevercInterfaceID Driver = { NEVERC_INTERFACE_DRIVER_HIGH,
                             NEVERC_INTERFACE_DRIVER_LOW };
const void *Table = NULL;
uint16_t Minor = 0;
uint64_t TableSize = 0;
NevercStatus S = Bootstrap->QueryInterface(
    Bootstrap->Context, Driver, NEVERC_DRIVER_API_MAJOR,
    NEVERC_DRIVER_API_MINOR, &Table, &Minor, &TableSize);
```

請要求相符的主版本，並在讀取欄位前以 `offsetof` 檢查 `TableSize`。介面依網域劃分：
Core、Driver、Source、Prep、AST、Sema、IR、MIR、Target、MC、Object、Link、LTO
與 DynCode。

## 註冊：`Register*Pass` + 掛鉤 → 觀察者／攔截器／提供者

原型的註冊是把回呼掛到掛鉤上：

```c
API->RegisterModulePass(Reg, NEVERC_INTERPOSE_PRE_OPT, myPass, ud, "my-pass");
```

第一版則是在 `Register` 內，針對以 128 位元 `NevercInterfaceID` 標識的階段註冊型別化
處理常式：

| 原型呼叫 | 第一版註冊器呼叫 |
|---|---|
| 唯讀 pass | 指定 `NEVERC_OBSERVER_BEFORE`／`NEVERC_OBSERVER_AFTER` 觀察點的 `Registrar->RegisterObserver(NevercObserverDescriptor)` |
| 包裹或短路階段的 pass | `Registrar->RegisterInterceptor(NevercInterceptorDescriptor)`；`Continuation->InvokeNext` 最多呼叫一次，並設定 `OutResult->Action` |
| 取代內建轉換的 pass | 在 `REPLACEABLE` 階段上呼叫 `Registrar->RegisterProvider(...)` |
| 讀取 `-fplugin-pass-arg=` | 以 `Registrar->RegisterOption(NevercOptionDescriptor)` 宣告真正的驅動器選項 |

原型中「位於 `PRE_OPT` 的模組 pass」會變成 IR 階段 `neverc.ir.pass.pre_opt` 上的
觀察者、攔截器或提供者。

## 掛鉤 → 階段對應

| 原型掛鉤 | 第一版階段（名稱） |
|---|---|
| `NEVERC_INTERPOSE_PRE_OPT` | `neverc.ir.pass.pre_opt` |
| `NEVERC_INTERPOSE_POST_OPT` | `neverc.ir.pass.post_opt` |
| `NEVERC_INTERPOSE_PIPELINE_START` | `neverc.ir.pass.pipeline_start` |
| `NEVERC_INTERPOSE_PIPELINE_LAST` | `neverc.ir.pass.optimizer_last` |
| `NEVERC_INTERPOSE_BEFORE_CODEGEN_PREEMIT` | `neverc.mir.pass.preemit` |
| `NEVERC_INTERPOSE_AFTER_CODEGEN_FINAL_MIR` | `neverc.mir.pass.final` |
| `NEVERC_INTERPOSE_LTO_PRE_OPT` / `LTO_POST_OPT` | LTO 階段 `neverc.link.lto_resolve` / `neverc.link.lto_generate`（見 [mir.md](mir.md)） |
| `NEVERC_INTERPOSE_LINK_PRE_LAYOUT` / `LINK_POST_LAYOUT` | 於 `BEFORE` / `AFTER` 觀察的 `neverc.link.layout` |
| `NEVERC_INTERPOSE_LINK_POST_EMIT` | `neverc.link.post_emit` |
| `NEVERC_INTERPOSE_SC_*`（dyncode） | [dyncode.md](dyncode.md) 中型別化的 dyncode 階段 |

階段 ID、政策、穩定性層級與驗證閘門的規範性清單位於
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`；可執行的覆蓋率契約則是
[coverage.json](coverage.json)。過去屬於單一觀察點的掛鉤，可能對應到多個階段 ID，
各自有其政策與證明。

## Pass 回呼、控制代碼與位元組編輯

| 原型 | 第一版 |
|---|---|
| `NevercModulePassFn(NevercModuleRef, API, ud)` 之類 | 回呼收到 `NevercPhaseFrame`；IR/MIR/AST/圖物件都是從對應能力表取得的型別化、有作用域的不透明控制代碼（見 [ir.md](ir.md)、[mir.md](mir.md)、[ast-sema.md](ast-sema.md)、[target-mc-object.md](target-mc-object.md)） |
| 泛用的 `NevercValueRef` | 已移除，改用型別化的 IR 控制代碼 |
| 對存活 `Ref` 的原地修改 | 所有變更都必須經由交易式宿主 API |
| `NevercBinaryPassFn(uint8_t **Data, uint64_t *Len, ...)` | 已移除；dyncode 的位元組編輯改用有檢查的映像建構器（read/write/insert/append/resize），見 [dyncode.md](dyncode.md) |

控制代碼與借用檢視僅在回呼作用域內有效，與先前完全相同；回呼返回後請勿快取它們。

## 已移除的便利層

原型把通用輔助工具打包進 vtable。這些**不屬於**第一版公開 ABI：

| 原型 | 第一版 |
|---|---|
| `ArenaCreate` / `StrMapCreate` / `IntMapCreate` / `StrBuilderCreate` / `ValueSetCreate` | 不予沿用；請使用 `Core->Allocate`／`Core->Deallocate` 搭配自有容器，或改用型別化的網域 API |
| `NEVERC_FOR_EACH_*` / `NEVERC_COLLECT_*` 巨集 | 由各網域能力表中的型別化迭代取代 |
| `API->PluginGetArg` / `-fplugin-pass-arg=` | 以 `RegisterOption` 宣告選項，並透過 Driver API 讀取 |
| `DiagNoteF` / `DiagWarningF` / `DiagErrorF` | `Core->EmitDiagnostic(NevercDiagnosticDescriptor)` |

## 載入與命令列

| 原型 | 第一版 |
|---|---|
| `-fplugin-pass=<path>` | `-fplugin=<path>` |
| `-fplugin-pass-arg=key=value` | 你在 `RegisterOption` 中宣告的選項拼寫（例如 `--driver-trace` 或 `--my-opt=value`） |
| 兩套載入器（`-fplugin` 與 `-fplugin-pass`） | 單一載入器；一個模組只交給一個載入器 |

## 版本控管

原型依賴單一且單調增長的 vtable 加上 `NEVERC_API_FN` 防護。第一版中每個能力表各自
版本化：請要求相符的主版本，並在讀取新增欄位前檢查 `StructSize`／`TableSize`。在第一個
ABI 主版本內，新函式一律附加在能力表的穩定前綴之後，因此針對較早次版本建置的外掛
在較新的宿主上仍可繼續運作。

## 完整範例

`pluginsdk/examples/DriverTracePlugin.c` 展示了第一版的完整樣貌：
`neverc_plugin_entry` 描述符、`ProcessBegin`／`Session`／`Task` 生命週期、為真正 CLI
旗標所做的 `RegisterOption`、在 `neverc.driver.raw_arguments` 上的
`RegisterObserver`，以及在 `neverc.driver.execute_job` 上、恰好呼叫一次 `InvokeNext`
的 `RegisterInterceptor`。`pluginsdk/examples/ExamplePlugin.c` 則涵蓋 IR、MIR、
object 與 link 各階段。
