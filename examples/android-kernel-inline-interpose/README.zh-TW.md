**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 範例](../../docs/examples/README.zh-TW.md)

# Android 核心函數 Interpose

使用 `neverc_krt_interpose_register` 在 `do_faccessat` 函數入口處進行 interpose。演示：

- **自動鏈式調度**：同一目標上的多個 handler，按優先級依次執行
- **呼叫原函數模式**：handler 接收 `orig` 指標，可呼叫原始函數
- **優先級控制**：數值越小越先執行；使用負數可搶在其他 interpose 之前
- **共存能力**：即使目標已被其他模組 interpose，也能正常運作

## API

```c
int neverc_krt_interpose_register(void *target, void *handler, int priority,
                             void **orig, struct neverc_krt_interpose_ref *ref);
int neverc_krt_interpose_unregister(struct neverc_krt_interpose_ref *ref);
```

Handler 簽名：

```c
long my_interpose(void *orig, void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
```

## 建置

```bash
cd examples/android-kernel-inline-interpose
neverc make
```

將 `KERNEL` 改為 `515`、`601`、`606`、`612` 或 `618` 以適配其他核心版本。

## 部署和執行

```bash
neverc make run
```

或手動操作：

```bash
adb push nvk_interpose_demo.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_interpose_demo.ko'
adb shell su -c 'dmesg | grep neverc_krt_interpose_demo'
```

## 卸載模組

```bash
neverc make rmmod
```
