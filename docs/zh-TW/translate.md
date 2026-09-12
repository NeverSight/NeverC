**語言**: [English](../translate.md) | [简体中文](../zh-CN/translate.md) | [繁體中文](translate.md) | [日本語](../ja/translate.md) | [한국어](../ko/translate.md) | [Français](../fr/translate.md) | [Deutsch](../de/translate.md) | [Español](../es/translate.md) | [Italiano](../it/translate.md) | [Русский](../ru/translate.md) | [العربية](../ar/translate.md)

[← 文件](README.md)

# 將 C++ 轉譯為 NeverC

實驗性命令 `neverc translate` 透過 `cpp-core-v1`、`cpp-core-v2`、`cpp-project-v1` 和 `cpp-math-v1` 產生可審查的 `.nc` 原始碼。

**目前僅實作了 C++ 輸入轉譯。** 易語言（E Language，`.e`）、Python、Go、Rust、TypeScript 和 JavaScript 均為未來計畫，目前尚無可用的轉譯器。

## 安裝與純量轉譯

使用正常安裝的 NeverC 及其標準資源即可。C++ 前端與核准的 SDK 標頭均已內建，無需另行安裝 Clang。建置細節見[前端說明](../../utils/translate-frontends/cpp/README.md)。

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

`cpp-core-v1` 接受一個不含 include 的獨立 C++17 原始檔，支援 `int`、`unsigned int`、`bool`、`void`、簡單聚合型別、自由函式、命名空間、多載及文件列出的控制流程。所有輸入宣告都會檢查，包括未使用的程式碼。

使用 `--profile cpp-core-v2` 可增加經過檢查的 `typedef`／`using` 型別別名、底層型別為受支援整數型別的列舉、`static_assert`，以及限定範圍的物件指標與左值參考。參考保留別名關係，包括參數和回傳參考；支援多層指標的 `const` 與空指標。仍限單一原始檔，且不允許 include。詳見 [core v2 支援範圍](../../utils/translate-frontends/docs/cpp-core-v2.md)。

core v2 也增加了固定長度的區域陣列與陣列欄位、多維下標存取，以及陣列指標和參考。部分初始化將省略的純量元素設為零，紀錄元素依所選的初始化方式處理；元素初始化保留原始碼順序與別名關係。陣列長度與初始化展開量有上限。全域陣列、變長陣列及元素的非平凡解構仍未支援。

core v2 支援 `switch`／`case`／`default`，包括 C++17 初始化陳述式、分支貫穿及經過驗證的 `[[fallthrough]]` 標註。選擇器只求值一次；巢狀 switch 與迴圈保留各自的 `break`／`continue` 目標。GNU case 範圍及其他陳述式屬性仍被拒絕。

Core v2 現支援有號與無號的 8／16／32／64 位整數、字元型別與常值，以及常數 `sizeof`／`alignof` 查詢。先依來源 C++ 規則完成整數提升與多載解析，再將位元寬度標準化；`long`、`wchar_t` 與大小型別遵循目標平台。列舉也支援窄整數與寬整數作為底層型別。執行期字串與 STL 仍在開發中。

Core v2 另支援物件指標偏移、差值、遞增／遞減及複合賦值，可用於走訪陣列並保留多維陣列的步長。產生的輔助函式保留 C++17 空指標加減零及空指標相減的行為。指標大小比較與指標／整數轉換仍未支援，STL 容器與演算法也尚未完成。

Core v2 會用 NeverC 自身的目標模型核對來源型別的大小與 ABI 對齊，包括結構體大小和欄位偏移。產生的程式碼透過編譯期斷言再次檢查配置，清單也會記錄這些驗證依據。

Core v2 支援已接納的紀錄型別中的具名非虛擬成員函式，包括 const 多載、左值限定方法、靜態方法和 `this`。呼叫保留原物件身分，並在引數之前求值接收物件。暫存物件上的非靜態呼叫、繼承、範本和完整 STL 仍未支援。

Core v2 也支援具有標準布局、平凡複製與解構的紀錄型別的一般使用者建構函式。區域物件、紀錄欄位與陣列元素直接在最終儲存位置建構，欄位依宣告順序初始化。明確 default 的建構函式、委派建構、使用者定義的複製／移動建構與解構、清理及例外仍未支援。

Core v2 為每個以值傳遞的紀錄參數建立獨立物件，並將紀錄回傳值直接寫入呼叫者的目標位置。建構函式與成員函式採用相同規則；原始碼要求的複製仍會執行，參考仍保留別名。這是 NeverC 對已接納平凡紀錄型別選擇的行為，並非 C++17 對所有實作的位址保證。非平凡複製／移動、解構、清理與完整 STL 仍在開發中。

## 多檔案專案

從編譯資料庫明確選擇翻譯單元，並指定專案根目錄。內建前端逐一分析翻譯單元，合併器檢查定義完整性、連結屬性及共用型別，並保守驗證單一定義規則（ODR）。

```sh
neverc translate --from cpp --profile cpp-project-v1 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/a.cpp src/b.cpp --out-dir generated
```

專案輸出為 `translated.nc` 與 `translated.h`，僅允許該根目錄內的專案標頭檔。檔案有多種組態時，以 `--compdb-entry src/a.cpp=4` 選擇編譯資料庫的零起始索引。儲存的編譯命令僅作為資料解析，絕不執行。

## 有限的雙精度數學支援

`cpp-math-v1` 在專案支援上增加 `double`、文件列出的轉換／比較，以及精確簽章 `std::fabs(double)` 和 `std::floor(double)`。它使用內建的 Clang 20.1.8／libc++ 200100／macOS 15.5 標頭集合，要求明確的 macOS 15.0 目標（arm64 或 x86_64）。暫不支援通用浮點算術。浮點環境要求遮罩例外陷阱且停用非正規數歸零模式；四種標準捨入模式均已測試。

```sh
neverc translate --from cpp --profile cpp-math-v1 \
  --target arm64-apple-macosx15.0.0 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/math.cpp --out-dir generated-math
```

數學轉譯還會驗證已安裝的 NeverC 數學標頭、嵌入實作的身分，並實際執行連結探測。產生的數學模組使用 NeverC 執行階段函式庫。使用 `-fno-builtin-std` 時，僅當最終輸出需要 `fabs`／`floor` 映射才會在寫入前失敗；無需這些映射的數學程式碼仍可轉譯。

## 驗證與輸出

以 `--check` 取代輸出選項，可執行相同的分析、產生、語法與目的碼驗證，但不保留產生的檔案。`--report PATH` 寫入結構化診斷。轉譯過程不會執行原始程式。

輸出及附屬檔案不會覆寫既有檔案。`--out-dir` 要求父目錄存在且目標目錄不存在；`-o` 要求新的 `.nc` 路徑。清單記錄目標需求、輸入／輸出雜湊及編譯方式，原始碼映射將產生行對應至原始位置。

CI 結果、執行與安裝驗證以及測試跳過項目依平台分別記錄。原生 macOS arm64 與透過 Rosetta 執行的 macOS x86_64 仍是不同的驗證環境。完整 C++／STL、例外、範本、字串及 `std::vector` 不在已公布的支援範圍內。請參閱[支援矩陣](../../utils/translate-frontends/docs/support-matrix.md)、[協定與復原規則](../../utils/translate-frontends/docs/protocol.md)及[專案範例](../../tests/neverc/Inputs/translate/cpp/project)。
