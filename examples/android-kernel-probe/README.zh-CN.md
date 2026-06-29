**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 内核 Probe

使用 `neverc_krt_probe_register` 在 `do_faccessat` 内部的任意指令处（非函数入口）进行 hook。演示：

- **任意地址 hook**：可以 hook 任何指令，不限于函数入口
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
neverc make
```

将 `KERNEL` 改为 `515`、`601`、`606` 或 `612` 以适配其他内核版本。

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

## 卸载模块

```bash
neverc make rmmod
```
