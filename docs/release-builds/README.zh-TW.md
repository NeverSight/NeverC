**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← 文件索引](../README.zh-TW.md) · [← NeverC 專案](../../README.md)

# 發布二進位檔與 `--strip`

產生要散佈的可執行檔、共享程式庫或最終 Android 核心模組時，請使用
`--strip`。其短別名為 `-s`，兩種拼法的行為完全相同。

## 快速開始

```bash
neverc -O2 --strip app.c -o app
neverc -O2 -s app.c -o app

cd examples/android-kernel-hello
neverc make release
```

NeverC 在整合式連結器內執行剝離，不會啟動外部 `llvm-strip`，所以同一
命令可用於交叉目標 ELF、Mach-O 與 PE/COFF 輸出。

請勿將此命令列選項與 CMake 封裝開關 `NEVERC_STRIP_BINARY` 混淆：後者
只會在建置後處理 `neverc` 編譯器執行檔，且可能呼叫外部 strip 工具；
它不會影響 NeverC 編譯出的程式。

## 偵錯資訊與符號策略

| 呼叫方式 | 原始碼層級偵錯資訊 | 一般靜態符號名稱 | Darwin `.dSYM` |
|----------|--------------------|------------------|----------------|
| 預設（無 `-g`） | 不產生 | 可能保留；精確預設值依格式而定 | 不產生 |
| `-g` | 產生 | 保留 | 一般 Darwin 連結會產生 |
| `--strip` | 若存在則移除 | 移除非執行期名稱 | 不產生 |
| `-g --strip` | 剝離策略優先；交付映像中不存在 | 移除非執行期名稱 | 抑制產生 |

沒有 `-g` 時，前端從不產生原始碼層級偵錯資訊開始。這**不代表**輸出已
完整剝離：ELF 與 Mach-O 仍可能帶有一般符號名稱；PE 通常沒有靜態 COFF
符號表，除非偵錯設定要求。Auto-LTO 可能捨棄部分區域名稱，但這不是
strip-all 保證。

`-g` 把策略從沒有原始碼偵錯切換為原始碼層級偵錯；它不是在預設偵錯
資訊上再加入「更多」資訊。ELF/Mach-O 的 `.eh_frame` 或 PE 的
`.pdata`/`.xdata` 等展開資料是執行期中繼資料，不是原始碼層級 DWARF，
剝離後仍可能保留。

## 實作與格式行為

驅動程式將 `--strip` 轉換為單一強型別連結策略，並傳給三個後端。每個
後端在仍理解格式時套用策略，同時保留載入器或動態 ABI 所需的名稱與記錄。

| 格式 | 移除內容 | 必要時保留 |
|------|----------|------------|
| ELF | `.debug*` 資料與一般靜態符號表/字串表 | 動態匯入匯出、重定位與載入器中繼資料、展開資訊 |
| Android 核心 `.ko`（ELF ET_REL） | `.debug*`、`.comment`，以及未被保留重定位使用的區域/未定義符號 | 一個連結至 `.strtab` 的 `.symtab`、所有重定位及其目標、已定義全域符號、匯入、`__versions`、`.codetag.alloc_tags`、模組 ABI 資料 |
| Mach-O | 偵錯映射/STABS、非執行期區域與全域符號項，以及伴隨 `.dSYM` 的產生 | 繫結/匯入資料、匯出 ABI 名稱、export trie 項目、執行期參照符號 |
| PE/COFF | 嵌入式 DWARF 區段，以及存在時的靜態 COFF 符號表/字串表 | PE 匯入匯出、展開表、載入設定與其他載入器中繼資料 |

## 範圍與優先順序

- `--strip` 支援最終連結的可執行檔、共享程式庫，以及下述嚴格限定的最終
  Android `.ko` 例外。
- 與 `-c`、一般 `-r`、Android 中間 `.o`、`--emit-static-lib` 或
  `-fdyncode` 合用時，NeverC 會明確報錯，而不會靜默產生未剝離的非最終
  產物。
- 剝離策略優先於 `-g` 與後端偵錯開關。
- NeverC 的預設 Auto-LTO 流程與 `-fno-lto` 均有涵蓋。
- 共享程式庫的匯入與匯出名稱在移除會破壞動態 ABI 時必須保留。

## Android 核心模組

Android 模組雖是最終交付物，但仍是 ELF `ET_REL`。Linux 模組載入器需要
符號表、關聯字串表、未定義匯入與重定位，因此會拒絕 strip-all 結果。
NeverC 僅在目標為 Android、同時啟用 `-fandroid-kernel-driver-mode` 與
`-r`，且輸出名稱以 `.ko` 結尾時允許 `-r --strip`。

此路徑實作 `llvm-strip --strip-unneeded` 的安全邊界，而非 `--strip-all`：
移除偵錯區段、`.comment` 與未被保留重定位使用的區域或未定義符號，並重建
`.strtab`，確保已刪名稱不會以廢棄位元組殘留。它保留恰好一個連結至
`.strtab` 的 `.symtab`、所有重定位與必要目標、已定義非區域符號、匯入、
`__versions`、`.codetag.alloc_tags`、`.gnu.linkonce.this_module` 及其他
模組載入中繼資料。

不要再對 `.ko` 執行 `llvm-strip --strip-all`，也不要任意移除
`.codetag.alloc_tags` 或 `__codetag_*`。若需簽署模組，必須先剝離，再簽署
最終位元組；簽署後的任何變更都會使簽章失效。`clean` 只能刪除檔案，絕不
能剝離或簽署現有模組。

## 安全邊界

剝離會移除高價值命名與偵錯中繼資料，因而提高分析成本，但它**不是**
混淆，也不能讓原生機器碼無法逆向。正確剝離的二進位檔仍可能包含：

- 載入器所需的動態匯入與匯出名稱；
- `.ko` 中保留重定位所必需的符號名稱；
- 字串常值、反射表或應用程式自訂中繼資料；
- 展開、重定位、簽章與載入設定記錄；
- 機器碼及其可觀察控制流程。

`--strip` 只約束最終映像，不會刪除明確要求的連結映射、最佳化記錄或
`-save-temps` 輸出等獨立產物；請稽核發布目錄，不要散佈這些旁生檔案。

需要時請將字串加密、混淆與防竄改作為獨立防護層；切勿在用戶端二進位檔
中嵌入必須保密的祕密。

## 驗證產物

可在 CI 中使用 LLVM 目的檔工具檢查發布產物。請依目標格式調整命令，
並明確允許程式所需的 ABI 名稱。

```bash
llvm-readobj --sections --symbols --dyn-symbols app
llvm-dwarfdump app
strings app | grep neverc_private_release_symbol
test ! -e app.dSYM

llvm-readelf -h -S -s -r examples/android-kernel-hello/nvk_hello.ko
llvm-dwarfdump examples/android-kernel-hello/nvk_hello.ko
```

剝離後的產物不應包含原始碼層級偵錯區段或私有靜態符號名稱。必要的動態
名稱與執行期中繼資料是預期內容，不應視為剝離失敗。
