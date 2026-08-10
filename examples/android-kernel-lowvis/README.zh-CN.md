**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 示例](../../docs/examples/README.zh-CN.md)

# Android 内核低可见性模块

模块可见性管理演示。编译时标志：无=基本列表可见性，`-DNVK_LOWVIS_FILTER`=完整可见性过滤（列表+sysfs+proc），`-DNVK_LOWVIS_FILTER_FULL`=扩展（dmesg+PID+挂载+maps），`-DNVK_LOWVIS_CRED`=凭证包装演示（`struct cred`），`-DNVK_LOWVIS_SELINUX`=SELinux 强制状态演示（permissive）。

## 构建

```bash
cd examples/android-kernel-lowvis
neverc make          # debug：-g（首次构建默认）
neverc make release  # release：-O2 --strip
neverc make debug    # 切回 debug
```

例如用 `neverc make KERNEL=612 release` 选择其他内核预设。
`neverc make release` 选择 `-O2 --strip`。Makefile 会把所选 `KERNEL` 与
`PROFILE` 记录在 `.nvk-build-flags` 中，因此后续 `make push`、`make run` 与
不带目标的 `make` 会继续使用该产物。没有此状态文件时，`make` 默认使用 debug。
`make debug` 或显式 `PROFILE=...` 会替换已保存的配置；`make clean` 删除状态
文件，使下一次构建恢复为 debug。

NeverC 会写入五类受 IDA 启发但不占用保留前缀的发布名称：函数
`fn_HEX`、可执行无类型标签 `code_HEX`、对象 `obj_HEX`、其他无类型标签
`sym_HEX`，以及绝对符号 `abs_HEX`。对于普通已分配定义，`HEX` 是根据最终
`SHF_ALLOC` 节布局确定性计算的 `analysis EA`（`abs_HEX` 改用绝对
`st_value`）；它不是 hash（哈希）、encryption（加密）、file offset（文件偏移）、
ELF virtual address（ELF 虚拟地址）或 runtime kernel address（内核运行时地址）。
NeverC 既不存储保留的 `sub_`/`loc_` 形式，也不故意清空普通名称。

必须原样保留的名称、IDA 合成的 `extern` 视图、安全边界以及发布收尾与签名的
先后顺序，统一参见[发布与剥离策略](../../docs/release-builds/README.zh-CN.md)。

## 部署和运行

```bash
neverc make run
```

或手动操作：

```bash
adb push nvk_lowvis.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_lowvis.ko'
adb shell su -c 'dmesg | grep neverc_krt_lowvis'
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
adb shell su -c 'insmod /data/local/tests/nvk_lowvis.ko'
```

加载瞬间的新日志会出现在终端 1。按 Ctrl+C 停止。

说明：部分 Android 自带的 `dmesg` 不支持 `-w`；`/proc/kmsg` 需要 root，但对模块加载调试更可靠。

## 卸载模块

```bash
neverc make rmmod
```

或手动操作：

```bash
adb shell su -c 'rmmod neverc_krt_lowvis'
```
