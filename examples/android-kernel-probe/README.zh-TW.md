**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 範例](../../docs/examples/README.zh-TW.md)

# Android 核心 Probe

使用 `neverc_krt_probe_register` 在 `do_faccessat` 內部的任意指令處（非函數入口）進行 interpose。演示：

- **任意地址 interpose**：可以 interpose 任何指令，不限於函數入口
- **完整暫存器上下文**：透過 `neverc_krt_reg_ctx` 讀寫所有通用暫存器
- **自動鏈式調度**：同一地址上的多個 handler，按優先級依次執行
- **控制流操作**：`NEVERC_KRT_CTX_SKIP` 中止執行，`NEVERC_KRT_CTX_REDIRECT` 重定向

## API

```c
int neverc_krt_probe_register(void *addr, neverc_krt_ctx_handler_t handler,
                              int priority, struct neverc_krt_probe_ref *ref);
int neverc_krt_probe_unregister(struct neverc_krt_probe_ref *ref);
```

Handler 簽名：

```c
void my_probe(neverc_krt_reg_ctx *ctx);
```

## 建置

```bash
cd examples/android-kernel-probe
neverc make
```

將 `KERNEL` 改為 `515`、`601`、`606`、`612` 或 `618` 以適配其他核心版本。

## 部署和執行

```bash
neverc make run
```

或手動操作：

```bash
adb push nvk_probe.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
adb shell su -c 'dmesg | grep neverc_krt_probe_demo'
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
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
```

載入當下的新日誌會出現在終端機 1。按 Ctrl+C 停止。

說明：部分 Android 內建的 `dmesg` 不支援 `-w`；`/proc/kmsg` 需要 root，但對模組載入除錯更可靠。

## 卸載模組

```bash
neverc make rmmod
```
