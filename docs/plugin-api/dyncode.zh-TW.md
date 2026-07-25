**語言**: [English](dyncode.md) | [简体中文](dyncode.zh-CN.md) | [繁體中文](dyncode.zh-TW.md) | [日本語](dyncode.ja.md) | [한국어](dyncode.ko.md) | [Français](dyncode.fr.md) | [Deutsch](dyncode.de.md) | [Español](dyncode.es.md) | [Italiano](dyncode.it.md) | [Русский](dyncode.ru.md) | [العربية](dyncode.ar.md)

# DynCode 外掛

`-fdyncode` 把一個翻譯單元編譯成扁平、位置無關的映像（`.bin`）：其程式碼零重定位、
無資料節。它以 macOS、Linux、Android 與 Windows 上的 arm64/x86_64 為目標，可選 user
或 kernel 執行層級。外掛透過與其他領域相同的純 C ABI，觀察、攔截或取代把 C 轉成該
映像的各個具型別階段：不跨界傳遞 LLVM C++ 物件、STL 型別、例外，或生命週期未由 API
表宣告的主機指標。

## DynCode 是編譯產物，不是 `main()` 之後的後處理

`-fdyncode` 是驅動程式 DAG 中一個正常的 Action/Job。編譯 job 會發布一份經過驗證、
位於記憶體中的 `ObjectGraph`；接著 `-dyncode-extract` job 消費該圖並寫出使用者的
`-o` 映像。`-###`、階段列印與 job 圖都會顯示這個抽取 job，因此外掛永遠不必為了得知
模式而去重建被改寫過的 argv。凍結後的請求會以任務區域方式與行程內的程式碼產生共用；
沒有 `getCurrentDynCodeOptions()`，沒有行程全域的模式旗標，也沒有暫存目的檔的往返。

恰好一個翻譯單元會被降階成一份映像。多個輸入、`-c/-S/-E` 以及不支援的 triple，都會
在最前面就以穩定的診斷訊息遭到拒絕。

## 相容性層級

階段 ID、產物 ID、request/report/image 容器，以及回呼契約，屬於首發版的 STABLE
ABI。與目標相關的重定位種類，以及目的檔格式的節／符號 schema，則屬於 LOCKSTEP：在
取用它們之前，必須比對目標 schema ID 與摘要。NeverC 會在呼叫 Provider 之前，就拒絕
不相符的 schema。

## 凍結後的請求

在 job 開始時，驅動程式會把命令列正規化成不可變的 `DynCodeRequest` 並將其凍結。子
任務只借用該快照，絕不修改它。該請求攜帶目標鍵與目的檔格式、執行層級（user/kernel）、
入口策略（明確符號、預設候選清單、入口必須位於零位址的要求）、PIC／節策略、外部參考
策略、壞位元組集合／設定檔與改寫旗標、字元集 Provider ID，以及最大長度、對齊與填充
位元組。

## 具型別的階段圖

DynCode 是一張固定的 34 階段圖。其中 30 個一般轉換為
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE`，另外 4 個為
`OBSERVABLE | SEALED_HOST_GATE`。這些密封關卡分別是 IR 最終驗證、MIR 最終驗證、映像
驗證與提交。外掛可以觀察任何階段，可以用攔截器包住可取代的轉換，也可以直接取代其
Provider；但它永遠無法取代、略過或繞開密封關卡，也不能把「停用某個變換」表達成「略過
回呼」——被停用的變換會執行一個明確的 no-op Provider，而主機驗證器仍會證明其等價
輸出。

各階段依序為：

1. 請求凍結；
2. IR 變換 —— prepare、間接分支降階、記憶體 intrinsic 降階（堆積前與堆積後）、字串
   執行期降階、堆積 arena、三個 `compiler_rt` 位置（前／後／最終）、syscall/PEB/
   核心匯入降階、兩個 `data_to_text` 位置（前／後）、內聯最佳化、字串定稿、stackify、
   全 `blr`，以及密封的 IR 最終驗證；
3. MIR prepare 變換與密封的 MIR 最終驗證；
4. 目的檔匯入 —— 把驗證過的 `ObjectGraph` 繫結到任務；
5. 抽取 —— 規劃、版面配置、重定位，並建構候選映像；
6. 有界的二進位階段 —— post-extract、壞位元組改寫、字元集編碼、大小／對齊／填充，
   以及 pre-verify；
7. 密封的映像驗證；
8. 密封的提交。

這些 ID、策略、穩定性層級與關卡的規範來源是
`neverc/include/neverc/Plugin/Schema/PhaseSchema.json`；可執行的涵蓋率契約則是
`docs/plugin-api/coverage.json`。

## 內建變換同樣是 Provider

每一個內建的 IR/MIR pass 都被包裝成具型別的 Provider；LLVM 的 pass 物件永遠不會跨越
C ABI 暴露出去。取代某個階段意味著內建 Provider 不會執行 —— 通過的測試所證明的是行為
或軌跡，而不只是註冊成功而已。`mem_intrin`、`compiler_rt` 與 `data_to_text` 階段會
出現在一個以上的位置；每個位置都是獨立的階段 ID，各自帶有自己的證明，因此重跑具備
冪等性，且絕不依賴隱藏的 pass 狀態。

## ObjectGraph 是唯一的一般目的檔輸入

抽取只消費一份由目標的程式碼產生路線所產出、且經過驗證的 `ObjectGraph`。
`dyncode.object.import` 會繫結該圖並檢查目標鍵與來源；它絕不會從磁碟重新讀取位元組，
也不會執行第二次目的檔剖析。只要某個自訂目的檔格式能被讀成 `ObjectGraph`，並具備相符
的重定位與目標 Provider，它就能進入 DynCode。多個目的檔與 LTO 圖集，會在凍結階段就
以穩定的 `CAPABILITY_UNAVAILABLE` 遭到拒絕。

## 外部參考與匯入降階

請求中允許的外部集合，只表示「某個 Provider 可以處理它」；它絕不允許未解析的重定位
存活到扁平映像裡。每一個外部參考最終都必須是以下之一：在 IR/MIR 中被消除、解析到映像
內的符號、轉換成一份已宣告且被驗證器接受的執行期解析器契約，或是硬性錯誤。syscall
stub、PEB 匯入與核心匯入是三個內建的 `ImportProvider`；每一個都會宣告自己的目標／
層級／符號比對器，以及它所產生的 ABI 契約。外掛可以新增 `ImportProvider`，但必須回報
取代來源、入口 ABI 變化、解析器參數與殘留參考。

## 映像、報告與有界的位元組編輯

抽取會產生 `DynCodeImage` 與 `DynCodeReport`。映像是一個有界的位元組建構器，加上入口
偏移／符號、來源節與來源符號的輸出對應、重定位處置，以及外部／執行期契約記錄。每一次
位元組編輯都要經過建構器帶檢查的 read/write/insert/append/resize API；不存在
`uint8_t **`。一次編輯會更新映像世代，並使任何與變動範圍重疊的重定位／PIC／入口證明
失效。

報告是不可變、確定性的稽核產物：request/route/input/output 摘要、逐階段的 Provider
日誌、被選取／被拒絕的節及其原因、入口選擇、已修補／被拒絕／執行期契約的重定位、殘留
的外部參考、大小／對齊／填充、壞位元組掃描，以及驗證器檢查清單。
`-fdyncode-report=<path>` 會寫出其正規 JSON；詳細診斷訊息也是從同一份報告算繪出