**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

[← NeverC 示例](../../docs/examples/README.zh-CN.md)

# Android 内核 Probe

使用 `neverc_krt_probe_register` 在 `do_faccessat` 内部的任意指令处（非函数入口）进行 interpose。演示：

- **任意地址 interpose**：可以 interpose 任何指令，不限于函数入口
- **完整寄存器上下文**：通过 `neverc_krt_reg_ctx` 读写所有通用寄存器
- **自动链式调度**：同一地址上的多个 handler，按优先级依次执行
- **控制流操作**：`NEVERC_KRT_CTX_SKIP` 中止执行，`NEVERC_KRT_CTX_REDIRECT` 重定向

## API

```c
int neverc_krt_probe_register(void *addr, neverc_krt_ctx_handler_t handler,
                              int priority, struct neverc_krt_probe_ref *ref);
int neverc_krt_probe_unregister(struct neverc_krt_probe_ref *ref);
```

Handler 签名：

```c
void my_probe(neverc_krt_reg_ctx *ctx);
```

## 构建

```bash
cd examples/android-kernel-probe
neverc make          # debug：-g（首次构建默认）
neverc make release  # release：-O2 --strip
neverc make debug    # 切回 debug
```

例如用 `neverc make KERNEL=612 release` 选择其他内核预设。Makefile 会同时
持久化 `KERNEL` 与 `PROFILE`，因此后续 `make push`/`run` 会继续使用已选择的
产物，不会静默切回另一种配置。

release 剥离由 NeverC 内置完成，并专门遵守内核模块约束：删除 DWARF、
`.comment` 以及未被重定位使用的私有/未定义符号名，同时保留 ET_REL 必需的
符号表/字符串表、重定位、导入、全局定义、`__versions`、
`.codetag.alloc_tags` 和其他加载 ABI 数据。它不是 strip-all，也不是混淆；
重定位必需的名称仍可能保留。模块若要签名，必须先剥离，再对最终字节签名。
不要在 `clean` 中剥离，不要对 `.ko` 使用 `llvm-strip --strip-all`，也不要
盲目删除 `.codetag.alloc_tags` 或 `__codetag_*` 段。

## 部署和运行

```bash
neverc make run
```

或手动操作：

```bash
adb push nvk_probe.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
adb shell su -c 'dmesg | grep neverc_krt_probe_demo'
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
adb shell su -c 'insmod /data/local/tests/nvk_probe.ko'
```

加载瞬间的新日志会出现在终端 1。按 Ctrl+C 停止。

说明：部分 Android 自带的 `dmesg` 不支持 `-w`；`/proc/kmsg` 需要 root，但对模块加载调试更可靠。

## 卸载模块

```bash
neverc make rmmod
```
