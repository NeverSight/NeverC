**語言**: [English](dyncode.md) | [简体中文](dyncode.zh-CN.md) | [繁體中文](dyncode.zh-TW.md) | [日本語](dyncode.ja.md) | [한국어](dyncode.ko.md) | [Français](dyncode.fr.md) | [Deutsch](dyncode.de.md) | [Español](dyncode.es.md) | [Italiano](dyncode.it.md) | [Русский](dyncode.ru.md) | [العربية](dyncode.ar.md)

[← NeverC 外掛 ABI](README.zh-TW.md)

# DynCode 外掛

`-fdyncode` 會把一個翻譯單元編譯成扁平、位置無關的映像（`.bin`），其程式碼沒有任何
重定位，也沒有資料區段。它支援 macOS、Linux、Android 與 Windows 上的 arm64／x86_64，
執行層級可為使用者模式或核心模式。外掛透過與其他網域相同的純 C ABI，觀察、攔截或
取代把 C 轉成該映像的型別化階段：不會出現 LLVM C++ 物件、STL 型別、例外，也不會有
生命週期未由 API 表明確規定的宿主指標。

## 介面

```c
#include "neverc/Plugin/PluginDynCode.h"
```

| 介面 | 能力表 | 插槽 | 用途 |
|---|---|--:|---|
| `NEVERC_INTERFACE_DYNCODE_{HIGH,LOW}` | `NevercDynCodeAPI` | 16 | 讀取請求、映像、報告，以及區段／符號／重定位／外部參考等對應表 |
| `NEVERC_INTERFACE_DYNCODE_REGISTRAR_{HIGH,LOW}` | `NevercDynCodeRegistrarAPI` | 5 | `RegisterTarget`、`RegisterImportProvider`、`RegisterExtractor`、`RegisterCharsetEncoder`、`RegisterBinaryVerifier` |
| `NEVERC_INTERFACE_DYNCODE_PHASE_{HIGH,LOW}` | `NevercDynCodePhaseAPI` | 4 | `GetPhaseInfo`、`GetRequest`、`GetImage`、`GetReport` |

三者在主版本 1 皆為 `NEVERC_INTERFACE_STABLE`。在階段回呼內部，
`NevercDynCodePhaseAPI` 是進入點——它把 frame 轉換成另一張表所消費的控制代碼：

```c
NevercDynCodeRequestHandle Request;
Phase->GetRequest(Phase->Context, Frame, Frame->Input, &Request);

NevercDynCodeRequestInfo Info = {0};
Info.Header = (NevercABITableHeader){sizeof(Info), NEVERC_DYNCODE_API_MAJOR,
                                     NEVERC_DYNCODE_API_MINOR, 0};
DynCode->GetRequestInfo(DynCode->Context, Task, Request, &Info);
```

四個對應表家族——區段對應、符號對應、重定位與外部參考——都以相同的 first/next/info
三件組走訪，例如 `GetFirstRelocation`、`GetNextRelocation`、`GetRelocationInfo`。
外掛因此無需解析報告 JSON，就能讀到抽取階段所做的決定。

## DynCode 是編譯產物，而非 `main()` 之後的後處理

`-fdyncode` 是驅動器 DAG 中一個尋常的 Action／Job。編譯工作會發布一份已驗證的
記憶體內 `ObjectGraph`；`-dyncode-extract` 工作消費該圖並寫出使用者的 `-o` 映像。
`-###`、階段列印與工作圖都會顯示這個抽取工作，因此外掛絕不需要為了辨識模式而重建被
改寫過的 argv。凍結後的請求以工作區域的方式與行程內的程式碼產生共享；沒有
`getCurrentDynCodeOptions()`，沒有行程全域的模式旗標，也沒有暫存目的檔的來回。

恰好一個翻譯單元會降階為一個映像。多重輸入、`-c/-S/-E` 以及不受支援的 triple 會在
最前面就以穩定診斷拒絕。

## 相容性層級

階段 ID、產物 ID、請求／報告／映像容器，以及回呼契約，屬於首次發行的 STABLE ABI。
目標特定的重定位種類與目的檔格式的區段／符號 schema 則是 LOCKSTEP：使用它們之前，
請先比對目標 schema ID 與摘要。若 schema 不符，NeverC 會在呼叫提供者之前就拒絕。

## 凍結的請求

工作開始時，驅動器會把命令列正規化為不可變的 `DynCodeRequest` 並加以凍結。子工作
只借用該快照，絕不修改它。請求承載目標鍵與目的檔格式、執行層級（使用者／核心）、
進入點政策（明確符號、預設候選清單、進入點必須位於位移 0 的要求）、PIC／區段政策、
外部參考政策、壞位元組集合／設定檔與改寫旗標、charset 提供者 ID，以及最大長度、
對齊與填補位元組。

## 型別化的階段圖

DynCode 是由 34 個階段構成的固定圖。其中 30 個一般轉換為
`OBSERVABLE | INTERCEPTABLE | REPLACEABLE`，4 個為
`OBSERVABLE | SEALED_HOST_GATE`。封閉閘門分別是 IR 最終驗證、MIR 最終驗證、映像驗證
與提交。外掛可以觀察任何階段，可以用攔截器包裹可取代的轉換，或直接取代其提供者；
但永遠不能取代、跳過或繞過封閉閘門，也不能把「已停用的轉換」表達成「未被呼叫的
回呼」——已停用的轉換會執行一個明確的 no-op 提供者，其等價輸出仍由宿主驗證器加以
證明。

各階段依序如下：

1. 請求凍結；
2. IR 轉換群——prepare、間接分支降階、記憶體 intrinsic 降階（堆積前）、
   字串執行期降階、堆積 arena、記憶體 intrinsic 降階（堆積後）、`compiler_rt`
   （pre）、syscall／PEB／核心匯入降階、`data_to_text`（pre）、內聯最佳化、
   `compiler_rt`（post）、字串定案、`data_to_text`（post）、stackify、全 `blr`
   化、`compiler_rt`（final），以及封閉的 IR 最終驗證；
3. MIR prepare 轉換與封閉的 MIR 最終驗證；
4. 目的檔匯入——把已驗證的 `ObjectGraph` 繫結到工作；
5. 抽取——規劃、佈局、重定位，並建構候選映像；
6. 有界的二進位階段群——post-extract、壞位元組改寫、charset 編碼、大小／對齊／填補，
   以及 pre-verify；
7. 封閉的映像驗證；
8. 封閉的提交。

ID、政策、穩定性層級與閘門的規範性來源是 [`Schema/PhaseSchema.json`]；可執行的
覆蓋率契約則是 [`coverage.json`]。

## 內建轉換同樣是提供者

每個內建的 IR／MIR pass 都被包裝成型別化的提供者；LLVM 的 pass 物件絕不會跨越 C ABI
曝露。取代一個階段就意味著內建提供者不會執行——通過的測試證明的是行為或軌跡本身，
而不僅僅是註冊成功。`mem_intrin`、`compiler_rt` 與 `data_to_text` 這幾個階段會出現在
一個以上的位置；每個位置都是各自獨立、擁有自身證明的階段 ID，因此重新執行具有
冪等性，且絕不依賴隱藏的 pass 狀態。

## ObjectGraph 是唯一的一般目的檔輸入

抽取只消費一份由目標的程式碼產生路線所製作、且已驗證的 `ObjectGraph`。
`dyncode.object.import` 會繫結該圖並檢查目標鍵與來源；它絕不從磁碟重新讀取位元組，
也不會執行第二次目的檔剖析。只要自訂目的檔格式能被讀入 `ObjectGraph`，並具備相符的
重定位與目標提供者，就能進入 DynCode。多重目的檔與 LTO 圖集合會在凍結時以穩定的
`CAPABILITY_UNAVAILABLE` 拒絕。

## 外部參考與匯入降階

請求中的「允許外部」集合僅代表「提供者可以處理它」，絕不允許未解析的重定位存活到
扁平映像中。每個外部參考最終都必須落入下列其中之一：在 IR／MIR 中被消除、解析到
映像內的符號、轉換為已宣告且經驗證器接受的執行期解析器契約，或直接是硬性錯誤。
syscall stub、PEB 匯入與核心匯入是三個內建的 `ImportProvider`，各自宣告其目標／層級／
符號比對器，以及所產生的 ABI 契約。外掛可以新增 `ImportProvider`，但必須回報替換
來源、進入點 ABI 變更、解析器參數，以及殘留的參考。

## 映像、報告與有界的位元組編輯

抽取會產出一份 `DynCodeImage` 與一份 `DynCodeReport`。映像除了有界的位元組建構器之
外，還包含進入點位移／符號、來源區段與來源符號的輸出對應、重定位處置，以及外部／
執行期契約紀錄。每一次位元組編輯都必須經由建構器帶檢查的 read/write/insert/append/
resize API；不存在 `uint8_t **`。一次編輯會更新映像世代，並使與變更範圍重疊的任何
重定位／PIC／進入點證明失效。

報告是不可變且具決定性的稽核產物：請求／路線／輸入／輸出摘要、逐階段的提供者日誌、
被採用與被拒絕的區段及其原因、進入點選擇、已修補／被拒絕／轉為執行期契約的重定位、
殘留的外部參考、大小／對齊／填補、壞位元組掃描，以及驗證器檢查清單。
`-fdyncode-report=<path>` 會寫出其正規 JSON；詳盡診斷同樣由這份報告繪製，而不是另做
一套計數。

壞位元組改寫鏈以凍結的拓撲順序執行，每個步驟都回傳一筆變更紀錄。charset 編碼器以
精確的穩定 ID 選定，並回傳解碼器 stub、編碼後的酬載、進入點更新與目標證明；未知或
含糊的 ID 屬於硬性錯誤。停用改寫會選中一個明確的 no-op 步驟——最終稽核照樣執行。

## 最終驗證器與定案後的時序

所有可寫入的階段都在封閉的最終驗證器之前結束。驗證器會檢查：沒有未處理的外部
重定位／參考殘留；不存在被禁止的資料／TLS／unwind／除錯／中繼資料區段；進入點存在、
對齊正確，並在必要時位於位移 0；每個重定位位置都落在範圍內，且對目前映像位元組具備
相符的 PIC 證明；區段／符號對應彼此不重疊；長度／對齊／填補規則成立；以及最終位元組
——包含解碼器、標頭與填補——不含任何被禁止的位元組。任何一項失敗都會回傳結構化診斷
並丟棄整個輸出組合。

稽核之後不存在可寫入的掛鉤。若某項位元組轉換觸及可執行範圍，凍結的路線就必須提供
相符的二進位驗證器能力，由宿主呼叫它，針對最終且不可變的映像重新簽發 PIC 證明。

## 驅動器選項

`-fdyncode` 啟用此模式。`-fdyncode-entry=` 選擇進入點符號。
`-fdyncode-bad-bytes=` ／ `-fdyncode-bad-byte-profile=` 設定被禁止的位元組，
`-fdyncode-bad-byte-rewrite`（預設開啟）選擇改寫鏈，`-fdyncode-charset=` 選擇已註冊的
編碼器。`-fdyncode-max-length=`、`-fdyncode-align=` 與 `-fdyncode-pad=` 約束最終
大小。`-fdyncode-keep-obj=` 會另存中介的可重定位目的檔，`-fdyncode-report=` 則寫出
稽核報告。`-mdyncode-context=user|kernel` 選擇執行層級。

## 並行與失敗規則

- 可變狀態請放在宿主提供的行程／工作階段／工作範圍中；絕不要使用「目前外掛」或
  「目前選項」之類的單例。
- 回呼返回後，不要快取工作控制代碼或借用檢視。
- 攔截器的接續最多呼叫一次，且只能在回呼執行緒上呼叫。
- 回傳原本的 `NevercStatus`；已宣告 `REPLACE` 的處理若失敗，不會默默退回內建提供者。
- 以據實而言最窄的並行與可重入模型進行宣告。

規範宣告請見 [`PluginDynCode.h`]，唯讀的階段追蹤器請見
[`pluginsdk/examples/DynCodeTracePlugin.c`]，charset 編碼器請見
[`pluginsdk/examples/DynCodeEncoderPlugin.c`]。

<!-- reference links -->
[`coverage.json`]: coverage.json
[`PluginDynCode.h`]: ../../neverc/include/neverc/Plugin/PluginDynCode.h
[`pluginsdk/examples/DynCodeEncoderPlugin.c`]: ../../pluginsdk/examples/DynCodeEncoderPlugin.c
[`pluginsdk/examples/DynCodeTracePlugin.c`]: ../../pluginsdk/examples/DynCodeTracePlugin.c
[`Schema/PhaseSchema.json`]: ../../neverc/include/neverc/Plugin/Schema/PhaseSchema.json
