**语言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 内核 Inline Hook

对 `do_faccessat` 的 inline hook。默认：简单替换 + trampoline。加 `-DNVK_CONTEXT_HOOK`：上下文 hook 接收完整 `nvk_reg_ctx` 寄存器状态。演示 BTI/PAC 安全补丁、PC 相对重定位和 D-cache→I-cache 一致的 trampoline。

## Hook 模式

| | Simple Hook (默认) | Context Hook (`-DNVK_CONTEXT_HOOK`) |
|---|---|---|
| **函数签名** | 必须声明精确的函数 typedef | 不需要 — 通过 `ctx->regs[0..7]` 访问 |
| **重入保护** | 手动 (`nvk_hook_enter`/`leave`) | 内置 (`guard_task`) |
| **启用/禁用** | 手动 (`WRITE_ONCE`) | stub 内置快速检查 |
| **调用原函数** | 通过 `orig` 函数指针 | 自动（handler 后执行） |
| **跳过原函数** | 不调用 `orig` | `NVK_CTX_SKIP(ctx, ret)` |
| **重定向** | N/A | `NVK_CTX_REDIRECT(ctx, addr)` |
| **修改参数** | 调用 `orig` 前修改参数 | `NVK_CTX_SET_ARG(ctx, n, val)` |
| **FP 安全** | 调用者保存约定 | `NVK_CTX_FP_GUARD_BEGIN`/`END` |
| **开销** | 较低（4 指令 patch + trampoline） | 较高（116 指令 stub + 全寄存器保存） |
| **适用场景** | 已知签名、性能敏感 | 监控、不稳定 ABI、快速原型 |

**推荐**：优先使用 context hook，除非你需要拦截返回值或有严格的性能要求。

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
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## 卸载模块

```bash
neverc make rmmod
```

或手动操作：

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```
