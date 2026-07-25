**語言**: [English](source.md) | [简体中文](source.zh-CN.md) | [繁體中文](source.zh-TW.md) | [日本語](source.ja.md) | [한국어](source.ko.md) | [Français](source.fr.md) | [Deutsch](source.de.md) | [Español](source.es.md) | [Italiano](source.it.md) | [Русский](source.ru.md) | [العربية](source.ar.md)

# Source 與 I/O 外掛 API

首個公開外掛 ABI 透過 `PluginSource.h` 公開原始碼輸入、虛擬檔案、相依關係與
編譯器輸出。所有路徑都是正規化後的 VFS 路徑，所有 handle 都限定在目前
`TranslationUnit` 任務的範圍內。

## Source 階段

穩定的 source 流水線為：

1. `neverc.source.resolve_input` 驗證並正規化請求的輸入。
2. `neverc.source.open` 透過宿主／外掛組合而成的 VFS 開啟它。
3. `neverc.source.after_open` 為已驗證的 `SourceUnit` 發布一個唯讀事件。

`resolve_input` 可觀察、可攔截；`open` 還可替換。宿主會在將任何替換結果發布為
`SourceUnit` 之前先驗證它。外掛不能替換 `after_open`。

## VFS Provider

在外掛註冊期間查詢 `NevercIOAPI` 並呼叫 `RegisterVFSProvider`。Provider 先回答
`MatchesPath`，接著實作它負責的操作。回傳
`NEVERC_VFS_RESULT_NOT_HANDLED` 會委派給下一個 Provider；回傳 `HANDLED` 則表示
格式錯誤的狀態或內容會成為硬錯誤，而不是悄悄退回預設路徑。

Provider 回傳的緩衝區只在該回呼期間被借用。NeverC 會把接受的位元組複製到任務擁有
的儲存空間中。Provider 必須宣告其結果是否具決定性、是否可快取。

可建置的
[`VirtualHeaderPlugin.c`](../../pluginsdk/examples/VirtualHeaderPlugin.c)
範例在不繞過宿主 VFS 的前提下提供了一個記憶體中的標頭檔。

## 輸出 sink 與相依關係

檔案輸出與記憶體輸出使用同一套交易式 sink：

- 寫入候選產物；
- 呼叫 finish 使其具備被驗證的資格；
- 讓密封的宿主 gate 驗證它；
- 任務成功時原子提交，出現任何錯誤或取消時中止。

外掛絕不透過直接寫入目標路徑來發布結果。無法回復的串流目標會拒絕那些需要原子候選
產物的轉換。相依記錄使用正規化的 VFS 識別，因此原生檔案與外掛提供的檔案具有相同
的來源與快取語意。

## 安全規則

- 不要在回呼結束後繼續持有 source、file、buffer、sink 或 task handle。
- 把 `NevercStringView` 與 `NevercByteView` 當作帶長度的視圖處理。
- 當資料需要存活到回呼之外時，請使用宿主配置器。
- 不要在 VFS 契約背後使用宿主檔案系統 API。
- 在執行昂貴的 Provider 工作之前先檢查取消狀態。
