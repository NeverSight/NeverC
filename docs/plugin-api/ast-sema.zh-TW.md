**語言**: [English](ast-sema.md) | [简体中文](ast-sema.zh-CN.md) | [繁體中文](ast-sema.zh-TW.md) | [日本語](ast-sema.ja.md) | [한국어](ast-sema.ko.md) | [Français](ast-sema.fr.md) | [Deutsch](ast-sema.de.md) | [Español](ast-sema.es.md) | [Italiano](ast-sema.it.md) | [Русский](ast-sema.ru.md) | [العربية](ast-sema.ar.md)

# AST、剖析器與語意外掛 API

`PluginAST.h` 與 `PluginSema.h` 提供任務範圍的純 C 存取方式，用來操作前端語法樹與
語意流水線。穩定的節點、屬性與子槽 ID 由 NeverC 的具體 AST 定義產生；外掛永遠不會
取得 C++ 的 `Decl`、`Stmt`、`Type` 或 `Sema` 指標。

## 讀取與建構 AST 節點

使用 `NevercASTAPI` 查詢節點資訊、schema 屬性、子節點、父節點、宣告上下文、型別、
屬性，以及常見具體節點的細節。批次 API 必須明確給定元素數量、容量與間距。

`NevercASTBuilder` 只會建構 schema 中宣告過的節點種類。必要的屬性與子槽會在提交時
接受驗證。提交成功會發布一個由任務擁有的節點；提交失敗則不會留下任何部分可見的
節點。無論提交成功或失敗，都必須銷毀每一個建構器。

## 不可分割的變更

AST 變更透過 `BeginASTMutation`、暫存操作與 `CommitASTMutation` 完成。主機會在變動
語法樹之前，驗證歸屬、槽位相容性、基數、父連結、環路與語意不變式。
`AbortASTMutation` 會丟棄所有暫存操作。原生的 `TreeMutationListener` 通知只在提交
成功之後才送出。

可建置的 [`ASTRewritePlugin.c`](../../pluginsdk/examples/ASTRewritePlugin.c)
展示了一個剖析器攔截器：它呼叫內建剖析器，建構一個整數字面值，然後不可分割地取代
變數初始設定式。

## 取代剖析器與 Sema

`neverc.syntax.parse` 把已驗證的 token 串流對應為 `ASTUnit`。`neverc.sema.analyze`
把 AST 產物對應為 `SemanticUnit`。這兩個階段都具備具型別的攔截器與 Provider。若只想
取代前端的一部分，宣告、陳述式、運算式、型別名稱、屬性、查找、轉換與關鍵字等細緻的
擴充階段依然可用。

內建的融合式 parser/Sema 路徑發布的產物契約，與取代實作完全相同。語意重播只接受那些
NeverC 能夠重建範圍、名稱查找、重複宣告與型別檢查狀態的節點種類。遇到不支援的具體
種類時會回傳 `NEVERC_STATUS_UNSUPPORTED_AST_KIND`，絕不會把只重播了一部分的語法樹
標示為語意完整。

## 生命週期與清理

AST 與 Sema 的生命週期觀察者，透過主機的 `TreeConsumer` 橋接依原始碼順序送達。即使
發生語法錯誤、外掛錯誤或取消，begin/end 事件仍保持成對。任務控制代碼只有在最終的
唯讀 end 事件與清理回呼執行完畢之後才會失效。

## 驗證

```sh
python3 utils/plugin-api/gen-ast-schema.py --check
ctest --test-dir build-neverc \
  -R 'Plugin(AST|Parser|Sema|Frontend)' --output-on-failure
```

啟用 `NEVERC_ENABLE_PLUGIN_FUZZERS=ON` 後，`plugin-ast-mutation-fuzzer` 會涵蓋屬性
解碼、格式錯誤的建構器、偽造的控制代碼與變更回復。
