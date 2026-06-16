**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android カーネル インラインフック

`do_faccessat` のインラインフック。デフォルト：トランポリン付きシンプル置換。`-DNVK_CONTEXT_HOOK` 付き：完全な `nvk_reg_ctx` レジスタ状態を受け取るコンテキストフック。BTI/PAC セーフパッチ、PC 相対リロケーション、D-cache→I-cache コヒーレントトランポリンを実演。

## フックモード

| | Simple Hook（デフォルト） | Context Hook (`-DNVK_CONTEXT_HOOK`) |
|---|---|---|
| **関数シグネチャ** | 正確な typedef 宣言が必要 | 不要 — `ctx->regs[0..7]` でアクセス |
| **再入ガード** | 手動 (`nvk_hook_enter`/`leave`) | 組み込み (`guard_task`) |
| **有効/無効** | 手動 (`WRITE_ONCE`) | stub 内蔵の高速チェック |
| **元関数の呼出** | `orig` 関数ポインタ経由 | 自動（ハンドラ後に実行） |
| **元関数のスキップ** | `orig` を呼ばない | `NVK_CTX_SKIP(ctx, ret)` |
| **リダイレクト** | N/A | `NVK_CTX_REDIRECT(ctx, addr)` |
| **引数の変更** | `orig` 呼出前にパラメータ変更 | `NVK_CTX_SET_ARG(ctx, n, val)` |
| **FP 安全性** | 呼出元保存規約 | `NVK_CTX_FP_GUARD_BEGIN`/`END` |
| **オーバーヘッド** | 低い（4命令 patch + trampoline） | 高い（116命令 stub + 全レジスタ保存） |
| **適用場面** | 既知のシグネチャ、性能重視 | モニタリング、不安定な ABI、ラピッドプロトタイピング |

**推奨**：リターン値のインターセプトや厳しいパフォーマンス要件がない限り、context hook を優先してください。

## ビルド

```bash
cd examples/android-kernel-inline-hook
neverc make
```

他のカーネルバージョンには `KERNEL` を `515`、`601`、`606`、`612` に変更してください。

## デプロイと実行

```bash
neverc make run
```

または手動で：

```bash
adb push nvk_inline_hook.ko /data/local/tests/
adb shell su -c 'insmod /data/local/tests/nvk_inline_hook.ko'
adb shell su -c 'dmesg | grep nvk_inline_hook'
```

## アンロード

```bash
neverc make rmmod
```

または手動で：

```bash
adb shell su -c 'rmmod nvk_inline_hook'
```
