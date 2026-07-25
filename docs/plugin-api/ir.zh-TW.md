**語言**: [English](ir.md) | [简体中文](ir.zh-CN.md) | [繁體中文](ir.zh-TW.md) | [日本語](ir.ja.md) | [한국어](ir.ko.md) | [Français](ir.fr.md) | [Deutsch](ir.de.md) | [Español](ir.es.md) | [Italiano](ir.it.md) | [Русский](ir.ru.md) | [العربية](ir.ar.md)

# NeverC 外掛 IR API

首個公開外掛 ABI 透過穩定的 C 表格公開 LLVM IR。外掛不會納入 LLVM 標頭檔，也不得把
NeverC 控制代碼強制轉型為 LLVM 物件。

## 介面

在 `neverc_plugin_entry` 中以 `NevercBootstrapAPI.QueryInterface` 查詢介面：

- `NEVERC_INTERFACE_IR_CORE` —— 模組、型別、值、CFG、中繼資料、屬性、常數與序列化
  查詢。
- `NEVERC_INTERFACE_IR_BUILDER` —— 交易式 IR 建構與變更。
- `NEVERC_INTERFACE_IR_ANALYSIS` —— 內建分析與外掛自訂分析。
- `NEVERC_INTERFACE_IR_PASS` —— Module、CGSCC、Function 與 Loop pass。
- `NEVERC_INTERFACE_IR_GEN` —— 取代 SemanticUnit 到 IR 的降階過程。
- `NEVERC_INTERFACE_IR_OPTIMIZATION` —— 取代整條最佳化流水線。

務必請求標頭檔中的 major/minor 組合，並驗證回傳的 `StructSize` 已涵蓋外掛要呼叫的
最後一個函式指標。較新的主機可能會附加欄位；外掛必須忽略未知的尾端。

## 控制代碼與所有權

IR 控制代碼是限定於任務範圍的不透明 `{Owner, Value}` 配對。它們所參照的所有物件都
歸主機所有。

- 絕不在回呼或任務結束後繼續持有任務範圍的控制代碼。
- 絕不在另一個 session 或 task 中使用某個控制代碼。
- 一次已提交的取代，會使被取代物件的控制代碼失效。
- 一次中止的變更，會使該變更所建立的控制代碼過期。
- API 會回傳 `NEVERC_STATUS_STALE_HANDLE`、`WRONG_OWNER` 或 `WRONG_TYPE`，而不會
  暴露 LLVM 指標。

除非某個 API 明確回傳可釋放的緩衝區，否則查詢呼叫所回傳的字串與位元組視圖都是借用
而來。

## 讀取 IR

`NevercIRCoreAPI` 提供：

- 模組識別碼、triple、data layout 與內嵌組合語言；
- 針對函式、全域變數、區塊、指令、use 與運算元的穩定值游標；
- 穩定的型別 ID 與 opcode ID；
- 函式、全域變數、指令、中繼資料與屬性的各項性質；
- 整數、浮點數、聚合、null、poison 與 undef 常數；
- bitcode 匯出／匯入以及經過驗證的模組產物。

集合游標是有界的：傳入輸出容量，然後重複收集，直到回傳數量為零。

## 交易式變更

所有結構性變更都使用 `NevercIRBuilderAPI`：

1. 開啟一次模組層級或函式層級的變更。
2. 建立一個繫結到該變更的建構器。
3. 設定插入點，建構指令、函式或區塊。
4. 提交該變更。
5. 銷毀建構器與變更控制代碼。

提交會驗證候選 IR 並不可分割地發布它。驗證器失敗時，主機會回復該變更並保留原模組。
`AbortMutation` 一律會回復暫存的變動。

變更 IR 之後不要宣告 `NEVERC_IR_PRESERVE_ALL`。pass 轉接器會檢查模組世代，並拒絕
不一致的保留宣告。

## Pass 層級與階段

`NevercIRPassDescriptor.Level` 支援：

- `NEVERC_IR_PASS_LEVEL_MODULE`
- `NEVERC_IR_PASS_LEVEL_CGSCC`
- `NEVERC_IR_PASS_LEVEL_FUNCTION`
- `NEVERC_IR_PASS_LEVEL_LOOP`

穩定的插入階段為 `PRE_OPT`、`PIPELINE_START`、`OPTIMIZER_LAST`、`POST_OPT` 與
`PRE_CODEGEN`。每次呼叫只會包含對應層級有效的控制代碼。函式 pass 與迴圈 pass 可能
並行執行，因此可變的外掛狀態必須遵守所宣告的並行契約。

主機一律會執行最終的密封 IR 驗證器。外掛無法取代、攔截或略過這道 gate。

## 分析

內建分析 ID 涵蓋呼叫圖、支配樹、後支配樹、迴圈資訊、純量演化、MemorySSA 與別名
分析。

外掛分析必須宣告相依性與生命週期回呼。結果會按呼叫快取，並依 pass 的保留結果失效。
遞迴相依環，以及從分析回呼中發起變更，都會遭到拒絕。

## 完整 Provider

IR 產生 Provider 可以取代內建的降階過程，並發布一個經過驗證的模組產物。最佳化
Provider 則可以取代整條內建最佳化流水線。這兩條路線都必須：

- 消費明確的階段輸入；
- 透過主機 API 發布結果，而不是回傳 LLVM 指標；
- 驗證目標相容性與模組有效性；
- 發布失敗時不可分割地保留舊模組。

在最佳化 Provider 之後，最終驗證器依然是強制的。

## 最小範例

`pluginsdk/examples/FunctionPass.c` 是一個唯讀的函式 pass。
`pluginsdk/examples/ExamplePlugin.c` 展示模組列舉，
`pluginsdk/examples/CustomCallConvPlugin.c` 則示範屬性與呼叫點性質。

建置並載入一個範例：

```sh
cmake --build build-neverc --target neverc-plugin-example-function-pass
build-neverc/bin/neverc \
  -fplugin=build-neverc/neverc/pluginsdk/examples/host/FunctionPass.so \
  -O2 -c input.c -o input.o
```

請使用 CMake 為當前平台產生的模組副檔名。

## 失敗規則

每個回呼都要回傳 `NevercStatus`。外掛失敗會轉為結構化診斷；不要讓例外穿越 C 邊界。
請初始化每一個輸出表頭與保留欄位，並在必要指標缺漏時回傳 `INVALID_ARGUMENT`。

規範性的 ABI 宣告、階段策略與測試證據，請見 `PluginIR.h`、`PluginPhaseSchema.h`
與 `coverage.json`。
