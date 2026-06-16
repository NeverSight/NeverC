**言語**: [English](README.md) | [简体中文](README.zh-CN.md) | [繁體中文](README.zh-TW.md) | [日本語](README.ja.md) | [한국어](README.ko.md) | [Français](README.fr.md) | [Deutsch](README.de.md) | [Español](README.es.md) | [Italiano](README.it.md) | [Русский](README.ru.md) | [العربية](README.ar.md)

# Android カーネル インラインフック

`do_faccessat` のインラインフック。デフォルト：トランポリン付きシンプル置換。`-DNVK_CONTEXT_HOOK` 付き：完全な `nvk_reg_ctx` レジスタ状態を受け取るコンテキストフック。BTI/PAC セーフパッチ、PC 相対リロケーション、D-cache→I-cache コヒーレントトランポリンを実演。

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
