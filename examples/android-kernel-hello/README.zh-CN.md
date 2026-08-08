**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 示例](../../docs/examples/README.zh-CN.md)

# Android 内核 Hello

最小的 NeverC Android 内核模块（.ko）。通过 kprobe 引导 `kallsyms_lookup_name`，打印加载消息后干净退出。用于验证编译→链接→insmod 完整流程的最简冒烟测试。

## 构建

```bash
cd examples/android-kernel-hello
neverc make
```

将 `KERNEL` 改为 `515`、`601`、`606`、`612` 或 `618` 以适配其他内核版本。

## 部署和运行

```bash
neverc make run
```

或手动操作：

```bash
adb push nvk_hello.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_hello.ko'
adb shell su -c 'dmesg | grep neverc_krt_hello'
```

## 查看内核日志（实时）

在设备上执行 `cat /proc/kmsg` 可持续读取内核 ring buffer，效果类似 Windows 上的 **DbgView**。当 `insmod` 只返回含糊错误、或需要看清 vermagic、modversions、section 大小等真实拒绝原因时，应优先用这种方式。

终端 1（保持运行）：

```bash
adb shell
su
cat /proc/kmsg
```

终端 2：

```bash
adb shell su -c 'insmod /data/local/tests/nvk_hello.ko'
```

加载瞬间的新日志会出现在终端 1。按 Ctrl+C 停止。

说明：部分 Android 自带的 `dmesg` 不支持 `-w`；`/proc/kmsg` 需要 root，但对模块加载调试更可靠。

## 卸载模块

```bash
neverc make rmmod
```

或手动操作：

```bash
adb shell su -c 'rmmod neverc_krt_hello'
```
