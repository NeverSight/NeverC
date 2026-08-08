**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 範例](../../docs/examples/README.zh-TW.md)

# Android 核心 Netlink

雙向 Netlink IPC 通道。建立 netlink socket 用於用戶態↔核心態通訊。支援 PING（回傳 PONG）、VERSION（核心版本字串）和 ECHO（負載回顯）。展示 `nvk_nl_open`、`nvk_nl_reply` 和分派回呼模式。

## 建置

```bash
cd examples/android-kernel-netlink
neverc make
```

將 `KERNEL` 改為 `515`、`601`、`606`、`612` 或 `618` 以適配其他核心版本。

## 部署與執行

```bash
neverc make run
```

或手動操作：

```bash
adb push nvk_netlink.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_netlink.ko'
adb shell su -c 'dmesg | grep neverc_krt_netlink'
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
adb shell su -c 'insmod /data/local/tests/nvk_netlink.ko'
```

載入當下的新日誌會出現在終端機 1。按 Ctrl+C 停止。

說明：部分 Android 內建的 `dmesg` 不支援 `-w`；`/proc/kmsg` 需要 root，但對模組載入除錯更可靠。

## 卸載模組

```bash
neverc make rmmod
```

或手動操作：

```bash
adb shell su -c 'rmmod neverc_krt_netlink'
```
