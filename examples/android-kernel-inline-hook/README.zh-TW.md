**語言**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android 核心 Inline Hook

對 `do_faccessat` 的 inline hook。預設：簡單替換 + trampoline。加 `-DNVK_CONTEXT_HOOK`：上下文 hook 接收完整 `nvk_reg_ctx` 暫存器狀態。展示 BTI/PAC 安全補丁、PC 相對重定位和 D-cache→I-cache 一致的 trampoline。

## Hook 模式

| | Simple Hook (預設) | Context Hook (`-DNVK_CONTEXT_HOOK`) |
|---|---|---|
| **函式簽名** | 必須宣告精確的函式 typedef | 不需要 — 透過 `ctx->regs[0..7]` 存取 |
| **重入保護** | 手動 (`nvk_hook_enter`/`leave`) | 內建 (`guard_task`) |
| **啟用/停用** | 手動 (`WRITE_ONCE`) | stub 內建快速檢查 |
| **呼叫原函式** | 透過 `orig` 函式指標 | 自動（handler 後執行） |
| **跳過原函式** | 不呼叫 `orig` | `NVK_CTX_SKIP(ctx, ret)` |
| **重新導向** | N/A | `NVK_CTX_REDIRECT(ctx, addr)` |
| **修改參數** | 呼叫 `orig` 前修改參數 | `NVK_CTX_SET_ARG(ctx, n, val)` |
| **FP 安全** | 呼叫者儲存約定 | `NVK_CTX_FP_GUARD_BEGIN`/`END` |
| **開銷** | 較低（4 指令 patch + trampoline） | 較高（116 指令 stub + 全暫存器儲存） |
| **適用場景** | 已知簽名、效能敏感 | 監控、不穩定 ABI、快速原型 |

**建議**：優先使用 context hook，除非您需要攔截回傳值或有嚴格的效能要求。

## 建置

```bash
cd examples/android-kernel-inline-hook
neverc make
```

將 `KERNEL` 改為 `515`、`601`、`606` 或 `612` 以適配其他核心版本。

## 部署與執行

```bash
neverc make run
```

或手動操作：

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## 卸載模組

```bash
neverc make rmmod
```

或手動操作：

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```
