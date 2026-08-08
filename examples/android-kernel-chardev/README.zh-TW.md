**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 範例](../../docs/examples/README.zh-TW.md)

# Android 核心字元裝置

帶有 ioctl 介面和 `/proc` 狀態頁面的混合字元裝置。展示 `misc_register`、ioctl 命令分派和基於 `seq_file` 的 proc 條目 —— Android 上標準的用戶態↔核心態 IPC 模式。

## 建置

```bash
cd examples/android-kernel-chardev
neverc make          # debug：-g（首次建置預設）
neverc make release  # release：-O2 --strip
neverc make debug    # 切回 debug
```

例如使用 `neverc make KERNEL=612 release` 選擇其他核心預設。Makefile 會同時
保存 `KERNEL` 與 `PROFILE`，因此後續 `make push`/`run` 會沿用已選擇的產物，
不會默默切回另一種設定。

release 剝離由 NeverC 內建完成，並遵守核心模組限制：移除 DWARF、
`.comment` 與未被重定位使用的私有/未定義符號名稱，同時保留 ET_REL 必需的
符號表/字串表、重定位、匯入、全域定義、`__versions`、
`.codetag.alloc_tags` 及其他載入 ABI 資料。這不是 strip-all，也不是混淆；
重定位必需的名稱仍可能保留。若模組需要簽章，請先剝離，再簽署最終位元組。
不要在 `clean` 中剝離，不要對 `.ko` 使用 `llvm-strip --strip-all`，也不要
任意移除 `.codetag.alloc_tags` 或 `__codetag_*` 區段。

## 部署與執行

```bash
neverc make run
```

或手動操作：

```bash
adb push nvk_chardev.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_chardev.ko'
adb shell su -c 'dmesg | grep neverc_krt_chardev'
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
adb shell su -c 'insmod /data/local/tests/nvk_chardev.ko'
```

載入當下的新日誌會出現在終端機 1。按 Ctrl+C 停止。

說明：部分 Android 內建的 `dmesg` 不支援 `-w`；`/proc/kmsg` 需要 root，但對模組載入除錯更可靠。

## 卸載模組

```bash
neverc make rmmod
```

或手動操作：

```bash
adb shell su -c 'rmmod neverc_krt_chardev'
```
