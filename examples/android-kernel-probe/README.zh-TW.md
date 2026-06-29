**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 核心 Probe

使用 `neverc_krt_probe_register` 在 `do_faccessat` 內部的任意指令處（非函數入口）進行 hook。演示：

- **任意地址 hook**：可以 hook 任何指令，不限於函數入口
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

將 `KERNEL` 改為 `515`、`601`、`606` 或 `612` 以適配其他核心版本。

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

## 卸載模組

```bash
neverc make rmmod
```
