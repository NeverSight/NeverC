**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android ELF 示例

使用 NeverC 交叉编译的 ARM64 原生 ELF 可执行文件，用于 Android 平台。设计为在已 root 的 Android 设备上通过 `adb shell` 直接运行。可从 macOS、Windows 或 Linux 构建——无需 Android NDK 或 CMake。

NeverC 在 `runtime/android/` 中内置了 Android sysroot（NDK r26c, API 21+），因此一次调用即可完成预处理、编译、优化（自动 LTO）和链接。

## 构建

从仓库根目录：

```bash
cd examples/android-elf
neverc make
```

使用独立的 NeverC 发行版：

```bash
neverc make NEVERC=/path/to/neverc
```

## 手动构建（不使用 Make）

```bash
neverc --target=aarch64-linux-android -Wall -fPIE -lm -ldl -llog -o android-elf main.c
```

## 部署和运行

通过 adb 推送到设备并运行：

```bash
neverc make run
```

或手动操作：

```bash
adb push android-elf /data/local/tmp/
adb shell chmod 755 /data/local/tmp/android-elf
adb shell /data/local/tmp/android-elf
```

## 功能说明

- 打印设备信息（`uname`）和内核版本
- 检查 root/权限状态（`uid`/`euid`，`su` 路径）
- 动态加载 `liblog.so` 并调用 `__android_log_print`
- 读取 `/proc/self/maps` 显示内存布局
- 演示 Android 上的 `dlopen`/`dlsym`、`readlink`、`fopen`
