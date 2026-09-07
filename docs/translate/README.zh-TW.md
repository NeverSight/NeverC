**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文件](../README.zh-TW.md)

# 將 C++ 轉譯為 NeverC

實驗性命令 `neverc translate` 透過 `cpp-core-v1`、`cpp-project-v1` 和 `cpp-math-v1` 產生可審查的 `.nc` 原始碼。

**目前僅實作了 C++ 輸入轉譯。** 易語言（E Language，`.e`）、Python、Go、Rust、TypeScript 和 JavaScript 均為未來計畫，目前尚無可用的轉譯器。

## 安裝與純量轉譯

依照[前端說明](../../utils/translate-frontends/cpp/README.md)建置或安裝固定版本 Clang 20.1.8 輔助程式。將它放在 NeverC 旁邊、設定 `NEVERC_CPP_FRONTEND`，或傳入 `--frontend PATH`。轉譯需要此程式；編譯已產生的純量／專案程式碼不再需要它。

```sh
neverc translate --from cpp input.cpp -o output.nc
neverc output.nc -c -o output.o
```

`cpp-core-v1` 接受一個不含 include 的獨立 C++17 原始檔，支援 `int`、`unsigned int`、`bool`、`void`、簡單聚合型別、自由函式、命名空間、多載及文件列出的控制流程。所有輸入宣告都會檢查，包括未使用的程式碼。

## 多檔案專案

從編譯資料庫明確選擇翻譯單元，並指定專案根目錄。輔助程式逐一分析翻譯單元，合併器檢查定義完整性、連結屬性及共用型別，並保守驗證單一定義規則（ODR）。

```sh
neverc translate --from cpp --profile cpp-project-v1 \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/a.cpp src/b.cpp --out-dir generated
```

專案輸出為 `translated.nc` 與 `translated.h`，僅允許該根目錄內的專案標頭檔。檔案有多種組態時，以 `--compdb-entry src/a.cpp=4` 選擇編譯資料庫的零起始索引。儲存的編譯命令僅作為資料解析，絕不執行。

## 有限的雙精度數學支援

`cpp-math-v1` 在專案支援上增加 `double`、文件列出的轉換／比較，以及精確簽章 `std::fabs(double)` 和 `std::floor(double)`。它要求固定的 Clang 20.1.8／libc++ 200100／macOS SDK 15.5、SDK 描述檔和明確的 macOS 15.0 目標（arm64 或 x86_64）。暫不支援通用浮點算術。浮點環境要求遮罩例外陷阱且停用非正規數歸零模式；四種標準捨入模式均已測試。

```sh
neverc translate --from cpp --profile cpp-math-v1 \
  --target arm64-apple-macosx15.0.0 --cpp-sdk /path/to/neverc-cpp-sdk.json \
  --project-root "$PWD" --compdb build/compile_commands.json \
  src/math.cpp --out-dir generated-math
```

數學轉譯還會驗證已安裝的 NeverC 數學標頭、嵌入實作的身分，並實際執行連結探測。產生的數學模組使用 NeverC 執行階段函式庫，不再需要 C++ 輔助程式或 SDK。使用 `-fno-builtin-std` 時，僅當最終輸出需要 `fabs`／`floor` 映射才會在寫入前失敗；無需這些映射的數學程式碼仍可轉譯。

## 驗證與輸出

以 `--check` 取代輸出選項，可執行相同的分析、產生、語法與目的碼驗證，但不保留產生的檔案。`--report PATH` 寫入結構化診斷。轉譯過程不會執行原始程式。

輸出及附屬檔案不會覆寫既有檔案。`--out-dir` 要求父目錄存在且目標目錄不存在；`-o` 要求新的 `.nc` 路徑。清單記錄目標需求、輸入／輸出雜湊及編譯方式，原始碼映射將產生行對應至原始位置。

執行驗證已涵蓋原生 macOS arm64，以及透過 Rosetta 執行的 macOS x86_64。這不代表已驗證原生 Intel Mac、Linux、Windows 或其他目標。完整 C++／STL、指標、參考、陣列、例外、範本、字串及 `std::vector` 不在已公布的支援範圍內。請參閱[支援矩陣](../../utils/translate-frontends/docs/support-matrix.md)、[協定與復原規則](../../utils/translate-frontends/docs/protocol.md)及[專案範例](../../tests/neverc/Inputs/translate/cpp/project/)。
