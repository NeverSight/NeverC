**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 内核多文件模块

演示多文件 NeverC 内核模块。要点：

- **单次引导**：`NEVERC_KRT_BOOTSTRAP()` 只需在 `module_init` 中调用一次
- **共享状态**：编译器将所有 `neverc_krt_*` 状态提升为 `weak_odr` 链接，所有 `.c` 文件共享同一符号解析器、缓存和子系统状态
- **分文件架构**：`main.c`（初始化/退出）、`hooks.c`（hook 逻辑）、`utils.c`（辅助函数）

## 构建

```bash
cd examples/android-kernel-multifile
neverc make
```

将 `KERNEL` 改为 `515`、`601`、`606`、`612` 或 `618` 以适配其他内核版本。

## 部署和运行

```bash
neverc make run
```

或手动操作：

```bash
adb push nvk_multi.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_multi.ko'
adb shell su -c 'dmesg | grep neverc_krt_multi'
```

## 卸载模块

```bash
neverc make rmmod
```
