**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 範例](../../docs/examples/README.zh-TW.md)

# Android 核心多檔案模組

演示多檔案 NeverC 核心模組。要點：

- **單次引導**：`NEVERC_KRT_BOOTSTRAP()` 只需在 `module_init` 中呼叫一次
- **共享狀態**：編譯器將所有 `neverc_krt_*` 狀態提升為 `weak_odr` 連結，所有 `.c` 檔案共享同一符號解析器、快取和子系統狀態
- **分檔架構**：`main.c`（初始化/退出）、`interposes.c`（interpose 邏輯）、`utils.c`（輔助函式）

## 建置

```bash
cd examples/android-kernel-multifile
neverc make          # debug：-g（首次建置預設）
neverc make release  # release：-O2 --strip
neverc make debug    # 切回 debug
```

例如使用 `neverc make KERNEL=612 release` 選擇其他核心預設。
`neverc make release` 選擇 `-O2 --strip`。Makefile 會把所選 `KERNEL` 與
`PROFILE` 記錄在 `.nvk-build-flags` 中，因此後續 `make push`、`make run` 與
不帶目標的 `make` 會繼續使用該產物。沒有此狀態檔時，`make` 預設使用 debug。
`make debug` 或明確的 `PROFILE=...` 會取代已保存的設定；`make clean` 刪除狀態
檔，使下一次建置恢復為 debug。

NeverC 會寫入五類受 IDA 啟發但不占用保留前綴的發布名稱：函式
`fn_HEX`、可執行無類型標籤 `code_HEX`、物件 `obj_HEX`、其他無類型標籤
`sym_HEX`，以及絕對符號 `abs_HEX`。對一般已配置定義而言，`HEX` 是依最終
`SHF_ALLOC` 節區布局確定性計算的 `analysis EA`（`abs_HEX` 改用絕對
`st_value`）；它不是 hash（雜湊）、encryption（加密）、file offset（檔案偏移）、
ELF virtual address（ELF 虛擬位址）或 runtime kernel address（核心執行期位址）。
NeverC 既不儲存保留的 `sub_`/`loc_` 形式，也不刻意清空一般名稱。

必須原樣保留的名稱、IDA 合成的 `extern` 檢視、安全邊界，以及發布收尾與簽署的
先後順序，統一參見[發布與剝離策略](../../docs/release-builds/README.zh-TW.md)。

## 部署和執行

```bash
neverc make run
```

或手動操作：

```bash
adb push nvk_multi.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
adb shell su -c 'dmesg | grep neverc_krt_multi'
```

## 查看核心日誌（即時）

在裝置上執行 `cat /proc/kmsg` 可持續讀取核心 ring buffer，效果類似 Windows 上的 **DbgView**。當 `insmod` 只回傳含糊錯誤、或需要看清 vermagic、modversions、section 大小等真實拒絕原因時，應優先用這種方式。

終端機 1（保持執行）：

```bash
adb shell
su
cat /proc/kmsg
```

終端機 2：

```bash
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
```

載入當下的新日誌會出現在終端機 1。按 Ctrl+C 停止。

說明：部分 Android 內建的 `dmesg` 不支援 `-w`；`/proc/kmsg` 需要 root，但對模組載入除錯更可靠。

## 卸載模組

```bash
neverc make rmmod
```
