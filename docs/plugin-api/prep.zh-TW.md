**語言**: [English](prep.md) | [简体中文](prep.zh-CN.md) | [繁體中文](prep.zh-TW.md) | [日本語](prep.ja.md) | [한국어](prep.ko.md) | [Français](prep.fr.md) | [Deutsch](prep.de.md) | [Español](prep.es.md) | [Italiano](prep.it.md) | [Русский](prep.ru.md) | [العربية](prep.ar.md)

# 前置處理器外掛 API

`PluginPrep.h` 公開穩定的 token、識別字、巨集、pragma 與 token 串流 schema，且不會
洩漏 NeverC 或 LLVM 的 C++ 型別。產生的 schema `Schema/PluginPrepSchema.inc` 是穩定
數值種類、類別、拼法與可建構性的唯一權威來源。

## 擴充層級

外掛可以在三個層級參與：

- 針對 include、巨集展開、條件式、pragma 與檔案切換的唯讀前置處理器事件；
- 針對 token、include、巨集、pragma 與特性查詢各階段的具型別攔截器；
- 一個完整的 `neverc.prep.build_token_stream` Provider，用來發布經過驗證的
  `TokenStream`。

token 階段支援有界的取代、刪除與展開。主機會強制套用展開預算，並在發布取代結果前
驗證拼法、位置、旗標、EOF 位置以及 token 歸屬。

## Token 建構器

以 `CreateTokenBuilder` 建立合成 token，設定且僅設定一份 token 酬載，指派一個有效
且由任務擁有的位置，然後呼叫 `TokenBuilderCommit`。在每條路徑上都要銷毀建構器。已
提交的建構器不可變更，提交失敗則不會發布任何 token。

Token 串流是連續且不可變的任務產物。取代用的串流必須恰好包含一個位於結尾的 EOF
token，且不得超過 `NEVERC_PREP_TOKEN_STREAM_MAX_TOKENS`。

## 觀察者與攔截器規則

觀察者接收唯讀事件資料，無法影響前置處理。攔截器遵循共同的延續契約：

- 最多呼叫一次 `InvokeNext`，然後回傳 `CONTINUE`；或者
- 不呼叫它，改為發布一個經過驗證的取代結果。

延續物件以及所有前置處理器控制代碼，只在其宣告的回呼／任務範圍內有效。外掛建立的
執行緒若碰觸這些值，必須在回呼返回前完成 join。

## 驗證

變更 token 定義後，請執行產生 schema 與涵蓋率檢查：

```sh
python3 utils/plugin-api/gen-prep-schema.py --check
python3 utils/plugin-api/check-coverage.py docs/plugin-api/coverage.json
```

啟用 `NEVERC_ENABLE_PLUGIN_FUZZERS=ON` 後，
`plugin-prep-token-builder-fuzzer` 會針對格式錯誤的 token 建構器、任務控制代碼、
輸出容量與 token 串流查詢進行測試。
