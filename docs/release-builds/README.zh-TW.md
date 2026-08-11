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
| Android 核心 `.ko`（ELF ET_REL） | `.debug*`、`.comment`、重定位不需要的區域/未定義項目，以及一般保留定義的可讀名稱 | 一個連結至 `.strtab` 的 `.symtab`、所有重定位及目標、精確的載入器/CFI 名稱、精確匯入、受保護區段內名稱與模組 ABI 中繼資料 |
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

`neverc make release` 仍是建議的發布命令，並展開為 `-O2 --strip`。沒有
`.nvk-build-flags` 時，`make` 預設使用 debug，不會自行選擇 release。範例
Makefile 會保存明確選取的 profile，因此後續 `make push`、`make run` 與不帶
目標的 `make` 會繼續使用同一產物。接受 `EXTRA` 的範例會在遞迴建置和後續
建置中完整保留其多詞值。`make debug` 或明確的 `PROFILE=...` 會取代
保存的選擇；`make clean` 會刪除狀態，使下一次建置恢復為 debug。明確執行
`make release` 會無條件重建一次模組/映射輸出包，因此再次執行即可修復映射遺失
或摘要不相符的輸出包。在最終路徑中，NeverC 會移除偵錯區段、`.comment` 與
重定位不需要的區域/未定義項目，然後重建 `.strtab`。

發布成功後，NeverC 會以交易方式發布模組及其旁邊的
`<module>.ko.symbols.json`。舊檔案會一直保留到各自發生原子替換，普通程序錯誤
指向同一輸出目錄的並行發布會透過 `.neverc-output.lock` 依序執行；範例的
`make clean` 會刻意保留這個內部鎖定檔案，以免解除使用中的鎖定。普通程序錯誤
在發布前會回復整個輸出包；較晚發生的持久性錯誤會保留復原日誌。由於兩個目錄
項目無法透過一次檔案系統
操作同時替換，異常關機後仍應驗證 `image_sha256`。映射記錄每個仍保留但已
重新命名之符號的 `original`（原名）與 `release`（`.ko` 中的名稱）：

```json
{
  "format": "neverc.android-kernel-symbol-map",
  "version": 2,
  "image_sha256": "…",
  "symbols": [
    {"original": "worker_dispatch", "release": "fn_C000"}
  ]
}
```

項目依 `release` 排序。已移除的符號，以及必須原樣保留的載入器、匯入或 CFI
名稱不會寫入，因為它們不需要轉換。若 debug 或其他非 strip 建置覆寫同一路徑，
NeverC 會移除舊映射，避免將過期的副產物用於新模組。ELF 允許符號名稱包含非
UTF-8 位元組；這類少見的原名會以 Base64 寫入 `original`，並帶有
`"original_encoding": "base64"`。其餘原名維持可讀。NeverC 在 POSIX 上以
`0600` 模式發布副產物，在 Windows 上套用受保護且僅允許擁有者存取的
`Windows ACL`；若無法套用該限制，發布會失敗。映射應作為私有偵錯產物封存；
不要隨 `.ko` 發布或推送至裝置。定位當機記錄前，請先確認映射確實屬於目前的
`.ko`，再查詢發布名稱：

```bash
actual="$(python3 -c 'import hashlib,sys; print(hashlib.sha256(open(sys.argv[1], "rb").read()).hexdigest())' \
  nvk_hello.ko)" &&
expected="$(jq -er '.image_sha256 | strings | select(test("^[0-9a-f]{64}$"))' \
  nvk_hello.ko.symbols.json)" &&
test "$actual" = "$expected" &&

python3 - nvk_hello.ko.symbols.json fn_C000 <<'PY'
import base64, json, sys
with open(sys.argv[1], encoding="utf-8") as stream:
    entry = next(item for item in json.load(stream)["symbols"]
                 if item["release"] == sys.argv[2])
original = entry["original"]
print(repr(base64.b64decode(original))
      if entry.get("original_encoding") == "base64" else original)
PY
```

符合條件且保留的定義會取得確定性的、受 IDA 啟發但不占用保留前綴的結構名稱：

- `STT_FUNC` 使用 `fn_HEX`；
- `STT_OBJECT` 使用 `obj_HEX`；
- 位於可執行區段的 `STT_NOTYPE` 使用 `code_HEX`；
- 其他已配置的 `STT_NOTYPE` 使用 `sym_HEX`；
- `SHN_ABS` 使用 `abs_HEX`；
- 不在 `SHF_ALLOC` 區段中的定義使用
  `sym_S<FINAL_SECTION_ORDINAL_HEX>_<OFFSET_HEX>`。

所有 `HEX` 欄位（包括非配置形式中的兩個欄位）均使用大寫十六進位且不補
多餘的前導零；多個符號需要相同名稱時，會按確定性順序附加十進位別名
`_1`、`_2` 等。

這些名稱借鑑 IDA 的表達方式，但不占用其 dummy-name 命名空間。實測在全新的
IDA 9.4 資料庫中，ELF 使用者符號 `sub_0`、`sub_4`、`loc_8` 會顯示為
`_sub_0`、`_sub_4`、`_loc_8`，而 `fn_0`、`code_8`、`obj_10` 會原樣顯示。
Hex-Rays 的 [`SN_NODUMMY`](https://python.docs.hex-rays.com/ida_name/index.html)
文件也說明，以 `sub_` 等 dummy 前綴開頭的使用者名稱會自動補上 `_`。NeverC
不會刻意清空一般定義的 `st_name` 來誘導 IDA 產生 `sub_`：Android/Linux
模組的 kallsyms 長期忽略零名稱項目，空名稱也會破壞可稽核的序列化命名合約。
原本就必須為空的項目與區段符號仍維持原樣。

ELF 允許多個符號共用同一個 canonical analysis EA。NeverC 會在 `.symtab` 中
保留或產生完整的別名集合；但 IDA 9.4 的位址命名模型可能只具現化同址符號中的
一個主要名稱。因此，IDA 未顯示某個別名並不表示它已從 ELF 遺失；完整集合應以
`llvm-readelf` 或 `llvm-nm` 稽核。

對已配置符號而言，`HEX` 是 NeverC canonical analysis EA，也就是僅供靜態
分析使用的規範有效位址。計算從游標 0 開始，依最終區段表順序走訪最終保留的
`SHF_ALLOC` 區段：先以 `max(sh_addralign, 1)` 對齊游標並記為該區段基址，
再累加 `max(sh_size, 1)`；EA 等於該基址加最終 `st_value`。`abs_HEX` 直接
使用最終絕對 `st_value`。在非配置形式中，`FINAL_SECTION_ORDINAL_HEX` 是
最終區段序號，`OFFSET_HEX` 是該區段內的最終 `st_value`。這些座標不是雜湊、
加密結果、檔案偏移、ELF 虛擬位址或核心執行期位址；載入器與 KASLR 可能在
執行期重新配置模組。

下列名稱保持完全不變：

- 所有 `SHN_UNDEF` 匯入，因為模組載入器依名稱解析它們；
- 定義於 `.modinfo`、`.text.ftrace_trampoline`、
  `.gnu.linkonce.this_module`、`__versions` 或 `.codetag.alloc_tags` 中的符號；
- `init_module`、`cleanup_module`、`__cfi_check`、`__cfi_check_fail`、
  `__cfi_jt_init_module` 與 `__cfi_jt_cleanup_module`；
- 以 `__typeid__` 或 `__kcfi_typeid_` 開頭的名稱。

IDA 顯示的 `extern` 區域只是分析器合成的檢視，並非真實 ELF 區段。在最終
`ET_REL` `.ko` 中，外部重定位目標是 `.symtab` 內的 `SHN_UNDEF` 項目，載入器
需要其原名。策略因此依據真實的 ELF 符號類別與定義區段：未定義匯入保持原名，
符合條件的定義則會重新命名，不受分析工具如何分組影響。

所有名稱都會在修改前進行全域規劃。共用同一基礎候選名稱的定義會依確定性
順序取得無編號形式、`_1`、`_2` 等；這種正常的名稱分配不是錯誤。產生的名稱與
必須原樣保留名稱的保留命名空間衝突，或座標/編號運算超出數值範圍時，發布
收尾才會失敗。遇到 `SHN_COMMON`、`SHN_LIVEPATCH` 或未知的 ELF 保留區段索引時，
發布收尾也會保守拒絕，而不是猜測處理方式。可載入的最終模組不應包含
`SHN_COMMON`，請以 `-fno-common` 編譯。Livepatch 模組要求保留原始符號表
順序、索引及額外重定位中繼資料，本發布策略不宣稱支援這些要求。

識別會使用多重訊號：任一 `SHN_LIVEPATCH` 符號、`.klp.*` 區段、
`SHF_RELA_LIVEPATCH` 旗標，或以 NUL 分隔且以 `livepatch=` 開頭的 `.modinfo`
欄位，都會把產物判定為 livepatch 模組並保守拒絕。即使不存在任何 `.klp.*`
區段或 livepatch 重定位旗標，只有該 `.modinfo` 標記也足以拒絕產物。

只有符合條件的 `.symtab` 名稱會被替換。可載入的 `.ko` 仍必須保留
`.symtab`、其關聯的 `.strtab` 與重定位，因此一般工具將它顯示為
`not stripped` 可能完全正常。BTF、模組匯出、`.modinfo`、`__versions`、
trace 中繼資料、`__ksymtab_strings`、`.rodata` 與字串常值等獨立儲存或
介面仍可能洩漏原名或其他識別文字。一般核心符號名稱在 kallsyms 與診斷中也會
變更，因此依符號使用 ftrace、kprobe/BPF，以及閱讀當機報告都會較不方便。
診斷時請使用未剝離的 debug 建置，發布模組也不得依賴私有符號的原始名稱。

### 最終 Android 發布的 plugin 邊界

發布收尾會在 plugin 輸出階段兩側建立兩個彼此獨立、失敗即關閉的身分邊界：

- 在任何可替換的 `ObjectGraph` 階段之前，圖身分封印會綁定每個保留邏輯區段的
  `section ID`、`final ordinal` 與精確名稱；也會把每個必須保持原名之符號的
  `symbol ID` 綁定至其名稱、類別、定義區段、值、大小、binding、type 與完整
  `st_other`。發布驗證器會另外重新計算一般結構名。
- 宿主建立可信寫出基線之後、`neverc.object.post_write` 之前，映像身分封印會
  綁定每個保留邏輯區段的序號/名稱、`.symtab` 總項目數，並把每個精確名稱
  符號的名稱與屬性綁定至原始 `.symtab` `slot`。

因此能力矩陣刻意限制為：

| 階段綁定 | 最終 Android 發布行為 |
|----------|-----------------------|
| `neverc.object.write` `provider` / `interceptor` | `REJECTED`；在它能替換宿主建立的可信寫出基線之前拒絕 |
| `plugin-owned ObjectFormat graph writer` | `REJECTED`；最終 Android 發布必須使用負責建立可信基線的宿主自有 graph writer |
| `observer` | `READ_ONLY`；允許觀察，但不能修改產物 |
| `neverc.object.post_write` `interceptor` | `VALIDATED`；只能修改不屬於身分面的 payload 位元組，而且結果必須繼續通過發布驗證器、輸入 ABI 合約與兩層身分封印 |

最終合併的所有權同樣由宿主封閉。來自 `third-party ObjectMergeProvider` 的
`MergedImage` 或獨立位元組會被丟棄，由 `host-owned graph writer` 序列化該 provider
已驗證並完成收尾的圖。反向一側，`built-in finalized input serialization` 會繞過
`external object phases`，把完全一致的 `audited native bytes` 交給宿主 merger；
這個內部輸入步驟不會繞過上述輸出邊界。

Finalization 只在 `Android module merge semantics` 下接受；
`relocatable output request` 與 `relocatable driver configuration` 也必須同時成立，
否則會在 `before routing` 失敗。對於最終 Android relocatable 發布，
`frozen input format`、
`TargetKey.ObjectFormatID` 與 `frozen output format` 必須共享
`one format identity`。不一致會在 `before provider dispatch` 被拒絕——這也早於
route planning 與 sink creation——因此能力預檢和實際 graph-writer dispatch
不可能看到不同格式。

對於 ObjectGraph 能完整表達的一般輸入，較早的圖 interceptor 只有在同時保持
圖封印與全部發布語意時才能執行。若輸入包含 `ObjectGraph` 無法表達、必須靠
原生映像透傳的事實，所有可替換的 `route-matching provider` 與所有 interceptor
都會被拒絕；target/CPU/features/object-format/execution-level route 不相符的 provider
既不執行，也不阻止發布。只允許唯讀 observer。只有發生在
`before sealed commit` 的拒絕或驗證失敗才會中止 staging
且不發布檔案；`AFTER_COMMIT` observer 的失敗發生在發布之後，只會被回報，
無法回滾已發布的檔案。

不要再用 `llvm-strip --strip-all` 或 `objcopy` 後處理 `.ko`，也不要任意移除
codetag/BTF/ABI 區段。若需簽署模組，必須先剝離，再簽署最終位元組；簽署後
的任何變更都會使簽章失效。`clean` 只能刪除檔案，絕不能剝離或簽署現有模組。

## 安全邊界

剝離會移除高價值命名與偵錯中繼資料，因而提高分析成本，但不能讓原生機器碼
無法逆向。正確剝離的二進位檔仍可能包含：

- 載入器所需的動態匯入與匯出名稱；
- `.ko` 中載入器所需的名稱，以及存放於 `.symtab` 之外的名稱；
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
下方反向的 `strings` 檢查預期沒有任何符合項目，且只有此時才會成功結束。

```bash
llvm-readobj --sections --symbols --dyn-symbols app
llvm-dwarfdump app
! strings app | grep -Fq -- neverc_private_release_symbol
test ! -e app.dSYM

file examples/android-kernel-hello/nvk_hello.ko
llvm-readelf -h -S -s -r examples/android-kernel-hello/nvk_hello.ko
llvm-dwarfdump examples/android-kernel-hello/nvk_hello.ko
```

對於可載入的 ELF `ET_REL` `.ko`，一般 `file` 工具仍可能顯示
`not stripped`，因為 `.symtab` 是刻意保留的。不要用該標籤判斷 release
成敗；應檢查 DWARF 與 `.comment` 已消失，符合條件的定義使用規範的
`fn_`/`obj_`/`code_`/`sym_`/`abs_` 大寫十六進位形式，
`SHN_UNDEF` 匯入與必要的載入器/CFI 名稱保持不變，且重定位有效。如需控制
名稱洩漏，還應分別稽核 BTF、匯出、modinfo、versions、trace 中繼資料與字串。

剝離後的產物不應包含原始碼層級偵錯區段或私有靜態符號名稱。必要的動態
名稱與執行期中繼資料是預期內容，不應視為剝離失敗。
