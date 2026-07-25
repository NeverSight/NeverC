**語言**: [English](mir.md) | [简体中文](mir.zh-CN.md) | [繁體中文](mir.zh-TW.md) | [日本語](mir.ja.md) | [한국어](mir.ko.md) | [Français](mir.fr.md) | [Deutsch](mir.de.md) | [Español](mir.es.md) | [Italiano](mir.it.md) | [Русский](mir.ru.md) | [العربية](mir.ar.md)

# NeverC 外掛 MIR API

首個公開外掛 ABI 透過 `PluginMIR.h` 公開 Machine IR。此 API 使用穩定的 C 識別字與
不透明控制代碼；外掛不依賴 LLVM 的類別佈局、列舉數值或 C++ ABI。

## 協商

查詢 `NEVERC_INTERFACE_MIR` 取得 `NevercMIRAPI`，查詢
`NEVERC_INTERFACE_MIR_PASS` 取得 `NevercMIRPassAPI`。在使用任何函式指標之前，先檢查
回傳的表格大小，並忽略較新主機所附加的欄位。

schema 摘要可辨識目前使用的穩定 ID 對主機的確切對應。`GetEntityInfo`、
`GetOperandKindInfo`、`GetGenericOpcodeInfo` 與 `GetMachinePropertyInfo` 會提供正規
名稱，以及某項操作是否需要目標 schema。

## 穩定模型

不透明控制代碼代表：

- machine function 與 basic block；
- machine instruction 與 operand；
- 變更交易；
- 分析結果；
- 常數池項目、堆疊框物件、跳躍表、記憶體運算元與目標參考。

控制代碼屬於單一程式碼產生任務。被抹除的實體、被回復的實體，以及因變更而失效的分析
結果，都會成為過期的控制代碼。

通用 schema 涵蓋與目標無關的 opcode、運算元種類、machine property、低階型別、指令
旗標、暫存器配置、堆疊框物件、常數、跳躍表、記憶體指標形式與原子順序。與目標相關的
opcode 則需要明確協商過的目標 schema。

## 讀取 MIR

`NevercMIRAPI` 支援：

- machine function 屬性與基本區塊走訪；
- 前驅、後繼、live-in、指令與運算元列舉；
- 指令 opcode 與旗標查詢；
- 所有公開的 machine operand 形式；
- 虛擬暫存器與實體暫存器資訊；
- 堆疊框、常數池、跳躍表與記憶體運算元狀態。

請使用「計數／查詢」成對呼叫與有界輸出緩衝區。除非另有說明，回傳的視圖僅在目前回呼
期間被借用。

## 交易式變更

MIR 的變動在變更租約（mutation lease）之下進行：

1. 對某個 machine function 呼叫 `BeginMutation`。
2. 建立、移動或抹除基本區塊與指令。
3. 附加或更新運算元與 CFG 邊。
4. 攜帶必要的證明來套用 machine property 變更。
5. `CommitMutation` 或 `AbortMutation`。

提交會執行結構性預檢與 Machine IR 驗證。不合法的運算元、CFG、通用 opcode 用法或屬性
宣告都會被不可分割地回復。中止則會還原基本區塊順序、指令、運算元、CFG 邊與 machine
property。

屬性變更使用 `NevercMIRPropertyProof`。證明必須讓一個前提已不再成立的屬性失效，或在
建立該屬性之前要求一次結構性檢查。

## Pass 與階段

`NevercMIRPassDescriptor.Level` 支援 MachineModule、MachineFunction 與
MachineBasicBlock 轉接器。穩定的掛鉤點為：

- 指令選擇之後；
- legalization 之後；
- 排程器之前／之後；
- 暫存器配置之前／之後；
- prologue/epilogue 之後；
- pre-emit；
- 最後的外掛槽位。

function pass 可能在並行的程式碼產生分區中執行。模組層級的 pass 則在序列化的流水線
屏障處執行。外掛所宣告的並行性與可重入性依然適用。

每條程式碼產生流水線，都會在最後的外掛槽位之後，以主機擁有的 `MachineVerifier`
收尾。它是密封 gate，外掛無法停用。

## 分析

分析表公開活躍變數、活躍區間、槽索引、支配樹、迴圈資訊與暫存器壓力。可用與否取決於
所選的掛鉤點，因為某些 LLVM 分析在其原生流水線階段之前或之後並不存在。

請在 pass 描述子中宣告所需與所保留的分析。一次成功提交的變更，會使受影響的結果控制
代碼失效。變更之後再宣告 preserve-all 會被拒絕。

## 最小範例

`pluginsdk/examples/MachinePass.c` 在穩定的 pre-emit 掛鉤點註冊一個唯讀的
machine-function pass。

```sh
cmake --build build-neverc --target neverc-plugin-example-machine-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/MachinePass.so \
  -O2 -fno-lto -c input.c -o input.o
```

請使用 CMake 為當前平台產生的模組副檔名。

## 安全要求

- 不要在回呼之後繼續持有任務控制代碼、MIR 控制代碼或借用的視圖。
- 不要偽造控制代碼值或 LLVM opcode 數值。
- 不要在租約之外進行變更。
- 初始化表頭與保留儲存空間。
- 跨 C 邊界回傳狀態；絕不讓 C++ 例外穿越它。

規範性宣告與涵蓋率證據，請見 `PluginMIR.h`、`MIRSchema.json`、
`PluginPhaseSchema.h` 與 `coverage.json`。
