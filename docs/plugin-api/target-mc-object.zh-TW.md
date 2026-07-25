**語言**: [English](target-mc-object.md) | [简体中文](target-mc-object.zh-CN.md) | [繁體中文](target-mc-object.zh-TW.md) | [日本語](target-mc-object.ja.md) | [한국어](target-mc-object.ko.md) | [Français](target-mc-object.fr.md) | [Deutsch](target-mc-object.de.md) | [Español](target-mc-object.es.md) | [Italiano](target-mc-object.it.md) | [Русский](target-mc-object.ru.md) | [العربية](target-mc-object.ar.md)

# Target、MC、組合語言與目的檔外掛

NeverC 首個發行版的外掛 ABI，讓 C 外掛能描述目標平台、取代程式碼產生路線、觀察機器
碼發射、剖析或列印組合語言，以及讀寫目的檔。公開邊界是純 C ABI：外掛不得跨界傳遞
LLVM 的 C++ 物件、STL 型別、例外，或生命週期未由某個 API 表明確宣告的主機指標。

## 相容性層級

與目標無關的描述子、階段 ID、產物 ID、MC 容器、ObjectGraph 容器、輸出交易與回呼契約
屬於首發版的 STABLE ABI。與目標相關的 opcode、暫存器、運算元、fixup、重定位與呼叫
慣例 schema 屬於 LOCKSTEP。外掛在取用 LOCKSTEP 值之前，必須比對目標 schema ID 與
摘要。NeverC 會在呼叫 Provider 之前拒絕不相符的 schema。

## 註冊目標與程式碼產生路線

在註冊期間查詢 `NevercTargetAPI`，註冊一或多筆 `NevercTargetDescriptor` 記錄，並掛上
target-machine 描述子與程式碼產生邊。路線由正規目標鍵來選擇：目標 ID、triple、CPU、
特性、ABI、重定位模型、程式碼模型、目的檔格式與 schema 摘要。

細緻路線使用 `IR -> MIR -> MC -> ObjectGraph -> ObjectImage`。粗略的邊可以取代整條
`IR -> ObjectImage` 路線。粗略輸出仍須通過主機強制的產物驗證器與交易式輸出提交；
Provider 無法繞過其中任何一道關卡。

## 建構與觀察 MC

`NevercMCAPI` 負責任務區域性的 `MCUnit` 變更。開啟一次變更，建立 section、fragment、
符號、運算式、指令與運算元，然後提交或放棄。控制代碼限定於任務範圍，並會做世代檢查。

與目標無關的發射串流公開有序事件，涵蓋 section 切換、標籤、指令、對齊、符號屬性、
CFI、除錯位置與資料。`neverc.mc.emission.pre_instruction` 可取代，其餘事件階段則是
唯讀觀察點。請參見 `pluginsdk/examples/MCObserverPlugin.c`。

編碼、解碼與版面配置 Provider 以相同的目標鍵與 schema 摘要運作。版面配置負責
relaxation 並產出證明摘要。版面配置之後的任何變更都會使該證明失效，並在寫出目的檔前
強制重新配置版面。

## 取代組合語言語法

組合語言剖析器 Provider 消費原始位元組並發布一個 `MCUnit`。組合語言列印器消費
`MCUnit`，且只能透過所提供的輸出交易寫出。經過前置處理的組合語言（`.S`）會先走正常
的前端前置處理器再進入剖析器 Provider；純組合語言（`.s`）則直接進入剖析器。

Provider 會先暫存輸出。剖析／列印驗證與主機提交關卡都在位元組可見之前執行，因此失敗
不會留下任何部分輸出。

## 讀取、改寫與寫出目的檔

`NevercObjectAPI` 把可重定位檔案表示為正規化的 ObjectGraph：section、符號、重定位、
group/COMDAT、匯入／匯出、TLS 中繼資料、展開記錄與除錯記錄。內建轉接器涵蓋 ELF、
COFF 與 Mach-O，外掛還可以註冊更多格式。

目的檔流水線為：

1. 探測並把位元組讀入 ObjectGraph；
2. 執行 `object.pre_write` 圖攔截器；
3. 進行版面配置並執行 `object.post_layout`（變更之後重新配置）；
4. 寫出有界的候選映像；
5. 執行 `object.post_write` 二進位攔截器；
6. 執行密封的最終驗證器與不可分割的主機提交。

觀察者取得的是唯讀橋接。若從觀察者發起變更，會以
`NEVERC_STATUS_POLICY_VIOLATION` 遭拒。寫出器與 post-write 攔截器只能存取有界的交易
式建構器；溢位、回呼失敗或驗證失敗都會中止暫存。請參見
`pluginsdk/examples/ObjectRewritePlugin.c`。

## 並行與失敗規則

- 把可變狀態放在主機提供的 process/session/task 狀態中。
- 回呼返回後不要快取任務控制代碼或借用的視圖。
- 攔截器延續最多呼叫一次，且必須在回呼執行緒上呼叫。
- 回傳原始的 `NevercStatus`；不要發布部分產物。
- 宣告最狹窄且屬實的並行與可重入模式。

可執行的涵蓋率契約是 `docs/plugin-api/coverage.json`。它把每個穩定階段對應到正向、
負向、取代、唯讀觀察者與密封 gate 的測試。
