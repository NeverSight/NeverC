**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# macOS ダイナミックライブラリ サンプル

NeverC でクロスコンパイルしたネイティブ macOS `.dylib` ダイナミックライブラリ。Mach カーネルインターフェースをラップし、タスク情報の取得と仮想メモリ操作を提供します — セキュリティ研究向け。macOS、Windows、Linux のいずれからでもビルド可能 — Xcode 不要。

## ビルド

リポジトリから（デフォルトターゲット：`arm64-apple-macos`）：

```bash
cd examples/macos-dylib
neverc make
```

Intel 向けビルド：

```bash
neverc make TARGET=x86_64-apple-macos
```

スタンドアロンの NeverC リリースを使用：

```bash
neverc make NEVERC=/path/to/neverc
```

## 手動ビルド（Make なし）

```bash
neverc --target=arm64-apple-macos -Wall -dynamiclib -o libneverc.dylib lib.c
```

## 機能

- `nc_task_basic_info` による Mach `task_info` クエリのラッパーをエクスポート
- `nc_vm_read`/`nc_vm_write` で Mach 仮想メモリの読み書き
- `nc_vm_alloc`/`nc_vm_dealloc` で Mach VM メモリの確保と解放
- XOR バッファ暗号化ヘルパーおよび PID/タスク取得関数
