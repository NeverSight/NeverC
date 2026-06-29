**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 内核函数 Hook

使用 `neverc_krt_hook_register` 在 `do_faccessat` 函数入口处进行 hook。演示：

- **自动链式调度**：同一目标上的多个 handler，按优先级依次执行
- **调用原函数模式**：handler 接收 `orig` 指针，可调用原始函数
- **优先级控制**：数值越小越先执行；使用负数可抢在其他 hook 之前
- **共存能力**：即使目标已被其他模块 hook，也能正常工作

## API

```c
int neverc_krt_hook_register(void *target, void *handler, int priority,
                             void **orig, struct neverc_krt_hook_ref *ref);
int neverc_krt_hook_unregister(struct neverc_krt_hook_ref *ref);
```

Handler 签名：

```c
long my_hook(void *orig, void *a0, void *a1, void *a2, void *a3, void *a4, void *a5);
```

## 构建

```bash
cd examples/android-kernel-inline-hook
neverc make
```

将 `KERNEL` 改为 `515`、`601`、`606` 或 `612` 以适配其他内核版本。

## 部署和运行

```bash
neverc make run
```

或手动操作：

```bash
adb push nvk_hook_demo.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_hook_demo.ko'
adb shell su -c 'dmesg | grep neverc_krt_hook_demo'
```

## 卸载模块

```bash
neverc make rmmod
```
