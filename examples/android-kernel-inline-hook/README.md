**Languages**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android Kernel Inline Hook

Inline hook on `do_faccessat`. Default: simple replacement with trampoline. With `-DNVK_CONTEXT_HOOK`: context hook receiving full `nvk_reg_ctx` register state. Demonstrates BTI/PAC-safe patching, PC-relative relocation, and D-cache→I-cache coherent trampoline.

## Hook Modes

| | Simple Hook (default) | Context Hook (`-DNVK_CONTEXT_HOOK`) |
|---|---|---|
| **Signature** | Must declare exact function typedef | Not required — use `ctx->regs[0..7]` |
| **Re-entrancy guard** | Manual (`nvk_hook_enter`/`leave`) | Built-in (`guard_task`) |
| **Enable/disable** | Manual (`WRITE_ONCE`) | Built-in fast-check in stub |
| **Call original** | Via `orig` function pointer | Automatic (runs after handler) |
| **Skip original** | Don't call `orig` | `NVK_CTX_SKIP(ctx, ret)` |
| **Redirect** | N/A | `NVK_CTX_REDIRECT(ctx, addr)` |
| **Modify args** | Change params before calling `orig` | `NVK_CTX_SET_ARG(ctx, n, val)` |
| **FP safety** | Caller-save convention | `NVK_CTX_FP_GUARD_BEGIN`/`END` |
| **Overhead** | Lower (4-insn patch + trampoline) | Higher (116-insn stub + full reg save) |
| **Best for** | Known signatures, perf-critical | Monitoring, unstable ABI, rapid prototyping |

**Recommendation**: prefer context hook unless you need to intercept the return value or have tight performance constraints.

## Build

```bash
cd examples/android-kernel-inline-hook
neverc make
```

Change `KERNEL` to `515`, `601`, `606`, or `612` for other kernel versions.

## Deploy & Run

```bash
neverc make run
```

Or manually:

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## Unload

```bash
neverc make rmmod
```

Or manually:

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```
